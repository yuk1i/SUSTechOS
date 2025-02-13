#include "ktest.h"

#include "ktest_printf.h"
#include "virtio_console.h"

void ktest_assert(int cond, char* info) {
    if (cond) {
        return;
    }
    ktest_printf("Condition failed. Info: %s", info);
}

void ktest_assert_eq(int left, int right) {
    if (left == right) {
        return;
    }
    ktest_printf("Assert failed, left: %d, right: %d", left, right);
}

void ktest_init() {
    virtio_console_init();
}
