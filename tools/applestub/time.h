#pragma once
#include <sys/types.h>
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; char *tm_zone; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 6
#define CLOCK_THREAD_CPUTIME_ID 16
struct tm *localtime(const time_t *); time_t mktime(struct tm *); struct tm *gmtime_r(const time_t *, struct tm *); time_t timegm(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *); int nanosleep(const struct timespec *, struct timespec *);
time_t time(time_t *); struct tm *localtime_r(const time_t *, struct tm *); int clock_gettime(clockid_t, struct timespec *); unsigned long long clock_gettime_nsec_np(clockid_t);
