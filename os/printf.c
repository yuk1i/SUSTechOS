#include <stdarg.h>

#include "console.h"
#include "defs.h"
#include "lock.h"
#include "log.h"

static char digits[] = "0123456789abcdef";
extern volatile int panicked;

static uint64 kernelprint_lock = 0;

// Kernel's output should be prioritized.
void acquire_kprint(void) {
    while (__sync_lock_test_and_set(&kernelprint_lock, 1) != 0);
    __sync_synchronize();
}

void release_kprint(void) {
    __sync_synchronize();
    __sync_lock_release(&kernelprint_lock);
}

static void printint(int xx, int base, int sign) {
    char buf[16];
    int i;
    uint x;

    if (sign == 1 && xx < 0)
        x = -xx;
    else
        x = xx;

    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign == 1 && xx < 0)
        buf[i++] = '-';
    else if (sign < 0) {
        // padding with '0' for hex.
        while (i < -sign) buf[i++] = '0';
    }

    while (--i >= 0) consputc(buf[i]);
}

static void printptr(uint64 x) {
    int i;
    consputc('0');
    consputc('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4) consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
static void vprintf(char *fmt, va_list ap) {
    int i, c;
    char *s;

    if (fmt == 0)
        panic("null fmt");

    // we use a simple local lock, to ensure printf is available before struct cpu init.
    int intr     = intr_off();
    int panicked = panicked;
    if (!panicked) {
        acquire_kprint();
    }

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c) {
            case 'd':
                printint(va_arg(ap, int), 10, 1);
                break;
            case 'x':
                printint(va_arg(ap, int), 16, 1);
                break;
            case 'X':
                printint(va_arg(ap, int), 16, -2);
                break;
            case 'p':
                printptr(va_arg(ap, uint64));
                break;
            case 's':
                if ((s = va_arg(ap, char *)) == 0)
                    s = "(null)";
                for (; *s; s++) consputc(*s);
                break;
            case 'c':
                consputc(va_arg(ap, int));
                break;
            case '%':
                consputc('%');
                break;
            default:
                // Print unknown % sequence to draw attention.
                consputc('%');
                consputc(c);
                break;
        }
    }

    if (!panicked) {
        release_kprint();
    }
    if (intr)
        intr_on();
}

void printf(char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

__noreturn void __panic(char *fmt, ...) {
    va_list ap;

    intr_off();

    panicked = 1;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    while (1) asm volatile("nop" ::: "memory");

    __builtin_unreachable();
}