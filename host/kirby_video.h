// 卡比过场 / 教程视频：WebM 解封装（kirby_webm.c）+ VP9 解码（Mac：kirby_vp9.c，VideoToolbox 硬解；iOS：kirby_vp9_vpx.c，libvpx 软解），由 hle_movie_kirby.c 接到游戏的 movie:: 播放器上。
// Kirby cutscene / tutorial video: WebM demux (kirby_webm.c) + VP9 decode (Mac: kirby_vp9.c, VideoToolbox hardware; iOS: kirby_vp9_vpx.c, libvpx software), wired to the game's movie:: player by hle_movie_kirby.c.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { KW_VIDEO = 1, KW_AUDIO = 2 };
typedef struct {
    int number, type;              // Matroska 轨道号；KW_VIDEO / KW_AUDIO | Matroska track number; KW_VIDEO / KW_AUDIO
    char codec[32];                // 如 V_VP9、A_VORBIS | e.g. V_VP9, A_VORBIS
    int width, height;             // 视频 | video
    int channels; double rate;     // 音频 | audio
    const uint8_t *priv; size_t priv_len;   // CodecPrivate（Vorbis 的三个头包）| CodecPrivate (Vorbis's three header packets)
} kw_track;
typedef struct { uint8_t track, key; int64_t pts_us; uint64_t off; uint32_t size; } kw_sample;   // track = tracks[] 下标 | track = index into tracks[]
typedef struct {
    const uint8_t *data; size_t len;   // 整个文件（mmap）| the whole file (mmap)
    int ntracks; kw_track tracks[4];
    int64_t duration_us;
    kw_sample *s; size_t ns;           // 按文件顺序 | in file order
} kw_file;

kw_file *kw_open(const char *host_path);   // 失败返回 NULL（原因写日志）| NULL on failure (reason logged)
void kw_close(kw_file *f);
int kw_find(const kw_file *f, int type);    // 第一条该类型轨道的下标，没有返回 -1 | index of the first track of that type, -1 if none

typedef struct kv_dec kv_dec;
int kv_available(void);                                   // 本机能不能解 VP9（VideoToolbox 能解，或带了 libvpx） | whether VP9 can be decoded here (VideoToolbox can, or libvpx is built in)
kv_dec *kv_open(int width, int height);                   // 失败返回 NULL | NULL on failure
// 解一个样本（可以是超帧）。出了一帧就把紧排 NV12（Y 行距 = 宽，UV 紧跟其后）写进 out 并返回 1；隐藏帧等不出画面的返回 0；出错返回 -1
// Decode one sample (may be a superframe). If a frame comes out, write packed NV12 (Y stride = width, UV right after) into out and return 1; 0 for hidden frames that show nothing; -1 on error
int kv_decode(kv_dec *d, const uint8_t *data, size_t len, uint8_t *out);
void kv_close(kv_dec *d);

// Vorbis 音轨（kirby_vorbis.c，stb_vorbis）| Vorbis audio track (kirby_vorbis.c, stb_vorbis)
typedef struct kvb_dec kvb_dec;
kvb_dec *kvb_open(const uint8_t *priv, size_t len, int *channels, int *rate);   // CodecPrivate（三个头包）；失败返回 NULL | CodecPrivate (three header packets); NULL on failure
int kvb_decode(kvb_dec *d, const uint8_t *pkt, size_t len, int16_t *out, int max_frames);   // 出 16 位交错 PCM，返回帧数（首包为 0 正常）| 16-bit interleaved PCM, returns frames (0 for the first packet is normal)
void kvb_reset(kvb_dec *d);   // 跳转后清解码状态 | clear decoder state after a seek
void kvb_close(kvb_dec *d);
