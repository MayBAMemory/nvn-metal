# H1 宿主拆分：通用部分 / D2R 专属部分

2026-10-14，主线程。**状态：代码已写完，还没编译过**（Windows 上编不了，要 Mac；见文末「验证」）。

## 做法：共用文件原样编，D2R 专属文件换成卡比自己的

不复制 `dm/host`，也不大改 D2R 的代码。卡比的宿主 `kirbyhost` 由三部分组成：

1. **`dm/host` 的通用文件**：直接引用原文件编译（`kirby/host/Makefile` 的 `HOST`）。
2. **`dm/gfx` 全部**：和 D2R 一样。
3. **`kirby/host/` 下的卡比文件**：

| 文件 | 取代 D2R 的哪个文件 | 内容 |
|---|---|---|
| `kirby_main.c` | `dm/host/main.c` | 入口和 SDK 启动顺序。去掉了存档节流、固定种子、TACT 预取、选怪、触屏布局；缺省装 `main sdk subsdk0` 三个模块 |
| `hle_hid_kirby.c` | `dm/host/hle_hid.c` | 手柄、体感、触屏。D2R 那份和触屏界面、按键脚本缠在一起；卡比导入了 36 个 hid 函数，D2R 只接了 13 个 |
| `kirby_glue.c` | `diag.c`、`dm_*.c`、`hle_tact.c`、`touch_mac.c` 等 | 共用文件会调到这些 D2R 文件里的函数，这里给出中性的垫片（不碰游戏内存） |

**不编的 D2R 专属文件**：`main.c diag.c hle_tact.c hle_granny.c hle_charname.c hle_target.c hle_private_aim.c hle_hid.c touch_mac.c romfs_validate.c`，以及 `dm/host/dm_*.c` 全部（共约 1.3 万行）。

## 对 D2R 代码的改动（都只加了判断，D2R 行为不变）

新增 `int dm_game_d2r(void)`（声明在 `dm.h`）：D2R 的 `main.c` 返回 1，`kirby_main.c` 返回 0。在链接时决定，运行时不用判断构建号。

| 文件 | 改了什么 | 不改的后果 |
|---|---|---|
| `hle_oe.c` | `scale3d_tick` 只在 D2R 下运行 | 会读 `main+0x31e0848`，越出卡比映像（约 10 MB）直接崩溃 |
| `hle_svc.c` | 主堆读数只在 D2R 下读；`StandardAllocator::Allocate` 的原函数，非 D2R 改成按名字查 sdk 自己的定义 | 原函数地址写死成 `sdk+0x2f9974`。卡比 SDK 恰好也是这个地址（见下），但不应依赖这种巧合 |
| `hle_time.c` | 建局种子那一处只在 D2R 下读 | 原代码有指令校验，不会崩，加上判断更稳妥 |
| `hle_fs.c` | 挂存档前补写储藏箱和 `Settings.json` 的那几步，只在 D2R 下做 | 会往卡比的存档目录里写 D2R 的设置文件 |
| `loader.c` | 「强制接管条目没被引用」：D2R 照旧中止，其他游戏只记一行（`DM_IMPORTS=1` 逐条列出） | 卡比**启动必中止**，见下一节 |

## 关键发现

1. **D2R 有 67 个强制接管条目在卡比里没有被任何重定位引用**（`kirby/tools/reloc_refs.py`，结果在 `D:\kirby-work\reloc_refs.txt`，2.8 MB，不进 git；重新生成：`python -I kirby/tools/reloc_refs.py D:\kirby-work\reloc_refs.txt D:\kirby-work\img\main.img D:\kirby-work\img\sdk.img D:\kirby-work\img\subsdk0.img`）。装载器原来的规矩是「登记了却没被引用就中止」，所以共用 D2R 的 `hle_*.c` 时必须放宽，上表最后一行就是为此改的。
   K02 用的是「SDK 导出表里有没有这个名字」，这个标准太宽：导出不等于被引用。**以后判断「某个强制接管会不会命中」，一律以 `D:\kirby-work\reloc_refs.txt` 为准。**
2. **卡比的 SDK 和 D2R 的 SDK 布局相同**，至少抽查的两处地址完全一样：`nn::init::Start` 都在 `sdk+0x2b0470`，`StandardAllocator::Allocate` 都在 `sdk+0x2f9974`。NSO 文件的哈希不同，大概是文件头里的构建信息不同。这意味着 D2R 在 SDK 层面（内核调用、线程、同步）踩过的坑，卡比大概率完全一样，宿主的这部分可以放心复用。
3. 卡比 SDK 的 `nn::init::Start` 反汇编结果与 `kirby_main.c` 的启动顺序逐条一致（`kirby/tools/sdis_sym.py` 可以复查）。
4. 共用文件里还有几处 D2R 的痕迹只影响外观：窗口标题「D2R Mobile」（`front_mac.m`）、`app_mac.m` 的双击启动流程、`hle_fdstat.c` 的路径分类。先不动。

## 已知风险（编译和运行时才能确认）

- **依赖扫描是用正则做的**，不是编译器。宏展开出来的引用可能漏掉；如果链接时报缺符号，就补进 `kirby_glue.c`，并在本文记一笔。
- **K08 会新增 `kirby/host/hle_offline.c`**，它会被 Makefile 的通配自动编进来。里面的名字不能和 D2R 的 `hle_net.c`、`hle_friends.c`、`hle_misc.c`、`hle_account.c` 重复：强制接管表重名的话，装载时会中止。验收 K08 时要逐个对照。
- `hle_movie.c` 只接管了卡比 28 个 movie 导入中的 12 个。剩下的会走 subsdk0 自己的实现，可能会碰到进程间通信。H4 时处理。
- `hle_audio.c` 是 AudioOut；卡比用 AudioRenderer，这部分接口目前全部没有接管，由 H2 负责。
- 真机上，SDK 函数一旦走到进程间通信（svc 0x21 SendSyncRequest），宿主就没有分派。K04 列出了 27 个 D2R 没处理的 svc 号。H4 运行时逐个暴露，逐个接管。

## 验证（要 Mac）

1. **Intel Mac（能做）**：`make -C kirby/host check`，交叉编译成 arm64、只做语法和类型检查，修到 0 错误；然后 `make -C kirby/host ARCH="-arch arm64"` 看链接是否通过（只链接不运行）。另外，D2R 原来的 `make -C dm/host` 也要编一遍，确认 D2R 没被改坏。
2. **Apple 芯片 Mac**：`kirbyhost <产物目录>`（产物用 K04 的 `D:\kirby-work\patched`），环境变量 `DM_ROMFS=<RomFS>`、`DM_SAVE=<空目录>`、`DM_TEMP=<临时目录>`、`DM_IMPORTS=1`，看能走到哪一步。这就是 H4 的起点。
