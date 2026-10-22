// WebM（Matroska）解封装：只做卡比的 mov/*.webm 用得到的部分——轨道信息、时长、所有帧按文件顺序的索引（SimpleBlock / BlockGroup，三种 lacing）。
// 不读 Cues：文件整个 mmap 进来，开头扫一遍建索引（最大的 Opening.webm 约 100 MB、两千多帧，扫描是毫秒级）。
// WebM (Matroska) demux: only what Kirby's mov/*.webm need -- track info, duration, and an index of every frame in file order (SimpleBlock / BlockGroup, all three lacings).
// Cues are not read: the whole file is mmapped and scanned once up front to build the index (the largest, Opening.webm, is ~100 MB and a few thousand frames; the scan takes milliseconds).
#include "dm.h"
#include "kirby_video.h"
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    ID_SEGMENT = 0x18538067, ID_INFO = 0x1549A966, ID_SCALE = 0x2AD7B1, ID_DURATION = 0x4489,
    ID_TRACKS = 0x1654AE6B, ID_ENTRY = 0xAE, ID_NUMBER = 0xD7, ID_TYPE = 0x83, ID_CODEC = 0x86, ID_PRIV = 0x63A2,
    ID_VIDEO = 0xE0, ID_W = 0xB0, ID_H = 0xBA, ID_AUDIO = 0xE1, ID_RATE = 0xB5, ID_CH = 0x9F,
    ID_CLUSTER = 0x1F43B675, ID_TIMECODE = 0xE7, ID_SIMPLEBLOCK = 0xA3, ID_GROUP = 0xA0, ID_BLOCK = 0xA1, ID_CUES = 0x1C53BB6B,
};
#define UNKNOWN_SIZE UINT64_MAX

// EBML 变长整数。keep = 1 保留长度标记位（元素 ID 的写法）| EBML variable-length integer. keep = 1 keeps the length marker (how element IDs are written)
static int vint(const uint8_t *p, const uint8_t *end, uint64_t *v, int keep) {
    if (p >= end || !*p) return 0;
    int n = __builtin_clz(*p) - 23; if (p + n > end) return 0;   // clz 按 32 位算，前 24 位为 0 | clz counts 32 bits, the top 24 are zero
    uint64_t x = keep ? *p : *p & (0xff >> n), all = x == (uint64_t)(0xff >> n);
    for (int i = 1; i < n; i++) { x = x << 8 | p[i]; all &= p[i] == 0xff; }
    *v = !keep && all ? UNKNOWN_SIZE : x; return n;
}
static uint64_t uint_be(const uint8_t *p, uint64_t n) { uint64_t v = 0; for (uint64_t i = 0; i < n && i < 8; i++) v = v << 8 | p[i]; return v; }
static double float_be(const uint8_t *p, uint64_t n) {
    if (n == 4) { uint32_t u = (uint32_t)uint_be(p, 4); float f; memcpy(&f, &u, 4); return f; }
    if (n == 8) { uint64_t u = uint_be(p, 8); double d; memcpy(&d, &u, 8); return d; }
    return 0;
}
typedef struct { uint64_t id, size; const uint8_t *body, *end; } elem;
static int next_elem(const uint8_t **pp, const uint8_t *end, elem *e) {
    const uint8_t *p = *pp; int a, b;
    if (!(a = vint(p, end, &e->id, 1)) || !(b = vint(p + a, end, &e->size, 0))) return 0;
    e->body = p + a + b;
    e->end = e->size == UNKNOWN_SIZE || e->size > (uint64_t)(end - e->body) ? end : e->body + e->size;
    *pp = e->end; return 1;
}

static int push(kw_file *f, size_t *cap, kw_sample s) {
    if (f->ns == *cap) { *cap = *cap ? *cap * 2 : 1024; kw_sample *n = realloc(f->s, *cap * sizeof *n); if (!n) return 0; f->s = n; }
    f->s[f->ns++] = s; return 1;
}
static int track_of(const kw_file *f, uint64_t num) { for (int i = 0; i < f->ntracks; i++) if ((uint64_t)f->tracks[i].number == num) return i; return -1; }

