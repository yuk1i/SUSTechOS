#ifndef __SIGNAL_H__
#define __SIGNAL_H__

#include "../types.h"

#define SIGUSR0 1
#define SIGUSR1 2
#define SIGUSR2 3
#define SIGKILL 4

#define SIGTERM 5
#define SIGCHLD 6
#define SIGSTOP 7
#define SIGCONT 8
#define SIGSEGV 9
#define SIGINT  10

#define SIGMIN SIGUSR0
#define SIGMAX SIGINT

#define sigmask(signo) (1 << (signo))

typedef uint64 sigset_t;
typedef struct sigaction sigaction_t;

typedef struct siginfo {
    int si_signo;
    int si_code;
    int si_pid;
    int si_status;
    void* addr;
} siginfo_t;

struct sigaction {
    void (*sa_sigaction)(int, siginfo_t*, void *);
    sigset_t sa_mask;
    void (*sa_restorer)(void);
};

struct ucontext {
    sigset_t uc_sigmask;
    struct mcontext {
        uint64 epc;
        uint64 regs[31];
    } uc_mcontext;
};

// sigaction:
#define SIG_DFL  ((void *)0)
#define SIG_IGN  ((void *)1)

// used in sigprocmask
#define SIG_BLOCK   1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

// sigset manipulate inline function

static inline void sigemptyset(sigset_t *set) {
    *set = 0;
}

static inline void sigfillset(sigset_t *set) {
    *set = -1;
}

static inline void sigaddset(sigset_t *set, int signo) {
    *set |= 1 << signo;
}

static inline void sigdelset(sigset_t *set, int signo) {
    *set &= ~(1 << signo);
}

static inline int sigismember(const sigset_t *set, int signo) {
    return *set & (1 << signo);
}

#endif  //__SIGNAL_H__