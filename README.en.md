# nvn-metal

**Kirby and the Forgotten Land (Switch) running natively on iPhone and Mac.**

The game's own aarch64 code executes on the device — there is no CPU emulator. Only the handful of instructions that cannot run as-is under the Apple kernel are rewritten. Platform services (syscalls, files, audio, input) are handled by a small host that implements the SDK functions (HLE); the NVN graphics API is reimplemented on Metal, and shaders are translated offline to MSL on a PC.

The approach and the base code come from [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (Diablo II: Resurrected on iOS, MIT). This repository is the Kirby part of its `kirby` branch: the Kirby host entry points and HLE, the iOS shell, unpacking and shader tools, analysis tables and reports.

> **No game content is included or distributed.** There is no game executable, art, text, data table or shader here. To run it you need your own copy of the Switch game: you export its ExeFS and RomFS with an external tool and the executable is rewritten locally. **The repository contains no decryption code and reads no keys.**

中文：[README.md](README.md)

## Status

| Part | State |
|---|---|
| Import ExeFS / RomFS, rewrite the three modules (`main`, `sdk`, `subsdk0`) | done, equivalence checked |
| Host `kirbyhost` (Mac, Apple silicon): NVN graphics, hid, audio, files, time | plays levels on the Mac: 60 fps, sound, cutscenes, remappable keys, local co-op (see "Running directly on the Mac"); on iPhone it reaches the first 3D level, stutter and memory still being worked on |
| Offline shader translation (BNSH → GLSL → MSL → metallib) | done; iOS metallibs precompiled on the Mac |
| iOS app (sign, install, push RomFS, pull logs) | working on device (iOS 27.2) |
| Audio (`nn::audio` AudioRenderer), cutscene video (VP9) | audio done; on the Mac game sound, cutscenes and tutorial clips all play (VideoToolbox VP9 + stb_vorbis); iOS still uses no-op video playback |
| Touch / controller | on-screen pad and physical controllers (Xbox / PS / Switch Pro) |

`TASKS.md` is the task list; `reports/` has one report per task (what was done, how to reproduce, numbers, open issues).

## Requirements

1. **Your own Switch copy of Kirby and the Forgotten Land v1.0.0** (title ID `01004D300C5AE000`), with its ExeFS and RomFS exported as decrypted directories. The repository does no decryption: use an external tool, e.g. nxdumptool on your own Switch, then `python -I tools/import_dump.py --exefs <ExeFS dir> --romfs <RomFS dir> ~/kirby-work --romfs-out ~/kirby-work/RomFS`.
2. **An upstream d2r-mobile checkout, pinned to commit `6a2474d`**: the host compiles the generic parts of its `dm/host` and all of `dm/gfx` in place — those files are not in this repository.
3. **The upstream changes Kirby needs**: `patches/d2r-mobile-kirby.patch` (11 files, ~630 lines; see [patches/README.md](patches/README.md)). The patch is against `6a2474d` and **does not apply to the latest upstream `main`** (checked 2026-10-22: upstream has since changed 6 of those files).
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

No phone and no signing: `host/kirbyhost` runs the game on the Mac itself. First run on 2026-10-22, on an M5 (10 cores): it reaches the new-save difficulty select with correct rendering and text. A 60 s headless run held 60 Hz (frame time p50 16.67 / p95 16.98 / max 17.7 ms), with about 1.6 ms of GPU per frame and about 1.5 GB resident. Since then it plays levels normally: 60 fps, sound, cutscenes, remappable keys and local co-op, each covered below.

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
- First-use stutter (a new enemy or move freezing the game for up to a second or more) comes from building Metal render pipelines on first use, not from shaders. Pipelines are now built **asynchronously** by default: a missing one compiles in the background and that draw is skipped. In an A/B test (same save copy, same key script, warm-up off, 100 s each), stall time went from 527 to 79 ms, frames over 200 ms from 2 to 0, and the worst frame from 348 to 94 ms. Set `DM_PSO_ASYNC=0` to go back. Pipelines you've played through are recorded in `~/kirby-work/mac/pso_user.tsv` and prewarmed at the next launch.
- Sound: the game's mixer runs in-process on the CPU (the host converted it from the system DSP), and in that mode its DeviceSink is silent, so there was no sound at all. When the game adds its device sink (`MainAudioOut`, 5.1), the host now adds a circular buffer sink on the same final mix, reads it after every frame and plays a stereo downmix through an AudioUnit (`host/hle_audio_kirby.c`). `run_mac.sh` sets `DM_AUDIO=1` for windowed play.
- Darker, more saturated picture: Kirby's swapchain is sRGB, and presenting it to the non-sRGB Mac window skipped the re-encode. The present shader now re-encodes in that case (`fs_enc` in upstream `nvn_mtl.m`; D2R unaffected).
- Keys are remappable: "Key Settings…" in the app menu (⌘,), shown automatically on first launch. Local co-op: 1P and 2P each get their own key bindings and controller (numbered by connection order; default 1P = controller 1, 2P = controller 2). When 2P has an input source the No.2 controller reports as connected and the controller applet answers 2 players; co-op starts from the pause menu, which keeps it greyed out early in the story. See `host/kirby_input.m`.
- Cutscenes (`mov/EventMovie/`, 1080p VP9 + Vorbis) and the bottom-right tutorial clips (`mov/GuideMovie/`, 480×270 VP9) now play on the Mac: the host demuxes WebM itself and decodes VP9 with VideoToolbox (`host/kirby_webm.c`, `host/kirby_vp9.c`, `host/hle_movie_kirby.c`). Cutscene Vorbis audio is decoded with stb_vorbis (`host/kirby_vorbis.c`) and played by the game through AudioOut. `KIRBY_MOVIE_NOOP=1` falls back to the old no-op playback. iOS still uses no-op playback.
- 60 fps (default on the Mac): levels ran at 30 because the game's variable-frame-rate mode waits for at least 2 ticks (1/60 s) per frame. That mode measures its own time step (the average ticks per frame, used as a multiplier of 1/60 s), so lowering the minimum to 1 tick gives a frame every tick and the multiplier becomes 1.0 by itself, keeping game speed unchanged. Measured frame time p50 33.33 → 16.67 ms and the game's step multiplier 2.000 → 1.000. See `host/kirby_fps.c`; `KIRBY_FPS=30` goes back to the original. iOS still defaults to 30.
- Redoing everything from your own dump on a Mac, with no Windows (Homebrew `dotnet spirv-cross llvm`): see the Chinese [README.md](README.md#从自己的游戏导出开始全在-mac-上从头重做不用-windows). Verified 2026-10-22; every number matched the original reports.

## Continuing on another computer

See [docs/handoff.en.md](docs/handoff.en.md).

## iPhone status (2026-10-22)

The Mac build plays levels without major issues; the open problems below are on the iPhone.

- Level 1 renders and holds 30 fps (native) when standing.
- The build on the phone (commit `d64b879`: integer-attribute variant fix, packaged-texture eviction, pipeline warm-up) is not yet verified on device.
- Open issues:
  - walking stutter from synchronous pipeline creation (async pipelines landed on 2026-10-22, see "Running directly on the Mac"; not yet verified on device);
  - garbled textures and low fps in the city;
  - tight memory (~145 MB headroom);
  - cutscene video is still no-op playback on iOS.

## Layout

| Directory | Contents |
|---|---|
| `host/` | Kirby host: entry, hid / audio / misc HLE, shims; `make` builds `kirbyhost` on the Mac |
| `ios/` | iOS shell: `Sources/` (entry, Metal view, touch), `tools/` (project generation, metallib compilation), `run.sh` |
| `tools/` | Importing and rewriting: ExeFS/NSO/RomFS, imports, rewriting and equivalence checks, shader translation, offline takeover inventory, syntax check |
| `analysis/` | Descriptive data measured from the game: imports/exports, NVN call surface, shader keys and counts, rewrite counts (each file under ~1 MB) |
| `docs/` | Formats and design: RomFS, shader containers, graphics, audio, host split, Intel Mac; machine handoff in `docs/handoff.en.md` |
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
