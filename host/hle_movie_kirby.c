// 过场动画（movie:: = subsdk0 的任天堂 movie 库）：卡比版。取代 D2R 的 dm/host/hle_movie.c（卡比构建不编那份）。
// 为什么不共用 D2R 的：卡比的播放器（main+0x76a000–0x773000，HAL 自写）协议不同——
//   main+0x76dce0 构造 Extractor → 槽 2 SetDataSource(char const* 路径)（D2R 用槽 15，槽 2 在 D2R 那份里是「被调到即中止」）
//   → 槽 4 GetTrackCount → 逐轨 槽 5 GetTrackConfiguration(idx, MediaData*) + MediaData::FindString("mime") / FindInt64("durationUs") / FindInt32(...)，
//     按 "video/" / "audio/" 置视频轨 [P+0x158]、音频轨 [P+0x15c] → 槽 6 SelectTrack → 各建一个 Decoder。
// 第一版报 0 条轨道：真机实测（2026-10-14）播放器不建解码器，也永远等不到流尾——开场动画 Opening.webm 处灰屏、游戏停住（关卡其实已在后台载完）。
// 现在的策略照 D2R 的空播（dm/host/hle_movie.c）：报 1 条视频轨（VP9，不带音频轨），不解码、不出画面，0.1 秒后按库的协议给出流尾，
// 游戏走自己的「播完」路径。时间线：Decoder::Start 挂定时 → 到点由客体计时线程发「输入可用」→ Read 报流尾 → Send(标志 4) 时发「输出可用」
// → AcquireOutputBufferIndex 交出标志 4 项。和 D2R 一样，过场动画内容不在本移植范围内。
// 其余虚槽一律「被调到即中止并报名」（与 D2R 同一原则：不靠返回 0 侥幸通过），运行时撞到哪个就补哪个。
// Buffer / MediaData：宿主自定布局，库里所有被卡比导入的成员函数都在这里接管，避免宿主布局与库里真实实现混用。
// Cutscenes (movie:: = Nintendo's movie library in subsdk0): Kirby version. Replaces D2R's dm/host/hle_movie.c (the Kirby build does not compile that one).
// Why not share D2R's: Kirby's player (main+0x76a000–0x773000, written by HAL) uses a different protocol --
//   main+0x76dce0 constructs the Extractor → slot 2 SetDataSource(char const* path) (D2R uses slot 15; in D2R's file slot 2 aborts when called)
//   → slot 4 GetTrackCount → per track slot 5 GetTrackConfiguration(idx, MediaData*) + MediaData::FindString("mime") / FindInt64("durationUs") / FindInt32(...),
//     marking video track [P+0x158] / audio track [P+0x15c] by "video/" / "audio/" → slot 6 SelectTrack → one Decoder each.
// The first version reported 0 tracks: on device (2026-10-14) the player then built no decoder and never saw end-of-stream -- grey screen at Opening.webm, game stalled (the level had in fact loaded in the background).
// The strategy now follows D2R's no-op playback (dm/host/hle_movie.c): report 1 video track (VP9, no audio track), decode nothing, show nothing, and after 0.1 s deliver
// end-of-stream per the library protocol, so the game takes its own "finished" path. Timeline: Decoder::Start arms a timer → at the deadline a guest timer thread signals
// "input available" → Read reports EOS → Send(flag 4) signals "output available" → AcquireOutputBufferIndex hands over the flag-4 item. As in D2R, cutscene content is out of scope.
// Every other virtual slot aborts and reports its name when called (same principle as D2R: no passing by luck with a 0 return); whichever one is hit at runtime gets implemented.
// Buffer / MediaData: host-defined layouts; every member function Kirby imports is taken over here, so the host layout is never mixed with the library's real implementation.
#include "hle.h"
#include <pthread.h>
#include <time.h>

