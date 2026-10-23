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
// 2026-10-23 起本机能硬解 VP9 时（Mac，kirby_vp9.c）真放：解封装 kirby_webm.c，游戏送输入时当场同步解码，出帧进 MV_NOUT 格的输出队列，
// 播放器取帧时拷进它给的内存（NV12：Y 行距 = 宽，UV 紧跟，见 main+0x771fbc 起：SetDataAndCapacity 给游戏自己的内存 → GetOutputBufferFormat 读
// width / height / nv12-y-offset / nv12-uv-offset → GetOutputBuffer）。队列满就不给输入缓冲，节奏由播放器按时间戳定。过场的 Vorbis 音轨也报给游戏：kirby_vorbis.c（stb_vorbis）解成 16 位 PCM，游戏自己经 AudioOut 出声。不能解时照旧空播。
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
// Since 2026-10-23, when the host can hardware-decode VP9 (Mac, kirby_vp9.c) videos really play: demux in kirby_webm.c, synchronous decode when the game submits input, frames go to an
// MV_NOUT-slot output queue and are copied into the memory the player hands over (NV12: Y stride = width, UV right after; from main+0x771fbc: SetDataAndCapacity gives the game's own memory →
// GetOutputBufferFormat reads width / height / nv12-y-offset / nv12-uv-offset → GetOutputBuffer). No input buffer while the queue is full, so the player paces by timestamp. The cutscenes' Vorbis audio track is reported too: kirby_vorbis.c (stb_vorbis) decodes it to 16-bit PCM and the game plays it through AudioOut itself. Otherwise no-op playback as before.
#include "hle.h"
#include "kirby_video.h"
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

