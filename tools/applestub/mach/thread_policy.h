#pragma once
/* 最小替身（syntax_check.py） */
#include <mach/mach.h>
typedef struct { integer_t affinity_tag; } thread_affinity_policy_data_t;
typedef struct { integer_t importance; } thread_precedence_policy_data_t;
#define THREAD_AFFINITY_POLICY 4
#define THREAD_AFFINITY_POLICY_COUNT 1
#define THREAD_PRECEDENCE_POLICY 3
#define THREAD_PRECEDENCE_POLICY_COUNT 1
kern_return_t thread_policy_set(thread_act_t, thread_policy_flavor_t, thread_policy_t, mach_msg_type_number_t);
typedef struct { uint32_t period, computation, constraint; boolean_t preemptible; } thread_time_constraint_policy_data_t;
#define THREAD_TIME_CONSTRAINT_POLICY 2
#define THREAD_TIME_CONSTRAINT_POLICY_COUNT 4
kern_return_t thread_policy_get(thread_act_t, thread_policy_flavor_t, thread_policy_t, mach_msg_type_number_t *, boolean_t *);
