#pragma once
#include <sys/types.h>
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *); void *aligned_alloc(size_t, size_t);
void abort(void) __attribute__((noreturn)); void exit(int) __attribute__((noreturn)); char *getenv(const char *); int setenv(const char *, const char *, int);
int atoi(const char *); long strtol(const char *, char **, int); long long strtoll(const char *, char **, int); unsigned long strtoul(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int); float strtof(const char *, char **); double strtod(const char *, char **);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
long atol(const char *); unsigned int arc4random(void); void arc4random_buf(void *, size_t); unsigned int arc4random_uniform(unsigned int);
int posix_memalign(void **, size_t, size_t); int abs(int); long labs(long); long long llabs(long long); int unsetenv(const char *); int atexit(void (*)(void));
