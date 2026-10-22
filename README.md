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
| 宿主 `kirbyhost`（Mac，Apple 芯片）：nv:nvn 图形、hid、音频、文件、时间 | 基本完成，真机已进第一关 3D 场景；Mac 上直接跑已到难易度选择画面（见「在 Mac 上直接跑」） |
| 着色器离线翻译（BNSH → GLSL → MSL → metallib） | 完成，iOS 版 metallib 在 Mac 上预编 |
| iOS App（签名、装机、推 RomFS、拉日志） | 走通，iOS 27.2 真机 |
| 音频（nn::audio AudioRenderer）、过场视频（VP9） | 音频部分完成；视频空播已通，实际解码待做 |
| 触屏 / 手柄 | 屏幕手柄 + 实体手柄（Xbox / PS / Switch Pro） |

`TASKS.md` 是任务清单，`reports/` 里每个任务一份报告（做了什么、怎么复现、结果数字、遗留问题）。

## 跑起来需要什么

1. **你自己购买的 Switch 版《星之卡比 探索发现》v1.0.0**（Title ID `01004D300C5AE000`）和对应的 `prod.keys`。
2. **上游 d2r-mobile 的一份检出，固定在提交 `6a2474d`**：宿主编译的是它的 `dm/host` 通用部分和整个 `dm/gfx`（原样编，不复制），这些文件不在本仓库里。
3. **卡比要用的上游改动**：`patches/d2r-mobile-kirby.patch`（11 个文件、约 630 行，见 [patches/README.md](patches/README.md)）。补丁是对 `6a2474d` 做的，**套不上上游最新的 `main`**（2026-10-22 核对：上游之后改过其中 6 个文件，补丁冲突）。
4. Mac 上有 Xcode；iOS 出包还要 `brew install xcodegen` 和 `xcodebuild -downloadComponent MetalToolchain`。

