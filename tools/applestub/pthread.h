#pragma once
#include <sys/types.h>
typedef struct { long _s; char _o[56]; } pthread_mutex_t; typedef struct { long _s; char _o[40]; } pthread_cond_t;
typedef struct { long _s; char _o[56]; } pthread_attr_t; typedef struct { long _s; char _o[8]; } pthread_once_t;
typedef struct _opaque_pthread_t *pthread_t; typedef unsigned long pthread_key_t; typedef struct { long _s; char _o[8]; } pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER {0x32AAABA7, {0}}
#define PTHREAD_COND_INITIALIZER {0x3CB0B1BB, {0}}
#define PTHREAD_ONCE_INIT {0x30B1BCBA, {0}}
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *); int pthread_mutex_lock(pthread_mutex_t *); int pthread_mutex_unlock(pthread_mutex_t *); int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *); int pthread_cond_signal(pthread_cond_t *); int pthread_cond_broadcast(pthread_cond_t *);
struct timespec; int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *); int pthread_join(pthread_t, void **); int pthread_detach(pthread_t);
int pthread_once(pthread_once_t *, void (*)(void)); int pthread_attr_init(pthread_attr_t *); int pthread_attr_setstacksize(pthread_attr_t *, size_t);
int pthread_key_create(pthread_key_t *, void (*)(void *)); void *pthread_getspecific(pthread_key_t); int pthread_setspecific(pthread_key_t, const void *);
#include <time.h>
#include <pthread/qos.h>
#include <mach/mach.h>
void *pthread_get_stackaddr_np(pthread_t); size_t pthread_get_stacksize_np(pthread_t); int pthread_setname_np(const char *); int pthread_kill(pthread_t, int);
int pthread_attr_destroy(pthread_attr_t *); int pthread_attr_set_qos_class_np(pthread_attr_t *, qos_class_t, int); int pthread_key_delete(pthread_key_t);
pthread_t pthread_self(void); int pthread_equal(pthread_t, pthread_t); mach_port_t pthread_mach_thread_np(pthread_t); int pthread_get_qos_class_np(pthread_t, qos_class_t *, int *);
