#pragma once
/* 最小替身（syntax_check.py） */
#include <stdint.h>
#include <sys/types.h>
typedef int kern_return_t; typedef unsigned int mach_port_t; typedef mach_port_t task_t, thread_t, thread_act_t, host_t; typedef int integer_t; typedef unsigned int natural_t;
typedef natural_t mach_msg_type_number_t; typedef uint64_t mach_vm_size_t; typedef uint64_t mach_vm_address_t; typedef int task_flavor_t; typedef integer_t *task_info_t;
typedef int thread_flavor_t; typedef integer_t *thread_info_t; typedef unsigned int thread_policy_flavor_t; typedef integer_t *thread_policy_t;
#define KERN_SUCCESS 0
extern mach_port_t mach_task_self_;
#define mach_task_self() mach_task_self_
mach_port_t mach_thread_self(void); kern_return_t mach_port_deallocate(task_t, mach_port_t);
struct task_vm_info { mach_vm_size_t virtual_size; integer_t region_count, page_size; mach_vm_size_t resident_size, resident_size_peak, device, device_peak, internal, internal_peak, external, external_peak, reusable, reusable_peak, purgeable_volatile_pmap, purgeable_volatile_resident, purgeable_volatile_virtual, compressed, compressed_peak, compressed_lifetime, phys_footprint, min_address, max_address, ledger_phys_footprint_peak; char _more[256]; };
typedef struct task_vm_info task_vm_info_data_t;
#define TASK_VM_INFO 22
#define TASK_VM_INFO_COUNT ((mach_msg_type_number_t)(sizeof(task_vm_info_data_t) / sizeof(natural_t)))
struct task_basic_info_64 { integer_t suspend_count; mach_vm_size_t virtual_size, resident_size; char _t[32]; };
kern_return_t task_info(task_t, task_flavor_t, task_info_t, mach_msg_type_number_t *);
struct thread_basic_info { struct { int seconds, microseconds; } user_time, system_time; integer_t cpu_usage, policy, run_state, flags, suspend_count, sleep_time; };
typedef struct thread_basic_info thread_basic_info_data_t;
#define THREAD_BASIC_INFO 3
#define THREAD_BASIC_INFO_COUNT ((mach_msg_type_number_t)(sizeof(thread_basic_info_data_t) / sizeof(natural_t)))
kern_return_t thread_info(thread_act_t, thread_flavor_t, thread_info_t, mach_msg_type_number_t *);
typedef int boolean_t;
struct thread_extended_info { uint64_t pth_user_time, pth_system_time; int32_t pth_cpu_usage, pth_policy, pth_run_state, pth_flags, pth_sleep_time, pth_curpri, pth_priority, pth_maxpriority; char pth_name[64]; };
typedef struct thread_extended_info thread_extended_info_data_t;
#define THREAD_EXTENDED_INFO 5
#define THREAD_EXTENDED_INFO_COUNT ((mach_msg_type_number_t)(sizeof(thread_extended_info_data_t) / sizeof(natural_t)))
kern_return_t mach_wait_until(uint64_t);
#include <mach/thread_policy.h>
