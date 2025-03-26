#include "defs.h"
#include "fs.h"

#define PIPESIZE 512

struct pipe {
    struct spinlock lock;
    char data[PIPESIZE];
    uint nread;     // number of bytes read
    uint nwrite;    // number of bytes written
    int readopen;   // read fd is still open
    int writeopen;  // write fd is still open
};

static struct file_operations pipeops = {
    .read  = piperead,
    .write = pipewrite,
    .close = pipeclose,
};

int pipealloc(struct file **f0, struct file **f1) {
    struct pipe *pi;
    void *pa = NULL;

    pi  = NULL;
    *f0 = *f1 = NULL;
    if ((*f0 = filealloc()) == NULL || (*f1 = filealloc()) == NULL)
        goto bad;
    if ((pa = kallocpage()) == NULL)
        goto bad;
    pi            = (struct pipe *)PA_TO_KVA(pa);
    pi->readopen  = 1;
    pi->writeopen = 1;
    pi->nwrite    = 0;
    pi->nread     = 0;
    spinlock_init(&pi->lock, "pipe");
    (*f0)->mode    = FMODE_READ | FTYPE_PIPE;
    (*f0)->ops     = &pipeops;
    (*f0)->private = pi;

    (*f1)->mode    = FMODE_WRITE | FTYPE_PIPE;
    (*f1)->ops     = &pipeops;
    (*f1)->private = pi;
    return 0;

bad:
    if (pa)
        kfreepage(pa);
    if (*f0)
        fput(*f0);
    if (*f1)
        fput(*f1);
    return -1;
}

int pipeclose(struct file *f) {
    struct pipe *pi = f->private;
    int writable    = (f->mode & FMODE_WRITE) != 0;

    acquire(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        wakeup(&pi->nread);
    } else {
        pi->readopen = 0;
        wakeup(&pi->nwrite);
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release(&pi->lock);
        kfreepage((void *)KVA_TO_PA(pi));
    } else
        release(&pi->lock);
    return 0;
}

int pipewrite(struct file *f, char *__user addr, int n) {
    int i           = 0;
    struct proc *pr = curr_proc();
    struct pipe *pi = f->private;

    acquire(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || iskilled(pr)) {
            release(&pi->lock);
            return -1;
        }
        if (pi->nwrite == pi->nread + PIPESIZE) {  // DOC: pipewrite-full
            wakeup(&pi->nread);
            sleep(&pi->nwrite, &pi->lock);
        } else {
            char ch;
            acquire(&pr->mm->lock);
            if (copy_from_user(pr->mm, &ch, (uint64)addr + i, 1) == -1) {
                release(&pr->mm->lock);
                break;
            }
            release(&pr->mm->lock);
            pi->data[pi->nwrite++ % PIPESIZE] = ch;
            i++;
        }
    }
    wakeup(&pi->nread);
    release(&pi->lock);

    return i;
}

int piperead(struct file *f, char *__user addr, int n) {
    int i;
    struct proc *pr = curr_proc();
    struct pipe *pi = f->private;
    char ch;

    acquire(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen) {  // DOC: pipe-empty
        if (iskilled(pr)) {
            release(&pi->lock);
            return -1;
        }
        sleep(&pi->nread, &pi->lock);  // DOC: piperead-sleep
    }
    for (i = 0; i < n; i++) {  // DOC: piperead-copy
        if (pi->nread == pi->nwrite)
            break;
        ch = pi->data[pi->nread++ % PIPESIZE];
        acquire(&pr->mm->lock);
        if (copy_to_user(pr->mm, (uint64)addr + i, &ch, 1) == -1) {
            release(&pr->mm->lock);
            break;
        }
        release(&pr->mm->lock);
    }
    wakeup(&pi->nwrite);  // DOC: piperead-wakeup
    release(&pi->lock);
    return i;
}
