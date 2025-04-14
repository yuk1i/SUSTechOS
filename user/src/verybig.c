#include "../lib/user.h"

char hugebuf[4096 * (200 - 19)];
// verybig should use 200 pages of memory.
// 19 pages are used by the stack, pagetable and so on.

int main() {
    yield();
    for (char *p = hugebuf; p < hugebuf + sizeof(hugebuf); p += 4096) {
        *(uint64*)p = (uint64) p;
        sleep(1);
    }
    sleep(20);

    for (char *p = hugebuf + sizeof(hugebuf) - 4096; p >= hugebuf; p -= 4096) {
        assert_eq(*(uint64*)p, (uint64)p);
        sleep(1);
    }
    exit(0);
    return 0;
}