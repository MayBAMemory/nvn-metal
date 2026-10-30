# Continuing on another computer

Git holds only code. The full checklist (work-directory table, signing) is in the Chinese [handoff.md](handoff.md), and test variables are in the Chinese [development.md](development.md#测试和调试); the short version:

1. **Code**: clone `nvn-metal` and `d2r-mobile` side by side, `git -C d2r-mobile checkout 6a2474d`, apply the patch. Verified 2026-10-22: the result matches the old `kirby` branch byte for byte and `make -C host check` has 0 errors. **Do not run d2r-mobile's `setup.sh`**: it rewrites `dm/gfx/nvn/dmsh_chain.h`, which changes the shader-chain version (`15cf7ca29eaf`) and invalidates `shlib/` and `shlib_ios/`. After editing `dm/`, regenerate the patch with `git -C ../d2r-mobile add -N dm && git -C ../d2r-mobile diff 6a2474d -- dm > patches/d2r-mobile-kirby.patch`.
2. **Tools**: Xcode 26 (Metal 4 SDK), `brew install xcodegen`, `xcodebuild -downloadComponent MetalToolchain`, Python 3 with `capstone lz4 zstandard unicorn` for `tools/`. Intel and Apple silicon Macs both build and install; only running `host/kirbyhost` on the Mac needs Apple silicon.
3. **Work directory `~/kirby-work`** (copy from the old machine, e.g. `rsync -a --exclude build --exclude run`). Required:
   - `patched/{main,sdk,subsdk0}.{patched.bin,fixups.tsv,gates.tsv}`;
   - `shlib_ios/` (462 MB of compiled iOS metallibs), or `shlib/` and then rerun `ios/run.sh shaders`;
   - the three device-recorded lists `pso_manifest.tsv`, `uint_masks.tsv`, `iattr_masks.tsv`.

   `RomFS/` (5.7 GB) is only needed if the app has to be reinstalled under a new bundle ID. Without it, re-export ExeFS / RomFS from your own copy and run `tools/import_dump.py`.
4. **Phone and signing**:
   - Phone: iPhone 16 Pro Max on iOS 27.2. App bundle ID `io.github.maybamemory.kirbyhost`, with the RomFS already in its container.
   - Keep the same bundle ID and the same Apple ID (team) so the install overwrites in place.
   - Re-query `KIRBY_DEVICE` with `xcrun devicectl list devices`.
   - It is a free account (`KIRBY_FREE_ACCOUNT=1`): profiles expire after 7 days. With a paid account, clear it to get the memory entitlements.
5. **Testing**:
   - Use a USB cable, since Wi-Fi installs drop. Keep the phone unlocked.
   - Pass variables with a `DEVICECTL_CHILD_` prefix: `KIRBY_KEYS` (key script, sticks `SU/SD/SL/SR`), `KIRBY_PROF=2` (sampling profiler), `DM_NVN_SHOT` (screenshots come out upside down), `MTL_DEBUG_LAYER=1`.
   - Never interrupt guest threads with signals: guest code uses x18 and XNU does not preserve it.
