#include "fs.h"
#include "log.h"
#include "defs.h"
#include "kalloc.h"
#include "buf.h"
#include "virtio.h"
#include "debug.h"



void fs_init() {
    infof("fs_init");

    struct buf* b = bread(0, 0);
    assert(b->valid);
    infof("first read done!");

    hexdump(b->data, BSIZE);

    memmove(b->data, "hello, world!", 13);
    bwrite(b);
    infof("first write done!");

    infof("fs_init ends");
}