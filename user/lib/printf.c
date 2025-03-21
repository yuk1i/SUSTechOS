#include "syscall.h"
#include "user.h"

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type)   (__builtin_va_arg(ap, type))
#define va_end(ap)         (__builtin_va_end(ap))
#define va_copy(d, s)      (__builtin_va_copy(d, s))
typedef __builtin_va_list va_list;

static char digits[] = "0123456789ABCDEF";

struct print_buf {
    char *buf;
    int fd;
    int size;
    int len;
};

static char stdout_buf[1024];
static struct print_buf stdoutbuf = {
    .buf  = stdout_buf,
    .fd   = 1,
    .len  = 0,
    .size = sizeof(stdout_buf),
};

void stdout_flush(void) {
    if (stdoutbuf.len > 0) {
        write(stdoutbuf.fd, stdoutbuf.buf, stdoutbuf.len);
        stdoutbuf.len = 0;
    }
}

void stdout_nobuf(void) {
    stdout_flush();
    stdoutbuf.buf = NULL;
}

static void putc(struct print_buf *pb, char c) {
    if (pb->buf) {
        if (pb->len == pb->size) {
            write(pb->fd, pb->buf, pb->len);
            pb->len = 0;
        } else {
            pb->buf[pb->len] = c;
            pb->len++;
            if (c == '\r' || c == '\n') {
                // flush buffer
                write(pb->fd, pb->buf, pb->len);
                pb->len = 0;
            }
        }
    } else {
        write(pb->fd, &c, 1);
    }
}

static void printint(struct print_buf *pb, int xx, int base, int sgn) {
    char buf[16];
    int i, neg;
    uint x;

    neg = 0;
    if (sgn && xx < 0) {
        neg = 1;
        x   = -xx;
    } else {
        x = xx;
    }

    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);
    if (neg)
        buf[i++] = '-';

    while (--i >= 0) {
        putc(pb, buf[i]);
    }
}

static void printptr(struct print_buf *pb, uint64 x) {
    int i;
    putc(pb, '0');
    putc(pb, 'x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4) {
        putc(pb, digits[x >> (sizeof(uint64) * 8 - 4)]);
    }
}

// Print to the given fd. Only understands %d, %x, %p, %s.
static void vprintf(struct print_buf *pb, const char *fmt, va_list ap) {
    char *s;
    int c, i, state;

    state = 0;
    for (i = 0; fmt[i]; i++) {
        c = fmt[i] & 0xff;
        if (state == 0) {
            if (c == '%') {
                state = '%';
            } else {
                putc(pb, c);
            }
        } else if (state == '%') {
            if (c == 'd') {
                printint(pb, va_arg(ap, int), 10, 1);
            } else if (c == 'l') {
                printint(pb, va_arg(ap, uint64), 10, 0);
            } else if (c == 'x') {
                printint(pb, va_arg(ap, int), 16, 0);
            } else if (c == 'p') {
                printptr(pb, va_arg(ap, uint64));
            } else if (c == 's') {
                s = va_arg(ap, char *);
                if (s == 0)
                    s = "(null)";
                while (*s != 0) {
                    putc(pb, *s);
                    s++;
                }
            } else if (c == 'c') {
                putc(pb, va_arg(ap, uint));
            } else if (c == '%') {
                putc(pb, c);
            } else {
                // Unknown % sequence.  Print it to draw attention.
                putc(pb, '%');
                putc(pb, c);
            }
            state = 0;
        }
    }
}

void fprintf(int fd, const char *fmt, ...) {
    va_list ap;

    struct print_buf dummy = {
        .buf  = NULL,
        .fd   = fd,
        .len  = 0,
        .size = 0,
    };

    va_start(ap, fmt);
    vprintf(&dummy, fmt, ap);
}

void printf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(&stdoutbuf, fmt, ap);
}
