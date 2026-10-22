# nvn-metal

**Kirby and the Forgotten Land (Switch) running natively on iPhone and Mac.**

The game's own aarch64 code executes on the device — there is no CPU emulator. Only the handful of instructions that cannot run as-is under the Apple kernel are rewritten. Platform services (syscalls, files, audio, input) are handled by a small host that implements the SDK functions (HLE); the NVN graphics API is reimplemented on Metal, and shaders are translated offline to MSL on a PC.

The approach and the base code come from [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (Diablo II: Resurrected on iOS, MIT). This repository is the Kirby part of its `kirby` branch: the Kirby host entry points and HLE, the iOS shell, unpacking and shader tools, analysis tables and reports.

> **No game content is included or distributed.** There is no game executable, art, text, data table or shader here. To run it you need your own copy of the Switch game: the executable is unpacked and rewritten locally, the RomFS is exported from your copy, and `prod.keys` is only ever passed to scripts as an argument on your own machine.

中文：[README.md](README.md)

## Status

| Part | State |
|---|---|
| Unpack XCI / ExeFS / RomFS, rewrite the three modules (`main`, `sdk`, `subsdk0`) | done, equivalence checked |
| Host `kirbyhost` (Mac, Apple silicon): NVN graphics, hid, audio, files, time | mostly done; reached the first 3D level on device; running directly on the Mac reaches the difficulty select (see "Running directly on the Mac") |
| Offline shader translation (BNSH → GLSL → MSL → metallib) | done; iOS metallibs precompiled on the Mac |
| iOS app (sign, install, push RomFS, pull logs) | working on device (iOS 27.2) |
| Audio (`nn::audio` AudioRenderer), cutscene video (VP9) | audio done; video plays as black frames, real decoding pending |
| Touch / controller | on-screen pad and physical controllers (Xbox / PS / Switch Pro) |

`TASKS.md` is the task list; `reports/` has one report per task (what was done, how to reproduce, numbers, open issues).

## Requirements

1. **Your own Switch copy of Kirby and the Forgotten Land v1.0.0** (title ID `01004D300C5AE000`) and its `prod.keys`.
2. **An upstream d2r-mobile checkout, pinned to commit `6a2474d`**: the host compiles the generic parts of its `dm/host` and all of `dm/gfx` in place — those files are not in this repository.
3. **The upstream changes Kirby needs**: `patches/d2r-mobile-kirby.patch` (11 files, ~520 lines; see [patches/README.md](patches/README.md)). The patch is against `6a2474d` and **does not apply to the latest upstream `main`** (checked 2026-10-22: upstream has since changed 6 of those files).
4. Xcode on a Mac; for device builds also `brew install xcodegen` and `xcodebuild -downloadComponent MetalToolchain`.

```bash
# Upstream lives next to this repo by default; set D2R_MOBILE to the d2r-mobile root if it is elsewhere
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile checkout 6a2474d     # the patch's base; not the latest main
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch

# Mac host
make -C host            # produces host/kirbyhost
make -C host check      # syntax-only check on an Intel Mac

# iPhone
ios/run.sh build && ios/run.sh install && ios/run.sh run
```

Fill in device, signing and `KIRBY_WORK` in `ios/local.env` (sample: `ios/local.env.example`); it is not committed.

## Running directly on the Mac (Apple silicon)

No phone and no signing: `host/kirbyhost` runs the game on the Mac itself. First run on 2026-10-22, on an M5 (10 cores): it reaches the new-save difficulty select with correct rendering and text. A 60 s headless run held 60 Hz (frame time p50 16.67 / p95 16.98 / max 17.7 ms), with about 1.6 ms of GPU per frame and about 1.5 GB resident. It has not entered a level yet.

It needs `host/kirbyhost` from `make -C host`, plus `patched/`, `RomFS/` and a **macOS shader library `shlib_mac/`** in `$KIRBY_WORK` (default `~/kirby-work`).

```bash
# One-off: compile shlib/ into macOS metallibs (same transforms as the iOS build, different target)
python3 -I ios/tools/shlib_ios.py ~/kirby-work/shlib ~/kirby-work/shlib_mac --sdk macosx
make -C host
host/run_mac.sh          # windowed, to play
host/run_mac.sh 60       # headless, stops after 60 s; log in ~/kirby-work/mac/runN.txt
```

- Keys: WASD = left stick, J K U I = A B X Y, Q E = L R, Z C = ZL ZR, Return = +, Tab = −, arrows = D-pad.
- Saves go to `~/kirby-work/mac/save`. Missing-variant lists and `pso_user.tsv` are written to `~/kirby-work/mac/`.
- Why a separate library: the Mac host (no `DM_SHELL`) loads real metallibs. `kirby_shlib.py` output holds MSL source, and `shlib_ios/` targets iOS.
- The compile is slow: about 38,000 `metal` invocations at about 1 s each, nearly all in the compiler's optimizer. It took 62 minutes on the M5. Don't measure performance while it runs, because it saturates every core.
- Redoing everything from the XCI on a Mac, with no Windows (Homebrew `dotnet spirv-cross llvm`): see the Chinese [README.md](README.md#只有-xci-时全在-mac-上从头重做不用-windows). Verified 2026-10-22; every number matched the original reports.

## Continuing on another computer

Git holds only code. The full checklist (work-directory table, signing, test variables, current status) is in the Chinese [README.md](README.md#换一台电脑接着做); the short version:

1. **Code**: clone `nvn-metal` and `d2r-mobile` side by side, `git -C d2r-mobile checkout 6a2474d`, apply the patch. Verified 2026-10-22: the result matches the old `kirby` branch byte for byte and `make -C host check` has 0 errors. **Do not run d2r-mobile's `setup.sh`**: it rewrites `dm/gfx/nvn/dmsh_chain.h`, which changes the shader-chain version (`15cf7ca29eaf`) and invalidates `shlib/` and `shlib_ios/`. After editing `dm/`, regenerate the patch with `git -C ../d2r-mobile add -N dm && git -C ../d2r-mobile diff 6a2474d -- dm > patches/d2r-mobile-kirby.patch`.
2. **Tools**: Xcode 26 (Metal 4 SDK), `brew install xcodegen`, `xcodebuild -downloadComponent MetalToolchain`, Python 3 with `capstone lz4 zstandard unicorn` for `tools/`. Intel and Apple silicon Macs both build and install; only running `host/kirbyhost` on the Mac needs Apple silicon.
3. **Work directory `~/kirby-work`** (copy from the old machine, e.g. `rsync -a --exclude build --exclude run`). Required:
   - `patched/{main,sdk,subsdk0}.{patched.bin,fixups.tsv,gates.tsv}`;
   - `shlib_ios/` (462 MB of compiled iOS metallibs), or `shlib/` and then rerun `ios/run.sh shaders`;
   - the three device-recorded lists `pso_manifest.tsv`, `uint_masks.tsv`, `iattr_masks.tsv`.

   `RomFS/` (5.7 GB) is only needed if the app has to be reinstalled under a new bundle ID. Keys are only needed to re-extract from the XCI; copy them privately and never commit them.
4. **Phone and signing**:
   - Phone: iPhone 16 Pro Max on iOS 27.2. App bundle ID `io.github.maybamemory.kirbyhost`, with the RomFS already in its container.
   - Keep the same bundle ID and the same Apple ID (team) so the install overwrites in place.
   - Re-query `KIRBY_DEVICE` with `xcrun devicectl list devices`.
   - It is a free account (`KIRBY_FREE_ACCOUNT=1`): profiles expire after 7 days. With a paid account, clear it to get the memory entitlements.
5. **Testing**:
   - Use a USB cable, since Wi-Fi installs drop. Keep the phone unlocked.
   - Pass variables with a `DEVICECTL_CHILD_` prefix: `KIRBY_KEYS` (key script, sticks `SU/SD/SL/SR`), `KIRBY_PROF=2` (sampling profiler), `DM_NVN_SHOT` (screenshots come out upside down), `MTL_DEBUG_LAYER=1`.
   - Never interrupt guest threads with signals: guest code uses x18 and XNU does not preserve it.
6. **Status**:
   - Level 1 renders and holds 30 fps (native) when standing.
   - The build on the phone (commit `d64b879`: integer-attribute variant fix, packaged-texture eviction, pipeline warm-up) is not yet verified on device.
   - Open issues:
     - walking stutter from synchronous pipeline creation (next: async PSOs);
     - garbled textures and low fps in the city;
     - tight memory (~145 MB headroom).

## Layout

| Directory | Contents |
|---|---|
| `host/` | Kirby host: entry, hid / audio / misc HLE, shims; `make` builds `kirbyhost` on the Mac |
| `ios/` | iOS shell: `Sources/` (entry, Metal view, touch), `tools/` (project generation, metallib compilation), `run.sh` |
| `tools/` | Unpacking and rewriting: XCI/NSO/RomFS, imports, rewriting and equivalence checks, shader translation, offline takeover inventory, syntax check |
| `analysis/` | Descriptive data measured from the game: imports/exports, NVN call surface, shader keys and counts, rewrite counts (each file under ~1 MB) |
| `docs/` | Formats and design: RomFS, shader containers, graphics, audio, host split, Intel Mac |
| `reports/` | Per-task reports |
| `patches/` | Kirby changes to upstream d2r-mobile (as a patch, not a copy) |

## Development rules

1. **Never commit** game files, keys, or anything extracted from the game (binaries, images, shaders, text). The repository holds scripts, docs, and small analysis tables containing only symbol names, addresses and counts.
2. Run scripts that read game data with `python -I`; keep new scripts in `tools/`, never in the game-data directory.
3. Changes to upstream `dm/` live in `patches/`; Kirby-only overrides go in `host/` with a `_kirby` filename suffix (`hle_*_kirby.c`).
4. Write a report per task in `reports/<task id>.md`; only change your own row's status in `TASKS.md`.
5. After changing C code run `python -I tools/syntax_check.py`: an arm64 Apple-target syntax and type check of the host's C files against the minimal system-header stand-ins in `tools/applestub/`. When a failure is only a missing declaration in a stand-in, extend the stand-in — do not edit the source to suit it.

## License

MIT, see [LICENSE](LICENSE). Parts derived from d2r-mobile are Copyright (c) 2026 jhaizhou; the Kirby port is Copyright (c) 2026 MayBAMemory. See [NOTICE.md](NOTICE.md).

Kirby and related names and game content are trademarks and copyrights of Nintendo. This project is not affiliated with Nintendo and contains no game content.
