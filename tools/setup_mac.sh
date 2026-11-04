#!/bin/bash
# 一条命令：从你自己的游戏做到在 Mac 上能玩（Apple 芯片），最后包成能双击的 Kirby Host.app。每一步做完记一笔，中断了重跑同一条命令，从断的地方接着做。
#   tools/setup_mac.sh <游戏包 .xci 或 .nsp> <prod.keys>       有游戏包：交给 hactool 拆包（仓库不带解密代码）
#   tools/setup_mac.sh --dump <ExeFS 目录> <RomFS 目录>          已经用 nxdumptool 等工具导出了 ExeFS / RomFS
#   tools/setup_mac.sh                                           工作区里已经有游戏文件（之前导入过，或从旧电脑拷来）
# 选项：--no-test 最后不跑 30 秒无窗口自检。环境变量：KIRBY_WORK（工作区，缺省 ~/kirby-work）、D2R_MOBILE（上游检出，
#   缺省本仓库的兄弟目录 d2r-mobile）、PYTHON（不设就建 ~/.venvs/kirby）、HACTOOL、JOBS（并发，缺省全部核）。
# 要：Apple 芯片 Mac、macOS 26 起、Xcode 26（开过一次、同意过许可）、Homebrew；约 16 GB 空闲磁盘（导出目录方式约 10 GB）。
# 全程约 1.5 小时，大头是最后编 3.8 万个着色器。日志在 $KIRBY_WORK/setup/setup.log；某一步要重做就删 $KIRBY_WORK/setup/<步>.ok。
# One command: from your own game to playable on an Apple silicon Mac, ending with a double-clickable Kirby Host.app. Each finished step is recorded, so re-running the same command after an interruption resumes there.
#   tools/setup_mac.sh <game dump .xci or .nsp> <prod.keys>     from a game dump: hactool unpacks it (this repository has no decryption code)
#   tools/setup_mac.sh --dump <ExeFS dir> <RomFS dir>            ExeFS / RomFS already dumped with nxdumptool or similar
#   tools/setup_mac.sh                                           the work directory already has the game files (imported before, or copied from another computer)
# Option: --no-test skips the final 30-second headless self-test. Environment: KIRBY_WORK (default ~/kirby-work), D2R_MOBILE (upstream checkout, default
#   d2r-mobile next to this repository), PYTHON (otherwise ~/.venvs/kirby is created), HACTOOL, JOBS (default: all cores).
# Needs: Apple silicon Mac, macOS 26+, Xcode 26 (opened once, license accepted), Homebrew; about 16 GB free (about 10 GB with an exported dump).
# About 1.5 hours in total, mostly compiling 38k shaders at the end. Log: $KIRBY_WORK/setup/setup.log; to redo a step delete $KIRBY_WORK/setup/<step>.ok.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd)
usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
PKG=; KEYS=; DEXE=; DROM=; TEST=1
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage;;
    --no-test) TEST=0; shift;;
    --dump) [ $# -ge 3 ] || usage; DEXE=$2; DROM=$3; shift 3;;
    -*) usage;;
    *) [ $# -ge 2 ] && [ -z "$PKG" ] || usage; PKG=$1; KEYS=$2; shift 2;;
  esac
done

W=${KIRBY_WORK:-$HOME/kirby-work}; export KIRBY_WORK=$W
D2R=${D2R_MOBILE:-$(dirname "$REPO")/d2r-mobile}; export D2R_MOBILE=$D2R
BASE=6a2474d                     # 补丁的基线 | the patch's base commit
CHAIN=15cf7ca29eaf               # 着色器链版本（dm/gfx/nvn/dmsh_chain.h），上游 setup.sh 会改掉它 | shader chain version; upstream's setup.sh rewrites it
UPSTREAM=https://github.com/jhaizhou-ops/d2r-mobile.git
ST=$W/setup
mkdir -p "$ST" || { echo "🔴 建不了 $ST"; exit 1; }
exec > >(tee -a "$ST/setup.log") 2>&1
T0=$(date +%s)
say() { echo "[$(date +%T)] $*"; }
die() { echo "🔴 $*"; echo "   （修好后重跑同一条命令，做完的步骤会跳过；完整日志 $ST/setup.log）"; exit 1; }
done_() { touch "$ST/$1.ok"; }
have() { [ -f "$ST/$1.ok" ]; }
head_() { echo; say "━━ $* ━━"; }
NCPU=$(sysctl -n hw.ncpu 2> /dev/null || echo 4); JOBS=${JOBS:-$NCPU}
say "工作区 ${W}；上游 ${D2R}；并发 $JOBS"

# ── 0. 检查这台 Mac | check this Mac ────────────────────────────────────────
head_ "0/9 检查这台 Mac"
[ "$(uname -s)" = Darwin ] || die "只能在 macOS 上跑（这台是 $(uname -s)）"
if [ "$(uname -m)" != arm64 ]; then
  [ "$(sysctl -n hw.optional.arm64 2> /dev/null)" = 1 ] && die "终端在 Rosetta 下（x86_64）。用原生终端重跑，或：arch -arm64 $0 ..."
  die "要 Apple 芯片的 Mac（这台是 $(uname -m)）。Intel Mac 只能出 iPhone 的包，见 ios/README.md"
fi
osv=$(sw_vers -productVersion); [ "${osv%%.*}" -ge 26 ] 2> /dev/null || die "要 macOS 26 起（图形层用 Metal 4），这台是 $osv"
xcrun -sdk macosx metal -v > /dev/null 2>&1 || die "没有可用的 Metal 编译器。装 Xcode 26、开一次同意许可（sudo xcodebuild -license accept），再 xcodebuild -downloadComponent MetalToolchain"
command -v git > /dev/null || die "没有 git（xcode-select --install）"
command -v brew > /dev/null || die "没有 Homebrew：见 https://brew.sh"
miss=; for p in dotnet spirv-cross llvm; do brew list --versions "$p" > /dev/null 2>&1 || miss="$miss $p"; done
if [ -n "$miss" ]; then say "装 Homebrew 包：$miss"; brew install $miss || die "brew install$miss 没成"; fi
LLVM_BIN=$(brew --prefix llvm)/bin; DOTNET_ROOT=$(brew --prefix dotnet)/libexec; SPIRV=$(brew --prefix spirv-cross)/bin/spirv-cross
[ -x "$LLVM_BIN/llvm-objcopy" ] || die "Homebrew 的 LLVM 里没有 llvm-objcopy（${LLVM_BIN}）"
[ -x "$SPIRV" ] || SPIRV=$(command -v spirv-cross) || die "找不到 spirv-cross"
if [ -n "${PYTHON:-}" ]; then PY=$PYTHON
else
  V=${KIRBY_VENV:-$HOME/.venvs/kirby}; PY=$V/bin/python
  [ -x "$PY" ] || { say "建 Python 虚拟环境 $V"; python3 -m venv "$V" || die "python3 -m venv 没成"; }
fi
"$PY" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || die "Python 要 3.9 起（${PY}）"
"$PY" -c 'import capstone, lz4, zstandard, unicorn' 2> /dev/null || { say "装 Python 包"; "$PY" -m pip install -q capstone lz4 zstandard unicorn || die "pip install 没成"; }
if [ -n "$PKG" ] && ! have k01; then
  HT=${HACTOOL:-$(command -v hactool || true)}
  [ -n "$HT" ] && [ -x "$HT" ] || die "从游戏包取文件要 hactool：https://github.com/SciresM/hactool 按说明 make，放进 PATH 或设 HACTOOL=<路径>"
  export HACTOOL=$HT
fi
needgb=3; have k01 || { [ -n "$PKG" ] && needgb=16 || needgb=10; }
freegb=$(( $(df -Pk "$W" | awk 'NR==2 {print $4}') / 1048576 ))
[ "$freegb" -ge "$needgb" ] || die "$W 所在的盘只剩 ${freegb} GB，要约 ${needgb} GB"
say "✓ macOS $osv / $(sysctl -n machdep.cpu.brand_string 2> /dev/null) / Python $("$PY" -c 'import platform; print(platform.python_version())') / 空闲 ${freegb} GB"

# ── 1. 上游 d2r-mobile 固定在 6a2474d 并套补丁 | upstream at 6a2474d plus the patch ──
head_ "1/9 上游 d2r-mobile（$BASE + 补丁）"
P=$REPO/patches/d2r-mobile-kirby.patch
if [ ! -d "$D2R/.git" ]; then
  [ ! -e "$D2R" ] || die "$D2R 已存在但不是 git 检出"
  say "克隆 $UPSTREAM"; git clone -q "$UPSTREAM" "$D2R" || die "克隆上游没成（网络？）"
fi
if git -C "$D2R" apply --reverse --check "$P" 2> /dev/null; then
  say "✓ 补丁已经套上"
else
  case $(git -C "$D2R" rev-parse HEAD) in
    $BASE*) ;;
    *) [ -z "$(git -C "$D2R" status --porcelain)" ] || die "$D2R 不在 ${BASE}，而且有没提交的改动；先自己处理（不要套到最新 main 上，会冲突）"
       git -C "$D2R" cat-file -e "$BASE^{commit}" 2> /dev/null || git -C "$D2R" fetch -q origin || die "取不到上游提交 $BASE"
       git -C "$D2R" checkout -q "$BASE" || die "检出 $BASE 没成";;
  esac
  git -C "$D2R" apply "$P" || die "补丁套不上 ${D2R}（有别的改动？）"
  say "✓ 检出 $BASE 并套上补丁"
