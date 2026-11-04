#!/bin/bash
# 给 iOS 版编 libvpx（只要 VP9 解码器，静态库），过场动画软解用（host/kirby_vp9_vpx.c）。ios/run.sh build 自动调，编好就缓存，版本没变不重编。
#   ios/tools/build_libvpx.sh <输出目录>      产出 <输出目录>/include/vpx/*.h、<输出目录>/lib/libvpx.a（约 2 分钟）
# 源码固定在 libvpx v1.16.0（BSD-3，https://chromium.googlesource.com/webm/libvpx ，GitHub 镜像 webmproject/libvpx），按提交号核对。要 Xcode、perl、git。
# Build libvpx for the iOS App (VP9 decoder only, static library), for software cutscene decoding (host/kirby_vp9_vpx.c). ios/run.sh build calls it and caches the result; no rebuild while the version is unchanged.
#   ios/tools/build_libvpx.sh <out dir>      produces <out dir>/include/vpx/*.h and <out dir>/lib/libvpx.a (about 2 minutes)
# Source pinned to libvpx v1.16.0 (BSD-3), checked by commit id. Needs Xcode, perl, git.
set -u
TAG=v1.16.0; COMMIT=1024874c5919305883187e2953de8fcb4c3d7fa6
URL=${KIRBY_LIBVPX_URL:-https://github.com/webmproject/libvpx.git}
[ $# -eq 1 ] || { sed -n '2,4p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
[ -f "$OUT/lib/libvpx.a" ] && [ "$(cat "$OUT/.commit" 2> /dev/null)" = "$COMMIT" ] && exit 0
die() { echo "🔴 libvpx：$*" >&2; exit 1; }
SRC=$OUT.src BLD=$OUT.build
if [ "$(git -C "$SRC" rev-parse HEAD 2> /dev/null)" != "$COMMIT" ]; then
  rm -rf "$SRC"; git clone -q --depth 1 -b "$TAG" "$URL" "$SRC" || die "克隆 $URL 没成（网络？）"
  [ "$(git -C "$SRC" rev-parse HEAD)" = "$COMMIT" ] || die "$TAG 的提交号不是 $COMMIT，不用"
fi
rm -rf "$BLD" "$OUT"; mkdir -p "$BLD" || die "建不了 $BLD"
cd "$BLD" || die "进不了 $BLD"
# arm64-darwin-gcc = iOS 真机（libvpx 的叫法，实际用 Xcode 的 clang）；最低系统跟 App（ios/project.yml）一样 26.0
# arm64-darwin-gcc = iOS device (libvpx's name; it uses Xcode's clang); minimum OS 26.0 like the App (ios/project.yml)
"$SRC/configure" --target=arm64-darwin-gcc --prefix="$OUT" \
  --disable-docs --disable-examples --disable-tools --disable-unit-tests --disable-libyuv --disable-webm-io \
  --disable-vp8 --disable-vp9-encoder --enable-vp9-decoder --enable-multithread --enable-pic --enable-static --disable-shared \
  --extra-cflags="-miphoneos-version-min=26.0 -O3" > configure.log 2>&1 || { tail -20 configure.log; die "configure 没过（日志 $BLD/configure.log）"; }
make -j"$(sysctl -n hw.ncpu 2> /dev/null || echo 4)" > make.log 2>&1 || { tail -20 make.log; die "编不过（日志 $BLD/make.log）"; }
make install > install.log 2>&1 || die "装不进 $OUT（日志 $BLD/install.log）"
[ -f "$OUT/lib/libvpx.a" ] && [ -f "$OUT/include/vpx/vp8dx.h" ] || die "装完没找到 libvpx.a / vp8dx.h"
echo "$COMMIT" > "$OUT/.commit"
echo "libvpx $TAG（iOS arm64）→ $OUT"
