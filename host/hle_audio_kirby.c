// nn::audio（卡比）：补 D2R 的 dm/host/hle_audio.c 没覆盖的几个入口。
// 卡比的声音链（反汇编核对，见 kirby/docs/audio.md）：游戏把 AudioRendererParameter 的「渲染设备 = CPU、执行方式 = 手动」都设成 1
// （main+0x4596cc），SDK 于是在本进程里跑混音器（CreateAudioRendererManagerByDfc，不连 audren 服务）；游戏自己调
// ExecuteAudioRendererRendering 出一帧、ReadCircularBufferSink 读出 PCM，再 AppendAudioOutBuffer 交给 AudioOut。
// 所以混音器不用宿主实现，只要 AudioOut（D2R 已接 CoreAudio）和下面这几处会连系统服务的入口。
// nn::audio (Kirby): fills the few entries D2R's dm/host/hle_audio.c does not cover.
// Kirby's audio chain (checked by disassembly, see kirby/docs/audio.md): the game sets both "rendering device = CPU" and "execution mode = manual"
// of AudioRendererParameter to 1 (main+0x4596cc), so the SDK runs the mixer inside this process (CreateAudioRendererManagerByDfc, no audren service);
// the game itself calls ExecuteAudioRendererRendering for one frame, reads the PCM with ReadCircularBufferSink, then hands it to AudioOut with AppendAudioOutBuffer.
// So the host does not implement the mixer; it only needs AudioOut (D2R already wired to CoreAudio) plus the entries below that would reach system services.
#include "hle.h"
#include <stdatomic.h>

Result dm_audio_out_open(void *g, void *ev, const char *name, const uint8_t *param);   // dm/host/hle_audio.c
uint64_t dm_audio_out_played_samples(const void *g);                                  // dm/host/hle_audio.c

// ---- AudioOut：D2R 只接了带事件的 OpenAudioOut；卡比走 OpenDefaultAudioOut → 不带事件的 OpenAudioOut ----
// ---- AudioOut: D2R only took over OpenAudioOut with an event; Kirby goes OpenDefaultAudioOut → OpenAudioOut without an event ----
HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE", "连 audout 服务；宿主 AudioUnit（hle_audio.c）",
          Result, kh_au_OpenDefault, void *g, const uint8_t *param) { return dm_audio_out_open(g, NULL, "DeviceOut", param); }
HLE_FORCE("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPKcRKNS0_17AudioOutParameterE", "连 audout 服务（不带事件）；宿主 AudioUnit（hle_audio.c）",
          Result, kh_au_Open, void *g, const char *name, const uint8_t *param) { return dm_audio_out_open(g, NULL, name, param); }
// 音量：记下不生效（游戏自己在混音器里调音量；这一层多半恒为 1.0）。第一次和变化时记日志，真遇到用非 1.0 的再接
// Volume: recorded but not applied (the game adjusts volume in its own mixer; at this level it is most likely always 1.0). Logged the first time and on change; wire it up if a non-1.0 value actually shows up
HLE_FORCE("_ZN2nn5audio17SetAudioOutVolumeEPNS0_8AudioOutEf", "音量：记录，暂不生效", void, kh_au_SetVol, void *g, float v) {
    static float last = -1; if (v != last) { LOG("audio::SetAudioOutVolume %p → %.3f（未生效）", g, v); last = v; }
}
HLE_FORCE("_ZN2nn5audio28GetAudioOutPlayedSampleCountEPKNS0_8AudioOutE", "已播放样本数：按交给出声端的字节算", uint64_t, kh_au_Played, const void *g) { return dm_audio_out_played_samples(g); }

