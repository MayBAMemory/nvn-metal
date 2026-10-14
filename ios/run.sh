#!/bin/bash
# 卡比 iOS App：出包、编着色器、装机、推 RomFS、启动并拉日志。参照 dm/ios/run.sh（D2R）。
# 用法（在任意目录）：
#   kirby/ios/run.sh shaders            Windows 出的着色器库（$KIRBY_SHLIB）→ iOS 版 metallib（$KIRBY_SHLIB_IOS）；只在着色器库更新后跑
#   kirby/ios/run.sh build              生成工程并构建（要 $KIRBY_PATCHED 和 $KIRBY_SHLIB_IOS）
#   KIRBY_NO_GAME=1 kirby/ios/run.sh build   不带游戏内容只编外壳和宿主（没有游戏文件时检查编译 / 链接 / 签名用；装上也跑不了）
#   kirby/ios/run.sh install            装到手机
#   kirby/ios/run.sh romfs [RomFS 目录]  把 RomFS 推进 App 的 Library/Application Support/romfs（缺省 $KIRBY_WORK/RomFS）
#   kirby/ios/run.sh run [上限秒]        启动并把 Documents/boot.txt 持续拉回到 $KIRBY_WORK/run/boot.txt（缺省 120 秒）
#   kirby/ios/run.sh log                只拉一次日志
# 本机配置：kirby/ios/local.env（不进仓，样例 local.env.example）。
# Kirby iOS App: build, compile shaders, install, push the RomFS, launch and pull the log. Modelled on dm/ios/run.sh (D2R). See the usage lines above.
set -u
cd "$(dirname "$0")"
IOS=$(pwd); REPO=$(cd ../.. && pwd)
[ -f local.env ] && { set -a; source local.env; set +a; }
W=${KIRBY_WORK:-$HOME/kirby-work}
PATCHED=${KIRBY_PATCHED:-$W/patched}; SHLIB=${KIRBY_SHLIB:-$W/shlib}; SHLIB_IOS=${KIRBY_SHLIB_IOS:-$W/shlib_ios}
R=$W/run; mkdir -p "$R"
BLD=${KIRBY_BUILD_DIR:-$W/build}
APP=$BLD/sym/Release-iphoneos/KirbyIOS.app
MODS="main sdk subsdk0"
t(){ echo "[$(date +%T)] $*"; }
need(){ local v miss=; for v in "$@"; do [ -n "${!v:-}" ] || miss="$miss $v"; done; [ -z "$miss" ] || { t "🔴 缺本机配置：${miss}（复制 kirby/ios/local.env.example 成 kirby/ios/local.env 再填）"; exit 1; }; }
D=${KIRBY_DEVICE:-}; B=${KIRBY_BUNDLE_ID:-}
V=$(sed -n 's/^#define DMSH_CHAIN "\(.*\)"/\1/p' "$REPO/dm/gfx/nvn/dmsh_chain.h")

case "${1:-}" in
shaders)
  python3 -I tools/shlib_ios.py "$SHLIB" "$SHLIB_IOS" || { t "🔴 有着色器没编过，见 $SHLIB_IOS/b-$V/failed.tsv（其余照常可用）"; exit 1; }
  t "着色器完成：$SHLIB_IOS/b-$V" ;;

