#ifndef FS_H
#define FS_H

#include "types.h"

void fs_init();

// bio.c
void binit(void);
struct buf *bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

#endif