// ---- 播放状态：一段视频（mv_t，取源器和它建的解码器共用）+ 每个解码器一份（kdec）----
// 同一时刻只有一段视频（照 D2R）。真放时最多两条轨道：0 = 视频（VP9），1 = 音频（Vorbis，有且能解时才报）。
// 播放器只建一个取源器、两个读取线程共用一个读取位置（main+0x76ad88：轨道号对不上就等别的线程读），所以音频输出队列要够深，
// 交错距离实测 ≤ 55 ms（4 段过场），视频 4 格（133 ms）+ 音频 32 格（每格约 12 ms）不会互相卡住。
// ---- Playback state: one video (mv_t, shared by the extractor and the decoders it feeds) + one per decoder (kdec) ----
// One video at a time (as in D2R). Real playback reports up to two tracks: 0 = video (VP9), 1 = audio (Vorbis, only when present and decodable).
// The player builds a single extractor whose read position is shared by two reader threads (main+0x76ad88: a reader whose track does not match waits for the other),
// so the audio output queue must be deep enough; the measured interleave is ≤ 55 ms (all 4 cutscenes), and 4 video slots (133 ms) + 32 audio slots (~12 ms each) never block each other.
#define VOUT 4                   // 视频输出队列格数 | video output slots
#define AOUT 32                  // 音频输出队列格数 | audio output slots
#define VINCAP (4 << 20)         // 视频输入缓冲：1080p 关键帧也放得下 | video input buffer: fits a 1080p keyframe
#define AINCAP (64 << 10)        // 音频输入缓冲：Vorbis 包 < 1 KB | audio input buffer: Vorbis packets are < 1 KB
#define APCM (4096 * 2 * 2)      // 一个音频输出项：Vorbis 一次最多 4096 帧 × 2 声道 × 16 位（游戏的缓冲 32 KB）| one audio item: at most 4096 frames × 2 ch × 16-bit (the game's buffer is 32 KB)
typedef struct { uint8_t *buf; size_t len; int64_t pts; int state; uint64_t seq; } kout;   // state 0 空 1 解好待取 2 播放器拿着 | state 0 free 1 decoded, waiting 2 held by the player
typedef struct mv mv_t;
typedef struct kdec {
    int64_t deadline; void *ev_in, *ev_out;   // 计时线程到点发「输入可用」| the timer thread signals "input available" at the deadline
    mv_t *m; int audio, real, eos_out;          // eos_out：空播时还要交出的输出项数 | eos_out: output items still to hand over in no-op playback
    kv_dec *kv; kvb_dec *vb; uint8_t *in; size_t incap;
    kout o[AOUT]; int nout; size_t ocap; uint64_t seq; int eos_in, eos_pending;
} kdec;
struct mv {
    int64_t dur_ns, last_pts_us; int real, ext_gone, refs; kdec *vdec;
    kw_file *f; int vt, at, ntr; unsigned sel; size_t cur;
    int w, h; size_t fsz; int ach, arate;
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static mv_t *g_pending; static kdec *g_armed[8]; static void (*g_signal_event)(void *);
static int64_t mono_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ll + t.tv_nsec; }
static mv_t *M(void *self) { return ((mv_t **)self)[1]; }
static kdec *D(void *self) { return ((kdec **)self)[1]; }
static void timer_main(void *arg) {   // 客体线程（nn::os::CreateThread 建），SignalEvent 在正规客体线程上调 | guest thread, so SignalEvent runs on a proper guest thread
    (void)arg; pthread_mutex_lock(&g_lock);
    for (;;) {
        int at = -1; for (int i = 0; i < 8; i++) if (g_armed[i] && g_armed[i]->deadline && (at < 0 || g_armed[i]->deadline < g_armed[at]->deadline)) at = i;
        if (at < 0) { pthread_cond_wait(&g_cv, &g_lock); continue; }
        kdec *d = g_armed[at]; int64_t now = mono_ns();
        if (now < d->deadline) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); int64_t t = ts.tv_sec * 1000000000ll + ts.tv_nsec + (d->deadline - now);
            ts.tv_sec = t / 1000000000; ts.tv_nsec = t % 1000000000; pthread_cond_timedwait(&g_cv, &g_lock, &ts); continue; }
        g_armed[at] = NULL; KMVLOG("到时长，发输入可用（%s）", d->audio ? "音频" : "视频"); g_signal_event(d->ev_in);   // deadline 留着：空播靠它判断到点 | deadline kept: no-op playback checks it
    }
}
static void signal_ensure(void) {
    if (!g_signal_event) g_signal_event = guest_sym("_ZN2nn2os11SignalEventEPNS0_9EventTypeE");
    if (!g_signal_event) die("movie(卡比)：缺 nn::os::SignalEvent");
}
static void timer_ensure(void) {
    static int started; if (started) return; started = 1;
    signal_ensure();
    void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size);
    dm_guest_thread_spawn(timer_main, NULL, NN_OS_DEFAULT_THREAD_PRIORITY, 0x10000);
}
static void disarm(kdec *d) { for (int i = 0; i < 8; i++) if (g_armed[i] == d) g_armed[i] = NULL; }   // 持锁调 | call with the lock held
static void arm(kdec *d, int64_t at) {   // 持锁调 | call with the lock held
    d->deadline = at; disarm(d);
    int i = 0; while (i < 8 && g_armed[i]) i++; if (i == 8) die("movie(卡比)：同时挂的定时超过 8 个");
    g_armed[i] = d; pthread_cond_signal(&g_cv);
}
static void mv_unref(mv_t *m) {   // 持锁调；取源器和解码器都没了才关文件 | call with the lock held; closes the file once the extractor and all decoders are gone
    if (m->refs > 0 || !m->ext_gone) return;
    if (g_pending == m) g_pending = NULL;
    kw_close(m->f); free(m);
}

