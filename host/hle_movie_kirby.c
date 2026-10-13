// 过场动画（movie:: = subsdk0 的任天堂 movie 库）：卡比版。取代 D2R 的 dm/host/hle_movie.c（卡比构建不编那份）。
// 为什么不共用 D2R 的：卡比的播放器（main+0x76a000–0x773000，HAL 自写）协议不同——
//   main+0x76dce0 构造 Extractor → 槽 2 SetDataSource(char const* 路径)（D2R 用槽 15，槽 2 在 D2R 那份里是「被调到即中止」）
//   → 槽 4 GetTrackCount → 逐轨 槽 5 GetTrackConfiguration(idx, MediaData*) + MediaData::FindString("mime") / FindInt64("durationUs") / FindInt32(...)，
//     按 "video/" / "audio/" 置视频轨 [P+0x158]、音频轨 [P+0x15c] → 槽 6 SelectTrack → 各建一个 Decoder。
// 本文件的第一版策略：报 0 条轨道。播放器于是既不建视频解码器也不建音频解码器（main+0x76dd20、+0x76df58、+0x76e04c 三处分支）。
// 之后它怎么收尾（直接当「播完」，还是一直等），要到 Mac 上运行才能确认。
// 其余虚槽一律「被调到即中止并报名」（与 D2R 同一原则：不靠返回 0 侥幸通过），运行时撞到哪个就补哪个。
// Buffer / MediaData：宿主自定布局，库里所有被卡比导入的成员函数都在这里接管，避免宿主布局与库里真实实现混用。
// Cutscenes (movie:: = Nintendo's movie library in subsdk0): Kirby version. Replaces D2R's dm/host/hle_movie.c (the Kirby build does not compile that one).
// Why not share D2R's: Kirby's player (main+0x76a000–0x773000, written by HAL) uses a different protocol --
//   main+0x76dce0 constructs the Extractor → slot 2 SetDataSource(char const* path) (D2R uses slot 15; in D2R's file slot 2 aborts when called)
//   → slot 4 GetTrackCount → per track slot 5 GetTrackConfiguration(idx, MediaData*) + MediaData::FindString("mime") / FindInt64("durationUs") / FindInt32(...),
//     marking video track [P+0x158] / audio track [P+0x15c] by "video/" / "audio/" → slot 6 SelectTrack → one Decoder each.
// First-version strategy of this file: report 0 tracks. The player then builds neither a video nor an audio decoder (branches at main+0x76dd20, +0x76df58, +0x76e04c).
// How it wraps up afterwards (treats it as "finished playing", or keeps waiting) can only be confirmed by running on a Mac.
// Every other virtual slot aborts and reports its name when called (same principle as D2R: no passing by luck with a 0 return); whichever one is hit at runtime gets implemented.
// Buffer / MediaData: host-defined layouts; every member function Kirby imports is taken over here, so the host layout is never mixed with the library's real implementation.
#include "hle.h"

#define ST_OK 0
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
#define TRAP(cls, slot, what) static void kmv_trap_##cls##_##slot(void) { die("movie(卡比)：%s 槽%d %s 被调用（还没接，见 kirby/host/hle_movie_kirby.c）", #cls, slot, what); }

