#pragma once
/* 最小替身（syntax_check.py） */
#include <sys/time.h>
struct rusage { struct timeval ru_utime, ru_stime; long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw; };
#define RUSAGE_SELF 0
#define RUSAGE_INFO_V4 4
int getrusage(int, struct rusage *); int setpriority(int, unsigned, int); int getpriority(int, unsigned);
#define PRIO_DARWIN_THREAD 3
#define PRIO_DARWIN_BG 0x1000
