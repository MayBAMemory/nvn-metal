#pragma once
#include <sys/types.h>
#include <stdarg.h>
typedef struct __sFILE FILE; extern FILE *__stderrp; extern FILE *__stdoutp;
#define stderr __stderrp
#define stdout __stdoutp
int fprintf(FILE *, const char *, ...) __attribute__((format(printf, 2, 3))); int printf(const char *, ...) __attribute__((format(printf, 1, 2))); int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4))); int vsnprintf(char *, size_t, const char *, va_list);
int fputc(int, FILE *); int fputs(const char *, FILE *); FILE *fopen(const char *, const char *); int fclose(FILE *); int fflush(FILE *);
size_t fread(void *, size_t, size_t, FILE *); size_t fwrite(const void *, size_t, size_t, FILE *); int fscanf(FILE *, const char *, ...); int sscanf(const char *, const char *, ...);
int rename(const char *, const char *); int remove(const char *); int sprintf(char *, const char *, ...) __attribute__((format(printf, 2, 3))); char *fgets(char *, int, FILE *); int fileno(FILE *); int setvbuf(FILE *, char *, int, size_t);
int ferror(FILE *); int feof(FILE *); long ftell(FILE *); int fseek(FILE *, long, int); int vfprintf(FILE *, const char *, va_list);
#define SEEK_SET 0
#define SEEK_END 2
#define _IOLBF 1
#define _IONBF 2
