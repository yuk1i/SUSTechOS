#ifndef FS_H
#define FS_H

#include "types.h"
#include "vm.h"

typedef uint32 fmode_t;

struct file_operations;
struct file {
    spinlock_t lock;  // lock protects ref and pos.
    int ref;          // reference count
    int pos;          // read/write position

    // --- following fields are immutable since filealloc ---
    fmode_t mode;
    struct file_operations *ops;
    void *private;

    // pipe: struct pipe *
    // normal file: struct inode*
};

struct file_operations {
    int (*read)(struct file *file, char *__user buf, int len);
    int (*write)(struct file *file, char *__user buf, int len);
    // int (*stat)(struct file *file, struct stat *st);
    int (*close)(struct file *file);
};

#define FMODE_READ  0x1
#define FMODE_WRITE 0x2

#define FMODE_DEVICE 0x100
#define FTYPE_PIPE   0x200

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// fs.c
void fs_init();

// bio.c
void bio_init(void);
struct buf *bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

// file.c
void file_init(void);
struct file *filealloc(void);
void fget(struct file *f);
void fput(struct file *f);
int fileread(struct file *f, char *__user buf, int len);
int filewrite(struct file *f, char *__user buf, int len);

// pipe.c
int pipealloc(struct file **f0, struct file **f1);
int pipeclose(struct file *f);
int pipewrite(struct file *f, char *__user addr, int n);
int piperead(struct file *f, char *__user addr, int n);

#endif