#define ST_OK 0
#define ST_EOS 0x2000        // movie::Status 流尾 | end of stream
#define ST_TRY_AGAIN 0x1008  // 暂无输出缓冲 | no output buffer yet
#define FLAG_EOS 4
#define MV_SKIP_NS 100000000ll   // 空播时长 0.1 秒 | no-op playback length 0.1 s
#define MV_OUT_ITEMS 3           // 交出的输出项数（2 帧黑帧 + 流尾），见 dec_AcquireOutputBufferIndex | output items handed over (2 black frames + EOS)
// MediaData / Buffer 的宿主布局（说明见文件后部）| host layouts of MediaData / Buffer (described further below)
typedef struct { int n; struct { char key[48]; int kind; int64_t i; const char *s; } e[32]; } kmd;
typedef struct { void *base; uint64_t cap; int32_t off, size; int64_t i64; int32_t i32; uint32_t u32; uint32_t owned, _r; } kbuf;
#define NN_OS_DEFAULT_THREAD_PRIORITY 16
#define KMVLOG(fmt, ...) do { if (g_flag.movie) LOG("movie(卡比) " fmt, ##__VA_ARGS__); } while (0)

static void (*g_free)(void *, void *); static void *g_user;
HLE_FORCE("_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", "过场：登记客体分配器（删除析构用其 free）",
          void, kmv_SetAllocator, void *alloc, void (*fr)(void *, void *), void *realloc_, void *user) {
    (void)alloc; (void)realloc_; g_free = fr; g_user = user;
}
HLE_FORCE("_ZN5movie11SetCoreMaskEm", "过场：解码线程的核掩码；宿主不分核", void, kmv_SetCoreMask, uint64_t mask) { (void)mask; }
HLE_FORCE("_ZN5movie17GetThreadPriorityEv", "过场：只作 Decoder 构造参数", int, kmv_GetThreadPriority, void) { return NN_OS_DEFAULT_THREAD_PRIORITY; }

// ---- Extractor / Decoder：对象首字 = 宿主虚表 ----
// ---- Extractor / Decoder: first word of the object = host vtable ----
static void kmv_dtor(void *self) { (void)self; }
static void kmv_delete(void *self) { if (!g_free) die("movie(卡比)：未登记分配器"); g_free(self, g_user); }
#define TRAP(cls, slot, what) static void kmv_trap_##cls##_##slot(void) { die("movie(卡比)：%s 槽%d %s 被调用（还没接，见 host/hle_movie_kirby.c）", #cls, slot, what); }

// ---- 空播状态（同一时刻只有一段视频；照 D2R）| no-op playback state (one video at a time; as in D2R) ----
typedef struct { int64_t dur_ns, deadline; void *ev_in, *ev_out; int eos_out; int64_t last_pts_us; } mv_t;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static mv_t *g_pending, *g_armed; static void (*g_signal_event)(void *);
static int64_t mono_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ll + t.tv_nsec; }
static mv_t *M(void *self) { return ((mv_t **)self)[1]; }
static void timer_main(void *arg) {   // 客体线程（nn::os::CreateThread 建），SignalEvent 在正规客体线程上调 | guest thread, so SignalEvent runs on a proper guest thread
    (void)arg; pthread_mutex_lock(&g_lock);
    for (;;) {
        mv_t *m = g_armed;
        if (!m || !m->deadline) { pthread_cond_wait(&g_cv, &g_lock); continue; }
        int64_t now = mono_ns();
        if (now < m->deadline) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); int64_t t = ts.tv_sec * 1000000000ll + ts.tv_nsec + (m->deadline - now);
            ts.tv_sec = t / 1000000000; ts.tv_nsec = t % 1000000000; pthread_cond_timedwait(&g_cv, &g_lock, &ts); continue; }
        g_armed = NULL; KMVLOG("到时长，发输入可用"); g_signal_event(m->ev_in);
    }
}
static void timer_ensure(void) {
    static int started; if (started) return; started = 1;
    g_signal_event = guest_sym("_ZN2nn2os11SignalEventEPNS0_9EventTypeE"); if (!g_signal_event) die("movie(卡比)：缺 nn::os::SignalEvent");
    void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size);
    dm_guest_thread_spawn(timer_main, NULL, NN_OS_DEFAULT_THREAD_PRIORITY, 0x10000);
}
static void arm(mv_t *m, int64_t at) { m->deadline = at; g_armed = m; pthread_cond_signal(&g_cv); }   // 持锁调 | call with the lock held

