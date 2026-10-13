#pragma once
/* 最小替身（syntax_check.py） */
void *dlopen(const char *, int); char *dlerror(void); void *dlsym(void *, const char *);
#define RTLD_NOW 2
#define RTLD_DEFAULT ((void *)-2)
#define RTLD_LAZY 1
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
int dladdr(const void *, Dl_info *); int dlclose(void *);
