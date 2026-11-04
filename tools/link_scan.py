"""近似链接检查（不用 Mac）：找出卡比宿主编的文件里调用到、却只在卡比不编的上游文件（D2R 专属，见 host/Makefile 的 HOST_EXCL 和 dm_*.c）里定义的函数。
这些在 Mac 上链接时会报缺符号，要在 host/kirby_glue.c 一类文件里补中性实现。按源码文本找（去掉注释和字符串后认函数定义 / 调用），不是真链接：
宏展开出来的名字、函数指针查不到；弱定义（__attribute__((weak))）算已定义。tools/syntax_check.py 查不到这一类问题（它只编不链）。
  python3 -I tools/link_scan.py             列出可能缺的符号：名字、在哪个不编的文件里定义、谁调用；没有就退出码 0
Approximate link check (no Mac needed): functions called from files the Kirby host builds but defined only in upstream files Kirby does not build (D2R-only, see HOST_EXCL in host/Makefile and dm_*.c).
These fail to link on the Mac and need neutral implementations in host/kirby_glue.c or similar. Text based (function definitions / calls after stripping comments and strings), not a real link:
names produced by macros and function pointers are missed; weak definitions (__attribute__((weak))) count as defined. tools/syntax_check.py cannot catch this (it compiles without linking).
  python3 -I tools/link_scan.py             lists possibly missing symbols: name, the unbuilt file defining it, callers; exit 0 when there are none
"""
import glob, os, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DMROOT = Path(os.environ.get('D2R_MOBILE', ROOT.parent / 'd2r-mobile')).resolve()
DM = DMROOT / 'dm'


def makefile_excl():
    m = re.search(r'^HOST_EXCL=(.*)$', (ROOT / 'host/Makefile').read_text(encoding='utf-8'), re.M)
    return set(m.group(1).split()) if m else set()


def built_sources():
    excl = makefile_excl()
    b = [DM / 'host' / n for n in ('loader.c', 'hle.c', 'crash.c', 'thread.c', 'memstat.m', 'front_mac.m')] + [DM / 'ios/Sources/dm_gamepad.m']
    b += [Path(p) for p in sorted(glob.glob(str(DM / 'host/hle_*.c'))) if os.path.basename(p) not in excl]
    for pat in ('gfx/*.c', 'gfx/nvn/*.c', 'gfx/nvn/*.m', 'gfx/tex/*.c', 'gfx/tex/*.m'):
        b += [Path(p) for p in sorted(glob.glob(str(DM / pat)))]
    b += [Path(p) for p in sorted(glob.glob(str(ROOT / 'host/*.c')) + glob.glob(str(ROOT / 'host/*.m')))]
    return b


DEF = re.compile(r'^(?:__attribute__\s*\(\([^)]*\)\)\s*)?(?!static\b)(?:[A-Za-z_][\w\s\*]*?[\s\*])([a-z_]\w*)\s*\([^;{]*\)\s*\{', re.M)
HLEDEF = re.compile(r'\b(?:HLE|HLE_FORCE|NVN|NVN_NOPROTO)\([^,]*,[^,]*,[^,]*,\s*(\w+)')


def strip(s):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', '', s, flags=re.S)


def text(p):
    return strip(Path(p).read_text(encoding='utf-8', errors='replace'))


def defs(s):
    return set(DEF.findall(s)) | set(HLEDEF.findall(s))


def main():
    if not DM.is_dir():
        sys.exit('🔴 没有上游检出 %s（设 D2R_MOBILE）' % DM)
    built = built_sources()
    bset = {p.resolve() for p in built}
    unbuilt = [Path(p) for p in glob.glob(str(DM / 'host/*.c')) + glob.glob(str(DM / 'host/*.m')) if Path(p).resolve() not in bset]
    have = set()
    for p in built:
        have |= defs(text(p))
    elsewhere = {}
    for p in unbuilt:
        for d in defs(text(p)):
            elsewhere.setdefault(d, p.name)
    miss = {}
    for p in built:
        for name in set(re.findall(r'\b([a-z_]\w*)\s*\(', text(p))):
            if name in elsewhere and name not in have:
                miss.setdefault(name, set()).add(os.path.relpath(p, DMROOT.parent))
    for n in sorted(miss):
        print('%s\t%s\t%s' % (n, elsewhere[n], ' '.join(sorted(miss[n]))))
    print('--- 可能缺 %d 个符号（%d 个卡比要编的文件，%d 个不编的上游文件）' % (len(miss), len(built), len(unbuilt)), file=sys.stderr)
    return 1 if miss else 0


if __name__ == '__main__':
    sys.exit(main())