static int ext_SetDataSourcePath(void *self, const char *path) {
    char shown[160]; size_t n = 0;
    if (path) for (; n < sizeof shown - 1 && path[n] >= 0x20 && path[n] < 0x7f; n++) shown[n] = path[n];
    shown[n] = 0;
    mv_t *m = calloc(1, sizeof *m); m->dur_ns = MV_SKIP_NS;
    pthread_mutex_lock(&g_lock); ((mv_t **)self)[1] = m; g_pending = m; pthread_mutex_unlock(&g_lock);
    LOG("movie(卡比)：打开视频 %s → 空播 %.1f 秒（报 1 条视频轨，不解码）", n ? shown : "(非文本参数)", MV_SKIP_NS / 1e9);
    return ST_OK;
}
static int ext_GetTrackCount(void *self, int *n) { (void)self; *n = 1; return ST_OK; }
static void md_put(kmd *m, const char *key, int kind, int64_t i, const char *str) {
    if (m->n == 32) return; snprintf(m->e[m->n].key, sizeof m->e[0].key, "%s", key); m->e[m->n].kind = kind; m->e[m->n].i = i; m->e[m->n].s = str; m->n++;
}
static int ext_GetTrackConfiguration(void *self, int idx, void **md) {   // 唯一一条：VP9 视频 | the only track: VP9 video
    KMVLOG("GetTrackConfiguration %d", idx);
    if (idx != 0) return ST_EOS;
    free(*md); kmd *m = calloc(1, sizeof *m); *md = m;
    md_put(m, "mime", 1, 0, "video/x-vnd.on2.vp9"); md_put(m, "durationUs", 64, M(self)->dur_ns / 1000, NULL);
    md_put(m, "width", 32, 1920, NULL); md_put(m, "height", 32, 1080, NULL);
    return ST_OK;
}
static int ext_SelectTrack(void *self, int idx) { (void)self; KMVLOG("SelectTrack %d", idx); return ST_OK; }
static int ext_SeekTo(void *self, int64_t us, int mode) {
    (void)us; (void)mode; mv_t *m = M(self); KMVLOG("SeekTo");
    pthread_mutex_lock(&g_lock); arm(m, mono_ns() + m->dur_ns); m->last_pts_us = 0; pthread_mutex_unlock(&g_lock); return ST_OK;
}
// 卡比的读取循环（main+0x76ad88）先问「哪条轨道有数据」：返回 0x2000 就按本轨道到头处理、等到输入缓冲后送流尾（标志 4）；
// 空播没有样本可读，所以一律回流尾——到点时解码器发「输入可用」，播放器拿到输入缓冲就送流尾
// Kirby's reader loop (main+0x76ad88) first asks "which track has data": 0x2000 means this track is at its end, and it sends end-of-stream (flag 4) once it has an
// input buffer; no-op playback has no samples, so it always answers end-of-stream -- at the deadline the decoder signals "input available" and the player sends EOS
static int ext_GetTrackIndexForAvailableData(void *self, int *idx) {
    (void)self; *idx = -1; static int n; if (n++ < 3) KMVLOG("GetTrackIndexForAvailableData → 流尾"); return ST_EOS;
}
static int ext_Read(void *self, void *buf) { (void)buf; mv_t *m = M(self); m->last_pts_us = m->dur_ns / 1000; KMVLOG("Read → 流尾"); return ST_EOS; }
static int ext_Advance(void *self) { (void)self; KMVLOG("Advance"); return ST_OK; }
static int ext_GetSampleTime(void *self, int64_t *us) { *us = M(self)->last_pts_us; KMVLOG("GetSampleTime"); return ST_OK; }
TRAP(ext, 3, "SetDataSource(void const*, int)") TRAP(ext, 7, "UnselectTrack") TRAP(ext, 13, "GetSampleMetaData") TRAP(ext, 14, "GetBufferedRange")
TRAP(ext, 15, "SetDataSource(ClientStreamReader*, char const*)")
static void kmv_ext_delete(void *self) {
    pthread_mutex_lock(&g_lock); mv_t *m = M(self); if (m && g_pending == m) g_pending = NULL; pthread_mutex_unlock(&g_lock);
    kmv_delete(self);
}
static void *const g_ext_vt[16] = {
    kmv_dtor, kmv_ext_delete, ext_SetDataSourcePath, kmv_trap_ext_3, ext_GetTrackCount, ext_GetTrackConfiguration, ext_SelectTrack, kmv_trap_ext_7,
    ext_SeekTo, ext_GetTrackIndexForAvailableData, ext_Read, ext_Advance, ext_GetSampleTime, kmv_trap_ext_13, kmv_trap_ext_14, kmv_trap_ext_15,
};
HLE_FORCE("_ZN5movie9ExtractorC1ENS_13ContainerTypeE", "过场空播：Extractor 装宿主虚表（1 条视频轨）",
          void, kmv_Extractor, void **self, int type) { (void)type; self[0] = (void *)g_ext_vt; self[1] = NULL; }

