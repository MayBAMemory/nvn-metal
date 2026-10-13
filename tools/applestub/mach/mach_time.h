#pragma once
/* 最小替身（syntax_check.py） */
#include <stdint.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t; int mach_timebase_info(mach_timebase_info_data_t *); uint64_t mach_absolute_time(void); uint64_t mach_continuous_time(void);
