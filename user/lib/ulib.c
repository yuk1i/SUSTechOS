#include "../../os/types.h"
#include "syscall.h"

char *strcpy(char *s, const char *t) {
    char *os;

    os = s;
    while ((*s++ = *t++) != 0);
    return os;
}

int strcmp(const char *p, const char *q) {
    while (*p && *p == *q) p++, q++;
    return (uchar)*p - (uchar)*q;
}

int strncmp(const char *_l, const char *_r, uint64 n) {
    const unsigned char *l = (void *)_l, *r = (void *)_r;
    if (!n--)
        return 0;
    for (; *l && *r && n && *l == *r; l++, r++, n--);
    return *l - *r;
}

uint strlen(const char *s) {
    int n;

    for (n = 0; s[n]; n++);
    return n;
}

void *memset(void *dst, int c, uint n) {
    char *cdst = (char *)dst;
    int i;
    for (i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

void *memmove(void *vdst, const void *vsrc, int n) {
    char *dst;
    const char *src;

    dst = vdst;
    src = vsrc;
    if (src > dst) {
        while (n-- > 0) *dst++ = *src++;
    } else {
        dst += n;
        src += n;
        while (n-- > 0) *--dst = *--src;
    }
    return vdst;
}

int memcmp(const void *s1, const void *s2, uint n) {
    const char *p1 = s1, *p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void *memcpy(void *dst, const void *src, uint n) {
    return memmove(dst, src, n);
}

char *strchr(const char *s, char c) {
    for (; *s; s++)
        if (*s == c)
            return (char *)s;
    return 0;
}

char *gets(char *buf, int max) {
    int i, cc;
    char c;

    for (i = 0; i + 1 < max;) {
        cc = read(0, &c, 1);
        if (cc < 1)
            break;
        buf[i++] = c;
        if (c == '\n' || c == '\r')
            break;
    }
    buf[i] = '\0';
    return buf;
}

int putch(char c) {
	return write(1, &c, 1);
}

int putchar(char* buf) {
	int cc;
	cc = write(1, buf, strlen(buf));
	return cc;
}

int atoi(const char *s) {
    int n;

    n = 0;
    while ('0' <= *s && *s <= '9') n = n * 10 + *s++ - '0';
    return n;
}
