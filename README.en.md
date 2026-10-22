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
| Host `kirbyhost` (Mac, Apple silicon): NVN graphics, hid, audio, files, time | mostly done; reached the first 3D level on device |
| Offline shader translation (BNSH → GLSL → MSL → metallib) | done; iOS metallibs precompiled on the Mac |
| iOS app (sign, install, push RomFS, pull logs) | working on device (iOS 27.2) |
| Audio (`nn::audio` AudioRenderer), cutscene video (VP9) | audio done; video plays as black frames, real decoding pending |
| Touch / controller | on-screen pad and physical controllers (Xbox / PS / Switch Pro) |

`TASKS.md` is the task list; `reports/` has one report per task (what was done, how to reproduce, numbers, open issues).

## Requirements

1. **Your own Switch copy of Kirby and the Forgotten Land v1.0.0** (title ID `01004D300C5AE000`) and its `prod.keys`.
2. **An upstream d2r-mobile checkout**: the host compiles the generic parts of its `dm/host` and all of `dm/gfx` in place — those files are not in this repository.
3. **The upstream changes Kirby needs**: `patches/d2r-mobile-kirby.patch` (11 files, ~520 lines; see [patches/README.md](patches/README.md)).
4. Xcode on a Mac; for device builds also `brew install xcodegen` and `xcodebuild -downloadComponent MetalToolchain`.

```bash
# Upstream lives next to this repo by default; set D2R_MOBILE to the d2r-mobile root if it is elsewhere
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch

# Mac host
make -C host            # produces host/kirbyhost
make -C host check      # syntax-only check on an Intel Mac

# iPhone
ios/run.sh build && ios/run.sh install && ios/run.sh run
```

Fill in device, signing and `KIRBY_WORK` in `ios/local.env` (sample: `ios/local.env.example`); it is not committed.

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
