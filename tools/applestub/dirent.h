#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/types.h>
struct dirent { unsigned long long d_ino, d_seekoff; unsigned short d_reclen, d_namlen; unsigned char d_type; char d_name[1024]; };
typedef struct _DIR DIR;
DIR *opendir(const char *); struct dirent *readdir(DIR *); int closedir(DIR *); int dirfd(DIR *); DIR *fdopendir(int);
#define DT_DIR 4
#define DT_REG 8
