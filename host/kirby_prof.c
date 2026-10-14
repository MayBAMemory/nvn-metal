// 卡比宿主的汇总采样分析器：KIRBY_PROF=间隔毫秒（如 2）。真机上 Instruments 连不上 iOS 27（xctrace「等待设备启动」超时），
// D2R 的 DM_SAMPLE 在 diag.c（卡比不编）且逐条打印，所以另写一个只做汇总的：采样线程定时用 Mach thread_suspend 停住提交图形命令的线程，
// thread_get_state 取 pc/lr/fp，沿帧指针链读回溯后 thread_resume，再按函数汇总，每 5 秒打一次前 25 名：
//   自身 = 采到时正在执行的函数；包含 = 调用链上出现过的函数（一个样本里同一函数只计一次）。客体代码按「模块+偏移」归到 4 KB 一档。
// 不用信号：客体代码把 x18 当普通寄存器用（main 里 8250 处），苹果内核递送信号时不保留 x18（真机崩溃现场 x18=0、读空指针，只在开分析器时出现）。
// Kirby host aggregate sampling profiler: KIRBY_PROF=interval ms (e.g. 2). On device Instruments cannot reach iOS 27 (xctrace times out "waiting for device to boot"),
// and D2R's DM_SAMPLE lives in diag.c (not built for Kirby) and prints every sample, so this one only aggregates: every interval the sampling thread stops the graphics-submit thread
// with Mach thread_suspend, takes pc/lr/fp via thread_get_state, walks the frame-pointer chain, thread_resumes it, and aggregates per function, printing the top 25 every 5 s:
//   self = the function executing when sampled; inclusive = functions appearing in the call chain (counted once per sample). Guest code is bucketed by "module+offset" in 4 KB bins.
// No signals: guest code uses x18 as an ordinary register (8250 sites in main), and the Apple kernel does not preserve x18 across signal delivery (device crash had x18=0 and a null load, only with the profiler on).
#include "dm.h"
#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unistd.h>

#define DEPTH 24
// 取一个样本：目标线程停住期间只做 Mach 调用与内存读，不碰 malloc / dladdr（它可能正拿着这些锁） | one sample: while the target is stopped only Mach calls and memory reads, no malloc / dladdr (it may hold their locks)
static int take(thread_act_t th, uint64_t *pc, uint64_t *bt) {
    if (thread_suspend(th)) return -1;
    arm_thread_state64_t st; mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT; int n = -1;
    if (!thread_get_state(th, ARM_THREAD_STATE64, (thread_state_t)&st, &cnt)) {
        *pc = arm_thread_state64_get_pc(st); uint64_t fp = arm_thread_state64_get_fp(st); n = 0;
        bt[n++] = arm_thread_state64_get_lr(st);
        for (; n < DEPTH && fp && !(fp & 7); n++) {   // 安全读：被打断的函数可能把 x29 当普通寄存器用 | safe reads: the interrupted function may use x29 as a plain register
            uint64_t fr[2]; vm_size_t got = 0;
            if (vm_read_overwrite(mach_task_self(), fp, sizeof fr, (vm_address_t)fr, &got) || got != sizeof fr) break;
            bt[n] = fr[1];
            if (fr[0] <= fp) { n++; break; }
            fp = fr[0];
        }
    }
    thread_resume(th);
    return n;
}

