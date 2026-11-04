#!/bin/bash
# libvpx 软解（host/kirby_vp9_vpx.c，iOS 过场用）的回归测试：不用游戏文件，ffmpeg 现做几段 VP9 + Vorbis 的 WebM，
# 用 host/kirby_webm.c + kirby_vp9_vpx.c 解成 NV12，和 ffmpeg 自己解出的 NV12 逐字节比。Linux（apt install libvpx-dev ffmpeg）或 Mac（brew install libvpx ffmpeg）上跑。
#   tools/test_vp9_vpx.sh
# Regression test for libvpx software decoding (host/kirby_vp9_vpx.c, used for iOS cutscenes): no game files; ffmpeg makes a few VP9 + Vorbis WebM clips,
# host/kirby_webm.c + kirby_vp9_vpx.c decode them to NV12, compared byte for byte with ffmpeg's own NV12. Runs on Linux (apt install libvpx-dev ffmpeg) or a Mac (brew install libvpx ffmpeg).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
command -v ffmpeg > /dev/null || { echo "🔴 要 ffmpeg"; exit 2; }
CF=$(pkg-config --cflags vpx 2> /dev/null); LF=$(pkg-config --libs vpx 2> /dev/null || echo -lvpx)
cc -O2 -Wall -DKIRBY_VPX -I"$HERE/vp9_check" -I"$REPO/host" $CF -o "$T/vp9_check" "$HERE/vp9_check/main.c" "$REPO/host/kirby_webm.c" "$REPO/host/kirby_vp9_vpx.c" $LF -lpthread || { echo "🔴 编不过"; exit 1; }
fail=0
# 尺寸：教程视频 480x270、奇数宽高、过场 1920x1080（短）；带 alt-ref（隐藏帧、超帧）和 Vorbis 音轨，像游戏的 mov/*.webm
# sizes: tutorial 480x270, odd width / height, cutscene 1920x1080 (short); with alt-ref (hidden frames, superframes) and a Vorbis track, like the game's mov/*.webm
for spec in 480x270:60 481x271:30 1920x1080:20; do
  size=${spec%%:*}; n=${spec##*:}
  # testsrc2 / crop 遇到奇数尺寸会取偶，所以先出 640x360 再缩放到确切尺寸 | testsrc2 / crop round odd sizes to even, so render 640x360 and scale to the exact size
  ffmpeg -v error -f lavfi -i "testsrc2=size=640x360:rate=30,scale=${size/x/:}" -f lavfi -i "sine=frequency=440:sample_rate=48000" -frames:v "$n" -t 2 \
    -c:v libvpx-vp9 -b:v 1M -auto-alt-ref 1 -lag-in-frames 16 -g 30 -c:a libvorbis -shortest "$T/in.webm" -y || { echo "🔴 ffmpeg 做不出 $size"; fail=1; continue; }
  out=$("$T/vp9_check" "$T/in.webm" "$T/ours.nv12" 2> "$T/log") || { echo "🔴 $size：解码失败"; cat "$T/log"; fail=1; continue; }
  ffmpeg -v error -c:v libvpx-vp9 -i "$T/in.webm" -an -f rawvideo -pix_fmt nv12 "$T/ref.nv12" -y
  if cmp -s "$T/ours.nv12" "$T/ref.nv12"; then echo "✓ $size：$out，和 ffmpeg 逐字节相同"
  else echo "🔴 $size：$out，和 ffmpeg 不同（$(stat -c %s "$T/ours.nv12" 2> /dev/null || stat -f %z "$T/ours.nv12") vs $(stat -c %s "$T/ref.nv12" 2> /dev/null || stat -f %z "$T/ref.nv12") 字节）"; fail=1; fi
done
exit $fail
