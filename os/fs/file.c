#include "defs.h"
#include "fs.h"

spinlock_t ftable_lock;
allocator_t file_allocator;

void file_init(void) {
    spinlock_init(&ftable_lock, "ftable");
    allocator_init(&file_allocator, "file", sizeof(struct file), NFILE);
}

/**
 * @brief Allocate a file structure.
 *
 * caller must initialize the returned file's fields.
 *
 * @return struct file*
 */
struct file* filealloc(void) {
    struct file* f = kalloc(&file_allocator);

    spinlock_init(&f->lock, "filelock");
    f->ref = 1;
    f->pos = 0;
    // do not touch another fields

    return f;
}

/**
 * @brief increase ref count for file f.
 *
 * @param f
 */
void fget(struct file* f) {
    assert(f != NULL);
    
    acquire(&f->lock);
    f->ref++;
    release(&f->lock);
}

/**
 * @brief decrease ref count for file f. If refcount drops to zero, free the file.
 *
 * @param f
 */
void fput(struct file* f) {
    assert(f != NULL);
    
    acquire(&f->lock);
    if (--f->ref > 0) {
        release(&f->lock);
        return;
    }
    if (f->ops->close)
        f->ops->close(f);  // ops->close is called with f->lock held
    release(&f->lock);
    kfree(&file_allocator, f);
}

int fileread(struct file* f, char* buf, int len) {
    assert(f != NULL);
    
    int ret = -EINVAL;
    // increase the refcnt instead of holding the lock
    fget(f);

    if (!(f->mode & FMODE_READ))
        goto out;

    // accessing f->ops does not require holding the lock
    //  , because f->ops is set when the file is created and never changed
    if (f->ops->read)
        ret = f->ops->read(f, buf, len);

out:
    fput(f);
    return ret;
}

int filewrite(struct file* f, char* buf, int len) {
    assert(f != NULL);

    int ret = -EINVAL;
    fget(f);
    if (!(f->mode & FMODE_WRITE))
        goto out;

    if (f->ops->write)
        ret = f->ops->write(f, buf, len);

out:
    fput(f);
    return ret;
}