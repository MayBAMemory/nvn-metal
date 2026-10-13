#pragma once
/* 最小替身（syntax_check.py） */
#include <stdint.h>
struct mach_header_64; uint8_t *getsectiondata(const struct mach_header_64 *, const char *, const char *, unsigned long *);
