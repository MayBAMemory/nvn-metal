// 卡比用到、D2R 的 hle_*.c 没覆盖、真机上会连系统服务的零散接口（文件系统、运行环境、显示、时间、Opus、设置、账号）。
// 清单与分类依据：kirby/tools/svc_reach.py（静态判断会不会走到服务代理），见 kirby/docs/host_split.md。只在游戏进程里算的接口不接，原样跑 SDK。
// Miscellaneous interfaces Kirby uses that D2R's hle_*.c does not cover and that reach system services on the real device (file system, operating environment, display, time, Opus, settings, account).
// List and classification: kirby/tools/svc_reach.py (static check of whether a service proxy is reached), see kirby/docs/host_split.md. Interfaces computed purely inside the game process are not taken over and run the SDK as is.
#include "hle.h"
#include <time.h>

Result dm_fs_read_exact(uint64_t h, int64_t off, void *buf, size_t sz);   // dm/host/hle_fs.c
Result dm_fs_commit(const char *name);
Result dm_fs_clean_dir(const char *path);

// ---- 文件系统 | File system ----
HLE_FORCE("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", "fsp-srv → pread(2)，要求读满（hle_fs.c）", Result, kfs_ReadFile4, uint64_t h, int64_t off, void *buf, size_t sz) { return dm_fs_read_exact(h, off, buf, sz); }
HLE_FORCE("_ZN2nn2fs14CommitSaveDataEPKc", "fsp-srv；同 Commit：事务式存档原子换入（hle_fs.c）", Result, kfs_CommitSave, const char *name) { return dm_fs_commit(name); }
HLE_FORCE("_ZN2nn2fs25CleanDirectoryRecursivelyEPKc", "fsp-srv → 清空目录（保留目录本身）", Result, kfs_CleanDir, const char *path) { return dm_fs_clean_dir(path); }
HLE_FORCE("_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", "fs 库的分配器：宿主文件系统不用它", void, kfs_SetAllocator, void *a, void *f) { (void)a; (void)f; }
HLE_FORCE("_ZN2nn2fs26SetPriorityOnCurrentThreadENS0_8PriorityE", "fs 访问优先级：宿主不分优先级", void, kfs_SetPrio, int p) { (void)p; }

// ---- 运行环境（oe）：都是给系统的提示，宿主空操作 | Operating environment (oe): all hints to the system, no-ops on the host ----
HLE_FORCE("_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE", "CPU 加速模式：宿主不调频", void, koe_CpuBoost, int m) { (void)m; }
HLE_FORCE("_ZN2nn2oe17FinishStartupLogoEv", "结束启动 Logo：宿主没有系统 Logo", void, koe_FinishLogo, void) { LOG("oe：游戏结束启动 Logo"); }
HLE_FORCE("_ZN2nn2oe24SetExpectedVolumeBalanceEff", "系统 / 游戏音量平衡：宿主不做", void, koe_VolBalance, float s, float a) { (void)s; (void)a; }
HLE_FORCE("_ZN2nn2oe31EnterExitRequestHandlingSectionEv", "退出请求处理区段：宿主退出流程自管", void, koe_EnterExit, void) {}
HLE_FORCE("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", "退出请求处理区段：宿主退出流程自管", void, koe_LeaveExit, void) {}
HLE_FORCE("_ZN2nn2oe38SetUserInactivityDetectionTimeExtendedEb", "闲置检测延长：宿主没有自动熄屏控制", void, koe_Inactivity, unsigned on) { (void)on; }

// ---- 显示（vi）：打开 / 建层由 dm/gfx/hle_vi.c 给静态对象，关闭与销毁都是空操作 ----
// ---- Display (vi): open / create layer get static objects from dm/gfx/hle_vi.c, so close and destroy are no-ops ----
HLE_FORCE("_ZN2nn2vi12CloseDisplayEPNS0_7DisplayE", "vi 服务；宿主显示对象是静态的", void, kvi_CloseDisplay, void *d) { (void)d; }
HLE_FORCE("_ZN2nn2vi12DestroyLayerEPNS0_5LayerE", "vi 服务；宿主图层对象是静态的", void, kvi_DestroyLayer, void *l) { (void)l; }
HLE_FORCE("_ZN2nn2vi8FinalizeEv", "vi 服务；宿主无需收尾", void, kvi_Finalize, void) {}

