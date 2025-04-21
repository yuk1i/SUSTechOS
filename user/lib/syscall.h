#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "../../os/types.h"
#include "../../os/syscall_ids.h"
#include "../../os/signal/signal.h"

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

int sigaction(int signo, const sigaction_t *act, sigaction_t *oldact);
void sigreturn();
int sigkill(int pid, int signo, int code);
int sigpending(sigset_t *set);
int sigprocmask(int how, const sigset_t *newset, sigset_t *oldset);

#endif // __SYSCALL_H
