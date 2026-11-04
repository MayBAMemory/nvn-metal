#!/bin/bash
# 把 Mac 宿主包成能双击起的 App：tools/mac_app.sh [输出目录]   产出 <输出目录>/Kirby Host.app（缺省 $KIRBY_WORK，即 ~/kirby-work）
# 包里只有宿主程序和 Resources/paths.txt：改写后的游戏程序、着色器库、RomFS 都从游戏内容来，不进包，paths.txt 指向这台机器的工作区。
# 所以这个 App 只能在构建它的这台 Mac 上跑，也**不要分享**。存档和 host/run_mac.sh 共用（$KIRBY_WORK/mac/save）。
# 要先有：tools/setup_mac.sh 跑完（或者 make -C host，加上工作区里的 patched/、shlib_mac/、RomFS/）。签名用本地临时签名。
# Package the Mac host as a double-clickable App: tools/mac_app.sh [output dir]   produces <output dir>/Kirby Host.app (default $KIRBY_WORK, i.e. ~/kirby-work)
# The package holds only the host program and Resources/paths.txt: the rewritten game program, shader library and RomFS all come from game content, stay out of the package, and paths.txt points at this machine's work directory.
# So the App runs only on the Mac that built it, and must **not be shared**. Saves are shared with host/run_mac.sh ($KIRBY_WORK/mac/save).
# Needs: a finished tools/setup_mac.sh (or make -C host plus patched/, shlib_mac/, RomFS/ in the work directory). Signed ad hoc for local use.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd)
W=${KIRBY_WORK:-$HOME/kirby-work}; OUT=${1:-$W}
D2R=${D2R_MOBILE:-$(dirname "$REPO")/d2r-mobile}
NAME="Kirby Host"; EXE=kirbyhost; BID=${KIRBY_MAC_BUNDLE_ID:-io.github.maybamemory.kirbyhost.mac}
die() { echo "🔴 $*"; exit 1; }
[ "$(uname -s)" = Darwin ] || die "只能在 macOS 上打包"
for f in "$W/patched/main.patched.bin" "$W/shlib_mac/shader_map.tsv" "$W/RomFS/basil/App.bin"; do
  [ -e "$f" ] || die "缺 ${f}（先跑 tools/setup_mac.sh）"
done
make -C "$REPO/host" D2R_MOBILE="$D2R" > /dev/null || die "宿主编不过（make -C host 看报错）"
APP="$OUT/$NAME.app"; mkdir -p "$OUT" || die "建不了 $OUT"
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" || die "建不了 $APP"
cp "$REPO/host/kirbyhost" "$APP/Contents/MacOS/$EXE" || die "拷宿主程序没成"
mkdir -p "$W/mac/save"
{
  echo "product=$W/patched"
  echo "shlib=$W/shlib_mac"
  echo "romfs=$W/RomFS"
  echo "state=$W/mac"
} > "$APP/Contents/Resources/paths.txt"
# 管线种子清单写到 shlib_mac/ 的映射表旁边，宿主按那里找（同 run_mac.sh） | pipeline seed next to the shader map in shlib_mac/, where the host looks (as run_mac.sh)
python3 -I "$HERE/pso_merge.py" seed "$W/shlib_mac/pso_manifest.tsv" "$REPO/pso/shared.tsv" "$W/pso_manifest.tsv" > /dev/null || die "管线种子清单没合成"
VER=$(git -C "$REPO" describe --always --dirty 2> /dev/null || echo 0)
cat > "$APP/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>$NAME</string>
<key>CFBundleDisplayName</key><string>$NAME</string>
<key>CFBundleIdentifier</key><string>$BID</string>
<key>CFBundleExecutable</key><string>$EXE</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0</string>
<key>CFBundleVersion</key><string>$VER</string>
<key>LSMinimumSystemVersion</key><string>26.0</string>
<key>LSArchitecturePriority</key><array><string>arm64</string></array>
<key>NSHighResolutionCapable</key><true/>
<key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
<key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PL
codesign --force -s - "$APP" > /dev/null 2>&1 || die "本地签名失败"
echo "✓ $APP"
echo "  双击就能玩，也可以拖进「应用程序」或程序坞；日志在 ~/Library/Logs/$NAME/host.log。"
echo "  这个 App 指向 $W 里的游戏文件，只能在这台 Mac 上用，不要分享。工作区挪了位置就重跑本脚本。"