```bash
# 上游放在本仓库的兄弟目录，脚本默认就在这儿找；放在别处就设 D2R_MOBILE 指到 d2r-mobile 的根
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile checkout 6a2474d     # 补丁的基线；不要用最新 main
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

## 在 Mac 上直接跑（Apple 芯片）

不上手机、不用签名，宿主 `host/kirbyhost` 直接在 Mac 上跑游戏。2026-10-22 第一次在 M5（10 核）上跑通：走到新存档的难易度选择画面，画面和中文都正常；无窗口跑 60 秒稳定 60 Hz（帧时间 p50 16.67 / p95 16.98 / 最大 17.7 ms），GPU 每帧约 1.6 ms，常驻内存约 1.5 GB。还没进过关卡。

要有：`make -C host` 编出的 `host/kirbyhost`；`$KIRBY_WORK`（缺省 `~/kirby-work`）里的 `patched/`、`RomFS/`，以及 **macOS 版着色器 `shlib_mac/`**。

```bash
# 一次性：把 shlib/ 编成 macOS 版 metallib（变换和 iOS 版完全相同，只换编译目标）
python3 -I ios/tools/shlib_ios.py ~/kirby-work/shlib ~/kirby-work/shlib_mac --sdk macosx
make -C host
host/run_mac.sh          # 开窗口玩
host/run_mac.sh 60       # 不开窗口跑 60 秒就停，日志在 ~/kirby-work/mac/runN.txt（测启动、测帧时间用）
```

- 键位：WASD 左摇杆；J K U I = A B X Y；Q E = L R；Z C = ZL ZR；回车 = +；Tab = −；方向键 = 十字键。
- 存档在 `~/kirby-work/mac/save`。运行时缺的变体清单（`uint_masks.tsv`、`iattr_masks.tsv`）和管线清单 `pso_user.tsv` 写在 `~/kirby-work/mac/`；合并进 `~/kirby-work/` 下的同名清单后，重跑上面的编译命令就只补编缺的。
- 为什么要单独编一份：Mac 上的宿主没有 `DM_SHELL`，读的是真正编好的 metallib（`newLibraryWithData`）；`kirby_shlib.py` 出的 `_b.metallib` 里装的是 MSL 源码，`shlib_ios/` 是 iOS 目标的，都不能直接用。
- 编译要很久：约 3.8 万次 `metal` 编译（主库 16985 + 全整数输出 `_u` 约 8500 + 部分整数输出 `_um` 约 4400 + 整数属性 `_ia` 约 2300），每次约 1 秒，几乎全花在编译器的优化阶段（`xcrun` 的开销、合并成一个进程批量编都量过，省不了）。M5 上 62 分钟。**编着色器时别测性能**：它占满所有核，系统负载到 100 左右，测出来的长帧不能算数。
- **新动作 / 新敌人第一次出现时的卡顿**：预编的只是着色器函数（AIR），真正在 GPU 上跑的「管线」（顶点 + 片元 + 顶点格式 + 渲染目标格式 + 混合）要到游戏第一次用这个组合时才知道，当场由 Metal 编成 M5 的机器码；原来提交线程会停下等它编完。第一次玩 6 分钟：90 个超过 200 ms 的长帧里 85 个在建管线，共约 64 秒，最长一次 3.6 秒。现在默认**异步建管线**：没编过的丢给后台编，这次绘制跳过、不等，代价是那个东西第一次出现时少画一两帧。A/B（同一份存档副本、同一份按键脚本、关预热、各 100 秒）：建管线卡住的总时间 527 → 79 ms，>200 ms 的帧 2 → 0，最长一帧 348 → 94 ms。剩下的是合并通道 / 特化片元的管线，仍是同步。`DM_PSO_ASYNC=0` 退回原来的做法。
- 玩过的管线记在 `~/kirby-work/mac/pso_user.tsv`，下次启动在后台先编好，同一处第二次基本不卡。
- 帧数最高 30：卡比原生就是 30 帧，是游戏自己按 30 帧出图（帧时间中位数 33.3 ms）；每帧的活对 M5 很轻，所以机器不热。要 60 帧得先查游戏逻辑是不是按帧走的，没做。
- 已知：窗口标题和菜单是上游 Mac 外壳的（「D2R Mobile」、`dmAppMenuView`），不影响玩。

### 只有 XCI 时，全在 Mac 上从头重做（不用 Windows）

2026-10-22 在 M5 上照下面做了一遍，每一步的数字都和原来 Windows 上的报告一致：K01 RomFS 9373 个文件；K04 三个模块 STRUCT / EQUIV 全 OK、站点数相同；K06 27239 组全部对上；H3 出库 16985、跳过 10 份几何着色器。

```bash
brew install dotnet spirv-cross llvm xcodegen
python3 -m venv ~/.venvs/kirby && ~/.venvs/kirby/bin/pip install capstone lz4 zstandard unicorn pycryptodomex
PY=~/.venvs/kirby/bin/python; W=~/kirby-work; DM=../d2r-mobile/dm
# K01 解包
$PY -I tools/xci_extract.py <你的.xci> <你的 prod.keys> $W --romfs-out $W/RomFS
$PY -I tools/verify_romfs.py $W/RomFS $W/romfs_list.txt
# K04 改写三个模块并核对（核对要 Homebrew 的 LLVM：Xcode 里没有 llvm-objcopy）
export DM_RULESET=native DM_ROOT=$W LLVM_BIN=$(brew --prefix llvm)/bin
for m in main sdk subsdk0; do
  $PY -I $DM/tools/patch_nso.py $W/exefs/$m $W/patched $m
  $PY -I tools/structck_kirby.py $W/patched $m $W/exefs/$m
  $PY -I tools/equiv_kirby.py $W/patched $m $W/exefs/$m --seed 1 --jobs 4 --tsv $W/patched/$m.equiv.tsv
