#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/types.h>
#include <time.h>
typedef unsigned short mode_t;
struct stat { int st_dev; mode_t st_mode; unsigned short st_nlink; unsigned long long st_ino; unsigned st_uid, st_gid; int st_rdev;
              struct timespec st_atimespec, st_mtimespec, st_ctimespec, st_birthtimespec; off_t st_size; long long st_blocks; int st_blksize; unsigned st_flags, st_gen; };
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
int stat(const char *, struct stat *); int fstat(int, struct stat *); int lstat(const char *, struct stat *); int mkdir(const char *, mode_t); int fstatat(int, const char *, struct stat *, int);