fi
got=$(sed -n 's/^#define DMSH_CHAIN "\(.*\)"/\1/p' "$D2R/dm/gfx/nvn/dmsh_chain.h")
[ "$got" = "$CHAIN" ] || die "上游的着色器链版本是 ${got:-?}，应是 ${CHAIN}。多半是跑过上游的 setup.sh：git -C $D2R checkout -- dm/gfx/nvn/dmsh_chain.h"

# ── 2. 游戏文件 | game files (K01) ──────────────────────────────────────────
head_ "2/9 游戏文件（ExeFS / RomFS）"
v100() {   # $1 = 放 main / sdk / subsdk0 的目录 | directory holding main / sdk / subsdk0
  local ok=0 want name got
  while read -r want name; do
    got=$(shasum -a 256 "$1/$name" 2> /dev/null | cut -c1-64)
    if [ "$got" = "$want" ]; then echo "  ✓ $name v1.0.0"; else echo "  ✗ $name 不是 v1.0.0 的（${got:-文件不在}）"; ok=1; fi
  done < "$HERE/v100.sha256"
  return $ok
}
if have k01; then say "✓ 已做（跳过）"
else
  if [ -d "$W/RomFS" ] && [ -f "$W/exefs/main" ] && [ -f "$W/romfs_list.txt" ]; then
    say "工作区里已有游戏文件（之前导入过或从旧电脑拷来），只核对、不重新取"
  elif [ -n "$PKG" ]; then
    [ -n "$KEYS" ] && [ -f "$KEYS" ] || die "没有这个密钥文件：$KEYS"
    PYTHON=$PY "$HERE/extract_game.sh" "$PKG" "$KEYS" || die "从游戏包取文件没成（上面有原因）"
  elif [ -n "$DEXE" ]; then
    [ -d "$DEXE" ] && [ -d "$DROM" ] || die "导出目录不对：--dump <ExeFS 目录> <RomFS 目录>"
    [ ! -e "$W/RomFS" ] || [ "$(cd "$DROM" && pwd -P)" = "$(cd "$W/RomFS" && pwd -P)" ] || die "已有 $W/RomFS，不覆盖。要重做先自己把它移走"
    "$PY" -I "$HERE/import_dump.py" --exefs "$DEXE" --romfs "$DROM" "$W" --romfs-out "$W/RomFS" || die "导入没成"
  else
    die "工作区里还没有游戏文件。给游戏包和密钥：$0 <游戏包> <prod.keys>；或者给导出目录：$0 --dump <ExeFS> <RomFS>"
  fi
  v100 "$W/exefs" || die "游戏版本不对：本项目只认 v1.0.0（没有更新包）"
  "$PY" -I "$HERE/verify_romfs.py" "$W/RomFS" "$W/romfs_list.txt" || die "RomFS 核对没过（导出不完整？）"
  say "✓ 游戏文件在 $W"; done_ k01
