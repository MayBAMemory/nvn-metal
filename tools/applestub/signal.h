#pragma once
/* 最小替身（syntax_check.py）：arm64 Darwin 的信号与线程现场布局（按 <arm/_mcontext.h>） */
#include <sys/types.h>
#include <stdint.h>
typedef uint32_t sigset_t; typedef int sig_atomic_t;
typedef struct { void *ss_sp; size_t ss_size; int ss_flags; } stack_t;
union sigval { int sival_int; void *sival_ptr; };
typedef struct { int si_signo, si_errno, si_code; pid_t si_pid; unsigned si_uid; int si_status; void *si_addr; union sigval si_value; long si_band; unsigned long __pad[7]; } siginfo_t;
struct __darwin_arm_exception_state64 { uint64_t __far; uint32_t __esr, __exception; };
struct __darwin_arm_thread_state64 { uint64_t __x[29], __fp, __lr, __sp, __pc; uint32_t __cpsr, __pad; };
struct __darwin_arm_neon_state64 { __uint128_t __v[32]; uint32_t __fpsr, __fpcr; };
struct __darwin_mcontext64 { struct __darwin_arm_exception_state64 __es; struct __darwin_arm_thread_state64 __ss; struct __darwin_arm_neon_state64 __ns; };
typedef struct __darwin_mcontext64 *mcontext_t;
typedef struct __darwin_ucontext { int uc_onstack; sigset_t uc_sigmask; stack_t uc_stack; struct __darwin_ucontext *uc_link; size_t uc_mcsize; mcontext_t uc_mcontext; } ucontext_t;
struct sigaction { union { void (*__sa_handler)(int); void (*__sa_sigaction)(int, siginfo_t *, void *); } __sigaction_u; sigset_t sa_mask; int sa_flags; };
#define sa_handler __sigaction_u.__sa_handler
#define sa_sigaction __sigaction_u.__sa_sigaction
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 10
#define SIGFPE 8
#define SIGKILL 9
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGURG 16
#define SIGUSR1 30
#define SIGUSR2 31
#define SIGPROF 27
#define SA_ONSTACK 0x0001
#define SA_RESTART 0x0002
#define SA_NODEFER 0x0010
#define SA_RESETHAND 0x0004
#define SA_SIGINFO 0x0040
#define SS_DISABLE 4
#define SIGSTKSZ 131072
#define MINSIGSTKSZ 32768
#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
int sigaction(int, const struct sigaction *, struct sigaction *); int sigaltstack(const stack_t *, stack_t *); int sigemptyset(sigset_t *); int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int); int sigdelset(sigset_t *, int); int sigprocmask(int, const sigset_t *, sigset_t *); int pthread_sigmask(int, const sigset_t *, sigset_t *);
int raise(int); int kill(pid_t, int); void (*signal(int, void (*)(int)))(int);
