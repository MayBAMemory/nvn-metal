#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/types.h>
ssize_t getxattr(const char *, const char *, void *, size_t, unsigned int, int);
int setxattr(const char *, const char *, const void *, size_t, unsigned int, int);
int removexattr(const char *, const char *, int);
#define XATTR_NOFOLLOW 1
