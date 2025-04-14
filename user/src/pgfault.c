#include "../../os/ktest/ktest.h"
#include "../lib/user.h"

int main() {
    int pid;
    char* verybig[] = {"verybig", 0};

    printf(
        "=====\n Page Fault Lab: \n"
        "    demand paging: fork & exec verybig.\n"
        "    verybig allocates a huge buf in its bss segment, and access them one by one.\n"
        "    You will see the page fault handler actually allocate and fill out the page, which is marked as demand paging.\n"
        "====\n\n");
    sleep(100);

    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // child
        exec(verybig[0], verybig);
        exit(101);
    } else {
        // parent
        int status;
        wait(-1, &status);
        assert(status == 0);
    }

    printf(
        "=====\n Page Fault Lab: \n"
        "    swap: set kernel's free page count to 150, and fork & exec verybig.\n"
        "    verybig should consume 200 pages.\n"
        "    under a unmodified kernel, the second `exec` should fail, \n"
        "     because there are no sufficient free pages to be allocated in `load_user_elf`.\n"
        "====\n\n");
    sleep(100);

    ktest(KTEST_SET_NRFREEPGS, (void*)150, 0);
    printf("kernel # of free pages: %d\n", ktest(KTEST_GET_NRFREEPGS, NULL, 0));

    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // child
        exec(verybig[0], verybig);
        printf("exec fails\n");
        exit(101);
    }
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // child
        exec(verybig[0], verybig);
        printf("exec fails\n");
        exit(101);
    }
    // parent
    int status;
    while (wait(-1, &status) > 0) {
        assert_eq(status, 0);
    }

    return 0;
}