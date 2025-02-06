//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
// qemu presents a "legacy" virtio interface.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "virtio.h"

#include "buf.h"
#include "defs.h"
#include "lock.h"
#include "memlayout.h"
#include "riscv.h"

// the address of virtio mmio register r.
#define VIRTIO_Reg(r) ((volatile uint32 *)(KERNEL_VIRTIO0_BASE + (r)))

static struct disk {
    // the virtio driver and device mostly communicate through a set of
    // structures in RAM. `virtq` allocates that memory. `virtq` is a
    // global (instead of calls to kalloc()) because it must consist of
    // two contiguous pages of page-aligned physical memory.
    //      watch out: use KIVA_TO_PA to convert to PA.

    // `virtq` is divided into three regions (descriptors, avail, and
    // used), as explained in Section 2.6 of the virtio specification
    // for the legacy interface.
    // https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf

    struct {
        // the first region of `virtq` is a set (not a ring) of DMA
        // descriptors, with which the OS tells the device where to read
        // and write individual disk operations. there are NUM descriptors.
        // most commands consist of a "chain" (a linked list) of a couple of
        // these descriptors.
        struct virtq_desc {
            uint64 addr;
            uint32 len;
            uint16 flags;
            uint16 next;
        } __attribute__((packed)) desc[NUM];

        /**
         * @brief The OS uses the available ring to offer buffers to the device:
         * each ring entry refers to the head of a descriptor chain.
         * It is only written by the OS and read by the device.
         */
        struct virtq_avail {
            uint16 flags;      // always zero
            uint16 idx;        // OS will write ring[idx] next
            uint16 ring[NUM];  // descriptor numbers of chain heads
            uint16 unused;
        } __attribute__((packed)) avail;

        /**
         * @brief The used ring is where the device returns buffers once it is done with them:
         *   it is only written to by the device, and read by the driver.
         */
        volatile struct virtq_used {
            uint16 flags;  // always zero
            uint16 idx;    // device increments when it adds a ring[] entry
            struct virtq_used_elem {
                uint32 id;   // index of start of completed descriptor chain
                uint32 len;  // Total length of the descriptor chain which was used (written to)
            } ring[NUM];
        } __attribute__((packed)) __attribute__((aligned(PGSIZE))) used;

        /*
        When the driver wants to send a buffer to the device,
         it fills in a slot in the descriptor table (or chains several together),
         and writes the descriptor index into the available ring.
        It then notifies the device.
        When the device has finished a buffer,
         it writes the descriptor index into the used ring,
          and sends a used buffer notification.
        */
    } __attribute__((aligned(PGSIZE))) virtq;

    // our own book-keeping.

    char free[NUM];   // is a descriptor free?
    uint16 used_idx;  // we've looked this far in used[2..NUM].

    // track info about in-flight operations,
    // for use when completion interrupt arrives.
    // indexed by first descriptor index of chain.
    struct {
        struct buf *b;
        uint8 status;
    } info[NUM];

    // disk command headers.
    // one-for-one with descriptors, for convenience.
    struct virtio_blk_req ops[NUM];

    struct spinlock vdisk_lock;

} __attribute__((aligned(PGSIZE))) disk;

