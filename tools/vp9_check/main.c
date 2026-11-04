// 用 host/kirby_webm.c 解封装、host/kirby_vp9_vpx.c 解码一个 .webm，把每个出画面的帧（紧排 NV12）顺序写到 <输出>；对照用 ffmpeg 解出的 NV12（tools/test_vp9_vpx.sh）
// Demux a .webm with host/kirby_webm.c and decode it with host/kirby_vp9_vpx.c, writing every shown frame (packed NV12) to <out> in order; compared against ffmpeg's NV12 (tools/test_vp9_vpx.sh)
#include "dm.h"
#include "kirby_video.h"
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: vp9_check <in.webm> <out.nv12>\n"); return 2; }
    kw_file *f = kw_open(argv[1]); if (!f) return 1;
    int vt = kw_find(f, KW_VIDEO); if (vt < 0) { fprintf(stderr, "no video track\n"); return 1; }
    const kw_track *t = &f->tracks[vt];
    if (strcmp(t->codec, "V_VP9")) { fprintf(stderr, "codec %s\n", t->codec); return 1; }
    kv_dec *d = kv_open(t->width, t->height); if (!d) return 1;
    size_t fsz = (size_t)t->width * t->height + (size_t)((t->width + 1) / 2 * 2) * ((t->height + 1) / 2);
    uint8_t *buf = malloc(fsz); FILE *o = fopen(argv[2], "wb"); if (!buf || !o) return 1;
    int shown = 0, hidden = 0, errs = 0;
    for (size_t i = 0; i < f->ns; i++) {
        if (f->s[i].track != vt) continue;
        int r = kv_decode(d, f->data + f->s[i].off, f->s[i].size, buf);
        if (r > 0) { fwrite(buf, 1, fsz, o); shown++; } else if (r == 0) hidden++; else errs++;
    }
    fclose(o); kv_close(d);
    printf("%dx%d tracks=%d samples=%zu shown=%d hidden=%d errors=%d duration=%.3fs\n", t->width, t->height, f->ntracks, f->ns, shown, hidden, errs, f->duration_us / 1e6);
    kw_close(f);
    return errs ? 1 : 0;
}
