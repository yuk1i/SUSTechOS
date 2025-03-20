#include "../../os/ktest/ktest.h"
#include "../lib/user.h"

// Base Checkpoint 1: sigaction, sigkill, and sigreturn

// send SIGUSR0 to a child process, which default action is to terminate it.
void basic1(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sleep(10);
        exit(1);
    } else {
        // parent
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGUSR0);
    }
}

// send SIGUSR0 to a child process, but should be ignored.
void basic2(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = SIG_IGN,
            .sa_mask      = 0,
            .sa_restorer  = NULL,
        };
        sigaction(SIGUSR0, &sa, 0);
        sleep(30);
        exit(1);
    } else {
        // parent
        sleep(5);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert(ret == 1);
    }
}

void handler3(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    getpid();
    sleep(1);
    exit(103);
}

// set handler for SIGUSR0
void basic3(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler3,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (1);
        exit(1);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 103);
    }
}

volatile int handler4_flag = 0;
void handler4(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    sleep(1);
    sleep(1);
    fprintf(1, "handler4 triggered\n");
    handler4_flag = 1;
}

// set handler for SIGUSR0, and return from handler.
void basic4(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler4,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (handler4_flag == 0);
        exit(104);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 104);
    }
}

static volatile int handler5_cnt = 0;
void handler5(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    static volatile int nonreentrace = 0;
    assert(!nonreentrace);    // non-reentrance
    nonreentrace = 1;
    sleep(5);
    sleep(5);
    if (handler5_cnt < 5)
        sigkill(getpid(), SIGUSR0, 0);
    sleep(5);
    sleep(5);
    fprintf(1, "handler5 triggered\n");
    nonreentrace = 0;
    handler5_cnt++;
}

// set handler for SIGUSR0, kernel should block it from re-entrance.
// signal handler itself should not be reentrant.
void basic5(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler5,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (handler5_cnt < 5);
        exit(105);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 105);
    }
}

// Base Checkpoint: SIGKILL

// child process is killed by signal: SIGKILL
void basic10(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        while (1);
        exit(1);
    } else {
        // parent
        sigkill(pid, SIGKILL, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGKILL);
    }
}