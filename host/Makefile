# 卡比宿主 kirbyhost（Mac，Apple 芯片原生跑；Intel Mac 上可加 -arch arm64 只做编译检查）。
# 组成 = dm/host 的通用部分（原样编，不复制）+ dm/gfx 全部 + 本目录（卡比入口、手柄、垫片，以及之后的卡比专属接管）。
# 不编 D2R 专属文件：HOST_EXCL 里的，以及全部 dm/host/dm_*.c。拆分依据见 kirby/docs/host_split.md。
# 用法：make -C kirby/host        运行：kirbyhost <改写产物目录> [main sdk subsdk0] [-- 游戏参数]
# Kirby host kirbyhost (Mac, runs natively on Apple silicon; on an Intel Mac add -arch arm64 for compile checking only).
# Made of = the generic part of dm/host (compiled in place, not copied) + all of dm/gfx + this directory (Kirby entry, controller, shims, and later Kirby-only takeovers).
# D2R-only files are not built: those in HOST_EXCL, and all of dm/host/dm_*.c. See kirby/docs/host_split.md for the split.
DM=../../dm
CFLAGS?=-O1 -g -Wall -Wno-unused-function
ARCH?=
HOST_EXCL=main.c diag.c hle_tact.c hle_granny.c hle_charname.c hle_target.c hle_private_aim.c hle_hid.c hle_movie.c touch_mac.c romfs_validate.c
HOST=$(DM)/host/loader.c $(DM)/host/hle.c $(DM)/host/crash.c $(DM)/host/thread.c $(DM)/host/memstat.m $(DM)/host/entry.S \
     $(DM)/host/front_mac.m $(DM)/host/app_mac.m $(DM)/ios/Sources/dm_gamepad.m \
     $(filter-out $(addprefix $(DM)/host/,$(HOST_EXCL)),$(sort $(wildcard $(DM)/host/hle_*.c)))
KIRBY=$(sort $(wildcard *.c))
GFX=$(sort $(wildcard $(DM)/gfx/*.c $(DM)/gfx/nvn/*.c $(DM)/gfx/nvn/*.m $(DM)/gfx/nvn/*.S $(DM)/gfx/tex/*.c $(DM)/gfx/tex/*.m))
HDR=$(wildcard *.h) $(DM)/host/dm.h $(DM)/host/hle.h $(DM)/host/front.h $(DM)/host/dm_input.h $(wildcard $(DM)/gfx/nvn/*.h $(DM)/gfx/tex/*.h)
.DEFAULT_GOAL := kirbyhost
kirbyhost: $(KIRBY) $(HOST) $(GFX) $(HDR)
	clang $(ARCH) $(CFLAGS) -I. -I$(DM)/host -I$(DM)/gfx/nvn/include -fobjc-arc -o $@ $(KIRBY) $(HOST) $(GFX) \
	  -framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox -framework ImageIO -framework UniformTypeIdentifiers -framework GameController
# 只编译不链接：Intel Mac 上 make -C kirby/host check（逐个文件 -fsyntax-only，报出所有编译错误）
# Compile without linking: make -C kirby/host check on an Intel Mac (-fsyntax-only file by file, reports every compile error)
check:
	@fail=0; for f in $(KIRBY) $(HOST) $(GFX); do case $$f in *.S) continue;; esac; \
	  clang -arch arm64 $(CFLAGS) -I. -I$(DM)/host -I$(DM)/gfx/nvn/include -fobjc-arc -fsyntax-only $$f || fail=1; done; exit $$fail
clean:
	rm -f kirbyhost
.PHONY: check clean
