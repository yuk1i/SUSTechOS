#ifndef __KSIGNAL_H__
#define __KSIGNAL_H__

#include <vm.h>
#include "signal.h"

struct ksignal {
    sigaction_t sa[SIGMAX + 1];
    siginfo_t siginfos[SIGMAX + 1];
    sigset_t sigmask;       // signal mask, when set to 1, the signal is blocked
    sigset_t sigpending;
};

struct proc;  // forward declaration
int siginit(struct proc *p);
int siginit_fork(struct proc *parent, struct proc* child);
int siginit_exec(struct proc *p);

int do_signal(void);

// syscall handler:
int sys_sigaction(int signo, const sigaction_t __user *act, sigaction_t __user *oldact);
int sys_sigreturn();
int sys_sigprocmask(int how, const sigset_t __user *set, sigset_t __user *oldset);
int sys_sigpending(sigset_t __user *set);
int sys_sigkill(int pid, int signo, int code);

#endif