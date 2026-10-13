# 卡比：探索发现 — 移植工作区

把 Switch 版《星之卡比 探索发现》按 D2R Mobile 的路子原生跑在 iPhone / Mac 上：游戏的 aarch64 代码直接执行，只改写少数指令；系统服务由宿主按 SDK 函数接管（HLE）；NVN 图形在 Metal 上重写；着色器离线翻译。整套做法先读 [../dm/README.zh-CN.md](../dm/README.zh-CN.md) 和 [../dm/gfx/README.zh-CN.md](../dm/gfx/README.zh-CN.md)。

任务清单与分工见 [TASKS.md](TASKS.md)。

## 路径

| 东西 | 位置 | 说明 |
|---|---|---|
| 仓库 | `D:\d2r-mobile`，分支 `kirby` | 卡比相关的新东西都放在 `kirby/` 下 |
| 游戏包 | `D:\swtich\Kirby and the Forgotten Land[010091201605A000][v0].xci` | 只读；文件名里的 ID 是错的，实际 Title ID 是 `01004D300C5AE000` |
| 密钥 | `D:\swtich\keys\prod.keys` | **只能作为参数传给脚本；不许打印、复制、提交，也不许写进日志或报告** |
| 工作区 | `D:\kirby-work` | 游戏文件和所有衍生的大文件都放这里，不进 git |
| ExeFS 原件 | `D:\kirby-work\exefs\` | `main`、`sdk`、`subsdk0`、`rtld`、`main.npdm`（NSO，LZ4 压缩） |
| 解压后的映像 | `D:\kirby-work\img\<模块>.img` | 按 NSO 段头把三段铺到各自的内存偏移（`kirby/tools/nso_info.py` 生成） |
| RomFS | `D:\kirby-work\RomFS\` | 由任务 K01 生成 |

## 已知事实（2026-10-13 分析）

- 版本：v1.0.0（XCI 里没有更新包）。SDK 11.4.0。改写规则和地址都按构建号绑定；以后换版本，地址类的结论都得重做。**脚本里跟版本有关的常量（地址、构建号）一律写成参数或放在文件开头的表里，不要散落在代码里。**
- 构建号（NSO 头 0x40 起的前 16 字节）：`main` = `d9ba7db72ffafecd07f5f5535dec0573`，`sdk` = `b39add63421be525d352e576dab438d3`，`subsdk0` = `535249a546887cac875a099e9c8ef367`。
- 模块：
  - `main` 是游戏本体，.text 8.0 MB，内含 PhysX 3.4、NintendoWare（G3d / Vfx2 / Ui2d / Font / Atk2）、nn::gfx、NEX 4.6.5（联网）。
  - `sdk` 是 NintendoSDK 11.4 + NVN 驱动。
  - `subsdk0` 是 NintendoSDK_movie（视频播放）。
- 图形：只走 NVN，没有 Vulkan。`main` 只导入一个 `nvnBootstrapLoader`，和 D2R 一样。
- 音频：用 nn::audio 的 **AudioRenderer**（经 NintendoWare Atk2）。D2R 只实现了 AudioOut，所以这一块是新活。
- 视频：`mov/*.webm` 共 36 个、350 MB，是 VP9 编码。
- 导入：`main` 导入 707 个符号，D2R 宿主已接管其中 96 个（清单在 `analysis/main.imports.txt`）。
- RomFS：9373 个文件，共 6,140,261,775 字节。按体积前几位是 gfx 4.0 GB、snd 0.9 GB、mov 0.35 GB。大部分文件是 `.cmp`（格式未知）；`shader/` 下有 24 个 `.bfsha`。

## 现成工具（`kirby/tools/`）

| 脚本 | 做什么 |
|---|---|
| `xci_extract.py <xci> <prod.keys> <输出目录>` | 纯 Python（pycryptodomex）解 XCI：解出 ExeFS，并列出 RomFS 清单 `romfs_list.txt` |
| `nso_info.py <exefs目录> <输出目录>` | 解压 NSO 成映像，打印构建号、中间件标签和 NVN 名字 |
| `imports.py <映像> <输出前缀>` | 列出动态导入 / 导出符号 |

## 所有任务都要遵守的规矩

1. **不提交**游戏文件、密钥，以及从游戏里提取出来的二进制、图片、着色器、文本。git 里只放脚本、文档，以及只含符号名、地址、统计数字的小分析表（`kirby/analysis/`，单个文件不超过约 1 MB）。
2. 读游戏数据的 Python 一律用 `python -I` 运行；新写的脚本放在 `kirby/tools/`，不要放进游戏数据目录。
3. **不改** `dm/` 下 D2R 原有的代码，除非任务里明确写了可以改。要用就复制到 `kirby/` 下再改，并在文件开头注明来源。
4. 每个任务做完，在 `kirby/reports/<任务号>.md` 写报告：做了什么、怎么复现（命令）、结果数字、没解决的问题、对后续任务的建议。在 `TASKS.md` 里**只改自己那一行的状态**。
5. 提交到 `kirby` 分支，提交信息以任务号开头（如 `K01: ...`）。**不要 push**。
6. 碰到任务描述没覆盖到的设计决定，不要自己拍板：写进报告里的「待决定」，状态标成「卡住」。
7. 环境：Windows 11，x86-64（**跑不了** aarch64 代码），Python 3.14（已装 pycryptodomex），git 和 cmake 用 scoop 装的，.NET 已装。缺的 Python 包用 `python -m pip install` 装；访问 GitHub 或 PyPI 不通时，可以加代理 `http://127.0.0.1:7897`。
