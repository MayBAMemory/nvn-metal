# nvn-metal

**把 Switch 版《星之卡比 探索发现》原生跑在 iPhone / Mac 上。**

游戏的 aarch64 代码直接在设备上执行，没有 CPU 模拟器；只有少数在苹果内核下不能原样执行的指令被改写。系统调用、文件、音频、输入这些平台服务由一个小宿主按 SDK 函数接管（HLE），图形接口 NVN 在 Metal 上重写，着色器在 PC 上离线翻译成 MSL。

思路和基础代码来自 [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)（《暗黑破坏神 II：狱火重生》的 iOS 兼容层，MIT）。本仓库是它 `kirby` 分支里卡比相关的部分：卡比专属的宿主入口与 HLE、iOS 外壳、解包与着色器工具、分析表和报告。

> **不含、也不分发任何游戏内容。** 仓库里没有游戏的程序、美术、文字、数据表或着色器。要跑起来得用你自己购买的 Switch 版游戏：程序在本地解包和改写，RomFS 从原版导出，密钥（`prod.keys`）只在你自己机器上作为参数传给脚本。

English: [README.en.md](README.en.md)

## 现在到哪一步

| 部分 | 状态 |
|---|---|
| 解包 XCI / ExeFS / RomFS、改写三个模块（`main`、`sdk`、`subsdk0`） | 完成，等价性已核对 |
| 宿主 `kirbyhost`（Mac，Apple 芯片）：nv:nvn 图形、hid、音频、文件、时间 | 基本完成，真机已进第一关 3D 场景 |
| 着色器离线翻译（BNSH → GLSL → MSL → metallib） | 完成，iOS 版 metallib 在 Mac 上预编 |
| iOS App（签名、装机、推 RomFS、拉日志） | 走通，iOS 27.2 真机 |
| 音频（nn::audio AudioRenderer）、过场视频（VP9） | 音频部分完成；视频空播已通，实际解码待做 |
| 触屏 / 手柄 | 屏幕手柄 + 实体手柄（Xbox / PS / Switch Pro） |

`TASKS.md` 是任务清单，`reports/` 里每个任务一份报告（做了什么、怎么复现、结果数字、遗留问题）。

## 跑起来需要什么

1. **你自己购买的 Switch 版《星之卡比 探索发现》v1.0.0**（Title ID `01004D300C5AE000`）和对应的 `prod.keys`。
2. **上游 d2r-mobile 的一份检出**：宿主编译的是它的 `dm/host` 通用部分和整个 `dm/gfx`（原样编，不复制），这些文件不在本仓库里。
3. **卡比要用的上游改动**：`patches/d2r-mobile-kirby.patch`（11 个文件、约 520 行，见 [patches/README.md](patches/README.md)）。
4. Mac 上有 Xcode；iOS 出包还要 `brew install xcodegen` 和 `xcodebuild -downloadComponent MetalToolchain`。

```bash
# 上游放在本仓库的兄弟目录，脚本默认就在这儿找；放在别处就设 D2R_MOBILE 指到 d2r-mobile 的根
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch

# 游戏侧：解包、解 NSO、改写模块、出着色器库，产物都放 ~/kirby-work（不进 git）
python -I tools/xci_extract.py <你的.xci> <你的 prod.keys> ~/kirby-work
# ……完整流程见 docs/ 和 TASKS.md 里的任务

# Mac 宿主
make -C host            # 产物 host/kirbyhost
make -C host check      # Intel Mac 上只做语法检查

# iPhone
ios/run.sh build && ios/run.sh install && ios/run.sh run
```

`ios/local.env`（样例 `ios/local.env.example`）里填设备、签名和 `KIRBY_WORK` 路径，不进仓库。

## 目录

| 目录 | 内容 |
|---|---|
| `host/` | 卡比宿主：入口、hid / 音频 / 杂项 HLE、垫片，Mac 上 `make` 出 `kirbyhost` |
| `ios/` | iOS 外壳：`Sources/`（入口、Metal 视图、触屏）、`tools/`（工程生成、着色器编 metallib）、`run.sh` |
| `tools/` | 解包与改写脚本：XCI/NSO/RomFS、导入表、改写与等价核对、着色器翻译、离线接管清单、语法检查 |
| `analysis/` | 从游戏里量出来的**描述性**数据：导入/导出符号、NVN 调用面、着色器键与统计、改写计数（单个文件不超过约 1 MB） |
| `docs/` | 格式与设计说明：RomFS、着色器容器、图形、音频、宿主拆分、Intel Mac |
| `reports/` | 每个任务的报告 |
| `patches/` | 上游 d2r-mobile 的卡比改动（补丁，不是拷贝） |

## 已知事实（2026-10-13 分析，v1.0.0）

- 版本 v1.0.0（XCI 里没有更新包），SDK 11.4.0。改写规则和地址都按构建号绑定；换版本后地址类结论都得重做。**跟版本有关的常量（地址、构建号）一律写成参数或放在文件开头的表里。**
- 构建号（NSO 头 0x40 起 16 字节）：`main` = `d9ba7db72ffafecd07f5f5535dec0573`，`sdk` = `b39add63421be525d352e576dab438d3`，`subsdk0` = `535249a546887cac875a099e9c8ef367`。
- 模块：`main` 是游戏本体（.text 8.0 MB，含 PhysX 3.4、NintendoWare G3d/Vfx2/Ui2d/Font/Atk2、nn::gfx、NEX 4.6.5）；`sdk` 是 NintendoSDK 11.4 + NVN 驱动；`subsdk0` 是 NintendoSDK_movie。
- 图形只走 NVN，没有 Vulkan；`main` 只导入一个 `nvnBootstrapLoader`。
- 音频走 nn::audio 的 **AudioRenderer**（经 Atk2）。D2R 只实现了 AudioOut，这块是新写的。
- `main` 导入 707 个符号，上游宿主已接管其中 96 个（清单 `analysis/main.imports.txt`）。
- RomFS 9373 个文件、约 5.7 GB；`shader/` 下 24 个 `.bfsha`，`mov/` 是 36 个 VP9 `.webm`。

## 开发约定

1. **不提交**游戏文件、密钥，以及从游戏里提取出来的二进制、图片、着色器、文本。git 里只放脚本、文档，和只含符号名、地址、统计数字的小分析表。
2. 读游戏数据的 Python 一律用 `python -I` 运行；脚本放 `tools/`，不要放进游戏数据目录。
3. 改上游 `dm/` 只在 `patches/` 里体现；要在卡比这边覆盖的行为，写到 `host/` 并用文件名后缀区分（`hle_*_kirby.c`）。
4. 每个任务在 `reports/<任务号>.md` 写报告；`TASKS.md` 里只改自己那一行的状态。
5. 改了 C 代码就跑 `python -I tools/syntax_check.py`：对宿主要编的 C 文件做 arm64 苹果目标的语法和类型检查（用 `tools/applestub/` 的最小系统头替身）。只因替身缺声明而报错时补替身，不要改源码迁就替身。

## 许可

本仓库的代码以 MIT 发布，见 [LICENSE](LICENSE)。其中来自 d2r-mobile 的部分版权所有 (c) 2026 jhaizhou，卡比移植部分版权所有 (c) 2026 MayBAMemory；详见 [NOTICE.md](NOTICE.md)。

《星之卡比》《Kirby》及相关名称、游戏内容是任天堂的商标和版权作品，本项目与任天堂无关，不包含也不分发任何游戏内容。