fi

# ── 3. 改写三个模块并核对 | rewrite and verify the three modules (K04) ─────
head_ "3/9 改写 main / sdk / subsdk0 并核对（几分钟）"
if have k04; then say "✓ 已做（跳过）"
else
  export DM_RULESET=native DM_ROOT=$W LLVM_BIN   # 同 docs/build.md 的 K04 | as in K04 of docs/build.md
  for m in main sdk subsdk0; do
    say "${m}：改写"
    "$PY" -I "$D2R/dm/tools/patch_nso.py" "$W/exefs/$m" "$W/patched" "$m" || die "$m 改写没成"
    say "${m}：结构核对"
    "$PY" -I "$HERE/structck_kirby.py" "$W/patched" "$m" "$W/exefs/$m" || die "$m 结构核对没过"
    say "${m}：等价核对"
    "$PY" -I "$HERE/equiv_kirby.py" "$W/patched" "$m" "$W/exefs/$m" --seed 1 --jobs "$JOBS" --tsv "$W/patched/$m.equiv.tsv" || die "$m 等价核对没过"
  done
  say "✓ 三个模块改写完，结构和等价核对全过：$W/patched"; done_ k04
fi

# ── 4. 提取着色器 | extract shaders (K06) ──────────────────────────────────
head_ "4/9 提取着色器（十几分钟）"
if have k06; then say "✓ 已做（跳过）"
else
  # bnsh_extract.py / main_shaders.py 会改写仓库里的 analysis/ 索引，做完还原；所以先要求 analysis/ 没有别的改动
  # bnsh_extract.py / main_shaders.py rewrite the analysis/ index in the repository and are restored afterwards, so analysis/ must have no other changes first
  [ -z "$(git -C "$REPO" status --porcelain -- analysis)" ] || die "仓库的 analysis/ 有没提交的改动，这一步会覆盖它；先提交或 git stash"
  restore() { git -C "$REPO" checkout -q -- analysis && git -C "$REPO" clean -fdq -- analysis; }
  k06() {
    "$PY" -I "$HERE/bnsh_extract.py" "$W/RomFS" "$W/shaders" "$REPO/analysis/kirby_shaders.tsv" --inventory "$REPO/docs/cmp_shader_containers.tsv" || return 1
    "$PY" -I "$HERE/verify_k06.py" "$REPO/analysis" "$W/shaders" || return 1
    "$PY" -I "$HERE/nso_info.py" "$W/exefs" "$W/img" || return 1
    "$PY" -I "$HERE/main_shaders.py" "$W/img/main.img" "$W/shaders" || return 1
  }
  if k06; then restore; say "✓ 着色器在 $W/shaders"; done_ k06; else restore; die "提取着色器没成"; fi