static int ext_SetDataSourcePath(void *self, const char *path) {
    char shown[160]; size_t n = 0;
    if (path) for (; n < sizeof shown - 1 && path[n] >= 0x20 && path[n] < 0x7f; n++) shown[n] = path[n];
    shown[n] = 0;
    mv_t *m = calloc(1, sizeof *m); m->dur_ns = MV_SKIP_NS; m->vt = m->at = -1; m->ntr = 1;
    char hp[1024]; int dm_fs_host_path(const char *guest, char *out, size_t n);
    const char *noop = getenv("KIRBY_MOVIE_NOOP");   // =1 强制空播（对照用）| =1 forces no-op playback (for comparison)
    if (n && !(noop && *noop == '1') && kv_available() && dm_fs_host_path(path, hp, sizeof hp) && (m->f = kw_open(hp))) {
        m->vt = kw_find(m->f, KW_VIDEO);
        if (m->vt >= 0 && !strcmp(m->f->tracks[m->vt].codec, "V_VP9")) {
            m->real = 1; m->w = m->f->tracks[m->vt].width; m->h = m->f->tracks[m->vt].height; m->fsz = (size_t)m->w * m->h * 3 / 2;
            m->dur_ns = m->f->duration_us * 1000;
            // 音轨：Vorbis、1–2 声道、48 kHz（宿主 AudioOut 只做 48 kHz）才报给游戏 | audio: reported only for Vorbis, 1–2 channels, 48 kHz (the host AudioOut only does 48 kHz)
            int at = kw_find(m->f, KW_AUDIO); kw_track *t = at >= 0 ? &m->f->tracks[at] : NULL;
            if (t && !strcmp(t->codec, "A_VORBIS") && t->channels >= 1 && t->channels <= 2 && (int)t->rate == 48000) { m->at = at; m->ach = t->channels; m->arate = 48000; m->ntr = 2; }
        } else { kw_close(m->f); m->f = NULL; }
    }
    pthread_mutex_lock(&g_lock); ((mv_t **)self)[1] = m; g_pending = m; pthread_mutex_unlock(&g_lock);
    if (m->real) LOG("movie(卡比)：打开视频 %s → 播放 %dx%d %.1f 秒（VP9 硬解%s）", shown, m->w, m->h, m->dur_ns / 1e9, m->at >= 0 ? "，Vorbis 音轨" : "，无音轨");
    else LOG("movie(卡比)：打开视频 %s → 空播 %.1f 秒（报 1 条视频轨，不解码）", n ? shown : "(非文本参数)", MV_SKIP_NS / 1e9);
    return ST_OK;
}
static int ext_GetTrackCount(void *self, int *n) { *n = M(self)->real ? M(self)->ntr : 1; return ST_OK; }
static void md_put(kmd *m, const char *key, int kind, int64_t i, const char *str) {
    if (m->n == 32) return; snprintf(m->e[m->n].key, sizeof m->e[0].key, "%s", key); m->e[m->n].kind = kind; m->e[m->n].i = i; m->e[m->n].s = str; m->n++;
}
static int ext_GetTrackConfiguration(void *self, int idx, void **md) {   // 0 = VP9 视频，1 = Vorbis 音频 | 0 = VP9 video, 1 = Vorbis audio
    mv_t *v = M(self); KMVLOG("GetTrackConfiguration %d", idx);
    if (idx < 0 || idx >= (v->real ? v->ntr : 1)) return ST_EOS;
    free(*md); kmd *m = calloc(1, sizeof *m); *md = m;
    if (idx == 1) {
        md_put(m, "mime", 1, 0, "audio/vorbis"); md_put(m, "durationUs", 64, v->dur_ns / 1000, NULL);
        md_put(m, "sample-rate", 32, v->arate, NULL); md_put(m, "channel-count", 32, v->ach, NULL); return ST_OK;
    }
    md_put(m, "mime", 1, 0, "video/x-vnd.on2.vp9"); md_put(m, "durationUs", 64, v->dur_ns / 1000, NULL);
    md_put(m, "width", 32, v->real ? v->w : 1920, NULL); md_put(m, "height", 32, v->real ? v->h : 1080, NULL);
    return ST_OK;
}
static int ext_SelectTrack(void *self, int idx) { KMVLOG("SelectTrack %d", idx); if (idx >= 0 && idx < 2) M(self)->sel |= 1u << idx; return ST_OK; }
// 真放：文件里的轨道 → 报给游戏的轨道号（0 视频 / 1 音频 / -1 不报）；从读取位置起下一个「已选轨道」的样本 | real playback: file track → reported index; the next sample of a selected track from the cursor on
static int tr_of(const mv_t *m, int t) { return t == m->vt ? 0 : t == m->at && m->at >= 0 ? 1 : -1; }
static size_t snext(mv_t *m) {
    size_t i = m->cur;
    while (i < m->f->ns) { int r = tr_of(m, m->f->s[i].track); if (r >= 0 && (m->sel >> r & 1)) break; i++; }
    return i;
}
static int ext_SeekTo(void *self, int64_t us, int mode) {
    (void)mode; mv_t *m = M(self); KMVLOG("SeekTo %lld", (long long)us);
    if (m->real) {   // 退到不晚于目标的最近视频关键帧 | back to the nearest video keyframe not after the target
        size_t at = 0; for (size_t i = 0; i < m->f->ns; i++) { kw_sample *s = &m->f->s[i]; if (s->track != m->vt) continue; if (s->pts_us > us) break; if (s->key) at = i; }
        m->cur = at; m->last_pts_us = 0; return ST_OK;
    }
    pthread_mutex_lock(&g_lock); if (m->vdec) arm(m->vdec, mono_ns() + m->dur_ns); m->last_pts_us = 0; pthread_mutex_unlock(&g_lock); return ST_OK;
}
// 卡比的读取循环（main+0x76ad88）先问「哪条轨道有数据」：返回 0x2000 就按本轨道到头处理、等到输入缓冲后送流尾（标志 4）；
// 空播没有样本可读，所以一律回流尾——到点时解码器发「输入可用」，播放器拿到输入缓冲就送流尾
// Kirby's reader loop (main+0x76ad88) first asks "which track has data": 0x2000 means this track is at its end, and it sends end-of-stream (flag 4) once it has an
// input buffer; no-op playback has no samples, so it always answers end-of-stream -- at the deadline the decoder signals "input available" and the player sends EOS
// 输出参数是 64 位：读取循环先填 -1（8 字节）、回来按 64 位和自己的轨道号比（main+0x76ad88），只写低 32 位会永远对不上
// The out parameter is 64-bit: the reader loop pre-fills -1 (8 bytes) and compares all 64 bits with its own track index (main+0x76ad88); writing only the low 32 bits never matches
static int ext_GetTrackIndexForAvailableData(void *self, int64_t *idx) {
    mv_t *m = M(self);
    if (m->real) { size_t i = snext(m); if (i >= m->f->ns) { *idx = -1; return ST_EOS; } *idx = tr_of(m, m->f->s[i].track); return ST_OK; }
    *idx = -1; static int n; if (n++ < 3) KMVLOG("GetTrackIndexForAvailableData → 流尾"); return ST_EOS;
}
static int ext_Read(void *self, kbuf *b) {
    mv_t *m = M(self);
    if (m->real) {
        size_t i = snext(m); if (i >= m->f->ns) { m->last_pts_us = m->dur_ns / 1000; return ST_EOS; }
        kw_sample *s = &m->f->s[i]; uint32_t n = s->size;
        if (n > b->cap) { LOG("movie(卡比)：样本 %u 字节放不下输入缓冲 %llu，截断", n, (unsigned long long)b->cap); n = (uint32_t)b->cap; }
        memcpy(b->base, m->f->data + s->off, n); b->off = 0; b->size = (int32_t)n; b->i64 = s->pts_us; m->last_pts_us = s->pts_us;
        return ST_OK;
    }
    m->last_pts_us = m->dur_ns / 1000; KMVLOG("Read → 流尾"); return ST_EOS;
}
static int ext_Advance(void *self) { mv_t *m = M(self); if (m->real) m->cur = snext(m) + 1; KMVLOG("Advance"); return ST_OK; }
static int ext_GetSampleTime(void *self, int64_t *us) {
    mv_t *m = M(self);
    if (m->real) { size_t i = snext(m); *us = i < m->f->ns ? m->f->s[i].pts_us : m->dur_ns / 1000; return ST_OK; }
    *us = m->last_pts_us; KMVLOG("GetSampleTime"); return ST_OK;
}
TRAP(ext, 3, "SetDataSource(void const*, int)") TRAP(ext, 7, "UnselectTrack") TRAP(ext, 13, "GetSampleMetaData") TRAP(ext, 14, "GetBufferedRange")
TRAP(ext, 15, "SetDataSource(ClientStreamReader*, char const*)")
static void kmv_ext_delete(void *self) {
    pthread_mutex_lock(&g_lock); mv_t *m = M(self);
    if (m) { if (g_pending == m) g_pending = NULL; m->ext_gone = 1; mv_unref(m); }
    pthread_mutex_unlock(&g_lock);
    kmv_delete(self);
}
static void *const g_ext_vt[16] = {
    kmv_dtor, kmv_ext_delete, ext_SetDataSourcePath, kmv_trap_ext_3, ext_GetTrackCount, ext_GetTrackConfiguration, ext_SelectTrack, kmv_trap_ext_7,
    ext_SeekTo, ext_GetTrackIndexForAvailableData, ext_Read, ext_Advance, ext_GetSampleTime, kmv_trap_ext_13, kmv_trap_ext_14, kmv_trap_ext_15,
};
HLE_FORCE("_ZN5movie9ExtractorC1ENS_13ContainerTypeE", "过场：Extractor 装宿主虚表（真放 / 空播）",
          void, kmv_Extractor, void **self, int type) { (void)type; self[0] = (void *)g_ext_vt; self[1] = NULL; }