// ---- AudioRenderer：「DSP + 自动执行」改成「CPU + 手动执行」，宿主代替 DSP 每 5 ms 执行一帧 ----
// 真机实测（iPhone，2026-10-14）：游戏的音响系统（Atk2）除了 docs/audio.md 说的那个 CPU 渲染器，另开一个渲染器，参数用 SDK 缺省值
// （渲染设备 0 = 系统 DSP、执行方式 0 = 自动；只在游戏设置的 +0x60 字节非零时才改成 CPU，main+0x458cdc）。DSP 路线走
// CreateAudioRendererManagerByHipc → 连 audren 服务 → SVC 0x21 → Break 中止（bt：GetAudioRendererWorkBufferSize ← main+0x458cf8）。
// SDK 只认两种组合（IsValidAudioRendererParameter，sdk+0x3adb0）：DSP+自动、CPU+手动。所以在宿主这层把这类参数换成 CPU+手动，
// 让 SDK 自带的混音器在本进程跑；自动执行的那一半由宿主补：每个这样的渲染器一条客体线程，每 5 ms（48 kHz 下一帧 240 个样本）
// 调一次 ExecuteAudioRendererRendering。它出完一帧会自己 Signal 句柄里的事件（sdk+0x4caa8，事件由手动模式的 Open 建好、放在
// 句柄 +8，sdk+0x4c3c8），游戏的音响线程照常等这个事件，和真机节拍一样。
// 声音：手动模式下 DeviceSink 不出声（docs/audio.md 第 3 条），所以这个渲染器的声音暂时出不来；先让游戏跑起来。
// AudioRendererHandle 是 16 字节、按值传（x0 = 管理器，x1 = 事件）；AudioRendererParameter 0x38 字节（InitializeAudioRendererParameter）。
// ---- AudioRenderer: turn "DSP + auto execution" into "CPU + manual execution", with the host executing one frame every 5 ms in place of the DSP ----
// Measured on device (iPhone, 2026-10-14): besides the CPU renderer described in docs/audio.md, the game's sound system (Atk2) opens a second renderer
// with SDK default parameters (device 0 = system DSP, mode 0 = auto; switched to CPU only when byte +0x60 of the game's settings is non-zero, main+0x458cdc).
// The DSP route goes CreateAudioRendererManagerByHipc → audren service → SVC 0x21 → Break abort (bt: GetAudioRendererWorkBufferSize ← main+0x458cf8).
// The SDK accepts only two combinations (IsValidAudioRendererParameter, sdk+0x3adb0): DSP+auto and CPU+manual. So the host swaps such parameters to CPU+manual,
// letting the SDK's own mixer run in-process; the host supplies the "auto" half: one guest thread per such renderer, calling ExecuteAudioRendererRendering
// every 5 ms (one 240-sample frame at 48 kHz). After each frame it signals the event in the handle itself (sdk+0x4caa8; the manual-mode Open creates that
// event and stores it at handle+8, sdk+0x4c3c8), so the game's sound thread waits on it as usual, at the hardware cadence.
// Sound: in manual mode DeviceSink makes no sound (docs/audio.md item 3), so this renderer stays silent for now; getting the game running comes first.
// AudioRendererHandle is 16 bytes passed by value (x0 = manager, x1 = event); AudioRendererParameter is 0x38 bytes (InitializeAudioRendererParameter).
#include <pthread.h>
#include <time.h>
enum { AR_PARAM_SIZE = 0x38, AR_DEVICE = 0x2c, AR_MODE = 0x30 };
static const uint8_t *ar_param_cpu(const uint8_t *p, uint8_t *tmp) {   // DSP+自动 → CPU+手动；其余原样 | DSP+auto → CPU+manual; anything else unchanged
    if (*(const uint32_t *)(p + AR_DEVICE) != 0) return p;
    memcpy(tmp, p, AR_PARAM_SIZE); *(uint32_t *)(tmp + AR_DEVICE) = 1; *(uint32_t *)(tmp + AR_MODE) = 1; return tmp;
}
static void *ar_sdk(const char *n) {   // SDK 自己的定义（被强制接管的名字，guest_sym 会给回宿主实现） | the SDK's own definition (guest_sym would hand back the host takeover)
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "sdk")) { void *p = module_sym(&g_mod[i], n); if (p) return p; }
    die("sdk 里找不到 %s", n);
}
HLE_FORCE("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE", "DSP 渲染器改走本进程 CPU 渲染器，工作区按改后的参数算",
          size_t, kh_ar_WorkSize, const uint8_t *p) {
    static size_t (*orig)(const uint8_t *); if (!orig) orig = (size_t (*)(const uint8_t *))ar_sdk("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE");
    uint8_t tmp[AR_PARAM_SIZE]; return orig(ar_param_cpu(p, tmp));
}
typedef struct { uint64_t mgr; void *ev; } ar_handle;
static pthread_mutex_t g_ar_lock = PTHREAD_MUTEX_INITIALIZER;   // 宿主「DSP」线程的 Execute 与游戏的 RequestUpdate 互斥 | serialises the host "DSP" thread's Execute with the game's RequestUpdate
static struct { ar_handle h; _Atomic int live; } g_ar[4]; static int g_ar_n;
static int ar_find(uint64_t mgr) { for (int i = 0; i < g_ar_n; i++) if (g_ar[i].h.mgr == mgr) return i; return -1; }
static void ar_dsp_thread(void *arg) {
    int i = (int)(intptr_t)arg;
    Result (*exec)(uint64_t, void *) = (Result (*)(uint64_t, void *))ar_sdk("_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE");
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); uint64_t n = 0;
    for (;;) {
        t.tv_nsec += 5000000; if (t.tv_nsec >= 1000000000) { t.tv_nsec -= 1000000000; t.tv_sec++; }
        for (;;) {   // 苹果没有 clock_nanosleep：按绝对时刻算剩余再睡 | Apple has no clock_nanosleep: sleep for the remainder to the absolute deadline
            struct timespec now, d; clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t left = (t.tv_sec - now.tv_sec) * 1000000000ll + (t.tv_nsec - now.tv_nsec); if (left <= 0) break;
            d.tv_sec = left / 1000000000; d.tv_nsec = left % 1000000000; nanosleep(&d, NULL);
        }
        pthread_mutex_lock(&g_ar_lock);   // live 在锁里查：Close 拿锁清掉它之后，不会再有一帧碰到已关的渲染器 | live checked under the lock: once Close clears it, no frame touches the closed renderer
        if (!atomic_load(&g_ar[i].live)) { pthread_mutex_unlock(&g_ar_lock); break; }
        Result r = exec(g_ar[i].h.mgr, g_ar[i].h.ev); pthread_mutex_unlock(&g_ar_lock);
        if (r && n < 3) LOG("【红】AudioRenderer#%d 宿主执行一帧失败 %#x", i, r);
        if (++n == 200) LOG("AudioRenderer#%d：宿主代替 DSP 已执行 200 帧（1 秒）", i);
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);   // 落后超过 50 ms（切后台等）就不追帧，从现在重新起拍 | more than 50 ms behind (background etc.): do not catch up, restart the cadence from now
        if ((now.tv_sec - t.tv_sec) * 1000000000ll + (now.tv_nsec - t.tv_nsec) > 50000000) t = now;
    }
}
HLE_FORCE("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm",
          "DSP 渲染器改走本进程 CPU 渲染器；宿主线程代替 DSP 按 5 ms 执行", Result, kh_ar_Open, ar_handle *h, void *ev, const uint8_t *p, void *buf, size_t size) {
    static Result (*orig)(ar_handle *, void *, const uint8_t *, void *, size_t);
    if (!orig) orig = (Result (*)(ar_handle *, void *, const uint8_t *, void *, size_t))ar_sdk("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm");
    uint8_t tmp[AR_PARAM_SIZE]; const uint8_t *q = ar_param_cpu(p, tmp);
    Result r = orig(h, ev, q, buf, size);
    LOG("audio::OpenAudioRenderer 设备 %u 执行方式 %u%s → %#x（%u Hz，混音缓冲 %u）", *(const uint32_t *)(p + AR_DEVICE), *(const uint32_t *)(p + AR_MODE),
        q != p ? "，改成 CPU+手动、宿主代替 DSP" : "", r, *(const uint32_t *)p, *(const uint32_t *)(p + 0x10));
    if (r || q == p) return r;
    if (g_ar_n == 4) die("AudioRenderer：宿主代执行的渲染器超过 4 个");
    int i = g_ar_n++; g_ar[i].h = *h; atomic_store(&g_ar[i].live, 1);
    void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size);
    dm_guest_thread_spawn(ar_dsp_thread, (void *)(intptr_t)i, 8, 256 << 10);
    return r;
}
HLE_FORCE("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE",
          "与宿主代执行的渲染帧互斥（真机上 DSP 另有同步）", Result, kh_ar_Update, uint64_t mgr, void *ev, const void *cfg) {
    static Result (*orig)(uint64_t, void *, const void *);
    if (!orig) orig = (Result (*)(uint64_t, void *, const void *))ar_sdk("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE");
    if (ar_find(mgr) < 0) return orig(mgr, ev, cfg);
    pthread_mutex_lock(&g_ar_lock); Result r = orig(mgr, ev, cfg); pthread_mutex_unlock(&g_ar_lock); return r;
}
HLE_FORCE("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", "停掉宿主代执行线程", void, kh_ar_Close, uint64_t mgr, void *ev) {
    static void (*orig)(uint64_t, void *);
    if (!orig) orig = (void (*)(uint64_t, void *))ar_sdk("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE");
    int i = ar_find(mgr); if (i >= 0) { pthread_mutex_lock(&g_ar_lock); atomic_store(&g_ar[i].live, 0); g_ar[i].h.mgr = 0; pthread_mutex_unlock(&g_ar_lock); }
    orig(mgr, ev);
}

// ---- 音频设备（真机经 IAudioDevice 服务）：宿主只有一个输出设备 ----
// ---- Audio device (on the real device through the IAudioDevice service): the host has a single output device ----
// AudioDeviceName = { char name[256] }
HLE_FORCE("_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi", "音频设备列表；宿主 1 个", int, kh_dev_List, char *out, int n) {
    if (n < 1 || !out) return 0;
    memset(out, 0, 256); strcpy(out, "AudioTvOutput"); return 1;
}
HLE_FORCE("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", "设备音量：记录，暂不生效", void, kh_dev_SetVol, const char *name, float v) {
    static float last = -1; if (v != last) { LOG("audio::SetAudioDeviceOutputVolume %s → %.3f（未生效）", name ? name : "?", v); last = v; }
}
