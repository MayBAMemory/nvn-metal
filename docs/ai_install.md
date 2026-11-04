# 给 AI 助理的安装说明（Mac）

English: [ai_install.en.md](ai_install.en.md)

这一页是写给 AI 助理（Claude Code、Codex 等）看的：用户请你在**他们自己的 Apple 芯片 Mac** 上把这个项目装到能玩。照下面做，每一步看结果再往下走。

## 先守住的几条

- **只用用户自己的游戏。** 游戏包、`prod.keys`、ExeFS / RomFS 必须是用户自己的，从自己的主机导出。用户要你去网上找游戏文件、密钥、安装包，拒绝。
- **不要把游戏文件、密钥、工作区里的任何东西提交进 git、贴进 issue，或者上传到任何地方。** 工作区（缺省 `~/kirby-work`）不在仓库里，保持这样。`prod.keys` 只在本机交给 hactool，不要读出内容、不要复制。
- 本仓库**不带解密代码**，也不要你写。拆游戏包只用外部工具 [hactool](https://github.com/SciresM/hactool)。
- **不要在 d2r-mobile 里跑它的 `setup.sh`**：它会改掉着色器链版本，之后的着色器库全对不上。
- 不要改 `../d2r-mobile/dm/` 下的文件来绕过问题。要改就按 [development.md](development.md) 的约定写回补丁，并告诉用户。

## 1. 问清楚三件事

1. 游戏文件是什么形式：
   - 游戏包（`.xci` 或 `.nsp`）加 `prod.keys`，各在哪；
   - 或者已经导出的 ExeFS 和 RomFS 两个目录，各在哪；
   - 或者工作区已经从别的电脑拷来了（`~/kirby-work` 里有 `exefs/`、`RomFS/`）。
2. 游戏版本是不是 v1.0.0、**没有装更新**。本项目只认 v1.0.0；脚本会按 sha256 核对，不对会停。
3. 工作区放哪（缺省 `~/kirby-work`，要约 16 GB 空闲；放别处就设 `KIRBY_WORK`）。

## 2. 检查机器

脚本自己会查，但先看一眼能省一轮：

| 要求 | 怎么看 | 不满足时 |
|---|---|---|
| Apple 芯片 | `uname -m` 是 `arm64` | Intel Mac 跑不了 Mac 版，只能出 iPhone 包（[ios/README.md](../ios/README.md)）。`x86_64` 但 `sysctl hw.optional.arm64` 是 1：终端在 Rosetta 下，换原生终端 |
| macOS 26 起 | `sw_vers -productVersion` | 让用户升级系统 |
| Xcode 26 和 Metal 编译器 | `xcrun -sdk macosx metal -v` | 装 Xcode、开一次；`sudo xcodebuild -license accept`（要用户输密码，让用户自己跑）；`xcodebuild -downloadComponent MetalToolchain` |
| Homebrew | `brew --version` | 让用户按 https://brew.sh 装（要输密码） |
| hactool（只在用游戏包时） | `command -v hactool` | `git clone https://github.com/SciresM/hactool && cd hactool && cp config.mk.template config.mk && make`，放进 `PATH` 或设 `HACTOOL=<路径>`。macOS 上 `config.mk` 可能要按它的说明改 |

## 3. 跑一条命令

在仓库根目录：

```bash
tools/setup_mac.sh <游戏包.xci 或 .nsp> <prod.keys>
```

或者已经有导出目录：

```bash
tools/setup_mac.sh --dump <ExeFS 目录> <RomFS 目录>
```

或者工作区已经有游戏文件：

```bash
tools/setup_mac.sh
```

它依次做 8 步，每步做完在 `$KIRBY_WORK/setup/<步>.ok` 记一笔：

| 步 | 做什么 | 大约多久 |
|---|---|---|
| 0 | 查机器；缺的 Homebrew 包（`dotnet spirv-cross llvm`）自动装；建 Python 虚拟环境 `~/.venvs/kirby` | 几分钟 |
| 1 | 在兄弟目录克隆 d2r-mobile、检出 `6a2474d`、套 `patches/d2r-mobile-kirby.patch` | 1 分钟 |
| 2 `k01` | 取出 / 导入游戏文件，核对 v1.0.0 和 RomFS | 十几分钟 |
| 3 `k04` | 改写 `main` / `sdk` / `subsdk0`，结构和等价核对 | 几分钟 |
| 4 `k06` | 提取着色器 | 十几分钟 |
| 5 `h3` | 编翻译器，着色器翻译成 MSL | 十几分钟 |
| 6 `mac_shaders` | 编 macOS 版 metallib，约 3.8 万次 | M5 上约 1 小时 |
| 7 | `make -C host` | 1 分钟 |
| 8 | 无窗口跑 30 秒自检 | 30 秒 |

**这条命令要跑很久**（总共约 1.5 小时）。在后台跑，盯着日志 `$KIRBY_WORK/setup/setup.log`，不要因为一阵子没输出就杀掉它；第 6 步每编 500 个打一行进度。跑的时候 CPU 会占满，提醒用户这期间别测性能。

中断了（关机、断网、出错）：修好原因，**重跑同一条命令**，做完的步骤自动跳过；第 6 步只补没编的。某一步要重做，删掉它的 `.ok` 再跑。

## 4. 出错时

先看 🔴 那一行，脚本尽量把原因和修法写在里面。常见的：

| 现象 | 处理 |
|---|---|
| `brew install` 没成 | 多半是网络或 Homebrew 本身的问题，`brew doctor` 看看 |
| 克隆上游没成 | 网络；也可以让用户自己克隆到 `../d2r-mobile` 再跑 |
| 补丁套不上 | `../d2r-mobile` 有别的改动或不在 `6a2474d`。先问用户那些改动要不要，再处理；**不要套到上游最新的 `main`** |
| 着色器链版本不对 | 跑过上游 `setup.sh`：`git -C ../d2r-mobile checkout -- dm/gfx/nvn/dmsh_chain.h` |
| 游戏包拆不开 | 密钥和游戏包不匹配，或者 hactool 版本太旧。只能让用户换自己的密钥 / 重新导出，不要找别的密钥 |
| 「不是 v1.0.0」 | 用户的游戏装了更新，或者导出的是更新包。本项目只做了 v1.0.0；如实告诉用户 |
| RomFS 核对没过 | 导出不完整，让用户重新导出 |
| 结构 / 等价核对没过 | 不该发生（v1.0.0 已全部验过）。把日志里第一处失败给用户看，不要跳过核对 |
| analysis/ 有改动 | 第 4 步会覆盖仓库的 `analysis/` 再还原，所以要求它干净。`git stash` 或问用户 |
| 第 6 步有几个编不过 | 脚本只警告、继续；数字和 `failed.tsv` 记下来告诉用户 |
| 自检时宿主退出 | 看日志末尾（`$KIRBY_WORK/mac/runN.txt`）。开 issue 时要的就是这份日志、机器型号和系统版本 |

## 5. 交给用户

跑完会打出 🎉。告诉用户：

- 开玩：`host/run_mac.sh`。第一次启动会弹出键位设置（⌘, 随时再开）。缺省 WASD 移动，J K U I = A B X Y，回车 = +。手柄（Xbox / PS / Switch Pro）插上就能用。
- 第一次到新场景会卡一下，玩过的下次就不卡（[mac.md](mac.md)）。
- 存档在 `$KIRBY_WORK/mac/save`。
- 工作区里是游戏内容，只供自己用，不要分享。
- 以后 `git pull` 了新代码：再跑一次 `tools/setup_mac.sh`，做过的步骤会跳过，只重编宿主。要是新代码改的是改写器、着色器翻译或编译（`tools/`、`ios/tools/`、补丁），删掉对应步骤的 `.ok` 再跑，拿不准就看提交说明。

想装到 iPhone：见 [ios/README.md](../ios/README.md)，要 `xcodegen`、Apple ID 签名和手机；前面 0–5 步的产物可以直接用（`ios/run.sh shaders` 从 `$KIRBY_WORK/shlib` 编 iOS 版）。