void virtio_disk_init(void) {
    // desc  = pages -- num * virtq_desc
    // avail = pages + 0x40 -- 2 * uint16, then num * uint16
    // used  = pages + 4096 -- 2 * uint16, then num * vRingUsedElem
    assert(PGALIGNED((uint64)&disk.virtq));
    assert(PGALIGNED((uint64)&disk.virtq.used));

    uint32 status = 0;

    spinlock_init(&disk.vdisk_lock, "virtio_disk");

    if (*VIRTIO_Reg(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 || *VIRTIO_Reg(VIRTIO_MMIO_VERSION) != 1 || *VIRTIO_Reg(VIRTIO_MMIO_DEVICE_ID) != 2 ||
        *VIRTIO_Reg(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
        panic("could not find virtio disk");
    }

    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *VIRTIO_Reg(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_DRIVER;
    *VIRTIO_Reg(VIRTIO_MMIO_STATUS) = status;

    // negotiate features
    uint64 features = *VIRTIO_Reg(VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_SCSI);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_BLK_F_MQ);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    *VIRTIO_Reg(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *VIRTIO_Reg(VIRTIO_MMIO_STATUS) = status;

    // tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *VIRTIO_Reg(VIRTIO_MMIO_STATUS) = status;

    *VIRTIO_Reg(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    // initialize queue 0.
    *VIRTIO_Reg(VIRTIO_MMIO_QUEUE_SEL) = 0;

    uint32 max = *VIRTIO_Reg(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio disk max queue too short");

    *VIRTIO_Reg(VIRTIO_MMIO_QUEUE_NUM) = NUM;
    memset(&disk.virtq, 0, sizeof(disk.virtq));
    *VIRTIO_Reg(VIRTIO_MMIO_QUEUE_PFN) = (KIVA_TO_PA(&disk.virtq)) >> PGSHIFT;

    // all NUM descriptors start out unused.
    for (int i = 0; i < NUM; i++) disk.free[i] = 1;

    // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int alloc_desc() {
    for (int i = 0; i < NUM; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// mark a descriptor as free.
static void free_desc(int i) {
    if (i >= NUM)
        panic("free desc overflow");
    if (disk.free[i])
        panic("double free");
    disk.virtq.desc[i].addr  = 0;
    disk.virtq.desc[i].len   = 0;
    disk.virtq.desc[i].flags = 0;
    disk.virtq.desc[i].next  = 0;
    disk.free[i]             = 1;
    wakeup(&disk.free);
}

// free a chain of descriptors.
static void free_chain(int i) {
    while (1) {
        int flag = disk.virtq.desc[i].flags;
        int nxt  = disk.virtq.desc[i].next;
        free_desc(i);
        if (flag & VRING_DESC_F_NEXT)
            i = nxt;
        else
            break;
    }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int alloc3_desc(int *idx) {
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc();
        if (idx[i] < 0) {
            for (int j = 0; j < i; j++) free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

void virtio_disk_rw(struct buf *b, int write) {
    uint64 sector = b->blockno * (BSIZE / 512);

    acquire(&disk.vdisk_lock);

    // the spec's Section 5.2 says that legacy block operations use
    // three descriptors: one for type/reserved/sector, one for the
    // data, one for a 1-byte status result.

    // allocate the three descriptors.
    int idx[3];
    while (1) {
        if (alloc3_desc(idx) == 0) {
            break;
        }
        sleep(&disk.free, &disk.vdisk_lock);
    }

    // format the virtio_blk_req structure, which contains three parts:
    //    1. the header, desc0
    //    2. the data buffer, with its length
    //    3. the status byte

    struct virtio_blk_req *blkreq = &disk.ops[idx[0]];

    if (write)
        blkreq->type = VIRTIO_BLK_T_OUT;  // write the disk
    else
        blkreq->type = VIRTIO_BLK_T_IN;  // read the disk
    blkreq->reserved = 0;
    blkreq->sector   = sector;

    disk.virtq.desc[idx[0]].addr  = KIVA_TO_PA(blkreq);
    disk.virtq.desc[idx[0]].len   = sizeof(struct virtio_blk_req);
    disk.virtq.desc[idx[0]].flags = VRING_DESC_F_NEXT;  // device reads this.
    disk.virtq.desc[idx[0]].next  = idx[1];

    disk.virtq.desc[idx[1]].addr = KVA_TO_PA(b->data);
    disk.virtq.desc[idx[1]].len  = PGSIZE;
    if (write)
        disk.virtq.desc[idx[1]].flags = 0;  // device reads b->data
    else
        disk.virtq.desc[idx[1]].flags = VRING_DESC_F_WRITE;  // device writes b->data
    disk.virtq.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.virtq.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status      = 0xff;  // device writes 0 on success
    disk.virtq.desc[idx[2]].addr  = KIVA_TO_PA(&disk.info[idx[0]].status);
    disk.virtq.desc[idx[2]].len   = 1;
    disk.virtq.desc[idx[2]].flags = VRING_DESC_F_WRITE;  // device writes the status
    disk.virtq.desc[idx[2]].next  = 0;

    // record struct buf for virtio_disk_intr().
    b->disk_using       = 1;
    disk.info[idx[0]].b = b;

    // tell the device the first index in our chain of descriptors.
    disk.virtq.avail.ring[disk.virtq.avail.idx % NUM] = idx[0];

    __sync_synchronize();

    // tell the device another avail ring entry is available.
    disk.virtq.avail.idx += 1;  // not % NUM ...

    __sync_synchronize();

    *VIRTIO_Reg(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;  // value is queue number

    // Wait for virtio_disk_intr() to say request has finished.
    while (b->disk_using == 1) {
        sleep(b, &disk.vdisk_lock);
    }

    disk.info[idx[0]].b = NULL;
    free_chain(idx[0]);

    release(&disk.vdisk_lock);
}

void virtio_disk_intr() {
    acquire(&disk.vdisk_lock);

    // the device won't raise another interrupt until we tell it
    // we've seen this interrupt, which the following line does.
    // this may race with the device writing new entries to
    // the "used" ring, in which case we may process the new
    // completion entries in this interrupt, and have nothing to do
    // in the next interrupt, which is harmless.
    *VIRTIO_Reg(VIRTIO_MMIO_INTERRUPT_ACK) = *VIRTIO_Reg(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

    __sync_synchronize();

    // the device increments disk.used->idx when it adds an entry to the used ring.

    // We MUST NOT assume that we will only process one index.
    while (disk.used_idx != disk.virtq.used.idx) {
        // VirtIO Spec: idx always increments, and wraps naturally at 65536:

        __sync_synchronize();
        int id = disk.virtq.used.ring[disk.used_idx % NUM].id;

        if (disk.info[id].status != 0)
            panic("virtio_disk_intr status != 0");

        struct buf *b = disk.info[id].b;
        b->disk_using = 0;  // disk is done with buf, release it to disk_rw.
        wakeup(b);

        disk.used_idx += 1;
    }

    release(&disk.vdisk_lock);
}
