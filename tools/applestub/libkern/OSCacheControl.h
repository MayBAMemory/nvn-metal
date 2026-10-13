#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/types.h>
void sys_icache_invalidate(void *, size_t); void sys_dcache_flush(void *, size_t);
