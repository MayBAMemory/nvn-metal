// nn::audio（卡比）：补 D2R 的 dm/host/hle_audio.c 没覆盖的几个入口。
// 卡比的声音链（反汇编核对，见 docs/audio.md）：游戏把 AudioRendererParameter 的「渲染设备 = CPU、执行方式 = 手动」都设成 1
// （main+0x4596cc），SDK 于是在本进程里跑混音器（CreateAudioRendererManagerByDfc，不连 audren 服务）；游戏自己调
// ExecuteAudioRendererRendering 出一帧、ReadCircularBufferSink 读出 PCM，再 AppendAudioOutBuffer 交给 AudioOut。
// 所以混音器不用宿主实现，只要 AudioOut（D2R 已接 CoreAudio）和下面这几处会连系统服务的入口。
// nn::audio (Kirby): fills the few entries D2R's dm/host/hle_audio.c does not cover.
// Kirby's audio chain (checked by disassembly, see docs/audio.md): the game sets both "rendering device = CPU" and "execution mode = manual"
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
// 声音：手动模式下 DeviceSink 不出声（docs/audio.md 第 3 条）。2026-10-23 起的做法见下面「设备输出旁路」：游戏 AddDeviceSink 时在同一个
// 最终混音上再挂一个循环缓冲输出，宿主每执行一帧就把它读出来送进 AudioUnit。为此这类渲染器的参数里输出数、效果数（决定内存池数）各加 1。
// AudioRendererHandle 是 16 字节、按值传（x0 = 管理器，x1 = 事件）；AudioRendererParameter 0x38 字节（InitializeAudioRendererParameter）。
// ---- AudioRenderer: turn "DSP + auto execution" into "CPU + manual execution", with the host executing one frame every 5 ms in place of the DSP ----
// Measured on device (iPhone, 2026-10-14): besides the CPU renderer described in docs/audio.md, the game's sound system (Atk2) opens a second renderer
// with SDK default parameters (device 0 = system DSP, mode 0 = auto; switched to CPU only when byte +0x60 of the game's settings is non-zero, main+0x458cdc).
// The DSP route goes CreateAudioRendererManagerByHipc → audren service → SVC 0x21 → Break abort (bt: GetAudioRendererWorkBufferSize ← main+0x458cf8).
// The SDK accepts only two combinations (IsValidAudioRendererParameter, sdk+0x3adb0): DSP+auto and CPU+manual. So the host swaps such parameters to CPU+manual,
// letting the SDK's own mixer run in-process; the host supplies the "auto" half: one guest thread per such renderer, calling ExecuteAudioRendererRendering
// every 5 ms (one 240-sample frame at 48 kHz). After each frame it signals the event in the handle itself (sdk+0x4caa8; the manual-mode Open creates that
// event and stores it at handle+8, sdk+0x4c3c8), so the game's sound thread waits on it as usual, at the hardware cadence.
// Sound: in manual mode DeviceSink makes no sound (docs/audio.md item 3). Since 2026-10-23 see "device sink bypass" below: when the game calls AddDeviceSink, a circular buffer sink is
// added on the same final mix and the host reads it after every frame it executes, feeding an AudioUnit. For that, such renderers get one more sink and one more effect (which sets the memory pool count) in their parameters.
// AudioRendererHandle is 16 bytes passed by value (x0 = manager, x1 = event); AudioRendererParameter is 0x38 bytes (InitializeAudioRendererParameter).
#include <pthread.h>
#include <time.h>
enum { AR_PARAM_SIZE = 0x38, AR_SAMPLES = 0x04, AR_SINKS = 0x14, AR_EFFECTS = 0x18, AR_DEVICE = 0x2c, AR_MODE = 0x30 };
// DSP+自动 → CPU+手动，另多留一个输出和一个效果（设备输出旁路要用）；其余原样 | DSP+auto → CPU+manual with one spare sink and one spare effect (for the device sink bypass); anything else unchanged
static const uint8_t *ar_param_cpu(const uint8_t *p, uint8_t *tmp) {
    if (*(const uint32_t *)(p + AR_DEVICE) != 0) return p;
    memcpy(tmp, p, AR_PARAM_SIZE); *(uint32_t *)(tmp + AR_DEVICE) = 1; *(uint32_t *)(tmp + AR_MODE) = 1;
    *(uint32_t *)(tmp + AR_SINKS) += 1; *(uint32_t *)(tmp + AR_EFFECTS) += 1; return tmp;
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
// ---- 设备输出旁路：把 DeviceSink 的输入原样再接一个循环缓冲输出，读出来自己出声 ----
// 循环缓冲里每一帧按声道分块（声道 0 的 240 个样本、声道 1 的 240 个……，16 位；同 Ryujinx 的 CircularBufferSinkCommand），
// 宿主转成立体声交错（6 声道时按 ITU 系数缩混）推进环形缓冲，AudioUnit 从里面拉。出不出声与 AudioOut 同一规则：iOS 缺省出声，Mac 要 DM_AUDIO=1。
// ---- Device sink bypass: hook the DeviceSink's inputs to a circular buffer sink as well, read it back and play it ourselves ----
// Each frame in the circular buffer is planar per channel (240 samples of channel 0, then channel 1, ..., 16-bit; as in Ryujinx's CircularBufferSinkCommand);
// the host converts to interleaved stereo (6 channels are downmixed with ITU coefficients) into a ring that an AudioUnit pulls from. Same rule as AudioOut: sound by default on iOS, DM_AUDIO=1 on the Mac.
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
enum { SINK_FRAMES = 8, SAMPLE_FMT_PCM16 = 2, RING = 48000 / 2, RING_KEEP = 48000 / 10 };   // 环形缓冲 0.5 秒，超过 0.1 秒就丢旧的 | 0.5 s ring, drop the oldest beyond 0.1 s
static uint8_t g_cfg_param[AR_PARAM_SIZE]; static const void *g_cfg;           // 被改成 CPU 渲染的那个配置及其（改后的）参数 | the config converted to CPU rendering and its (adjusted) parameter
static _Alignas(16) uint8_t g_cb[512], g_pool[512];                            // CircularBufferSinkType / MemoryPoolType（不透明，宽给）| opaque types, generously sized
static uint8_t *g_cb_buf; static size_t g_cb_size; static int g_cb_ch, g_cb_samples, g_cb_pending; static _Atomic int g_cb_ready;
static void *g_cb_fm; static int8_t g_cb_in[6];   // 等内存池挂上后再加循环缓冲输出时要用 | needed to add the circular buffer sink once the memory pool is attached
static int16_t g_ring[RING * 2]; static _Atomic uint32_t g_rd, g_wr; static AudioUnit g_au;
static int sink_real(void) { const char *e = getenv("DM_AUDIO"); return e ? atoi(e) != 0 : TARGET_OS_IPHONE; }
static OSStatus sink_render(void *ctx, AudioUnitRenderActionFlags *f, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames, AudioBufferList *bl) {
    (void)ctx; (void)f; (void)ts; (void)bus;
    int16_t *o = bl->mBuffers[0].mData; uint32_t rd = atomic_load(&g_rd), wr = atomic_load(&g_wr), have = (wr - rd) % RING, n = frames < have ? frames : have;
    for (uint32_t k = 0; k < n; k++) { o[2 * k] = g_ring[2 * ((rd + k) % RING)]; o[2 * k + 1] = g_ring[2 * ((rd + k) % RING) + 1]; }
    memset(o + 2 * n, 0, (frames - n) * 4); atomic_store(&g_rd, (rd + n) % RING); return noErr;
}
static void sink_au_open(void) {
    AudioComponentDescription d = {.componentType = kAudioUnitType_Output, .componentManufacturer = kAudioUnitManufacturer_Apple,
#if TARGET_OS_IPHONE
                                   .componentSubType = kAudioUnitSubType_RemoteIO};
#else
                                   .componentSubType = kAudioUnitSubType_DefaultOutput};
#endif
    AudioComponent c = AudioComponentFindNext(NULL, &d);
    AudioStreamBasicDescription fmt = {.mSampleRate = 48000, .mFormatID = kAudioFormatLinearPCM, .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
                                       .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4, .mChannelsPerFrame = 2, .mBitsPerChannel = 16};
    AURenderCallbackStruct cb = {sink_render, NULL};
    if (!c || AudioComponentInstanceNew(c, &g_au) || AudioUnitSetProperty(g_au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt) ||
        AudioUnitSetProperty(g_au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) || AudioUnitInitialize(g_au) || AudioOutputUnitStart(g_au)) {
        LOG("【红】设备输出旁路：AudioUnit 建不出来，没有声音"); g_au = NULL; }
}
static void sink_push(int16_t l, int16_t r) {
    uint32_t wr = atomic_load(&g_wr), rd = atomic_load(&g_rd);
    if ((wr + 1) % RING == rd) atomic_store(&g_rd, (rd + 1) % RING);   // 满了丢最旧的 | full: drop the oldest
    g_ring[2 * wr] = l; g_ring[2 * wr + 1] = r; atomic_store(&g_wr, (wr + 1) % RING);
}
static int16_t sat(float x) { return (int16_t)(x > 32767.f ? 32767 : x < -32768.f ? -32768 : x); }
// 持 g_ar_lock 调（紧跟在宿主执行一帧之后）| called with g_ar_lock held, right after the host executed one frame
static void sink_drain(void) {
    if (!atomic_load(&g_cb_ready)) return;
    static size_t (*rd)(void *, void *, size_t);
    if (!rd) rd = (size_t (*)(void *, void *, size_t))ar_sdk("_ZN2nn5audio22ReadCircularBufferSinkEPNS0_22CircularBufferSinkTypeEPvm");
    static int16_t tmp[6 * 240 * SINK_FRAMES]; size_t frame = (size_t)g_cb_ch * g_cb_samples * 2;
    size_t got = rd(g_cb, tmp, sizeof tmp < frame * SINK_FRAMES ? sizeof tmp : frame * SINK_FRAMES);
    static uint64_t total; static int logged; total += got;
    if (!logged && total >= 48000 * 4 * 2) { logged = 1; LOG("设备输出旁路：已读出 %llu 字节（%d 声道）", (unsigned long long)total, g_cb_ch); }
    static FILE *dump; static int dump_checked; static int16_t raw[6 * 240 * SINK_FRAMES];
    if (!dump_checked) { dump_checked = 1; const char *e = getenv("KIRBY_SOUND_DUMP"); if (e && *e) dump = fopen(e, "wb"); }   // 原始循环缓冲内容（排查用）| raw circular buffer content (for debugging)
    if (dump) { memcpy(raw, tmp, got); fwrite(raw, 1, got, dump); fflush(dump); }
    if (!g_au) return;
    for (size_t f = 0; f + frame <= got; f += frame) {
        const int16_t *b = tmp + f / 2; int n = g_cb_samples;
        for (int k = 0; k < n; k++) {
            float L = b[k], R = g_cb_ch > 1 ? b[n + k] : L;
            if (g_cb_ch >= 6) { float C = b[2 * n + k], SL = b[4 * n + k], SR = b[5 * n + k]; L += 0.707f * (C + SL); R += 0.707f * (C + SR); }
            sink_push(sat(L), sat(R));
        }
    }
    uint32_t have = (atomic_load(&g_wr) - atomic_load(&g_rd)) % RING;
    if (have > RING_KEEP) atomic_store(&g_rd, (atomic_load(&g_wr) - RING_KEEP) % RING);   // 堆积太多（切后台回来等）就追上，延迟不超过 0.1 秒 | too much backlog (back from background etc.): catch up, keep latency under 0.1 s
}
#define SDKFN(type, name, sym) static __typeof__(type) name; if (!name) name = (type)ar_sdk(sym)
HLE_FORCE("_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE", "配置工作区按改后的参数算（多一个输出、一个效果）",
          size_t, kh_cfg_WorkSize, const uint8_t *p) {
    SDKFN(size_t (*)(const uint8_t *), orig, "_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE");
    uint8_t tmp[AR_PARAM_SIZE]; return orig(ar_param_cpu(p, tmp));
}
HLE_FORCE("_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm", "配置按改后的参数初始化；记下这是被改成 CPU 渲染的配置",
          void, kh_cfg_Init, void *cfg, const uint8_t *p, void *buf, size_t size) {
    SDKFN(void (*)(void *, const uint8_t *, void *, size_t), orig, "_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm");
    uint8_t tmp[AR_PARAM_SIZE]; const uint8_t *q = ar_param_cpu(p, tmp);
    orig(cfg, q, buf, size);
    if (q != p) { memcpy(g_cfg_param, q, AR_PARAM_SIZE); g_cfg = cfg; }
}
HLE_FORCE("_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc", "设备输出：照常加，另在同一最终混音上挂循环缓冲输出（旁路出声）",
          Result, kh_AddDeviceSink, void *cfg, void *sink, void *fm, const int8_t *in, int count, const char *name) {
    SDKFN(Result (*)(void *, void *, void *, const int8_t *, int, const char *), orig, "_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc");
    Result r = orig(cfg, sink, fm, in, count, name);
    LOG("audio::AddDeviceSink %s 声道 %d（输入 %d %d %d %d %d %d）→ %#x%s", name ? name : "?", count, count > 0 ? in[0] : -1, count > 1 ? in[1] : -1, count > 2 ? in[2] : -1,
        count > 3 ? in[3] : -1, count > 4 ? in[4] : -1, count > 5 ? in[5] : -1, r, cfg == g_cfg ? "（CPU 渲染的配置，挂旁路）" : "");
    const char *off = getenv("KIRBY_SOUND_BYPASS");   // =0 不挂旁路（对照用）| =0 skips the bypass (for comparison)
    if (r || cfg != g_cfg || atomic_load(&g_cb_ready) || count < 1 || count > 6 || (off && *off == '0')) return r;
    SDKFN(size_t (*)(const uint8_t *, int, int, int), need, "_ZN2nn5audio42GetRequiredBufferSizeForCircularBufferSinkEPKNS0_22AudioRendererParameterEiiNS0_12SampleFormatE");
    SDKFN(int (*)(void *, void *, void *, size_t), acquire, "_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm");
    SDKFN(int (*)(void *), attach, "_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE");
    // 第一步：只申请并请求挂上内存池。循环缓冲输出要等内存池真挂上（下一次 RequestUpdate 之后）才加：同一次更新里一起加，混音器有时在
    // 内存池生效前就往缓冲里写，地址换算不对，写坏宿主的堆（实测约四次崩一次，崩在下一次 malloc）
    // Step one: only acquire the memory pool and request attaching it. The circular buffer sink is added only once the pool is really attached (after the next RequestUpdate):
    // adding both in the same update sometimes let the mixer write to the buffer before the pool took effect, with a wrong address translation that corrupted the host heap (about one run in four crashed at the next malloc)
    // 循环缓冲输出拿 SDK 算的精确大小（整数帧）；内存池按 4 KB 取整。别把取整后的大小交给输出：混音器按帧写、写到末尾才绕回，
    // 24576 不是一帧 2880 字节的整数倍，最后一帧会越界 1344 字节写坏宿主的堆（实测约四次崩一次）
    // The sink gets the SDK's exact size (whole frames); only the memory pool is rounded to 4 KB. Never hand the rounded size to the sink: the mixer writes whole frames and wraps only at the end,
    // and 24576 is not a multiple of a 2880-byte frame, so the last frame overran by 1344 bytes and corrupted the host heap (about one run in four crashed)
    size_t exact = need(g_cfg_param, count, SINK_FRAMES, SAMPLE_FMT_PCM16), sz = (exact + 0xfff) & ~(size_t)0xfff;
    g_cb_buf = aligned_alloc(0x1000, sz); memset(g_cb_buf, 0, sz); g_cb_size = exact;
    int ok_pool = acquire(cfg, g_pool, g_cb_buf, sz) && attach(g_pool);
    g_cb_fm = fm; memcpy(g_cb_in, in, (size_t)count); g_cb_ch = count; g_cb_samples = (int)*(const uint32_t *)(g_cfg_param + AR_SAMPLES); g_cb_pending = ok_pool;
    LOG("设备输出旁路：循环缓冲 %zu 字节（内存池 %zu），内存池%s（%d 声道 × %d 样本/帧）", exact, sz, ok_pool ? "已请求挂上，等它生效再接输出" : "申请失败，没有声音", count, g_cb_samples);
    return r;
}
// 第二步（游戏请求更新之前、游戏线程上）：内存池已挂上就加循环缓冲输出，随这次更新生效 | step two (before the game's update request, on the game thread): once the pool is attached, add the sink so it takes effect with this update
static void sink_add_when_ready(const void *cfg) {
    if (!g_cb_pending || cfg != g_cfg) return;
    SDKFN(int (*)(const void *), state, "_ZN2nn5audio18GetMemoryPoolStateEPKNS0_14MemoryPoolTypeE");
    SDKFN(Result (*)(void *, void *, void *, const int8_t *, int, void *, size_t, int), add, "_ZN2nn5audio21AddCircularBufferSinkEPNS0_19AudioRendererConfigEPNS0_22CircularBufferSinkTypeEPNS0_12FinalMixTypeEPKaiPvmNS0_12SampleFormatE");
    int st = state(g_pool); static int last = -1; if (st != last) { LOG("设备输出旁路：内存池状态 %d", st); last = st; }
    if (st != 1) return;   // nn::audio::MemoryPoolType::State：0 请求挂上、1 已挂上 | 0 RequestAttach, 1 Attached
    g_cb_pending = 0;
    Result ra = add((void *)cfg, g_cb, g_cb_fm, g_cb_in, g_cb_ch, g_cb_buf, g_cb_size, SAMPLE_FMT_PCM16);
    LOG("设备输出旁路：内存池已挂上，AddCircularBufferSink → %#x（%s）", ra, sink_real() ? "真出声" : "静音（Mac 要 DM_AUDIO=1）");
    if (!ra) { if (sink_real()) sink_au_open(); atomic_store(&g_cb_ready, 1); }
}
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
        Result r = exec(g_ar[i].h.mgr, g_ar[i].h.ev); sink_drain(); pthread_mutex_unlock(&g_ar_lock);
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
    sink_add_when_ready(cfg);
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
