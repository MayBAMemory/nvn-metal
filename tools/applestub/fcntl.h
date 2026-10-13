#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/types.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 8
#define O_CREAT 0x200
#define O_TRUNC 0x400
#define O_EXCL 0x800
#define O_DIRECTORY 0x100000
#define O_CLOEXEC 0x1000000
#define F_GETFL 3
#define F_SETFL 4
#define F_RDAHEAD 45
#define F_NOCACHE 48
#define F_FULLFSYNC 51
int open(const char *, int, ...); int openat(int, const char *, int, ...); int fcntl(int, int, ...);
