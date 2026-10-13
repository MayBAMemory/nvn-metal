#pragma once
#include <sys/time.h>
#define CTL_KERN 1
#define KERN_PROC 14
#define KERN_PROC_PID 1
struct extern_proc { struct timeval p_starttime; char _rest[256]; };
struct kinfo_proc { struct extern_proc kp_proc; char _eproc[512]; };
int sysctl(int *, unsigned int, void *, size_t *, void *, size_t);