static int ext_SetDataSourcePath(void *self, const char *path) {
    (void)self;
    char shown[160]; size_t n = 0;
    if (path) for (; n < sizeof shown - 1 && path[n] >= 0x20 && path[n] < 0x7f; n++) shown[n] = path[n];
    shown[n] = 0;
    LOG("movie(卡比)：打开视频 %s → 报 0 条轨道（过场动画跳过）", n ? shown : "(非文本参数)");
    return ST_OK;
}
static int ext_GetTrackCount(void *self, int *n) { (void)self; *n = 0; return ST_OK; }
TRAP(ext, 3, "SetDataSource(void const*, int)") TRAP(ext, 5, "GetTrackConfiguration") TRAP(ext, 6, "SelectTrack")
TRAP(ext, 7, "UnselectTrack") TRAP(ext, 8, "SeekTo") TRAP(ext, 9, "GetTrackIndexForAvailableData") TRAP(ext, 10, "Read")
TRAP(ext, 11, "Advance") TRAP(ext, 12, "GetSampleTime") TRAP(ext, 13, "GetSampleMetaData") TRAP(ext, 14, "GetBufferedRange")
TRAP(ext, 15, "SetDataSource(ClientStreamReader*, char const*)")
static void *const g_ext_vt[16] = {
    kmv_dtor, kmv_delete, ext_SetDataSourcePath, kmv_trap_ext_3, ext_GetTrackCount, kmv_trap_ext_5, kmv_trap_ext_6, kmv_trap_ext_7,
    kmv_trap_ext_8, kmv_trap_ext_9, kmv_trap_ext_10, kmv_trap_ext_11, kmv_trap_ext_12, kmv_trap_ext_13, kmv_trap_ext_14, kmv_trap_ext_15,
};
HLE_FORCE("_ZN5movie9ExtractorC1ENS_13ContainerTypeE", "过场：Extractor 装宿主虚表（0 条轨道）",
          void, kmv_Extractor, void **self, int type) { (void)type; self[0] = (void *)g_ext_vt; self[1] = NULL; }

TRAP(dec, 2, "Configure") TRAP(dec, 3, "RegisterClientEvents") TRAP(dec, 4, "GetOutputFormat") TRAP(dec, 5, "Start")
TRAP(dec, 6, "Stop") TRAP(dec, 7, "Reset") TRAP(dec, 8, "Flush") TRAP(dec, 9, "AcquireInputBufferIndex") TRAP(dec, 10, "GetInputBuffer")
TRAP(dec, 11, "SendInputBufferForDecode") TRAP(dec, 12, "AcquireOutputBufferIndex") TRAP(dec, 13, "GetOutputBuffer")
TRAP(dec, 14, "GetOutputBufferSize") TRAP(dec, 15, "ReleaseOutputBufferIndex") TRAP(dec, 16, "GetLastError")
TRAP(dec, 17, "GetOutputBuffer(MediaData)") TRAP(dec, 18, "GetOutputBufferFormat")
static void *const g_dec_vt[19] = {
    kmv_dtor, kmv_delete, kmv_trap_dec_2, kmv_trap_dec_3, kmv_trap_dec_4, kmv_trap_dec_5, kmv_trap_dec_6, kmv_trap_dec_7,
    kmv_trap_dec_8, kmv_trap_dec_9, kmv_trap_dec_10, kmv_trap_dec_11, kmv_trap_dec_12, kmv_trap_dec_13, kmv_trap_dec_14,
    kmv_trap_dec_15, kmv_trap_dec_16, kmv_trap_dec_17, kmv_trap_dec_18,
};
HLE_FORCE("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", "过场：Decoder 装宿主虚表（0 轨时不该被建）",
          void, kmv_Decoder, void **self, int type, int fmt, int mode, int prio) {
    LOG("movie(卡比)：建 Decoder 类型 %d 输出 %d 模式 %d 优先级 %d（0 轨策略下不该走到这里，之后的调用会报名中止）", type, fmt, mode, prio);
    self[0] = (void *)g_dec_vt; self[1] = NULL;
}

// ---- MediaData：键值表。宿主布局 = 首字指向宿主侧的表（游戏在栈上给的空间 ≥ 8 字节）----
// ---- MediaData: key/value table. Host layout = the first word points to the host-side table (the game provides ≥ 8 bytes on its stack) ----
typedef struct { int n; struct { char key[48]; int kind; int64_t i; const char *s; } e[32]; } kmd;
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
typedef struct { void *base; uint64_t cap; int32_t off, size; int64_t i64; int32_t i32; uint32_t u32; uint32_t owned, _r; } kbuf;
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