static int dec_Configure(void *self, void *md) { (void)self; (void)md; KMVLOG("Decoder Configure"); return ST_OK; }
static int dec_RegisterClientEvents(void *self, void **ev) { kdec *d = D(self); d->ev_in = ev[0]; d->ev_out = ev[1]; KMVLOG("RegisterClientEvents %p %p %p %p", ev[0], ev[1], ev[2], ev[3]); return ST_OK; }
static int out_busy(kdec *d) { int n = 0; for (int i = 0; i < d->nout; i++) n += d->o[i].state != 0; return n; }
static int out_ready(kdec *d) { int n = 0; for (int i = 0; i < d->nout; i++) n += d->o[i].state == 1; return n; }
static int dec_Start(void *self) {
    kdec *d = D(self); mv_t *m = d->m; KMVLOG("Decoder Start（%s）", d->audio ? "音频" : "视频");
    if (!d->ev_in) die("movie(卡比)：Start 前没登记事件（播放器协议与 D2R 不同，见 hle_movie_kirby.c）");
    if (d->real && !d->in) {
        if (d->audio) {
            int ch = 0, rate = 0; kw_track *t = &m->f->tracks[m->at];
            d->vb = kvb_open(t->priv, t->priv_len, &ch, &rate); d->incap = AINCAP; d->nout = AOUT; d->ocap = APCM;
            if (d->vb && (ch != m->ach || rate != m->arate)) { LOG("movie(卡比)：Vorbis 头说 %d 声道 %d Hz，和轨道信息不符", ch, rate); kvb_close(d->vb); d->vb = NULL; }
        } else { d->kv = kv_open(m->w, m->h); d->incap = VINCAP; d->nout = VOUT; d->ocap = m->fsz; }
        d->in = malloc(d->incap); for (int i = 0; i < d->nout; i++) d->o[i].buf = malloc(d->ocap);
        if (d->audio ? !d->vb : !d->kv) { LOG("movie(卡比)：建%s解码失败，这一路退回空播", d->audio ? " Vorbis " : " VP9 "); d->real = 0; }
    }
    // 「输入可用」不能在 Start 里当场发：实测播放器在 Start 返回后会把事件清掉，读取线程就永远等不到。照空播的做法交给计时线程稍后发
    // "Input available" cannot be signalled inside Start: measured, the player clears the events after Start returns and the reader then waits forever. As in no-op playback, the timer thread signals it shortly after
    timer_ensure();
    pthread_mutex_lock(&g_lock); arm(d, mono_ns() + (d->real ? 1000000 : m->dur_ns)); pthread_mutex_unlock(&g_lock); return ST_OK;
}
static int dec_Stop(void *self) { kdec *d = D(self); pthread_mutex_lock(&g_lock); d->deadline = 0; disarm(d); pthread_mutex_unlock(&g_lock); KMVLOG("Decoder Stop"); return ST_OK; }
static int dec_Flush(void *self) {
    kdec *d = D(self); KMVLOG("Decoder Flush（%s）", d->audio ? "音频" : "视频");
    if (d->real) {
        pthread_mutex_lock(&g_lock); for (int i = 0; i < d->nout; i++) d->o[i].state = 0; d->eos_in = d->eos_pending = 0; pthread_mutex_unlock(&g_lock);
        if (d->vb) kvb_reset(d->vb);
        signal_ensure(); g_signal_event(d->ev_in);
    }
    return ST_OK;
}
static int dec_AcquireInputBufferIndex(void *self, int *idx, int *remaining) {
    kdec *d = D(self);
    if (d->real) {   // 输入同步解完立即可用；输出队列满了就先不给 | input is free again as soon as it is decoded synchronously; none while the output queue is full
        pthread_mutex_lock(&g_lock); int ok = !d->eos_in && out_busy(d) < d->nout; pthread_mutex_unlock(&g_lock);
        *idx = ok ? 0 : -1; *remaining = 0; return ok ? ST_OK : ST_TRY_AGAIN;
    }
    pthread_mutex_lock(&g_lock); int due = d->deadline && mono_ns() >= d->deadline; pthread_mutex_unlock(&g_lock);
    *idx = due ? 0 : -1; *remaining = 0; KMVLOG("AcquireInputBufferIndex → %s", due ? "0" : "暂无"); return due ? ST_OK : ST_TRY_AGAIN;
}
static uint8_t g_inbuf[16];
static int dec_GetInputBuffer(void *self, int idx, kbuf *b) {
    kdec *d = D(self); KMVLOG("GetInputBuffer %d", idx); memset(b, 0, sizeof *b);
    if (d->real) { b->base = d->in; b->cap = d->incap; } else { b->base = g_inbuf; b->cap = sizeof g_inbuf; }
    return ST_OK;
}
static int dec_SendInputBufferForDecode(void *self, int idx, int off, int size, int64_t pts, int flags) {
    (void)idx; kdec *d = D(self); KMVLOG("SendInputBufferForDecode 标志 %#x", flags);
    if (d->real) {
        if (size > 0 && off >= 0 && (size_t)off + (size_t)size <= d->incap) {
            kout *o = NULL; pthread_mutex_lock(&g_lock); for (int i = 0; i < d->nout && !o; i++) if (!d->o[i].state) o = &d->o[i]; pthread_mutex_unlock(&g_lock);
            size_t len = 0;
            if (o && d->audio) { int fr = kvb_decode(d->vb, d->in + off, (size_t)size, (int16_t *)o->buf, (int)(d->ocap / (2u * (unsigned)d->m->ach))); if (fr > 0) len = (size_t)fr * 2u * (unsigned)d->m->ach; }
            else if (o && kv_decode(d->kv, d->in + off, (size_t)size, o->buf) > 0) len = d->ocap;
            if (len) { pthread_mutex_lock(&g_lock); o->len = len; o->pts = pts; o->seq = ++d->seq; o->state = 1; pthread_mutex_unlock(&g_lock); g_signal_event(d->ev_out); }
        }
        if (flags & FLAG_EOS) { pthread_mutex_lock(&g_lock); d->eos_in = d->eos_pending = 1; pthread_mutex_unlock(&g_lock); KMVLOG("输入到流尾（%s）", d->audio ? "音频" : "视频"); g_signal_event(d->ev_out); }
        else g_signal_event(d->ev_in);
        return ST_OK;
    }
    if (flags & FLAG_EOS) { pthread_mutex_lock(&g_lock); d->eos_out = MV_OUT_ITEMS; d->deadline = 0; pthread_mutex_unlock(&g_lock); KMVLOG("播完"); g_signal_event(d->ev_out); }
    return ST_OK;
}
// 空播：交出 MV_OUT_ITEMS 个输出项：前面几帧普通黑帧、最后一项带流尾标志。卡比的播放器（main+0x771a78）要帧队列攒到 ≥2 帧才「就绪」开始播放计时，
// 只给 1 个流尾项时它永远不开始、也就永远不结束（真机灰屏）。每交出一项都用 remaining 报剩余，并再发一次「输出可用」
// No-op playback: hand over MV_OUT_ITEMS output items: plain black frames first, the last one flagged end-of-stream. Kirby's player (main+0x771a78) only becomes "ready" and starts its
// playback clock once ≥2 frames are queued; with a single EOS item it never starts and so never ends (grey screen on device). Each hand-over reports what is left and re-signals "output available"
static int dec_AcquireOutputBufferIndex(void *self, int *idx, int64_t *pts, int *flags, int *remaining) {
    kdec *d = D(self);
    if (d->real) {   // 按解出的顺序交；都交完了再交流尾项（下标 nout，不带数据）| hand over in decode order; the EOS item (index nout, no data) only after all of them
        pthread_mutex_lock(&g_lock);
        int at = -1; for (int i = 0; i < d->nout; i++) if (d->o[i].state == 1 && (at < 0 || d->o[i].seq < d->o[at].seq)) at = i;
        int rc = ST_OK;
        if (at >= 0) { d->o[at].state = 2; *idx = at; *pts = d->o[at].pts; *flags = 0; *remaining = out_ready(d) + d->eos_pending; }
        else if (d->eos_pending) { d->eos_pending = 0; *idx = d->nout; *pts = d->m->dur_ns / 1000; *flags = FLAG_EOS; *remaining = 0; KMVLOG("交出流尾（%s）", d->audio ? "音频" : "视频"); }
        else { *remaining = 0; rc = ST_TRY_AGAIN; }
        pthread_mutex_unlock(&g_lock); return rc;
    }
    pthread_mutex_lock(&g_lock); int n = d->eos_out; if (n) d->eos_out--; pthread_mutex_unlock(&g_lock);
    if (!n) { *remaining = 0; return ST_TRY_AGAIN; }
    int k = MV_OUT_ITEMS - n;
    *idx = k; *flags = n == 1 ? FLAG_EOS : 0; *pts = n == 1 ? d->m->dur_ns / 1000 : (int64_t)k * 33333; *remaining = n - 1;
    KMVLOG("交出输出项 %d%s", k, n == 1 ? "（流尾）" : "");
    if (n > 1) g_signal_event(d->ev_out);
    return ST_OK;
}
static int dec_ReleaseOutputBufferIndex(void *self, int idx) {
    kdec *d = D(self); KMVLOG("ReleaseOutputBufferIndex %d", idx);
    if (d->real && idx >= 0 && idx < d->nout) { pthread_mutex_lock(&g_lock); d->o[idx].state = 0; pthread_mutex_unlock(&g_lock); g_signal_event(d->ev_in); }   // 腾出一格，读取线程可以接着送 | a slot is free, the reader can continue
    return ST_OK;
}
static void fmt_put(kdec *d, kmd *m) {
    if (d->audio) { md_put(m, "channel-count", 32, d->m->ach, NULL); md_put(m, "sample-rate", 32, d->m->arate, NULL); return; }
    int w = d->m->real ? d->m->w : 1920, h = d->m->real ? d->m->h : 1080;
    md_put(m, "width", 32, w, NULL); md_put(m, "height", 32, h, NULL);
    md_put(m, "nv12-y-offset", 32, 0, NULL); md_put(m, "nv12-uv-offset", 32, (int64_t)w * h, NULL);
}
static int dec_GetOutputBufferFormat(void *self, int idx, void **md) {
    KMVLOG("GetOutputBufferFormat %d", idx); free(*md); kmd *m = calloc(1, sizeof *m); *md = m; fmt_put(D(self), m); return ST_OK;
}
// 视频解码线程（main+0x770790）只读 width / height；音频线程（main+0x7700a8）读 channel-count / sample-rate，按 16 位 PCM 配 AudioOut 缓冲
// The video decode thread (main+0x770790) reads only width / height; the audio thread (main+0x7700a8) reads channel-count / sample-rate and sizes AudioOut buffers for 16-bit PCM
static int dec_GetOutputFormat(void *self, void **md) {
    KMVLOG("Decoder GetOutputFormat"); free(*md); kmd *m = calloc(1, sizeof *m); *md = m; fmt_put(D(self), m); return ST_OK;
}
// 流尾输出项的缓冲：视频是一帧黑的 NV12（1920x1080；Y=0、UV=128），万一被拿去上屏也只是一帧黑；音频是空的
// Buffer of the EOS output item: for video one black NV12 frame, harmless if it is ever presented; empty for audio
static int dec_GetOutputBuffer(void *self, int idx, kbuf *b) {
    kdec *d = D(self);
    if (d->real && idx >= 0 && idx < d->nout) {   // 播放器给了自己的内存（SetDataAndCapacity）就拷进去，没给就指向我们的格子 | copy into the player's own memory if it gave some, else point at our slot
        kout *o = &d->o[idx];
        if (b->base && b->cap >= o->len) memcpy(b->base, o->buf, o->len); else { b->base = o->buf; b->cap = d->ocap; }
        b->off = 0; b->size = (int32_t)o->len; b->i64 = o->pts; return ST_OK;
    }
    if (d->audio) { b->off = 0; b->size = 0; return ST_OK; }
    static uint8_t *frame; enum { W = 1920, H = 1088, N = W * H * 3 / 2 };
    if (!frame) { frame = malloc(N); memset(frame, 0, W * H); memset(frame + W * H, 128, N - W * H); }
    KMVLOG("GetOutputBuffer %d", idx); memset(b, 0, sizeof *b); b->base = frame; b->cap = N; b->size = N; b->i64 = (int64_t)idx * 33333; return ST_OK;
}
TRAP(dec, 7, "Reset") TRAP(dec, 14, "GetOutputBufferSize")
TRAP(dec, 16, "GetLastError") TRAP(dec, 17, "GetOutputBuffer(MediaData)")
static void kmv_dec_delete(void *self) {
    kdec *d = D(self);
    if (d) {
        pthread_mutex_lock(&g_lock); disarm(d); pthread_mutex_unlock(&g_lock);
        if (d->kv) kv_close(d->kv);
        if (d->vb) kvb_close(d->vb);
        free(d->in); for (int i = 0; i < AOUT; i++) free(d->o[i].buf);
        pthread_mutex_lock(&g_lock); mv_t *m = d->m; if (m->vdec == d) m->vdec = NULL; m->refs--; mv_unref(m); pthread_mutex_unlock(&g_lock);
        free(d);
    }
    kmv_delete(self);
}
static void *const g_dec_vt[19] = {
    kmv_dtor, kmv_dec_delete, dec_Configure, dec_RegisterClientEvents, dec_GetOutputFormat, dec_Start, dec_Stop, kmv_trap_dec_7,
    dec_Flush, dec_AcquireInputBufferIndex, dec_GetInputBuffer, dec_SendInputBufferForDecode, dec_AcquireOutputBufferIndex,
    dec_GetOutputBuffer, kmv_trap_dec_14, dec_ReleaseOutputBufferIndex, kmv_trap_dec_16, kmv_trap_dec_17, dec_GetOutputBufferFormat,
};
// 解码器类型：3 = VP9（实测）；音轨只报 Vorbis，所以真放时别的类型就是音频解码器
// Decoder type: 3 = VP9 (measured); only Vorbis audio is reported, so in real playback any other type is the audio decoder
#define DEC_TYPE_VP9 3
HLE_FORCE("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", "过场：Decoder 装宿主虚表（VP9 / Vorbis 真放，或空播）",
          void, kmv_Decoder, void **self, int type, int fmt, int mode, int prio) {
    pthread_mutex_lock(&g_lock); mv_t *m = g_pending;
    kdec *d = m ? calloc(1, sizeof *d) : NULL;
    if (d) { d->m = m; m->refs++; d->audio = type != DEC_TYPE_VP9 && m->real && m->at >= 0; d->real = m->real; if (!d->audio) m->vdec = d; }
    pthread_mutex_unlock(&g_lock);
    LOG("movie(卡比)：建 Decoder 类型 %d 输出 %d 模式 %d 优先级 %d%s", type, fmt, mode, prio, !m ? "（前面没取过源）" : d->audio ? "（音频）" : "（视频）");
    if (!m) die("movie(卡比)：Decoder 构造前没有取过源");
    self[0] = (void *)g_dec_vt; self[1] = d;
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