done
# K06 提取着色器（会改写 analysis/shader_index_parts.txt、丢掉 main 那一行，跑完 git checkout 还原）
$PY -I tools/bnsh_extract.py $W/RomFS $W/shaders analysis/kirby_shaders.tsv --inventory docs/cmp_shader_containers.tsv
$PY -I tools/verify_k06.py analysis $W/shaders
$PY -I tools/nso_info.py $W/exefs $W/img && $PY -I tools/main_shaders.py $W/img/main.img $W/shaders
# H3 出着色器库：翻译器按上游源码编（只编它，不跑 setup.sh）
export DOTNET_ROOT=$(brew --prefix dotnet)/libexec
dotnet publish $DM/gfx/tools/dmsh_xlate -c Release -o $W/tools/dmsh_xlate
$PY -I tools/kirby_shlib.py --shaders $W/shaders --out $W/shlib --xlate $W/tools/dmsh_xlate/dmsh_xlate --spirv-cross $(which spirv-cross) --jobs $(sysctl -n hw.ncpu)
```

- Homebrew 的 spirv-cross 是 1.4.363，原来验过的是 1.4.357。整条链自己从头出，前后自洽，没影响。
- 这样重做，真机记下的三份清单（`pso_manifest.tsv`、`uint_masks.tsv`、`iattr_masks.tsv`）就没有了，要重新跑出来。

## 换一台电脑接着做

git 里只有代码。下面这几样要另外准备或从旧电脑拷过来。

### 1. 代码

```bash
git clone git@github.com:MayBAMemory/nvn-metal.git
git clone https://github.com/jhaizhou-ops/d2r-mobile.git      # 和 nvn-metal 放在同一个父目录
git -C d2r-mobile checkout 6a2474d
git -C d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch
```

- 2026-10-22 核对过：在 `6a2474d` 上套补丁，结果和原 `kirby` 分支的 `dm/` 逐字节相同；`make -C host check` 0 错误。
- **不要在 d2r-mobile 里跑它的 `setup.sh`。** 它会按本机工具重写 `dm/gfx/nvn/dmsh_chain.h`，着色器链版本号（现在是 `15cf7ca29eaf`）一变，`shlib/b-<版本>` 和 `shlib_ios/b-<版本>` 就全对不上，得在 Windows 上重出着色器库。
- 改了 `dm/` 下的文件，要把改动写回补丁再提交：
  ```bash
  git -C ../d2r-mobile add -N dm          # 让新建的文件也进 diff
  git -C ../d2r-mobile diff 6a2474d -- dm > patches/d2r-mobile-kirby.patch
  ```
- 想跟上上游新版本，就是把这份补丁改到新的 `main` 上（6 个文件有冲突）。这是单独一项工作，还没做。

### 2. 本机工具

| 要什么 | 做什么用 | 怎么装 |
|---|---|---|
| macOS + Xcode 26（用过 26.5） | 图形层用到 Metal 4，要 26 版 SDK | App Store；装完开一次 Xcode 同意许可 |
| xcodegen | `ios/run.sh build` 生成工程 | `brew install xcodegen` |
| Metal 工具链 | 编 iOS 版 metallib | `xcodebuild -downloadComponent MetalToolchain`（约 690 MB） |
| Python 3 + `capstone lz4 zstandard unicorn` | `tools/` 里的分析和改写核对；单纯出包装机只用标准库 | `python3 -m pip install capstone lz4 zstandard unicorn` |
| Xcode 里登录 Apple ID | 签名 | Xcode → Settings → Accounts |

- Intel Mac 和 Apple 芯片的 Mac 都能出包、装机，游戏在手机上跑。只有想在 Mac 上直接跑 `host/kirbyhost` 才要 Apple 芯片。
- 从零重出着色器库（`shlib/`）原来用 Windows 那套：.NET SDK 10、SPIRV-Cross、ninja、LLVM，见 [reports/K07.md](reports/K07.md)、[docs/shaders.md](docs/shaders.md)。现在 Mac 上也能做，见「只有 XCI 时，全在 Mac 上从头重做」。拷旧电脑的 `shlib/` 就用不上它们。

### 3. 工作区 `~/kirby-work`（不进 git，从旧电脑拷）

用移动硬盘：`rsync -a --exclude build --exclude run ~/kirby-work/ /Volumes/<盘>/kirby-work/`，到新电脑再拷回 `~/kirby-work`。放在别处的话，在 `ios/local.env` 里设 `KIRBY_WORK`。

| 内容 | 大小 | 必须吗 | 说明 |
|---|---|---|---|
| `patched/` 里 `main`、`sdk`、`subsdk0` 各三个文件：`.patched.bin`、`.fixups.tsv`、`.gates.tsv` | 34 MB | **必须** | 改写后的游戏模块，`build` 直接嵌进 App |
| `shlib_ios/`（`shader_map.tsv` 和 `b-15cf7ca29eaf/`） | 462 MB | **必须**（或者拷 `shlib/` 再重编） | 编好的 iOS 版 metallib。没有它就要跑 `ios/run.sh shaders`，从 `shlib/` 重编约 1.7 万个，要很久 |
| `shlib/` | 512 MB | 建议 | `ios/run.sh shaders` 的输入（Windows 出的着色器库）。补编整数变体时要用 |
| `pso_manifest.tsv` | 1 MB | **必须** | 真机记下的管线清单（约 2000 条），`build` 带进 App，启动时后台预编，走动少卡顿 |
| `uint_masks.tsv`、`iattr_masks.tsv` | 很小 | **必须** | 真机记下的整数输出 / 整数顶点属性变体，`ios/run.sh shaders` 照着补编；丢了会重新出现模型拉伸 |
| `RomFS/` | 5.7 GB | 看情况 | 手机上的 App 里已经有一份。只要新电脑用**同一个包名、同一个 Apple ID** 覆盖安装就不用再推；换包名或删过 App 才要 `ios/run.sh romfs` |
| `xci_out/exefs/`、`img/` | 43 MB | 可选 | 原始模块和映像。重跑改写（`dm/tools/patch_nso.py`）、等价核对和 `tools/` 的分析时才要 |
| `prod.keys`、`title.keys` | — | 可选 | 只有从 XCI 重新解包才要。**私下拷，绝不进 git、不贴到任何地方** |
| `run/` | — | 不用 | 旧日志和截图 |
| `build/` | 1.4 GB | 不用 | 每次 `build` 重新生成 |

完全没有旧工作区时，从 XCI 重做一遍：K01（解包、RomFS）→ K04（改写三个模块）→ K06（提取着色器）→ K07 / H3（Windows 上翻译出库）→ `ios/run.sh shaders`。命令都在各自的报告里。这样做真机记下的三份清单也就没了。

### 4. 手机和签名

- 手机：iPhone 16 Pro Max，iOS 27.2。App 叫 Kirby Host，包名 `io.github.maybamemory.kirbyhost`。RomFS 已经推进 App 容器的 `Library/Application Support/romfs`。
- `ios/local.env` 不进 git：从旧电脑拷，或者照 `ios/local.env.example` 重填。旧电脑上这份在 `~/nvn-metal/ios/local.env`（更早在 `~/d2r-mobile/kirby/ios/local.env`）。
  - `KIRBY_BUNDLE_ID` 沿用 `io.github.maybamemory.kirbyhost`，这样新电脑装上去是覆盖安装，RomFS 和存档都在。
  - `KIRBY_TEAM_ID`：Xcode 登录同一个 Apple ID 后的团队号。换了 Apple ID，团队号就变了，覆盖装会被拒，只能删掉重装、重推 RomFS。
  - `KIRBY_DEVICE`：`xcrun devicectl list devices` 的 Identifier 列，每台电脑看到的可能不同，要重查。
  - `KIRBY_FREE_ACCOUNT=1`：现在用的是免费 Apple ID。描述文件 7 天过期，过期要重新 `build` + `install`；每台手机最多 3 个自签 App。
- 新电脑第一次签：Xcode 会给这台电脑生成新的开发证书。手机要连着，自动签名才会把设备登记进描述文件。

**以后换成付费开发者账号**：把 `KIRBY_FREE_ACCOUNT` 清空。这样会带上「提高内存上限」「扩展虚拟寻址」两项权限，卡比内存紧（见下面「现在的进度」），这两项很要紧。

## 测试和调试

**一定要用数据线**：Wi-Fi 装 280 MB 的包会断在半路。手机关掉自动锁屏；锁屏时启动会失败，`run` 会每 5 秒重试，最多 5 分钟。

```bash
ios/run.sh build && ios/run.sh install       # 出包 + 装机（USB 约 3 分钟）
ios/run.sh run 300 > ~/kirby-work/run/runN.txt   # 启动，持续拉回 Documents/boot.txt；结束时把真机记下的三份清单合并进工作区
ios/run.sh shaders                           # 清单里有新变体时补编，再 build
```

无人值守测试用环境变量。在 Mac 上加 `DEVICECTL_CHILD_` 前缀，`devicectl` 会把它原样传给手机上的进程：

| 变量 | 作用 |
|---|---|
| `KIRBY_KEYS="A@12,A@20+0.5,SU@60+20"` | 按键脚本：`键@第几秒[+按住几秒]`，从宿主启动算起，缺省按 0.2 秒。键名 `A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT`，左摇杆推到底 `SU SD SL SR`（卡比用十字键不走路，测走动用摇杆） |
| `KIRBY_PROF=2` | 采样分析器：每 2 毫秒停一下图形提交线程取样，每 5 秒打印前 25 名热点（自身 / 包含） |
| `DM_NVN_SHOT="帧号:shot_%d.png"` | 按帧截图到 Documents，再用 `xcrun devicectl device copy from` 拉回来。**截出来是上下颠倒的**，看之前 `sips -f vertical` |
| `MTL_DEBUG_LAYER=1` | Metal 验证层，出错时会指出具体哪一条 |
| `DM_OPMODE=0 / 1` | 掌机 720p / 主机 1080p。App 里也能切，会保存 |
| `DM_PSO_ASYNC=0` | 关掉异步建管线，退回「等编完再画」（卡比缺省开）。疑心哪个画面缺东西时用它对照 |
| `DM_PSO_MANIFEST=off` | 关掉启动时的管线预热（测卡顿 / 测异步时用） |

例：走动加分析器
`DEVICECTL_CHILD_KIRBY_PROF=2 DEVICECTL_CHILD_KIRBY_KEYS="A@12,A@16,A@20,A@30,A@40,A@50,SU@60+20" ios/run.sh run 100 > ~/kirby-work/run/run35.txt`

**坑**：
- **不能用信号打断客体线程**（`SIGPROF`、`pthread_kill` 等）。客体代码把 x18 当普通寄存器（`main` 里 8250 处），苹果内核递送信号时不保留 x18，会崩，现场 x18=0。要采样就用 Mach 的 `thread_suspend` / `thread_get_state`，见 `host/kirby_prof.c`。
- Instruments（xctrace）连不上 iOS 27 的手机，所以才自己写了 `KIRBY_PROF`。
- 装机只能装 `.ipa`。直接装 `.app` 要逐个传 3.4 万个着色器文件，CoreDevice 连接会断。`run.sh install` 已经处理好，失败重试 3 次。
- 真机日志在 App 的 `Documents/boot.txt`。真机会记下 `uint_masks.tsv`、`iattr_masks.tsv`、`pso_user.tsv` 三份清单，`run` / `log` 结束时自动合并进工作区。

## 现在的进度（2026-10-22）与下一步

**到哪了**：第一关 3D 场景渲染正确。站着不动稳定 30 帧（卡比原生就是 30 帧），1080p 模式下 GPU 每帧约 18.6 ms。

**最后装上手机的是提交 `d64b879` 的代码，还没在真机上验证。** 它包含：
- 没绑定的整数顶点属性也算整数，选 `_b_ia<掩码>` 变体，修第一关开头森林里猴子被拉扯 / 看不见；
- 预排布贴图 600 帧没用就回收（`pkg_sweep`），并统计内存；
- 启动时按 `pso_manifest.tsv` 和手机上的 `pso_user.tsv` 后台预编管线；
- 按键脚本加摇杆。

下一次真机测试就跑上面那条「走动加分析器」，在日志里看：
- 有没有「管线预热完成」；
- 有没有「预排布贴图回收」；
- 还缺不缺 `_ia` 变体（缺的会记进 `iattr_masks.tsv`，补编后重出包）；
- 走动时的长帧和热点。

**已知问题**（玩家实测，2026-10-17）：

| 问题 | 查到的原因 / 打算 |
|---|---|
| 走动卡顿 | 管线在提交线程上同步创建，一个 30–140 ms，走一段有约 250 个长帧。已加清单预编；2026-10-22 加了异步建管线（见「在 Mac 上直接跑」），真机还没验证 |
| 进城后贴图乱飞、帧数低 | 怀疑带骨骼的模型选错了变体（用了浮点版），还没核实。先用截图和 `MTL_DEBUG_LAYER` 定位 |
| 内存 | 余量只有约 145 MB。游戏自己的内存池占 2.1 GB；预排布贴图的 GPU 副本累计涨到 271 MB，已加回收。`nvnTextureInitialize` 重新初始化时不释放旧的序号，有泄漏，未修。付费账号的内存权限能缓解 |
| 其他没做的 | 截图上下颠倒；深度截断（depth clamp）没实现；缺 `c1_full.tsv`；几何着色器（10 份）跳过；过场视频只放黑帧 |

日志里以「内存 至帧」「内存池 共」开头的两行是内存统计（常驻、余量、离系统上限还有多少，预排布贴图现存 / 累计 / 回收），查内存先看它们。

## 目录

| 目录 | 内容 |
|---|---|
| `host/` | 卡比宿主：入口、hid / 音频 / 杂项 HLE、垫片，Mac 上 `make` 出 `kirbyhost`，`run_mac.sh` 直接在 Mac 上跑 |
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
