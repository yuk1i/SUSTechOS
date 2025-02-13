#include <stdarg.h>

#include "../defs.h"
#include "../lock.h"
#include "../log.h"
#include "virtio_console.h"

static char digits[] = "0123456789abcdef";
extern volatile int panicked;

static uint64 print_lock = 0;

static void printint(int xx, int base, int sign) {
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0) virtio_putchar(buf[i]);
}

static void printptr(uint64 x) {
    int i;
    virtio_putchar('0');
    virtio_putchar('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4) virtio_putchar(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
static void vprintf(char *fmt, va_list ap) {
    int i, c;
    char *s;

    if (fmt == 0)
        panic("null fmt");

    // we use a simple local lock, to avoid accidentally open the intr by pop_off.
    int intr = intr_off();
    while (__sync_lock_test_and_set(&print_lock, 1) != 0);
    __sync_synchronize();

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            virtio_putchar(c);
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
            case 'p':
                printptr(va_arg(ap, uint64));
                break;
            case 's':
                if ((s = va_arg(ap, char *)) == 0)
                    s = "(null)";
                for (; *s; s++) virtio_putchar(*s);
                break;
            case 'c':
                virtio_putchar(va_arg(ap, int));
                break;
            case '%':
                virtio_putchar('%');
                break;
            default:
                // Print unknown % sequence to draw attention.
                virtio_putchar('%');
                virtio_putchar(c);
                break;
        }
    }

    __sync_synchronize();
    __sync_lock_release(&print_lock);
    if (intr)
        intr_on();
}

void ktest_printf(char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
