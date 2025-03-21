#include "../../os/types.h"
#include "syscall.h"

// stdlib.c
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, int);
uint strlen(const char *);

void *memset(void *, int, uint);
void *memmove(void *, const void *, int);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);

char *strchr(const char *, char c);
char *gets(char *, int max);
int putchar(char c);
int puts(char *buf);
int atoi(const char *);

// printf.c
void fprintf(int, const char *, ...);
void printf(const char *, ...);
void stdout_flush(void);
void stdout_nobuf(void);

// umalloc.c
void *malloc(uint);
void free(void *);

// assert
#define _STRINGIFY(s) #s
#define STRINGIFY(s)  _STRINGIFY(s)

#define panic(fmt, ...)                                                                                                    \
    do {                                                                                                                   \
        int tid = getpid();                                                                                                \
        printf("\x1b[%dm[%s %d][%s:%d]: " fmt "\x1b[0m\n", 31, "PANIC", tid, __FILE__, __LINE__, ##__VA_ARGS__); \
        exit(-1);                                                                                                          \
    } while (0)

#ifndef assert
#define assert(f) \
    if (!(f))     \
    panic("assert error: " STRINGIFY(f))
#endif

#ifndef assert_eq
#define assert_eq(a, b)         \
    if (!((a) == (b)))          \
        panic(                  \
            "assert_eq error: " \
            "%p != %p",         \
            a,                  \
            b);
#endif