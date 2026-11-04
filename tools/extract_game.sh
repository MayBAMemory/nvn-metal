#!/bin/bash
# 从你自己的游戏包里取出 ExeFS 和 RomFS，导入工作区。本仓库不带解密代码：拆包和解密全由外部工具 hactool 做。
#   tools/extract_game.sh <游戏包 .xci 或 .nsp> <prod.keys>
# 游戏包：你自己导出的 Switch 版《星之卡比 探索发现》v1.0.0；prod.keys：从你自己的主机导出的密钥。两样都只在本机交给 hactool，不拷贝、不上传。
# 产物：$KIRBY_WORK（缺省 ~/kirby-work）下的 exefs/、RomFS/、romfs_list.txt。已有 RomFS/ 时不覆盖。
# 需要：hactool（https://github.com/SciresM/hactool ，按它的说明 make 出来），放进 PATH 或用 HACTOOL=<路径> 指定。磁盘留出约 15 GB（中间文件做完自动删）。
# Extract ExeFS and RomFS from your own game dump and import them into the work directory. This repository has no decryption code: unpacking and decryption are done entirely by the external tool hactool.
#   tools/extract_game.sh <game dump .xci or .nsp> <prod.keys>
# Game dump: your own dump of Kirby and the Forgotten Land v1.0.0; prod.keys: keys dumped from your own console. Both are only handed to hactool on this machine, never copied or uploaded.
# Output: exefs/, RomFS/ and romfs_list.txt under $KIRBY_WORK (default ~/kirby-work). An existing RomFS/ is not overwritten.
# Requires: hactool (https://github.com/SciresM/hactool, build it with make), on PATH or given as HACTOOL=<path>. Keep about 15 GB free (intermediate files are deleted when done).
set -u
[ $# -eq 2 ] || { sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
PKG=$1; KEYS=$2; W=${KIRBY_WORK:-$HOME/kirby-work}; PY=${PYTHON:-python3}
TID=01004d300c5ae000
die() { echo "🔴 $*" >&2; exit 1; }
say() { echo "[$(date +%T)] $*"; }
HT=${HACTOOL:-$(command -v hactool || true)}; [ -n "$HT" ] && [ -x "$HT" ] || die "没找到 hactool：装好后放进 PATH，或设 HACTOOL=<路径>（见本文件开头）"
[ -f "$PKG" ] || die "没有这个游戏包：$PKG"; [ -f "$KEYS" ] || die "没有这个密钥文件：$KEYS"
[ ! -e "$W/RomFS" ] || die "已有 $W/RomFS，不覆盖。要重做先自己把它移走"
ht() { "$HT" -k "$KEYS" --disablekeywarns --suppresskeys "$@"; }
mkdir -p "$W" || die "建不了 $W"
T=$W/extract.$$; mkdir -p "$T/nca" || die "建不了中间目录 $T"; trap 'rm -rf "$T" "$W/RomFS.part"' EXIT

say "拆游戏包"
case "$PKG" in
  *.xci|*.XCI) ht -t xci --securedir="$T/nca" "$PKG" > "$T/unpack.log" 2>&1 || die "游戏包拆不开（密钥不对或文件损坏）：$(tail -1 "$T/unpack.log")";;
  *.nsp|*.NSP) ht -t pfs0 --outdir="$T/nca" "$PKG" > "$T/unpack.log" 2>&1 || die "游戏包拆不开（密钥不对或文件损坏）：$(tail -1 "$T/unpack.log")";;
  *) die "只认 .xci 或 .nsp";;
esac

# .nsp 的内容按票据加密：把票据里的加密标题密钥原样交给 hactool，由它用 prod.keys 解（.xci 不需要）
# .nsp content is encrypted per ticket: hand the ticket's encrypted title key to hactool as-is and let it decrypt with prod.keys (.xci needs none)
TK=()
for t in "$T"/nca/*.tik; do
  [ -f "$t" ] || continue
  k=$(xxd -p -s 0x180 -l 16 "$t" | tr -d '\n'); [ ${#k} -eq 32 ] && TK=(--titlekey="$k")
done

# 按每个内容文件自己报的类型和程序号找本体程序，不靠文件名；带 BKTR 的是更新，本项目只做了 v1.0.0
# Find the base program by the type and program id each content file reports, not by file name; one with BKTR is an update, and this project only supports v1.0.0
PROG=; UPD=
for f in "$T"/nca/*.nca; do
  case "$f" in *.cnmt.nca) continue;; esac
  info=$(ht ${TK[@]+"${TK[@]}"} -t nca "$f" 2>&1)
  echo "$info" | grep -qi "Content Type: *Program" || continue
  echo "$info" | grep -qiE "^(Title|Program) ID: *$TID" || continue
  if echo "$info" | grep -q "BKTR"; then UPD=$f; else PROG=$f; fi
done
[ -n "$PROG" ] || die "游戏包里没找到《星之卡比 探索发现》（$TID）的程序"
[ -z "$UPD" ] || say "⚠ 游戏包里还有更新；本项目只做了 v1.0.0，只取本体"

say "取 ExeFS 和 RomFS（约 6 GB）"
ht ${TK[@]+"${TK[@]}"} -t nca --exefsdir="$T/ExeFS" --romfsdir="$W/RomFS.part" "$PROG" > "$T/extract.log" 2>&1 || die "程序内容取不出来：$(tail -1 "$T/extract.log")"

# 版本核对：三个模块必须是 v1.0.0 的 | version check: the three modules must be v1.0.0
ok=1
while read -r want name; do
  got=$(shasum -a 256 "$T/ExeFS/$name" 2> /dev/null | cut -c1-64)
  if [ "$got" = "$want" ]; then echo "  ✓ $name"; else echo "  ✗ $name 不是 v1.0.0 的（${got:-文件不在}）"; ok=0; fi
done < "$HERE/v100.sha256"
[ "$ok" = 1 ] || die "版本不对：本项目只认 v1.0.0"
mv "$W/RomFS.part" "$W/RomFS" || die "改名到 $W/RomFS 没成"

say "导入工作区"
"$PY" -I "$HERE/import_dump.py" --exefs "$T/ExeFS" --romfs "$W/RomFS" "$W" || die "导入失败"
"$PY" -I "$HERE/verify_romfs.py" "$W/RomFS" "$W/romfs_list.txt" || die "RomFS 核对没过"
say "好了：$W。下一步改写三个模块，见 docs/build.md「全在 Mac 上从头重做」的 K04"
