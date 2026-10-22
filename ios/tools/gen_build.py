# 来源：dm/ios/tools/gen_build.py（D2R），改成取卡比宿主 host/Makefile 的 kirbyhost 目标。
# 从 Makefile 实际展开的编译命令（make -n -B kirbyhost）取宿主的源文件 / 头文件路径 / 框架，生成 iOS 工程的构建输入，不手抄列表。
#   Gen/src/<扁平名>   源文件符号链接（排除 *_mac.*；kirby_main.c 另链到 Gen/main/kirby_main.c，以 -Dmain=dm_main 编）
#                     与 D2R 不同：dm/ios/Sources 下借来的文件（dm_gamepad.m）也收进来——卡比的 iOS 工程自己不编它
#   stdout            xcconfig 行：HEADER_SEARCH_PATHS、OTHER_CFLAGS（-D）、KIRBY_FRAMEWORKS（Cocoa 跳过）
# 用法：gen_build.py <kirby/host 目录> <Gen 目录>
# Source: dm/ios/tools/gen_build.py (D2R), changed to take the kirbyhost target of the Kirby host's host/Makefile.
# Takes the host's source files / header paths / frameworks from the Makefile's actually expanded compile command (make -n -B kirbyhost), with no hand-copied list.
#   Gen/src/<flat name>  symlinks to the sources (excluding *_mac.*; kirby_main.c is linked to Gen/main/kirby_main.c and built with -Dmain=dm_main)
#                        Unlike D2R: files borrowed from dm/ios/Sources (dm_gamepad.m) are collected too -- Kirby's iOS project does not build them itself
#   stdout               xcconfig lines: HEADER_SEARCH_PATHS, OTHER_CFLAGS (-D), KIRBY_FRAMEWORKS (Cocoa skipped)
# Usage: gen_build.py <kirby/host dir> <Gen dir>
import os, shlex, subprocess, sys
host, gen = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
ROOT = os.path.dirname(host)   # 本仓库根 | this repository
DMROOT = os.path.abspath(os.environ.get('D2R_MOBILE', os.path.join(ROOT, '..', 'd2r-mobile')))   # 上游 d2r-mobile 检出 | upstream checkout
cmd = subprocess.run(['make', '-s', '-n', '-B', '-C', host, 'kirbyhost'], capture_output=True, text=True, check=True).stdout
cmd = cmd.replace('\\\n', ' ')   # host/Makefile 的链接命令带续行 | host/Makefile's link command has a line continuation
toks = shlex.split(next(l for l in cmd.splitlines() if l.lstrip().startswith('clang')))
srcs, incs, defs, fws = [], [], [], []
i = 0
while i < len(toks):
    t = toks[i]
    if t == '-framework': fws.append(toks[i + 1]); i += 2; continue
    if t.startswith('-I'): incs.append(os.path.normpath(os.path.join(host, t[2:])))
    elif t.startswith('-D'): defs.append(t)
    elif t.endswith(('.c', '.m', '.S')): srcs.append(os.path.normpath(os.path.join(host, t)))
    i += 1
assert srcs, '编译命令里没有源文件'
dup = sorted({s for s in srcs if srcs.count(s) > 1})
if dup: sys.exit('编译命令里源文件重复：' + ' '.join(dup))
main = os.path.join(host, 'kirby_main.c')
assert main in srcs, 'Makefile 的编译命令里没有 kirby_main.c'
os.makedirs(f'{gen}/src'); os.makedirs(f'{gen}/main')
dirs = []
for s in srcs:
    d = os.path.dirname(s)
    if d not in dirs: dirs.append(d)
    b = os.path.basename(s)
    if os.path.splitext(b)[0].endswith('_mac'): continue   # Mac 专用（front_mac.m、app_mac.m）：front.h 由 Sources/front_ios_kirby.m 实现 | Mac only: front.h is implemented by Sources/front_ios_kirby.m
    if s == main: os.symlink(s, f'{gen}/main/kirby_main.c'); continue
    rel = os.path.relpath(s, ROOT)
    if rel.startswith('..'):   # 上游 dm/ 的文件 | files from upstream dm/
        rel = 'dm/' + os.path.relpath(s, os.path.join(DMROOT, 'dm'))
    flat = rel.replace('/', '__')   # 相对仓库根：不同目录同名文件不冲突 | relative to the repo root: same-named files in different dirs do not collide
    os.symlink(s, f'{gen}/src/{flat}')
print('HEADER_SEARCH_PATHS = $(inherited) ' + ' '.join(f'"{d}"' for d in incs + dirs))
print('OTHER_CFLAGS = $(inherited) ' + ' '.join(defs))
print('KIRBY_FRAMEWORKS = ' + ' '.join(f'-framework {f}' for f in fws if f != 'Cocoa'))
