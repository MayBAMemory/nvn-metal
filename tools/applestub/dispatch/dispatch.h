#pragma once
/* 最小替身（syntax_check.py） */
#include <stdint.h>
typedef struct dispatch_semaphore_s *dispatch_semaphore_t; typedef uint64_t dispatch_time_t; typedef struct dispatch_queue_s *dispatch_queue_t;
#define DISPATCH_TIME_NOW 0ull
#define DISPATCH_TIME_FOREVER (~0ull)
dispatch_semaphore_t dispatch_semaphore_create(long); long dispatch_semaphore_wait(dispatch_semaphore_t, dispatch_time_t); long dispatch_semaphore_signal(dispatch_semaphore_t);
void dispatch_release(void *); dispatch_time_t dispatch_time(dispatch_time_t, int64_t);
dispatch_queue_t dispatch_get_main_queue(void); void dispatch_async(dispatch_queue_t, void (^)(void));