// 一个 Block / SimpleBlock：轨道号、相对时间码、标志，然后按 lacing 切出一或多帧 | one Block / SimpleBlock: track number, relative timecode, flags, then one or more frames per the lacing
static int block(kw_file *f, size_t *cap, const uint8_t *p, const uint8_t *end, int64_t cluster_tc, uint64_t scale, int simple) {
    uint64_t num; int n = vint(p, end, &num, 0); if (!n || p + n + 3 > end) return 0;
    int t = track_of(f, num); p += n;
    int16_t rel = (int16_t)(p[0] << 8 | p[1]); uint8_t flags = p[2]; p += 3;
    if (t < 0) return 1;
    int64_t pts_us = (int64_t)((cluster_tc + rel) * (double)scale / 1000.0);
    int key = simple ? !!(flags & 0x80) : 1, lacing = (flags >> 1) & 3;
    if (!lacing) return push(f, cap, (kw_sample){(uint8_t)t, (uint8_t)key, pts_us, (uint64_t)(p - f->data), (uint32_t)(end - p)});
    if (p >= end) return 0;
    int count = *p++ + 1; uint64_t sizes[256]; uint64_t sum = 0;
    if (lacing == 2) { uint64_t each = (uint64_t)(end - p) / count; for (int i = 0; i < count; i++) sizes[i] = each; }
    else if (lacing == 1) {   // Xiph：每帧长度是一串 255 加一个小于 255 的数 | Xiph: each size is a run of 255s plus one byte < 255
        for (int i = 0; i < count - 1; i++) { uint64_t s = 0; while (p < end && *p == 255) { s += 255; p++; } if (p >= end) return 0; s += *p++; sizes[i] = s; sum += s; }
        sizes[count - 1] = (uint64_t)(end - p) - sum;
    } else {                  // EBML：第一帧绝对值，之后是有符号差值 | EBML: the first size absolute, then signed deltas
        uint64_t v; int k = vint(p, end, &v, 0); if (!k) return 0; p += k; sizes[0] = v; sum = v;
        for (int i = 1; i < count - 1; i++) { k = vint(p, end, &v, 0); if (!k) return 0; p += k;
            int64_t d = (int64_t)v - ((1ll << (7 * k - 1)) - 1); sizes[i] = (uint64_t)((int64_t)sizes[i - 1] + d); sum += sizes[i]; }
        sizes[count - 1] = (uint64_t)(end - p) - sum;
    }
    for (int i = 0; i < count; i++) {
        if (sizes[i] > (uint64_t)(end - p)) return 0;
        if (!push(f, cap, (kw_sample){(uint8_t)t, (uint8_t)key, pts_us, (uint64_t)(p - f->data), (uint32_t)sizes[i]})) return 0;
        p += sizes[i];
    }
    return 1;
}
static void parse_track(kw_file *f, const uint8_t *p, const uint8_t *end) {
    if (f->ntracks == (int)(sizeof f->tracks / sizeof f->tracks[0])) return;
    kw_track *t = &f->tracks[f->ntracks]; memset(t, 0, sizeof *t); elem e;
    while (next_elem(&p, end, &e)) {
        switch (e.id) {
        case ID_NUMBER: t->number = (int)uint_be(e.body, e.size); break;
        case ID_TYPE: { uint64_t ty = uint_be(e.body, e.size); t->type = ty == 1 ? KW_VIDEO : ty == 2 ? KW_AUDIO : 0; break; }
        case ID_CODEC: { size_t n = e.size < sizeof t->codec - 1 ? e.size : sizeof t->codec - 1; memcpy(t->codec, e.body, n); t->codec[n] = 0; break; }
        case ID_PRIV: t->priv = e.body; t->priv_len = e.size; break;
        case ID_VIDEO: { const uint8_t *q = e.body; elem v; while (next_elem(&q, e.end, &v)) {
            if (v.id == ID_W) t->width = (int)uint_be(v.body, v.size); else if (v.id == ID_H) t->height = (int)uint_be(v.body, v.size); } break; }
        case ID_AUDIO: { const uint8_t *q = e.body; elem a; while (next_elem(&q, e.end, &a)) {
            if (a.id == ID_RATE) t->rate = float_be(a.body, a.size); else if (a.id == ID_CH) t->channels = (int)uint_be(a.body, a.size); } break; }
        }
    }
    if (t->type) f->ntracks++;
}

