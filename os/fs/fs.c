#include "fs.h"
#include "log.h"
#include "defs.h"
#include "buf.h"
#include "kalloc.h"
#include "virtio.h"
#include "debug.h"

static allocator_t buf_allocator;

void fs_init() {
    release(&curr_proc()->lock);
    intr_on();

    infof("fs_init");
    allocator_init(&buf_allocator, "buf", sizeof(struct buf), 1024);

    struct buf* b = kalloc(&buf_allocator);
    memset(b, 0, sizeof(*b));
    uint64 pa = (uint64) kallocpage();
    assert(pa);
    b->data = (uint8*) PA_TO_KVA(pa);
    memset(b->data, 0, PGSIZE);

    b->blockno = 0;

    virtio_disk_rw(b, 0);

    infof("first read done!");

    hexdump(b->data, BSIZE);

    while (1) asm volatile("wfi");
}