// 函数名：宿主函数用 dladdr 的符号起点；客体代码用「模块+偏移 / 4 KB」 | name: host functions by dladdr symbol start; guest code by "module+offset / 4 KB"
#define NB 8192
static struct { uint64_t key; const char *name; uint32_t self, incl, seen; } g_tab[NB];
static uint32_t g_epoch;
static int slot_of(uint64_t pc) {
    uint64_t key; const char *name = NULL; char buf[64];
    module_t *m = module_of((const void *)pc);
    if (m) { uint64_t off = pc - (uint64_t)m->base; key = ((uint64_t)(m - g_mod) << 56) | (off >> 12); snprintf(buf, sizeof buf, "%s+%#llx", m->name, (unsigned long long)(off & ~0xfffull)); name = buf; }
    else { Dl_info di; if (dladdr((const void *)pc, &di) && di.dli_saddr) { key = (uint64_t)di.dli_saddr; name = di.dli_sname; } else { key = pc >> 12; snprintf(buf, sizeof buf, "?%#llx", (unsigned long long)pc); name = buf; } }
    for (unsigned h = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 51) % NB, k = 0; k < NB; h = (h + 1) % NB, k++) {
        if (g_tab[h].key == key && g_tab[h].name) return (int)h;
        if (!g_tab[h].name) { g_tab[h].key = key; g_tab[h].name = strdup(name ? name : "?"); return (int)h; }
    }
    return -1;
}
static int cmp_self(const void *a, const void *b) { return (int)g_tab[*(const int *)b].self - (int)g_tab[*(const int *)a].self; }
static int cmp_incl(const void *a, const void *b) { return (int)g_tab[*(const int *)b].incl - (int)g_tab[*(const int *)a].incl; }
static void report(unsigned total) {
    static int idx[NB]; int n = 0;
    for (int i = 0; i < NB; i++) if (g_tab[i].name && (g_tab[i].self || g_tab[i].incl)) idx[n++] = i;
    qsort(idx, n, sizeof *idx, cmp_self);
    LOG("[prof] 样本 %u：自身耗时前 25", total);
    for (int k = 0; k < n && k < 25; k++) LOG("[prof]   %5.1f%%  %s", 100.0 * g_tab[idx[k]].self / total, g_tab[idx[k]].name);
    qsort(idx, n, sizeof *idx, cmp_incl);
    LOG("[prof] 包含耗时前 25");
    for (int k = 0; k < n && k < 25; k++) LOG("[prof]   %5.1f%%  %s", 100.0 * g_tab[idx[k]].incl / total, g_tab[idx[k]].name);
}
static void *sampler(void *p) {
    useconds_t us = (useconds_t)(uintptr_t)p; unsigned total = 0; uint64_t last = now_ns(), pc = 0, bt[DEPTH];
    for (;;) {
        usleep(us);
        extern pthread_t g_nvn_submit_thread;   // hle_nvn.c：只采提交图形命令的线程（全部客体线程的样本几乎全是空等） | only the graphics-submit thread (samples of all guest threads were almost all idle waits)
        if (!g_nvn_submit_thread) continue;
        int n = take(pthread_mach_thread_np(g_nvn_submit_thread), &pc, bt); if (n < 0) continue;
        Dl_info di; if (dladdr((const void *)pc, &di) && di.dli_sname && (strstr(di.dli_sname, "wait") || strstr(di.dli_sname, "mach_msg") || strstr(di.dli_sname, "kevent"))) continue;   // 空等不计 | idle waits not counted
        total++; g_epoch++;
        int s = slot_of(pc); if (s >= 0) { g_tab[s].self++; g_tab[s].incl++; g_tab[s].seen = g_epoch; }
        for (int k = 0; k < n; k++) { int t = slot_of(bt[k] - 4); if (t >= 0 && g_tab[t].seen != g_epoch) { g_tab[t].incl++; g_tab[t].seen = g_epoch; } }
        if (now_ns() - last > 5000000000ull && total) { report(total); last = now_ns(); }
    }
    return NULL;
}
void kirby_prof_start(void) {
    const char *e = getenv("KIRBY_PROF"); if (!e) return;
    pthread_t t; pthread_create(&t, NULL, sampler, (void *)(uintptr_t)(atoi(e) * 1000)); pthread_detach(t);
    LOG("[prof] 采样分析器已开：每 %s 毫秒停一下提交图形命令的线程取样，每 5 秒汇总一次", e);
}
