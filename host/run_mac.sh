#!/bin/bash
# 在 Apple 芯片 Mac 上直接跑卡比宿主 kirbyhost（不上手机）。
#   host/run_mac.sh                窗口模式，玩
#   host/run_mac.sh 60             无窗口跑 60 秒就停，日志在 $KIRBY_WORK/mac/runN.txt（测试启动用）
# 要先有：make -C host；$KIRBY_WORK 里的 patched/、RomFS/，以及 macOS 版着色器 shlib_mac/
#   （python3 -I ios/tools/shlib_ios.py $KIRBY_WORK/shlib $KIRBY_WORK/shlib_mac --sdk macosx）
# 键位：WASD 左摇杆，J K U I = A B X Y，Q E = L R，Z C = ZL ZR，回车 = +，Tab = −，方向键 = 十字键
# Run the Kirby host kirbyhost directly on an Apple silicon Mac (no phone).
#   host/run_mac.sh                windowed, to play
#   host/run_mac.sh 60             headless, stops after 60 s; log in $KIRBY_WORK/mac/runN.txt (for boot testing)
# Needs: make -C host; patched/ and RomFS/ in $KIRBY_WORK, and the macOS shader library shlib_mac/
# Keys: WASD left stick, J K U I = A B X Y, Q E = L R, Z C = ZL ZR, Return = +, Tab = −, arrows = D-pad
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
W=${KIRBY_WORK:-$HOME/kirby-work}
M=$W/mac; mkdir -p "$M/save"
export DM_ROMFS=${DM_ROMFS:-$W/RomFS} DM_SAVE=${DM_SAVE:-$M/save}
export DM_NVN_SHMAP=${DM_NVN_SHMAP:-$W/shlib_mac/shader_map.tsv} DM_MSL_DIR=${DM_MSL_DIR:-$W/shlib_mac}
[ -f "$W/pso_manifest.tsv" ] && export DM_PSO_MANIFEST=${DM_PSO_MANIFEST:-$W/pso_manifest.tsv}
for f in "$HERE/kirbyhost" "$W/patched/main.patched.bin" "$DM_ROMFS/shader" "$DM_NVN_SHMAP"; do
  [ -e "$f" ] || { echo "🔴 缺 $f（见脚本开头注释）"; exit 1; }; done
cd "$M"   # 缺的着色器变体清单（uint_masks.tsv、iattr_masks.tsv）写在当前目录 | missing-variant lists are written to the current directory
if [ -z "${1:-}" ]; then
  DM_WINDOW=1 exec "$HERE/kirbyhost" "$W/patched"
fi
n=1; while [ -e "$M/run$n.txt" ]; do n=$((n + 1)); done; O=$M/run$n.txt
"$HERE/kirbyhost" "$W/patched" > "$O" 2>&1 & pid=$!
for ((s = 0; s < $1; s++)); do kill -0 $pid 2>/dev/null || break; sleep 1; done
kill -0 $pid 2>/dev/null && { kill $pid; sleep 1; kill -9 $pid 2>/dev/null; echo "到 $1 秒，已停"; } || { wait $pid; echo "宿主退出，退出码 $?"; }
echo "日志：$O（$(wc -l < "$O" | tr -d ' ') 行）"