// ---- 时间：ClockSnapshot（0xD0 字节，布局 = libnx TimeClockSnapshot）----
// ---- Time: ClockSnapshot (0xD0 bytes, layout = libnx TimeClockSnapshot) ----
typedef struct { int64_t value; uint8_t source[16]; } k_steady_point;                        // SteadyClockTimePoint，0x18
typedef struct { int64_t offset; k_steady_point stamp; } k_clock_ctx;                       // SystemClockContext，0x20
typedef struct { int16_t year; int8_t month, day, hour, minute, second, _pad; } k_cal;      // CalendarTime，8
typedef struct { int32_t wday, yday; char tzname[8]; uint8_t dst, _p[3]; int32_t utc_off; } k_cal_add;   // CalendarAdditionalInfo，0x18
typedef struct {
    k_clock_ctx user_ctx, net_ctx;     // 0x00 / 0x20
    int64_t user_time, net_time;       // 0x40 / 0x48：POSIX 秒 | POSIX seconds
    k_cal user_cal, net_cal;           // 0x50 / 0x58
    k_cal_add user_add, net_add;       // 0x60 / 0x78
    k_steady_point steady;             // 0x90
    char location[0x24];               // 0xA8
    uint8_t auto_correct, type;        // 0xCC / 0xCD
    uint16_t _r;
} k_snapshot;
_Static_assert(sizeof(k_snapshot) == 0xD0, "nn::time::ClockSnapshot");
static const uint8_t g_steady_src[16] = {'d', 'm', '-', 'h', 'o', 's', 't', '-', 's', 't', 'e', 'a', 'd', 'y', '0', '1'};   // 宿主只有一个稳定时钟源 | the host has a single steady clock source
static void fill_cal(int64_t t, k_cal *c, k_cal_add *a) {
    time_t tt = (time_t)t; struct tm tm; localtime_r(&tt, &tm);
    *c = (k_cal){(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday, (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    memset(a, 0, sizeof *a); a->wday = tm.tm_wday; a->yday = tm.tm_yday; strncpy(a->tzname, tm.tm_zone ? tm.tm_zone : "UTC", sizeof a->tzname);
    a->dst = tm.tm_isdst > 0; a->utc_off = (int32_t)tm.tm_gmtoff;
}
HLE_FORCE("_ZN2nn4time13ClockSnapshot31CreateWithAdjustableSystemClockEPS1_", "time 服务；按宿主时钟填快照", Result, ktime_Snapshot, k_snapshot *s) {
    struct timespec mono; clock_gettime(CLOCK_MONOTONIC, &mono);
    int64_t now = (int64_t)time(NULL);
    memset(s, 0, sizeof *s);
    s->steady.value = mono.tv_sec; memcpy(s->steady.source, g_steady_src, 16);
    s->user_ctx = (k_clock_ctx){now - (int64_t)mono.tv_sec, s->steady}; s->net_ctx = s->user_ctx;
    s->user_time = s->net_time = now;
    fill_cal(now, &s->user_cal, &s->user_add); s->net_cal = s->user_cal; s->net_add = s->user_add;
    strncpy(s->location, "UTC", sizeof s->location);
    s->auto_correct = 1; s->type = 0;
    return RES_OK;
}
// 两个快照的间隔：同一稳定时钟源时按稳定时钟差（秒）算，TimeSpan 单位纳秒
// Interval between two snapshots: with the same steady clock source, from the steady clock difference (seconds); TimeSpan is in nanoseconds
HLE_FORCE("_ZN2nn4time20CalculateSpanBetweenEPNS_8TimeSpanERKNS0_13ClockSnapshotES5_", "time 服务；按快照里的稳定时钟算间隔", Result, ktime_Span, int64_t *out, const k_snapshot *a, const k_snapshot *b) {
    if (memcmp(a->steady.source, b->steady.source, 16)) { *out = (b->user_time - a->user_time) * 1000000000ll; return RES_OK; }
    *out = (b->steady.value - a->steady.value) * 1000000000ll; return RES_OK;
}
HLE_FORCE("_ZN2nn4time8FinalizeEv", "time 服务；宿主无需收尾", Result, ktime_Finalize, void) { return RES_OK; }

// ---- Opus：D2R 的 hle_codec.c 把硬件解码器换成了 SDK 的软件 OpusDecoder；卡比另外直接调这两个，也转到软件版 ----
// ---- Opus: D2R's hle_codec.c swaps the hardware decoder for the SDK's software OpusDecoder; Kirby also calls these two directly, so route them to the software version too ----
HLE_FORCE("_ZN2nn5codec36GetHardwareOpusDecoderWorkBufferSizeEii", "hwopus 服务；按软件 OpusDecoder 的需求给（hle_codec.c 换了实现）", uint64_t, kcodec_HwWorkSize, int rate, int ch) {
    static uint64_t (*soft)(int, int);
    if (!soft && !(soft = guest_sym("_ZN2nn5codec28GetOpusDecoderWorkBufferSizeEii"))) die("缺 GetOpusDecoderWorkBufferSize");
    return soft(rate, ch);
}
HLE_FORCE("_ZN2nn5codec19HardwareOpusDecoderD1Ev", "hwopus：对象实为软件 OpusDecoder（hle_codec.c），按软件版析构", void, kcodec_HwDtor, void *self) {
    static void (*soft)(void *);
    if (!soft && !(soft = guest_sym("_ZN2nn5codec11OpusDecoderD1Ev"))) die("缺 OpusDecoder 析构");
    soft(self);
}

// ---- 设置：语言码与语言枚举比较（SDK 要连 settings 服务查表，宿主内置同一张表）----
// ---- Settings: comparing a language code with a language enum (the SDK queries the settings service for the table; the host carries the same table) ----
static const char *const g_lang_codes[] = {"ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl", "pt", "ru", "zh-TW",
                                           "en-GB", "fr-CA", "es-419", "zh-Hans", "zh-Hant", "pt-BR"};   // nn::settings::Language 顺序 | nn::settings::Language order
HLE_FORCE("_ZN2nn8settingseqERKNS0_12LanguageCodeERKNS0_8LanguageE", "settings 服务；宿主内置语言码表", unsigned, kset_LangEq, const char *code, const int32_t *lang) {
    int l = *lang; if (l < 0 || l >= (int)(sizeof g_lang_codes / sizeof *g_lang_codes)) return 0;
    return strncmp(code, g_lang_codes[l], 8) == 0;
}

// ---- 账号：异步操作一律「已完成」（联网类请求在 hle_account.c 已回不可用）；关闭用户空操作 ----
// ---- Account: every async operation is "already done" (online requests already answer unavailable in hle_account.c); closing a user is a no-op ----
HLE_FORCE("_ZN2nn7account12AsyncContext7HasDoneEPb", "account 服务；宿主异步操作立即完成", Result, kacc_HasDone, void *self, uint8_t *done) { (void)self; *done = 1; return RES_OK; }
HLE_FORCE("_ZN2nn7account12AsyncContext6CancelEv", "account 服务；无可取消", Result, kacc_Cancel, void *self) { (void)self; return RES_OK; }
HLE_FORCE("_ZN2nn7account12AsyncContextD1Ev", "account 服务；宿主异步上下文无资源", void, kacc_AsyncDtor, void *self) { (void)self; }
HLE_FORCE("_ZN2nn7account9CloseUserERKNS0_10UserHandleE", "account 服务；宿主单用户常开", void, kacc_CloseUser, const void *h) { (void)h; }