static int dec_Configure(void *self, void *md) { (void)self; (void)md; KMVLOG("Decoder Configure"); return ST_OK; }
static int dec_RegisterClientEvents(void *self, void **ev) { mv_t *m = M(self); m->ev_in = ev[0]; m->ev_out = ev[1]; KMVLOG("RegisterClientEvents %p %p %p %p", ev[0], ev[1], ev[2], ev[3]); return ST_OK; }
static int dec_Start(void *self) {
    timer_ensure(); mv_t *m = M(self); KMVLOG("Decoder Start");
    if (!m->ev_in) die("movie(卡比)：Start 前没登记事件（播放器协议与 D2R 不同，见 hle_movie_kirby.c）");
    pthread_mutex_lock(&g_lock); arm(m, mono_ns() + m->dur_ns); pthread_mutex_unlock(&g_lock); return ST_OK;
}
static int dec_Stop(void *self) { mv_t *m = M(self); pthread_mutex_lock(&g_lock); m->deadline = 0; if (g_armed == m) g_armed = NULL; pthread_mutex_unlock(&g_lock); KMVLOG("Decoder Stop"); return ST_OK; }
static int dec_Flush(void *self) { (void)self; KMVLOG("Decoder Flush"); return ST_OK; }
static int dec_AcquireInputBufferIndex(void *self, int *idx, int *remaining) {
    mv_t *m = M(self); pthread_mutex_lock(&g_lock); int due = m->deadline && mono_ns() >= m->deadline; pthread_mutex_unlock(&g_lock);
    *idx = due ? 0 : -1; *remaining = 0; KMVLOG("AcquireInputBufferIndex → %s", due ? "0" : "暂无"); return due ? ST_OK : ST_TRY_AGAIN;
}
static uint8_t g_inbuf[16];
static int dec_GetInputBuffer(void *self, int idx, kbuf *b) { (void)self; KMVLOG("GetInputBuffer %d", idx); memset(b, 0, sizeof *b); b->base = g_inbuf; b->cap = sizeof g_inbuf; return ST_OK; }
static int dec_SendInputBufferForDecode(void *self, int idx, int off, int size, int64_t pts, int flags) {
    (void)idx; (void)off; (void)size; (void)pts; mv_t *m = M(self); KMVLOG("SendInputBufferForDecode 标志 %#x", flags);
    if (flags & FLAG_EOS) { pthread_mutex_lock(&g_lock); m->eos_out = MV_OUT_ITEMS; m->deadline = 0; pthread_mutex_unlock(&g_lock); KMVLOG("播完"); g_signal_event(m->ev_out); }
    return ST_OK;
}
// 交出 MV_OUT_ITEMS 个输出项：前面几帧普通黑帧、最后一项带流尾标志。卡比的播放器（main+0x771a78）要帧队列攒到 ≥2 帧才「就绪」开始播放计时，
// 只给 1 个流尾项时它永远不开始、也就永远不结束（真机灰屏）。每交出一项都用 remaining 报剩余，并再发一次「输出可用」
// Hand over MV_OUT_ITEMS output items: plain black frames first, the last one flagged end-of-stream. Kirby's player (main+0x771a78) only becomes "ready" and starts its
// playback clock once ≥2 frames are queued; with a single EOS item it never starts and so never ends (grey screen on device). Each hand-over reports what is left and re-signals "output available"
static int dec_AcquireOutputBufferIndex(void *self, int *idx, int64_t *pts, int *flags, int *remaining) {
    mv_t *m = M(self);
    pthread_mutex_lock(&g_lock); int n = m->eos_out; if (n) m->eos_out--; pthread_mutex_unlock(&g_lock);
    if (!n) { *remaining = 0; return ST_TRY_AGAIN; }
    int k = MV_OUT_ITEMS - n;
    *idx = k; *flags = n == 1 ? FLAG_EOS : 0; *pts = n == 1 ? m->dur_ns / 1000 : (int64_t)k * 33333; *remaining = n - 1;
    KMVLOG("交出输出项 %d%s", k, n == 1 ? "（流尾）" : "");
    if (n > 1) g_signal_event(m->ev_out);
    return ST_OK;
}
static int dec_ReleaseOutputBufferIndex(void *self, int idx) { (void)self; KMVLOG("ReleaseOutputBufferIndex %d", idx); return ST_OK; }
static int dec_GetOutputBufferFormat(void *self, int idx, void **md) {
    (void)self; KMVLOG("GetOutputBufferFormat %d", idx);
    free(*md); kmd *m = calloc(1, sizeof *m); *md = m; md_put(m, "width", 32, 1920, NULL); md_put(m, "height", 32, 1080, NULL); return ST_OK;
}
static int dec_GetOutputFormat(void *self, void **md) {   // 视频解码线程 main+0x770790 只读 width / height | the video decode thread (main+0x770790) reads only width / height
    (void)self; KMVLOG("Decoder GetOutputFormat");
    free(*md); kmd *m = calloc(1, sizeof *m); *md = m; md_put(m, "width", 32, 1920, NULL); md_put(m, "height", 32, 1080, NULL); return ST_OK;
}
// 流尾输出项的缓冲：一帧黑的 NV12（1920x1080；Y=0、UV=128），万一被拿去上屏也只是一帧黑 | buffer of the EOS output item: one black NV12 frame, harmless if it is ever presented
static int dec_GetOutputBuffer(void *self, int idx, kbuf *b) {
    static uint8_t *frame; enum { W = 1920, H = 1088, N = W * H * 3 / 2 };
    if (!frame) { frame = malloc(N); memset(frame, 0, W * H); memset(frame + W * H, 128, N - W * H); }
    KMVLOG("GetOutputBuffer %d", idx); memset(b, 0, sizeof *b); b->base = frame; b->cap = N; b->size = N; b->i64 = (int64_t)idx * 33333; return ST_OK;
}
TRAP(dec, 7, "Reset") TRAP(dec, 14, "GetOutputBufferSize")
TRAP(dec, 16, "GetLastError") TRAP(dec, 17, "GetOutputBuffer(MediaData)")
static void kmv_dec_delete(void *self) {
    pthread_mutex_lock(&g_lock); mv_t *m = M(self); if (m && g_armed == m) g_armed = NULL; pthread_mutex_unlock(&g_lock);
    kmv_delete(self);
}
static void *const g_dec_vt[19] = {
    kmv_dtor, kmv_dec_delete, dec_Configure, dec_RegisterClientEvents, dec_GetOutputFormat, dec_Start, dec_Stop, kmv_trap_dec_7,
    dec_Flush, dec_AcquireInputBufferIndex, dec_GetInputBuffer, dec_SendInputBufferForDecode, dec_AcquireOutputBufferIndex,
    dec_GetOutputBuffer, kmv_trap_dec_14, dec_ReleaseOutputBufferIndex, kmv_trap_dec_16, kmv_trap_dec_17, dec_GetOutputBufferFormat,
};
HLE_FORCE("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", "过场空播：Decoder 装宿主虚表（按时长发流尾）",
          void, kmv_Decoder, void **self, int type, int fmt, int mode, int prio) {
    pthread_mutex_lock(&g_lock); mv_t *m = g_pending; g_pending = NULL; pthread_mutex_unlock(&g_lock);
    LOG("movie(卡比)：建 Decoder 类型 %d 输出 %d 模式 %d 优先级 %d%s", type, fmt, mode, prio, m ? "" : "（前面没取过源）");
    if (!m) die("movie(卡比)：Decoder 构造前没有取过源");
    self[0] = (void *)g_dec_vt; self[1] = m;
}