kw_file *kw_open(const char *path) {
    int fd = open(path, O_RDONLY); if (fd < 0) { LOG("webm：打不开 %s", path); return NULL; }
    struct stat st; if (fstat(fd, &st) || st.st_size < 16) { close(fd); LOG("webm：%s 太短", path); return NULL; }
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    if (m == MAP_FAILED) { LOG("webm：mmap %s 失败", path); return NULL; }
    kw_file *f = calloc(1, sizeof *f); f->data = m; f->len = (size_t)st.st_size;
    const uint8_t *p = f->data, *end = p + f->len; elem e; uint64_t scale = 1000000; double dur = 0; size_t cap = 0; int ok = 1;
    while (ok && next_elem(&p, end, &e)) {
        if (e.id != ID_SEGMENT) continue;
        const uint8_t *q = e.body; elem s;
        while (ok && next_elem(&q, e.end, &s)) {
            if (s.id == ID_INFO) { const uint8_t *r = s.body; elem i; while (next_elem(&r, s.end, &i)) {
                if (i.id == ID_SCALE) scale = uint_be(i.body, i.size); else if (i.id == ID_DURATION) dur = float_be(i.body, i.size); } }
            else if (s.id == ID_TRACKS) { const uint8_t *r = s.body; elem t; while (next_elem(&r, s.end, &t)) if (t.id == ID_ENTRY) parse_track(f, t.body, t.end); }
            else if (s.id == ID_CLUSTER) {
                // 大小未知的 Cluster 一直延伸到下一个一级元素 | an unknown-size Cluster runs until the next level-1 element
                const uint8_t *r = s.body, *cend = s.end; elem c; int64_t tc = 0;
                while (r < cend) {
                    const uint8_t *at = r;
                    if (!next_elem(&r, cend, &c)) break;
                    if (c.id == ID_CLUSTER || c.id == ID_CUES || c.id == ID_TRACKS || c.id == ID_INFO) { q = at; break; }
                    if (c.id == ID_TIMECODE) tc = (int64_t)uint_be(c.body, c.size);
                    else if (c.id == ID_SIMPLEBLOCK) ok = block(f, &cap, c.body, c.end, tc, scale, 1);
                    else if (c.id == ID_GROUP) { const uint8_t *g = c.body; elem b; while (next_elem(&g, c.end, &b)) if (b.id == ID_BLOCK) ok = ok && block(f, &cap, b.body, b.end, tc, scale, 0); }
                    if (!ok) break;
                }
                if (s.size == UNKNOWN_SIZE && r >= cend) q = cend;
            }
        }
    }
    f->duration_us = (int64_t)(dur * (double)scale / 1000.0);
    if (!ok || !f->ntracks || !f->ns) { LOG("webm：%s 解析失败（轨道 %d、帧 %zu）", path, f->ntracks, f->ns); kw_close(f); return NULL; }
    return f;
}
void kw_close(kw_file *f) { if (!f) return; if (f->data) munmap((void *)f->data, f->len); free(f->s); free(f); }
int kw_find(const kw_file *f, int type) { for (int i = 0; i < f->ntracks; i++) if (f->tracks[i].type == type) return i; return -1; }