build)
  rm -rf Gen; mkdir -p Gen/dm Gen/msl
  python3 -I tools/gen_build.py ../host Gen > Gen/kirby.xcconfig || { t "🔴 gen_build 失败"; exit 1; }
  if [ "${KIRBY_NO_GAME:-0}" = 1 ]; then
    t "⚠ KIRBY_NO_GAME：不带游戏模块和着色器，只检查外壳与宿主的编译 / 链接（这样的包装上也起不来）"
    echo '// KIRBY_NO_GAME：空' > Gen/embed.S; echo 'OTHER_LDFLAGS = $(inherited) $(KIRBY_FRAMEWORKS)' >> Gen/kirby.xcconfig
  else
    for m in $MODS; do for f in patched.bin fixups.tsv gates.tsv; do
      [ -f "$PATCHED/$m.$f" ] || { t "🔴 缺 $PATCHED/$m.$f（K04 的改写产物，Windows 上在 D:\\kirby-work\\patched）"; exit 1; }; done; done
    F=$(python3 -I "$REPO/dm/ios/tools/gen_embed.py" "$PATCHED" Gen/embed.S $MODS) || { t "🔴 gen_embed 失败"; exit 1; }
    echo "OTHER_LDFLAGS = \$(inherited) \$(KIRBY_FRAMEWORKS) $F" >> Gen/kirby.xcconfig
    for m in $MODS; do cp "$PATCHED/$m.fixups.tsv" "$PATCHED/$m.gates.tsv" Gen/dm/; done
    [ -f "$SHLIB_IOS/shader_map.tsv" ] && ls "$SHLIB_IOS/b-$V" 2>/dev/null | grep -q '_b.metallib$' || { t "🔴 缺 iOS 版着色器 $SHLIB_IOS/b-$V（先跑 run.sh shaders；翻译链版本 $V）"; exit 1; }
    cp "$SHLIB_IOS/shader_map.tsv" Gen/dm/
    mkdir -p "Gen/msl/b-$V"; find "$SHLIB_IOS/b-$V" -name '*_b.metallib' -o -name '*_b.json' | xargs -I{} cp -c {} "Gen/msl/b-$V/"   # APFS 克隆，不占空间 | APFS clones, no extra space
  fi
  # 权限：付费账号带「提高内存上限」和「扩展虚拟寻址」；免费账号签不了这两项 | entitlements: paid accounts get the two memory ones; free accounts cannot sign them
  if [ "${KIRBY_FREE_ACCOUNT:-0}" = 1 ]; then
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict/></plist>\n' > Gen/kirby.entitlements
  else
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict>\n<key>com.apple.developer.kernel.increased-memory-limit</key><true/>\n<key>com.apple.developer.kernel.extended-virtual-addressing</key><true/>\n</dict></plist>\n' > Gen/kirby.entitlements
  fi
  KIRBY_BUNDLE_ID=${KIRBY_BUNDLE_ID:-org.example.kirbyhost} KIRBY_TEAM_ID=${KIRBY_TEAM_ID:-} xcodegen generate -q >/dev/null || { t "🔴 xcodegen 失败"; exit 1; }
  BN=$(git log -1 --format=%ct 2>/dev/null); [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] && BN=1
  # 按 target 构建、不给 -destination：Xcode 26 给任何真机 / 通用目标都要求先装「iOS 平台」组件（模拟器运行时，约 8 GB），
  # 按 target + -sdk iphoneos 构建不查这一项，产物相同。 | Build by target without -destination: Xcode 26 demands the "iOS platform" component
  # (the ~8 GB simulator runtime) for any device / generic destination; a target build with -sdk iphoneos skips that check and produces the same app.
  SIGN=(-allowProvisioningUpdates)
  if [ -z "${KIRBY_TEAM_ID:-}" ]; then t "⚠ 没配 KIRBY_TEAM_ID：不签名构建（只验证能编过、能链接；不能装机）"; SIGN=(CODE_SIGNING_ALLOWED=NO); fi
  t "构建中（日志 $R/build.log）…"
  xcodebuild -project KirbyIOS.xcodeproj -target KirbyIOS -configuration Release -sdk iphoneos -arch arm64 \
    SYMROOT="$BLD/sym" OBJROOT="$BLD/obj" "${SIGN[@]}" KIRBY_BUILD_NUMBER=${BN:-1} build > "$R/build.log" 2>&1 || {
      t "🔴 构建失败"; grep -E "error:|Undefined symbols|ld: " "$R/build.log" | sort -u | head -20
      grep -qE 'no devices from which to generate a provisioning profile|has no devices' "$R/build.log" && t "原因：开发者团队名下还没有登记这台手机。见 kirby/ios/README.md「签名」"
      exit 1; }
  du -sh "$APP" | awk '{print "App 大小 " $1}'; t "构建完成：$APP" ;;

install)
  need KIRBY_DEVICE
  xcrun devicectl device install app --device "$D" "$APP" > "$R/install.log" 2>&1 || { t "🔴 装机失败"; tail -5 "$R/install.log"; exit 1; }
  t "装机完成" ;;

romfs)
  need KIRBY_DEVICE KIRBY_BUNDLE_ID
  SRC=${2:-$W/RomFS}; [ -d "$SRC/gfx" ] && [ -d "$SRC/shader" ] || { t "🔴 $SRC 不像卡比的 RomFS（要有 gfx/ 和 shader/）"; exit 1; }
  t "推 RomFS $SRC → 手机（约 6 GB，USB 下要十几分钟；App 要先装过一次）"
  xcrun devicectl device copy to --device "$D" --domain-type appDataContainer --domain-identifier "$B" \
    --source "$SRC" --destination 'Library/Application Support/romfs' || { t "🔴 推送失败"; exit 1; }
  t "RomFS 推送完成" ;;

run|log)
  need KIRBY_DEVICE KIRBY_BUNDLE_ID
  O=$R/boot.txt
  pull(){ xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents/boot.txt --destination "$O" >/dev/null 2>&1; }
  [ "$1" = log ] && { pull && cat "$O"; exit; }
  CAP=${2:-120}; rm -f "$O"
  xcrun devicectl device process launch --device "$D" --terminate-existing "$B" > "$R/launch.log" 2>&1 || { t "🔴 启动失败"; tail -8 "$R/launch.log"; exit 1; }
  t "已启动，日志拉回到 $O（上限 ${CAP}s）"
  s=$SECONDS; n=0; sleep 3
  while :; do
    pull; if [ -f "$O" ]; then c=$(wc -l < "$O"); [ "$c" -gt "$n" ] && { sed -n "$((n + 1)),${c}p" "$O"; n=$c; }; grep -q '^\[kirby\] DONE' "$O" && { t "宿主已退出（DONE）"; exit 0; }; fi
    xcrun devicectl device info processes --device "$D" 2>/dev/null | grep -q "KirbyIOS.app/KirbyIOS" || { sleep 1; pull; [ -f "$O" ] && sed -n "$((n + 1)),\$p" "$O"; t "进程已退出"; exit 0; }
    [ $((SECONDS - s)) -gt "$CAP" ] && { t "到上限 ${CAP}s，App 仍在运行"; exit 0; }
    sleep 3
  done ;;

*) sed -n '2,11p' "$IOS/$(basename "$0")"; exit 1 ;;
esac
