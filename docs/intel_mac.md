# 在 Intel Mac 上做编译检查（给 Mac 上的 Claude 看）

目标：把卡比宿主 `kirbyhost` 的全部源码按 **arm64** 编译、链接一遍，修到 0 错误。Intel Mac 跑不了游戏，Apple 芯片的 Mac 才能跑。这一步只为提前清掉编译和链接问题。

先读 `kirby/README.md`、`kirby/TASKS.md`、`kirby/docs/host_split.md`。

## 前提

- macOS 15.6 或更新，装好 **Xcode 26**。图形层用到了 Metal 4（`MTL4RenderCommandEncoder`），要 macOS 26 SDK。
- 打开 Xcode 一次，同意许可；然后运行 `xcode-select -p`，应输出 `/Applications/Xcode.app/...`。
- 不需要游戏文件。

## 步骤

```bash
cd d2r-mobile                       # 由 kirby-repo.bundle 克隆出来的仓库，分支 kirby
make -C kirby/host check            # 逐个文件 -arch arm64 -fsyntax-only，报出全部编译错误
make -C kirby/host ARCH="-arch arm64"   # 编译 + 链接出 kirbyhost（arm64，在 Intel 上不能运行）
```

`kirby/host/Makefile` 的 `check` 目标也会检查 `.m`（Objective-C）文件，Windows 上的 `kirby/tools/syntax_check.py` 查不到这些：`dm/gfx/nvn/nvn_mtl.m`、`front_mac.m`、`app_mac.m`、`memstat.m`、`dm_gamepad.m`。

## 修错的规矩

- 先分清错在哪一边：
  - 卡比专属文件（`kirby/host/`）可以直接改。
  - D2R 共用文件（`dm/`）只修编译错误本身，不改行为。凡是 D2R 专属的逻辑，一律用 `dm_game_d2r()` 隔开，见 `docs/host_split.md`。
- 链接时报缺符号：缺的如果是 D2R 专属文件里的函数，就在 `kirby/host/kirby_glue.c` 里补一个中性的垫片，并在 `docs/host_split.md` 里记一笔。
- 链接时报符号重复：一般是 `kirby/host/` 下的文件和 `dm/host` 的共用文件定义了同一个函数。保留一处。
- **D2R 自己的构建**（`make -C dm/host`）要用 D2R 的游戏文件生成几个表头，这台机器上没有，所以不要求编过。对 `dm/` 的改动，至少要保证 `make -C kirby/host check` 能过。
- 每修完一批就提交到 `kirby` 分支，提交信息以 `H4-mac:` 开头。修完后写 `kirby/reports/H4-intel-mac.md`：改了什么、为什么、还剩什么。

## 做完以后

把仓库打成包带回 Windows：

```bash
git bundle create kirby-from-mac.bundle kirby
```

在 Windows 上执行 `git pull <bundle> kirby` 合并回来。
