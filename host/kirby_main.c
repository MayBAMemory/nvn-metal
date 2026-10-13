// 卡比宿主入口：取代 D2R 的 dm/host/main.c（那份带存档节流、固定种子、TACT 预取、选怪、触屏布局等 D2R 专属步骤）。
// 启动顺序照卡比 SDK 的 nn::init::Start（sdk+0x2b0470，反汇编核对过，与 D2R 那一版相同）：
//   _init_libc0 → nnosInitialize → (rtld 回调 A) → _init_libc1 → nninitInitializeSdkModule → nninitStartup → _init_libc2 → (回调 B = 各模块 DT_INIT) → nnMain
// 参数：kirbyhost <产物目录> [模块…] [-- 游戏参数…]；缺省模块 main sdk subsdk0。
// Kirby host entry: replaces D2R's dm/host/main.c (which carries D2R-only steps: save throttle, fixed seed, TACT prefetch, targeting, touch layout).
// Boot order follows Kirby's SDK nn::init::Start (sdk+0x2b0470, checked by disassembly, same as D2R's):
//   _init_libc0 → nnosInitialize → (rtld callback A) → _init_libc1 → nninitInitializeSdkModule → nninitStartup → _init_libc2 → (callback B = each module's DT_INIT) → nnMain
// Arguments: kirbyhost <output dir> [modules…] [-- game arguments…]; default modules main sdk subsdk0.
#include "dm.h"
#include "front.h"
#include <stdatomic.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
void stubs_report(void); void crash_install(void); void guest_thread_init(void); uint32_t svc_thread_main_init(void);
typedef void (*fn0)(void); typedef void (*fn2)(uint64_t, uint64_t); typedef void (*fnp)(void *);
static void *req(const char *n) { void *p = guest_sym(n); if (!p) die("缺符号 %s", n); return p; }
int dm_game_d2r(void) { return 0; }   // 不是 D2R：共用文件里写死 D2R 地址的分支一律不走（dm.h） | Not D2R: shared-file branches with hard-coded D2R addresses are skipped (dm.h)

static char *g_argv[64] = {"Kirby"}; static int g_argc = 1;

static void guest_main(void) {
    guest_thread_init();
    uint32_t hmain = svc_thread_main_init();
    ((fn0)req("_init_libc0"))();
    LOG(">>> nnosInitialize"); ((fn2)req("nnosInitialize"))(hmain, 0);
    ((fn0)req("_init_libc1"))();
    LOG(">>> nninitInitializeSdkModule"); ((fn0)req("nninitInitializeSdkModule"))();
    LOG(">>> nninitStartup"); ((fn0)req("nninitStartup"))();
    ((fn0)req("_init_libc2"))();
    dm_prof_mark("② SDK 初始化（nnosInitialize → nninitStartup）");
    run_module_inits();
    dm_prof_mark("③ 各模块 init_array");
    ((fnp)req("_ZN2nn2os11SetHostArgcEi"))((void *)(uint64_t)g_argc);
    ((fnp)req("_ZN2nn2os11SetHostArgvEPPc"))(g_argv);
    LOG(">>> nnMain"); ((fn0)req("nnMain"))();
    LOG("nnMain returned");
}

struct dm_flags g_flag;
uint64_t g_t0_ns;
static void flags_init(void) {
    g_flag.thr = !!getenv("DM_THR"); g_flag.svc = !!getenv("DM_SVC"); g_flag.fs = !!getenv("DM_FS");
    g_flag.movie = !!getenv("DM_MOVIE"); g_flag.dbg = !!getenv("DM_DBG"); g_flag.imports = !!getenv("DM_IMPORTS");
    g_flag.bootprof = !!getenv("DM_BOOTPROF");
}
static uint64_t proc_start_mono_ns(void) {
    struct kinfo_proc kp; size_t n = sizeof kp; int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    uint64_t mono = now_ns(); struct timeval tv; gettimeofday(&tv, NULL);
    if (sysctl(mib, 4, &kp, &n, NULL, 0) || !n) return mono;
    int64_t age = (int64_t)(tv.tv_sec - kp.kp_proc.p_starttime.tv_sec) * 1000000000ll + (tv.tv_usec - kp.kp_proc.p_starttime.tv_usec) * 1000ll;
    return age > 0 ? mono - (uint64_t)age : mono;
}

static void host_preload(int argc, char **argv) {
    g_t0_ns = proc_start_mono_ns();
    flags_init();
    if (argc < 2) die("usage: kirbyhost <dir> [mod...] [-- 游戏参数...]");
    if (getenv("DM_FIXED_TIME")) setenv("TZ", "UTC", 1);
    crash_install();
    { void dm_term_install(void); dm_term_install(); }
    { void crash_set_dump_hook(void (*)(void)); void svc_thread_dump(void); crash_set_dump_hook(svc_thread_dump); }
    const char *names[MAX_MOD]; int n = 0;
    int i = 2;
    for (; i < argc && strcmp(argv[i], "--") && n < MAX_MOD; i++) names[n++] = argv[i];
    if (i < argc && !strcmp(argv[i], "--")) for (i++; i < argc && g_argc < 63; i++) g_argv[g_argc++] = argv[i];
    if (!n) { names[n++] = "main"; names[n++] = "sdk"; names[n++] = "subsdk0"; }
    load_modules(argv[1], names, n);
    stubs_report();
    dm_prof_mark("① 宿主装载（读 NSO / 重定位 / 强制接管）");
}

// iOS 外壳以后可在 App 的 main() 里先调它（同 D2R）；Mac 上 main 内同步装载
// The iOS shell may later call this first in the App's main() (same as D2R); on Mac loading is synchronous inside main
static struct { int argc; char **argv; pthread_t th; _Atomic int started; } g_pre;
static void *preload_thread(void *a) { (void)a; host_preload(g_pre.argc, g_pre.argv); return NULL; }
void dm_host_preload_start(int argc, char **argv) {
    if (atomic_exchange(&g_pre.started, 1)) return;
    g_pre.argc = argc; g_pre.argv = calloc((size_t)argc + 1, sizeof(char *));
    for (int i = 0; i < argc; i++) g_pre.argv[i] = strdup(argv[i]);
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 8 << 20);
    if (pthread_create(&g_pre.th, &a, preload_thread, NULL)) die("dm_host_preload_start：起线程失败");
}
#include <TargetConditionals.h>
int dm_app_bootstrap(int *argc, char ***argv);
int main(int argc, char **argv) {
#if TARGET_OS_OSX
    dm_app_bootstrap(&argc, &argv);
#endif
    if (atomic_load(&g_pre.started)) {
        pthread_join(g_pre.th, NULL);
        void crash_thread_init(void); crash_thread_init();
    } else host_preload(argc, argv);
    front_run(guest_main);
    return 0;
}
