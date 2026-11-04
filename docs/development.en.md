# Development

The full Chinese version, with test variables and pitfalls, is [development.md](development.md).

## Development rules

1. **Never commit** game files, keys, or anything extracted from the game (binaries, images, shaders, text). The repository holds scripts, docs, and small analysis tables containing only symbol names, addresses and counts.
2. Run scripts that read game data with `python -I`; keep new scripts in `tools/`, never in the game-data directory.
3. Changes to upstream `dm/` live in `patches/`; Kirby-only overrides go in `host/` with a `_kirby` filename suffix (`hle_*_kirby.c`).
4. Write a report per task in `reports/<task id>.md`; only change your own row's status in `TASKS.md`.
5. After changing C code run `python -I tools/syntax_check.py`: an arm64 Apple-target syntax and type check of the host's C files against the minimal system-header stand-ins in `tools/applestub/`. When a failure is only a missing declaration in a stand-in, extend the stand-in — do not edit the source to suit it.
6. After changing the file list in `host/Makefile` or the upstream base, run `python3 -I tools/link_scan.py`: it finds functions called from files Kirby builds but defined only in D2R-only files it does not build (the syntax check compiles without linking and cannot see these); add neutral implementations to `host/kirby_glue.c`. On 2026-11-04 it caught the missing `dm_front_navigation` after `app_mac.m` was dropped.

## iPhone status (2026-10-22)

The Mac build plays levels without major issues; the open problems below are on the iPhone.

- Level 1 renders and holds 30 fps (native) when standing.
- The build on the phone (commit `48c524b`: integer-attribute variant fix, packaged-texture eviction, pipeline warm-up) is not yet verified on device.
- Open issues:
  - walking stutter from synchronous pipeline creation (async pipelines landed on 2026-10-22, see [mac.en.md](mac.en.md); not yet verified on device);
  - garbled textures and low fps in the city;
  - tight memory (~145 MB headroom);
  - cutscene video is still no-op playback on iOS.
