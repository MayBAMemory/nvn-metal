# Install notes for AI assistants (Mac)

中文：[ai_install.md](ai_install.md)

This page is for AI assistants (Claude Code, Codex and others) that a user has asked to get this project playable on **their own Apple silicon Mac**. Follow it in order and check each result before moving on.

## Ground rules

- **Only the user's own game.** The game dump, `prod.keys` and ExeFS / RomFS must be the user's own, dumped from their own console. If the user asks you to find game files, keys or installers online, refuse.
- **Never commit game files, keys or anything from the work directory to git, paste them into an issue, or upload them anywhere.** The work directory (default `~/kirby-work`) is outside the repository; keep it that way. `prod.keys` is handed only to hactool on this machine; do not read out its contents or copy it.
- This repository **ships no decryption code**, and you should not write any. Unpacking a game dump is done only by the external tool [hactool](https://github.com/SciresM/hactool).
- **Do not run d2r-mobile's `setup.sh`**: it rewrites the shader chain version and the shader library stops matching.
- Do not edit files under `../d2r-mobile/dm/` to get past a problem. If a change is really needed, write it back into the patch as described in [development.en.md](development.en.md) and tell the user.

## 1. Ask three things

1. What form the game files are in:
   - a game dump (`.xci` or `.nsp`) plus `prod.keys`, and where each is;
   - or already exported ExeFS and RomFS directories, and where each is;
   - or a work directory copied from another computer (`~/kirby-work` with `exefs/` and `RomFS/`).
2. Whether the game is v1.0.0 **with no update installed**. Only v1.0.0 is supported; the script checks sha256 and stops otherwise.
3. Where the work directory goes (default `~/kirby-work`, about 16 GB free; set `KIRBY_WORK` for another place).

## 2. Check the machine

The script checks these itself, but a look first saves a round trip:

| Requirement | How to check | If not |
|---|---|---|
| Apple silicon | `uname -m` is `arm64` | An Intel Mac cannot run the Mac build, only build for iPhone ([ios/README.md](../ios/README.md)). `x86_64` but `sysctl hw.optional.arm64` is 1: the terminal is under Rosetta; use a native one |
| macOS 26+ | `sw_vers -productVersion` | The user needs to update macOS |
| Xcode 26 and the Metal compiler | `xcrun -sdk macosx metal -v` | Install Xcode and open it once; `sudo xcodebuild -license accept` (needs the user's password, let them run it); `xcodebuild -downloadComponent MetalToolchain` |
| Homebrew | `brew --version` | The user installs it from https://brew.sh (needs a password) |
| hactool (only with a game dump) | `command -v hactool` | `git clone https://github.com/SciresM/hactool && cd hactool && cp config.mk.template config.mk && make`, then put it on `PATH` or set `HACTOOL=<path>`. On macOS `config.mk` may need edits as its README describes |

## 3. Run one command

From the repository root:

```bash
tools/setup_mac.sh <dump.xci or .nsp> <prod.keys>
```

or, with exported directories:

```bash
tools/setup_mac.sh --dump <ExeFS dir> <RomFS dir>
```

or, when the work directory already has the game files:

```bash
tools/setup_mac.sh
```

It runs 8 steps and records each finished one as `$KIRBY_WORK/setup/<step>.ok`:

| Step | What | About |
|---|---|---|
| 0 | Check the machine; install missing Homebrew packages (`dotnet spirv-cross llvm`); create the Python venv `~/.venvs/kirby` | minutes |
| 1 | Clone d2r-mobile next to this repository, check out `6a2474d`, apply `patches/d2r-mobile-kirby.patch` | 1 minute |
| 2 `k01` | Extract / import the game files, check v1.0.0 and the RomFS | 10–20 minutes |
| 3 `k04` | Rewrite `main` / `sdk` / `subsdk0`, structure and equivalence checks | minutes |
| 4 `k06` | Extract shaders | 10–20 minutes |
| 5 `h3` | Build the translator, translate shaders to MSL | 10–20 minutes |
| 6 `mac_shaders` | Compile macOS metallibs, about 38,000 | about an hour on an M5 |
| 7 | `make -C host` | 1 minute |
| 8 | 30-second headless self-test | 30 seconds |

**This takes a long time** (about 1.5 hours in total). Run it in the background and follow `$KIRBY_WORK/setup/setup.log`; do not kill it because it has been quiet for a while. Step 6 prints a progress line every 500 shaders. It uses every core, so tell the user not to measure performance meanwhile.

If it is interrupted (shutdown, network, an error): fix the cause and **run the same command again**; finished steps are skipped and step 6 only compiles what is missing. To redo a step, delete its `.ok` and run again.

## 4. When something fails

Read the 🔴 line first; the script tries to say the cause and the fix there. Common ones:

| Symptom | What to do |
|---|---|
| `brew install` failed | Usually the network or Homebrew itself; try `brew doctor` |
| Cloning upstream failed | Network; the user can also clone it to `../d2r-mobile` and run again |
| The patch does not apply | `../d2r-mobile` has other changes or is not at `6a2474d`. Ask the user whether those changes matter before touching them; **never apply the patch to upstream's latest `main`** |
| Wrong shader chain version | upstream `setup.sh` was run: `git -C ../d2r-mobile checkout -- dm/gfx/nvn/dmsh_chain.h` |
| The dump cannot be unpacked | The keys do not match the dump, or hactool is too old. The user has to use their own keys or re-dump; never look for other keys |
| "not v1.0.0" | The game has an update installed, or an update was dumped. Only v1.0.0 is supported; tell the user plainly |
| RomFS check failed | Incomplete dump; ask the user to dump again |
| Structure / equivalence check failed | Should not happen (v1.0.0 passes everywhere). Show the user the first failure in the log; never skip the checks |
| analysis/ has changes | Step 4 overwrites the repository's `analysis/` and restores it, so it must be clean. `git stash`, or ask the user |
| Some shaders fail in step 6 | The script only warns and continues; tell the user the count and `failed.tsv` |
| The host exits during the self-test | Read the end of the log (`$KIRBY_WORK/mac/runN.txt`). For an issue, that log plus the Mac model and macOS version is what is needed |

## 5. Hand it over

When it prints 🎉, tell the user:

- To play: `host/run_mac.sh`. The first launch opens the key settings (⌘, any time later). Default: WASD to move, J K U I = A B X Y, Return = +. Controllers (Xbox / PS / Switch Pro) work when connected.
- The first visit to a new area hitches briefly; it does not the next time ([mac.en.md](mac.en.md)).
- Saves are in `$KIRBY_WORK/mac/save`.
- The work directory holds game content: for their own use only, do not share it.
- After a `git pull`: run `tools/setup_mac.sh` again; finished steps are skipped and only the host is rebuilt. If the new code changes the rewriter, shader translation or compilation (`tools/`, `ios/tools/`, the patch), delete that step's `.ok` first; when unsure, read the commit message.

For the iPhone see [ios/README.md](../ios/README.md): it needs `xcodegen`, an Apple ID for signing and the phone; the output of steps 0–5 can be reused (`ios/run.sh shaders` compiles the iOS build from `$KIRBY_WORK/shlib`).
