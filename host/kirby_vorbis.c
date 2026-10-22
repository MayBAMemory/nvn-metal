// 过场的 Vorbis 音轨解码：stb_vorbis（third_party/stb_vorbis.c，v1.22，nothings/stb a7d4c21，公有领域 / MIT）的 pushdata 接口。
// WebM 里的 Vorbis 是裸包（CodecPrivate = Xiph lacing 的三个头包），stb_vorbis 只吃 Ogg 页，所以每个包各包成一页（带正确的 CRC）再喂。
// 输出 16 位交错 PCM：游戏的音频线程按「采样率 × 声道 × 16 位」算缓冲（main+0x770164 起）。
// Cutscene Vorbis audio track decoding: the pushdata API of stb_vorbis (third_party/stb_vorbis.c, v1.22, nothings/stb a7d4c21, public domain / MIT).
// Vorbis in WebM is raw packets (CodecPrivate = the three header packets in Xiph lacing), while stb_vorbis only takes Ogg pages, so each packet is wrapped in its own page (with a correct CRC) before feeding.
// Output is 16-bit interleaved PCM: the game's audio thread sizes its buffers as "sample rate × channels × 16 bits" (from main+0x770164).
#include "dm.h"
#include "kirby_video.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#include "third_party/stb_vorbis.c"
#pragma clang diagnostic pop

struct kvb_dec { stb_vorbis *v; int ch, rate; uint32_t seq; uint8_t *page; size_t cap; };

static uint32_t g_crc[256];
static uint32_t ogg_crc(const uint8_t *p, size_t n) {
    if (!g_crc[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t r = i << 24; for (int k = 0; k < 8; k++) r = r & 0x80000000u ? r << 1 ^ 0x04c11db7u : r << 1; g_crc[i] = r; }
    uint32_t c = 0; for (size_t i = 0; i < n; i++) c = c << 8 ^ g_crc[(c >> 24 ^ p[i]) & 0xff]; return c;
}
// 一个包包成一页 Ogg，返回页长 | wrap one packet into one Ogg page; returns the page length
static size_t page(kvb_dec *d, const uint8_t *pkt, size_t len, int bos) {
    size_t nseg = len / 255 + 1; if (nseg > 255) return 0;
    size_t need = 27 + nseg + len;
    if (need > d->cap) { uint8_t *n = realloc(d->page, need); if (!n) return 0; d->page = n; d->cap = need; }
    uint8_t *p = d->page; memcpy(p, "OggS", 4); p[4] = 0; p[5] = bos ? 2 : 0;
    uint64_t gran = (uint64_t)d->seq * 1024; for (int i = 0; i < 8; i++) p[6 + i] = (uint8_t)(gran >> 8 * i);
    uint32_t serial = 0x4b495242; for (int i = 0; i < 4; i++) p[14 + i] = (uint8_t)(serial >> 8 * i);
    for (int i = 0; i < 4; i++) p[18 + i] = (uint8_t)(d->seq >> 8 * i);
    memset(p + 22, 0, 4); p[26] = (uint8_t)nseg;
    for (size_t i = 0; i < nseg; i++) p[27 + i] = i + 1 < nseg ? 255 : (uint8_t)(len % 255);
    memcpy(p + 27 + nseg, pkt, len);
    uint32_t c = ogg_crc(p, need); for (int i = 0; i < 4; i++) p[22 + i] = (uint8_t)(c >> 8 * i);
    d->seq++; return need;
}

kvb_dec *kvb_open(const uint8_t *priv, size_t len, int *ch, int *rate) {
    // CodecPrivate：首字节 = 包数 − 1（应为 2），然后两个 Xiph 长度，第三个包占剩下的 | first byte = packets − 1 (should be 2), then two Xiph lengths, the third packet takes the rest
    if (!priv || len < 3 || priv[0] != 2) { LOG("vorbis：CodecPrivate 不是三个头包"); return NULL; }
    size_t i = 1, sz[3] = {0};
    for (int k = 0; k < 2; k++) { while (i < len && priv[i] == 255) { sz[k] += 255; i++; } if (i >= len) return NULL; sz[k] += priv[i++]; }
    if (i + sz[0] + sz[1] > len) return NULL;
    sz[2] = len - i - sz[0] - sz[1];
    kvb_dec *d = calloc(1, sizeof *d);
    uint8_t *hdr = NULL; size_t hl = 0; const uint8_t *p = priv + i;
    for (int k = 0; k < 3; k++) {
        size_t n = page(d, p, sz[k], k == 0); if (!n) { free(hdr); free(d->page); free(d); return NULL; }
        hdr = realloc(hdr, hl + n); memcpy(hdr + hl, d->page, n); hl += n; p += sz[k];
    }
    int used = 0, err = 0;
    d->v = stb_vorbis_open_pushdata(hdr, (int)hl, &used, &err, NULL); free(hdr);
    if (!d->v) { LOG("vorbis：头包解析失败（错误 %d）", err); free(d->page); free(d); return NULL; }
    stb_vorbis_info info = stb_vorbis_get_info(d->v); d->ch = info.channels; d->rate = (int)info.sample_rate;
    *ch = d->ch; *rate = d->rate; return d;
}

int kvb_decode(kvb_dec *d, const uint8_t *pkt, size_t len, int16_t *out, int max_frames) {
    size_t n = page(d, pkt, len, 0); if (!n) return -1;
    const uint8_t *p = d->page; int left = (int)n, frames = 0;
    while (left > 0) {
        int ch = 0, got = 0; float **pcm = NULL;
        int used = stb_vorbis_decode_frame_pushdata(d->v, p, left, &ch, &pcm, &got);
        if (used <= 0 && !got) break;
        p += used; left -= used;
        for (int s = 0; s < got && frames < max_frames; s++, frames++)
            for (int c = 0; c < d->ch; c++) { float x = pcm[c < ch ? c : 0][s] * 32767.f; out[frames * d->ch + c] = (int16_t)(x > 32767.f ? 32767 : x < -32768.f ? -32768 : x); }
    }
    return frames;
}
void kvb_reset(kvb_dec *d) { stb_vorbis_flush_pushdata(d->v); }
void kvb_close(kvb_dec *d) { if (!d) return; stb_vorbis_close(d->v); free(d->page); free(d); }
