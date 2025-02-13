#ifndef KTEST_H
#define KTEST_H

#include "../types.h"

void ktest_assert(int cond, char* info);
void ktest_assert_eq(int left, int right);
void ktest_init();

#endif  // KTEST_H
