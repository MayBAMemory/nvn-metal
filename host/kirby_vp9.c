// VP9 硬解：VideoToolbox 同步解码，出紧排 NV12（游戏的播放器按「Y 行距 = 宽、UV 紧跟其后」读，见 hle_movie_kirby.c）。
// macOS 11 起系统带 VP9 解码器，要先 VTRegisterSupplementalVideoDecoderIfAvailable；M5 上是硬解。iOS 上没有这个入口，kv_available() 返回 0，过场退回空播。
// VP9 hardware decode: synchronous VideoToolbox decoding into packed NV12 (the game's player reads "Y stride = width, UV right after", see hle_movie_kirby.c).
// macOS 11+ ships a VP9 decoder that must first be enabled with VTRegisterSupplementalVideoDecoderIfAvailable; on the M5 it is hardware. iOS has no such entry point, so kv_available() returns 0 and cutscenes fall back to no-op playback.
#include "dm.h"
#include "kirby_video.h"
#include <TargetConditionals.h>
#include <VideoToolbox/VideoToolbox.h>

struct kv_dec {
    VTDecompressionSessionRef s; CMVideoFormatDescriptionRef fmt;
    int w, h; uint8_t *out; int got;
};

int kv_available(void) {
#if TARGET_OS_OSX
    static int v = -1;
    if (v < 0) { VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_VP9); v = VTIsHardwareDecodeSupported(kCMVideoCodecType_VP9) ? 1 : 0; }
    return v;
#else
    return 0;
#endif
}

// 解码回调（同步解码时在 DecodeFrame 返回前调）：把两个平面逐行拷成紧排 NV12
// Decode callback (called before DecodeFrame returns when decoding synchronously): copy both planes row by row into packed NV12
static void on_frame(void *ref, void *src, OSStatus st, VTDecodeInfoFlags info, CVImageBufferRef img, CMTime pts, CMTime dur) {
    (void)src; (void)info; (void)pts; (void)dur;
    kv_dec *d = ref;
    if (st != noErr || !img) return;
    if (CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
    int w = d->w, h = d->h;
    const uint8_t *y = CVPixelBufferGetBaseAddressOfPlane(img, 0), *uv = CVPixelBufferGetBaseAddressOfPlane(img, 1);
    size_t ys = CVPixelBufferGetBytesPerRowOfPlane(img, 0), uvs = CVPixelBufferGetBytesPerRowOfPlane(img, 1);
    int ph = (int)CVPixelBufferGetHeightOfPlane(img, 0), cw = (w + 1) / 2 * 2, ch = (h + 1) / 2;
    if (y && uv && ph >= h) {
        for (int r = 0; r < h; r++) memcpy(d->out + (size_t)r * w, y + r * ys, (size_t)w);
        uint8_t *o = d->out + (size_t)w * h;
        for (int r = 0; r < ch; r++) memcpy(o + (size_t)r * cw, uv + r * uvs, (size_t)cw);
        d->got = 1;
    }
    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
}

kv_dec *kv_open(int w, int h) {
    if (!kv_available() || w <= 0 || h <= 0) return NULL;
    kv_dec *d = calloc(1, sizeof *d); d->w = w; d->h = h;
    // vpcC（VP 编解码配置记录）：版本 1、配置 0、级别 0、8 位、4:2:0、BT.709、有限范围 | vpcC (VP codec configuration record): version 1, profile 0, level 0, 8-bit, 4:2:0, BT.709, limited range
    const uint8_t vpcc[] = {1, 0, 0, 0, 0, 0, 8 << 4 | 1 << 1 | 0, 1, 1, 1, 0, 0};
    CFDataRef atom = CFDataCreate(NULL, vpcc, sizeof vpcc);
    const void *ak[] = {CFSTR("vpcC")}, *av[] = {atom};
    CFDictionaryRef atoms = CFDictionaryCreate(NULL, ak, av, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void *ek[] = {kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms}, *ev[] = {atoms};
    CFDictionaryRef ext = CFDictionaryCreate(NULL, ek, ev, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSStatus st = CMVideoFormatDescriptionCreate(NULL, kCMVideoCodecType_VP9, w, h, ext, &d->fmt);
    CFRelease(ext); CFRelease(atoms); CFRelease(atom);
    if (st) { LOG("vp9：建格式描述失败 %d", (int)st); free(d); return NULL; }
    int32_t pf = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef pfn = CFNumberCreate(NULL, kCFNumberSInt32Type, &pf);
    const void *bk[] = {kCVPixelBufferPixelFormatTypeKey}, *bv[] = {pfn};
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, bk, bv, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    VTDecompressionOutputCallbackRecord cb = {on_frame, d};
    st = VTDecompressionSessionCreate(NULL, d->fmt, NULL, attrs, &cb, &d->s);
    CFRelease(attrs); CFRelease(pfn);
    if (st) { LOG("vp9：建解码会话失败 %d", (int)st); CFRelease(d->fmt); free(d); return NULL; }
    return d;
}

int kv_decode(kv_dec *d, const uint8_t *data, size_t len, uint8_t *out) {
    CMBlockBufferRef bb = NULL; CMSampleBufferRef sb = NULL;
    OSStatus st = CMBlockBufferCreateWithMemoryBlock(NULL, (void *)data, len, kCFAllocatorNull, NULL, 0, len, 0, &bb);
    if (!st) { size_t sz = len; st = CMSampleBufferCreateReady(NULL, bb, d->fmt, 1, 0, NULL, 1, &sz, &sb); }
    if (st) { if (bb) CFRelease(bb); return -1; }
    d->out = out; d->got = 0;
    VTDecodeInfoFlags info = 0;
    st = VTDecompressionSessionDecodeFrame(d->s, sb, 0, NULL, &info);   // 不带异步标志 = 同步，回调在返回前调完 | no async flag = synchronous, the callback has run before it returns
    CFRelease(sb); CFRelease(bb);
    if (st) { static int n; if (n++ < 8) LOG("vp9：解码失败 %d", (int)st); return -1; }
    return d->got;
}

void kv_close(kv_dec *d) {
    if (!d) return;
    VTDecompressionSessionWaitForAsynchronousFrames(d->s); VTDecompressionSessionInvalidate(d->s);
    CFRelease(d->s); CFRelease(d->fmt); free(d);
}