// ---- MediaData：键值表。宿主布局 = 首字指向宿主侧的表（游戏在栈上给的空间 ≥ 8 字节）----
// ---- MediaData: key/value table. Host layout = the first word points to the host-side table (the game provides ≥ 8 bytes on its stack) ----
static kmd *MD(const void *self) { return *(kmd *const *)self; }
HLE_FORCE("_ZN5movie9MediaDataC1Ev", "过场：空 MediaData", void, kmv_MediaData, void **self) { *self = NULL; }
HLE_FORCE("_ZN5movie9MediaDataD1Ev", "过场：释放宿主表", void, kmv_MediaDataD, void **self) { free(*self); *self = NULL; }
HLE_FORCE("_ZN5movie9MediaData5ClearEv", "过场：清空宿主表", void, kmv_MediaDataClear, void **self) { free(*self); *self = NULL; }
static int md_find(const void *self, const char *key, int kind, int64_t *i, const char **s) {
    kmd *m = MD(self); if (!m || !key) return 0;
    for (int k = 0; k < m->n; k++) if (m->e[k].kind == kind && !strcmp(m->e[k].key, key)) { if (i) *i = m->e[k].i; if (s) *s = m->e[k].s; return 1; }
    return 0;
}
// 返回 bool：没有就 false，不写输出（与库语义一致）
// Return bool: false if absent, the output is not written (consistent with the library's semantics)
HLE_FORCE("_ZNK5movie9MediaData9FindInt32EPKcPi", "过场：查宿主表", unsigned, kmv_FindInt32, const void *self, const char *key, int32_t *out) {
    int64_t v; if (!md_find(self, key, 32, &v, NULL)) return 0; *out = (int32_t)v; return 1;
}
HLE_FORCE("_ZNK5movie9MediaData9FindInt64EPKcPl", "过场：查宿主表", unsigned, kmv_FindInt64, const void *self, const char *key, int64_t *out) {
    int64_t v; if (!md_find(self, key, 64, &v, NULL)) return 0; *out = v; return 1;
}
HLE_FORCE("_ZNK5movie9MediaData10FindStringEPKcPS2_", "过场：查宿主表", unsigned, kmv_FindString, const void *self, const char *key, const char **out) {
    const char *s; if (!md_find(self, key, 1, NULL, &s)) return 0; *out = s; return 1;
}