fi

# ── 5. 翻译出着色器库 | translate the shader library (H3) ──────────────────
head_ "5/9 着色器翻译成 MSL（十几分钟）"
if have h3; then say "✓ 已做（跳过）"
else
  export DOTNET_ROOT
  XL=$W/tools/dmsh_xlate/dmsh_xlate
  if [ ! -x "$XL" ]; then
    say "编翻译器 dmsh_xlate（只编它，不跑上游的 setup.sh）"
    dotnet publish "$D2R/dm/gfx/tools/dmsh_xlate" -c Release -o "$W/tools/dmsh_xlate" > "$ST/dmsh_xlate.log" 2>&1 || { tail -20 "$ST/dmsh_xlate.log"; die "dmsh_xlate 编不过（日志 $ST/dmsh_xlate.log）"; }
  fi
  "$PY" -I "$HERE/kirby_shlib.py" --shaders "$W/shaders" --out "$W/shlib" --xlate "$XL" --spirv-cross "$SPIRV" --jobs "$JOBS" || die "出着色器库没成"
  [ -f "$W/shlib/shader_map.tsv" ] && [ -d "$W/shlib/b-$CHAIN" ] || die "着色器库不完整：缺 $W/shlib/shader_map.tsv 或 b-$CHAIN/"
  say "✓ 着色器库在 $W/shlib"; done_ h3
