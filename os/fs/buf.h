#ifndef BUF_H
#define BUF_H

#include "defs.h"
#include "lock.h"
#include "types.h"

struct buf {
    int valid;          // has data been read from disk?
    volatile int disk_using;  // does disk read/write complete?
    uint64 dev;
    uint64 blockno;

    sleeplock_t lock;
    uint64 refcnt;

    uint8 __kva *data;  // Buffer, must allocated by kallocpage().
};

#endif  // BUF_H