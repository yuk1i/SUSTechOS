#include "ksignal.h"

#include <defs.h>
#include <proc.h>
#include <trap.h>

int siginit(struct proc *p) {
    // init p->signal
    return 0;
}

int siginit_fork(struct proc *parent, struct proc *child) {
    return 0;
}

int siginit_exec(struct proc *p) {
    return 0;
}

int do_signal(void) {
    assert(!intr_get());

    return 0;
}

// syscall handler:
int sys_sigaction(int signo, const sigaction_t __user *act, sigaction_t __user *oldact) {
    return 0;
}

int sys_sigreturn() {
    return 0;
}

int sys_sigprocmask(int how, const sigset_t __user *set, sigset_t __user *oldset) {
    return 0;
}

int sys_sigpending(sigset_t __user *set) {
    return 0;
}

int sys_sigkill(int pid, int signo, int code) {
    return 0;
}