fi

# ── 6. 编成 macOS 版 metallib | compile macOS metallibs ───────────────────
head_ "6/9 编 macOS 版着色器（约 3.8 万次编译，M5 上约 1 小时；中断了重跑只补没编的）"
M=$ST/mac_shaders.ok
if [ -f "$M" ] && [ ! "$W/uint_masks.tsv" -nt "$M" ] && [ ! "$W/iattr_masks.tsv" -nt "$M" ]; then say "✓ 已做（跳过）"
else
  [ -f "$M" ] && say "清单（uint_masks / iattr_masks）比上次新，补编缺的变体"
  "$PY" -I "$REPO/ios/tools/shlib_ios.py" "$W/shlib" "$W/shlib_mac" --sdk macosx -j "$JOBS" --masks "$W/uint_masks.tsv" --iattr "$W/iattr_masks.tsv"
  rc=$?; F=$W/shlib_mac/b-$CHAIN/failed.tsv
  if [ $rc -ne 0 ]; then
    n=$(cat "$F" 2> /dev/null | grep -c .)
    [ "$n" -gt 0 ] || die "编 macOS 版着色器没成"
    say "⚠ 有 $n 个着色器没编过（见 ${F}），其余照常可用；用到它们的东西可能画不出来"
  fi
  say "✓ macOS 版着色器在 $W/shlib_mac"; done_ mac_shaders
fi

# ── 7. 编宿主 | build the host ─────────────────────────────────────────────
head_ "7/9 编宿主 host/kirbyhost"
make -C "$REPO/host" D2R_MOBILE="$D2R" || die "宿主编不过"

# ── 8. 自检：无窗口跑 30 秒 | self-test: 30 seconds headless ──────────────
head_ "8/9 自检（无窗口跑 30 秒）"
if [ "$TEST" = 1 ]; then
  out=$("$REPO/host/run_mac.sh" 30); echo "$out"
  log=$(echo "$out" | sed -n 's/^日志：\(.*\)（.*$/\1/p')
  if echo "$out" | grep -q "已停"; then say "✓ 宿主跑满 30 秒没退出"
  else
    [ -n "$log" ] && tail -30 "$log"
    die "宿主没跑满 30 秒就退出了（日志 ${log:-$W/mac/}）"
  fi
else say "跳过（--no-test）"
fi

# ── 9. 包成能双击的 App | package a double-clickable App ─────────────────
head_ "9/9 包成 App（Kirby Host.app）"
"$HERE/mac_app.sh" "$W" || die "打包 App 没成"

mins=$(( ($(date +%s) - T0) / 60 ))
echo
say "🎉 好了（这次用了 $mins 分钟）。开玩：双击 $W/Kirby Host.app，或者命令行 host/run_mac.sh"
echo "   第一次启动会弹出键位设置（⌘, 随时再开）；缺省 WASD 移动，J K U I = A B X Y，回车 = +。存档在 $W/mac/save。"
echo "   键位、双人、声音和卡顿的说明见 docs/mac.md。"
