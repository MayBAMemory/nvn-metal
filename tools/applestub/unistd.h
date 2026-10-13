#pragma once
#include <sys/types.h>
pid_t getpid(void); int usleep(useconds_t); unsigned int sleep(unsigned int); ssize_t pread(int, void *, size_t, off_t); int close(int);
void _exit(int) __attribute__((noreturn));
int unlink(const char *); int rmdir(const char *); int access(const char *, int); ssize_t write(int, const void *, size_t); ssize_t read(int, void *, size_t);
ssize_t pwrite(int, const void *, size_t, off_t); int ftruncate(int, off_t); int fsync(int); long sysconf(int); char *getcwd(char *, size_t); int chdir(const char *);
int isatty(int); off_t lseek(int, off_t, int); int getpagesize(void); int dup(int); int dup2(int, int); int pipe(int[2]); int renamex_np(const char *, const char *, unsigned int);
#define F_OK 0
#define W_OK 2
#define R_OK 4
#define _SC_PAGESIZE 29
#define RENAME_SWAP 2
#define RENAME_EXCL 4