// ---- Buffer：宿主布局 kbuf（游戏栈上留了 ≥ 0x40 字节；本结构 48 字节）----
// ---- Buffer: host layout kbuf (the game leaves ≥ 0x40 bytes on its stack; this struct is 48 bytes) ----
_Static_assert(sizeof(kbuf) <= 0x40, "movie::Buffer host layout must fit the game's space");
HLE_FORCE("_ZN5movie6BufferC1Ev", "过场：空 Buffer", void, kmv_Buffer, kbuf *b) { memset(b, 0, sizeof *b); }
HLE_FORCE("_ZN5movie6BufferC1EPvm", "过场：Buffer 指向调用方内存", void, kmv_BufferPtr, kbuf *b, void *p, uint64_t cap) { memset(b, 0, sizeof *b); b->base = p; b->cap = cap; }
HLE_FORCE("_ZN5movie6BufferC1Em", "过场：Buffer 自带内存（宿主分配，析构释放）", void, kmv_BufferAlloc, kbuf *b, uint64_t cap) {
    memset(b, 0, sizeof *b); b->base = calloc(1, cap ? cap : 1); b->cap = cap; b->owned = 1;
    if (!b->base) die("movie(卡比)：Buffer 分配 %llu 字节失败", (unsigned long long)cap);
}
// 复制：浅拷贝，副本不拥有内存（避免两次释放）
// Copy: shallow copy, the copy does not own the memory (avoids a double free)
HLE_FORCE("_ZN5movie6BufferC1ERKS0_", "过场：Buffer 浅拷贝", void, kmv_BufferCopy, kbuf *b, const kbuf *o) { *b = *o; b->owned = 0; }
HLE_FORCE("_ZN5movie6BufferaSERKS0_", "过场：Buffer 浅赋值", kbuf *, kmv_BufferAssign, kbuf *b, const kbuf *o) {
    if (b != o) { if (b->owned) free(b->base); *b = *o; b->owned = 0; } return b;
}
HLE_FORCE("_ZN5movie6BufferD1Ev", "过场：释放自带内存", void, kmv_BufferD, kbuf *b) { if (b->owned) free(b->base); b->base = NULL; b->owned = 0; }
HLE_FORCE("_ZNK5movie6Buffer4BaseEv", "过场：Buffer 基址", void *, kmv_BufBase, const kbuf *b) { return b->base; }
HLE_FORCE("_ZNK5movie6Buffer8CapacityEv", "过场：Buffer 容量", uint64_t, kmv_BufCap, const kbuf *b) { return b->cap; }
HLE_FORCE("_ZNK5movie6Buffer6OffsetEv", "过场：Buffer 偏移", int, kmv_BufOffset, const kbuf *b) { return b->off; }
HLE_FORCE("_ZNK5movie6Buffer4SizeEv", "过场：Buffer 长度", int, kmv_BufSize, const kbuf *b) { return b->size; }
HLE_FORCE("_ZN5movie6Buffer8SetRangeEii", "过场：Buffer 设范围", void, kmv_BufSetRange, kbuf *b, int off, int size) { b->off = off; b->size = size; }
HLE_FORCE("_ZN5movie6Buffer18SetDataAndCapacityEPvm", "过场：Buffer 设数据", void, kmv_BufSetData, kbuf *b, void *p, uint64_t cap) {
    if (b->owned) free(b->base); b->base = p; b->cap = cap; b->off = b->size = 0; b->owned = 0;
}
HLE_FORCE("_ZN5movie6Buffer12SetInt32DataEi", "过场：Buffer 附带数据", void, kmv_BufSetI32, kbuf *b, int32_t v) { b->i32 = v; }
HLE_FORCE("_ZN5movie6Buffer12SetInt64DataEl", "过场：Buffer 附带数据", void, kmv_BufSetI64, kbuf *b, int64_t v) { b->i64 = v; }
HLE_FORCE("_ZN5movie6Buffer13SetUint32DataEj", "过场：Buffer 附带数据", void, kmv_BufSetU32, kbuf *b, uint32_t v) { b->u32 = v; }
HLE_FORCE("_ZNK5movie6Buffer12GetInt64DataEv", "过场：Buffer 附带数据", int64_t, kmv_BufGetI64, const kbuf *b) { return b->i64; }
HLE_FORCE("_ZNK5movie6Buffer13GetUint32DataEv", "过场：Buffer 附带数据", uint32_t, kmv_BufGetU32, const kbuf *b) { return b->u32; }
