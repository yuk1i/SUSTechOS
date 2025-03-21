#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "../../os/types.h"
#include "../../os/syscall_ids.h"

// we only put syscall prototypes here, usys.pl will generate the actual syscall entry code
int fork();
int exec(char *path, char *argv[]);
void __attribute__((noreturn)) exit(int status);
void kill(int pid);
int wait(int pid, int *status);
int getpid();
int getppid();

int sleep(int ticks);
void yield();

void *sbrk(int increment);

int read(int fd, void *buf, int count);
int write(int fd, void *buf, int count);

int ktest(int type, void * arg, uint64 len);

#endif // __SYSCALL_H
