#include "buf.h"
#include "debug.h"
#include "defs.h"
#include "fs.h"
#include "kalloc.h"
#include "log.h"
#include "virtio.h"

// Block Level I/O

// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

struct {
    struct spinlock lock;
    allocator_t buf_allocator;
    struct buf *bufs[NBUF];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf *head;
} bcache;

void binit(void) {
    struct buf *b;

    spinlock_init(&bcache.lock, "bcache");
    allocator_init(&bcache.buf_allocator, "buf", sizeof(struct buf), NBUF);

    for (int i = 0; i < NBUF; i++) {
        b = kalloc(&bcache.buf_allocator);
        memset(b, 0, sizeof(*b));
        sleeplock_init(&b->lock, "buf");
        void *pa = kallocpage();
        assert(pa != NULL);
        b->data        = (uint8 *)PA_TO_KVA(pa);
        memset(b->data, 0, PGSIZE);
        bcache.bufs[i] = b;
    }

    // init head pointer
    bcache.head       = bcache.bufs[0];
    bcache.head->prev = bcache.head;
    bcache.head->next = bcache.head;

    for (int i = 0; i < NBUF; i++) {
        b = bcache.bufs[i];
        // append b to the end of the list
        b->prev                 = bcache.head->prev;
        b->next                 = bcache.head;
        bcache.head->prev->next = b;
        bcache.head->prev       = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return a *locked* buffer.
static struct buf *bget(uint dev, uint blockno) {
    struct buf *b;

    acquire(&bcache.lock);

    // Is the block already cached?
    for (b = bcache.head; b != bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache.head->prev; b != bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev     = dev;
            b->blockno = blockno;
            b->valid   = 0;
            b->refcnt  = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    panic("no enough buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("expect locked buffer");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("expect locked buffer");

    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        // no one is waiting for it.
        b->next->prev           = b->prev;
        b->prev->next           = b->next;
        b->next                 = bcache.head->next;
        b->prev                 = bcache.head;
        bcache.head->next->prev = b;
        bcache.head->next       = b;
    }

    release(&bcache.lock);
}

void bpin(struct buf *b) {
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

void bunpin(struct buf *b) {
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}
