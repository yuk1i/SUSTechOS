#ifndef BUF_H
#define BUF_H

#include "defs.h"
#include "lock.h"
#include "types.h"

struct buf {
    int64 refcnt;
    struct buf *prev;  // LRU cache list
    struct buf *next;
    // --- above fields are protected against spinlock bcache.lock ---

    sleeplock_t lock;
    int valid;                // has data been read from disk?
    volatile int disk_using;  // does disk read/write complete?
    uint64 dev;
    uint64 blockno;
    uint8 *__kva data;  // allocated by kallocpage, although we only use 512 bytes of 4KiB page
};

#endif  // BUF_H