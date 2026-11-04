// VP9 软解：libvpx（BSD），给 iOS 用——iOS 没有 VideoToolbox 的 VP9 入口（kirby_vp9.c）。接口和 kirby_vp9.c 完全相同（kirby_video.h），出紧排 NV12。
// 只在定义了 KIRBY_VPX 时编（ios/run.sh build 先用 ios/tools/build_libvpx.sh 编出 libvpx.a 再定义它）；这时 kirby_vp9.c 不编。
// 多线程解码（最多 4 个线程），每个样本同步解完：libvpx 自己处理超帧，隐藏帧不出画面就返回 0，和 VideoToolbox 那边一样。
// VP9 software decode: libvpx (BSD), for iOS -- iOS has no VideoToolbox VP9 entry point (kirby_vp9.c). Same interface as kirby_vp9.c (kirby_video.h), packed NV12 out.
// Built only when KIRBY_VPX is defined (ios/run.sh build first builds libvpx.a with ios/tools/build_libvpx.sh, then defines it); kirby_vp9.c is not built then.
// Multi-threaded decoding (up to 4 threads), each sample decoded synchronously: libvpx handles superframes itself, and hidden frames that show nothing return 0, as on the VideoToolbox side.
#ifdef KIRBY_VPX
#include "dm.h"
#include "kirby_video.h"
#include <unistd.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

struct kv_dec { vpx_codec_ctx_t c; int w, h; };

int kv_available(void) { return 1; }

kv_dec *kv_open(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    kv_dec *d = calloc(1, sizeof *d); if (!d) return NULL;
    d->w = w; d->h = h;
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    vpx_codec_dec_cfg_t cfg = {.threads = n > 4 ? 4 : n > 1 ? (unsigned)n : 1, .w = (unsigned)w, .h = (unsigned)h};
    vpx_codec_err_t e = vpx_codec_dec_init(&d->c, vpx_codec_vp9_dx(), &cfg, 0);
    if (e != VPX_CODEC_OK) { LOG("vp9（libvpx）：初始化失败 %s", vpx_codec_err_to_string(e)); free(d); return NULL; }
    LOG("vp9（libvpx %s）：%dx%d，%u 线程", vpx_codec_version_str(), w, h, cfg.threads);
    return d;
}

// I420（libvpx 输出）→ 紧排 NV12：Y 逐行拷；U、V 交错成一个平面，行宽 = 宽取偶 | I420 (libvpx output) -> packed NV12: Y row by row; U and V interleaved into one plane, row width = width rounded up to even
static int to_nv12(const kv_dec *d, const vpx_image_t *img, uint8_t *out) {
    if (img->fmt != VPX_IMG_FMT_I420 || (int)img->d_w < d->w || (int)img->d_h < d->h) {
        static int n; if (n++ < 4) LOG("【红】vp9（libvpx）：出帧格式 %#x %ux%u，要 I420 %dx%d", img->fmt, img->d_w, img->d_h, d->w, d->h);
        return -1;
    }
    int w = d->w, h = d->h, cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int r = 0; r < h; r++) memcpy(out + (size_t)r * w, img->planes[VPX_PLANE_Y] + (size_t)r * img->stride[VPX_PLANE_Y], (size_t)w);
    uint8_t *o = out + (size_t)w * h;
    for (int r = 0; r < ch; r++) {
        const uint8_t *u = img->planes[VPX_PLANE_U] + (size_t)r * img->stride[VPX_PLANE_U], *v = img->planes[VPX_PLANE_V] + (size_t)r * img->stride[VPX_PLANE_V];
        uint8_t *row = o + (size_t)r * cw * 2;
        for (int x = 0; x < cw; x++) { row[2 * x] = u[x]; row[2 * x + 1] = v[x]; }
    }
    return 1;
}

int kv_decode(kv_dec *d, const uint8_t *data, size_t len, uint8_t *out) {
    vpx_codec_err_t e = vpx_codec_decode(&d->c, data, (unsigned)len, NULL, 0);
    if (e != VPX_CODEC_OK) {
        static int n; if (n++ < 8) LOG("vp9（libvpx）：解码失败 %s（%s）", vpx_codec_err_to_string(e), vpx_codec_error_detail(&d->c) ?: "");
        return -1;
    }
    vpx_codec_iter_t it = NULL; vpx_image_t *img, *last = NULL;
    while ((img = vpx_codec_get_frame(&d->c, &it))) last = img;   // 一个样本最多出一帧可见帧；保险起见取最后一帧 | a sample yields at most one visible frame; take the last to be safe
    return last ? to_nv12(d, last, out) : 0;
}

void kv_close(kv_dec *d) { if (!d) return; vpx_codec_destroy(&d->c); free(d); }
#endif
