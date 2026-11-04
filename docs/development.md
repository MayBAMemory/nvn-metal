# 开发与调试

给想改代码的人。命令都在仓库根目录下跑。

## 开发约定

1. **不提交**游戏文件、密钥，以及从游戏里提取出来的二进制、图片、着色器、文本。git 里只放脚本、文档，和只含符号名、地址、统计数字的小分析表。
2. 读游戏数据的 Python 一律用 `python -I` 运行；脚本放 `tools/`，不要放进游戏数据目录。
3. 改上游 `dm/` 只在 `patches/` 里体现；要在卡比这边覆盖的行为，写到 `host/` 并用文件名后缀区分（`hle_*_kirby.c`）。
4. 每个任务在 `reports/<任务号>.md` 写报告；`TASKS.md` 里只改自己那一行的状态。
5. 改了 C 代码就跑 `python -I tools/syntax_check.py`：对宿主要编的 C 文件做 arm64 苹果目标的语法和类型检查（用 `tools/applestub/` 的最小系统头替身）。只因替身缺声明而报错时补替身，不要改源码迁就替身。

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
| `KIRBY_FPS=30 / 60` | 关卡帧率：Mac 缺省 60，iOS 缺省 30（见 [mac.md](mac.md)） |
| `DM_AUDIO=1` | Mac 上出声（`run_mac.sh` 开窗口时缺省开；iOS 缺省就出声） |
| `KIRBY_SOUND_BYPASS=0` | 不挂「设备输出旁路」（游戏主声音），对照用 |
| `KIRBY_MOVIE_NOOP=1` | 视频退回「空播」（不解码、0.1 秒后报播完），对照用 |
| `DM_MOVIE=1` | 打出视频播放器的每一步调用（`movie(卡比)` 开头） |

例：走动加分析器
`DEVICECTL_CHILD_KIRBY_PROF=2 DEVICECTL_CHILD_KIRBY_KEYS="A@12,A@16,A@20,A@30,A@40,A@50,SU@60+20" ios/run.sh run 100 > ~/kirby-work/run/run35.txt`

**坑**：
- **不能用信号打断客体线程**（`SIGPROF`、`pthread_kill` 等）。客体代码把 x18 当普通寄存器（`main` 里 8250 处），苹果内核递送信号时不保留 x18，会崩，现场 x18=0。要采样就用 Mach 的 `thread_suspend` / `thread_get_state`，见 `host/kirby_prof.c`。
- Instruments（xctrace）连不上 iOS 27 的手机，所以才自己写了 `KIRBY_PROF`。
- 装机只能装 `.ipa`。直接装 `.app` 要逐个传 3.4 万个着色器文件，CoreDevice 连接会断。`run.sh install` 已经处理好，失败重试 3 次。
- 真机日志在 App 的 `Documents/boot.txt`。真机会记下 `uint_masks.tsv`、`iattr_masks.tsv`、`pso_user.tsv` 三份清单，`run` / `log` 结束时自动合并进工作区。

## iPhone 端进度（2026-10-22）与下一步

**到哪了**：第一关 3D 场景渲染正确。站着不动稳定 30 帧（卡比原生就是 30 帧），1080p 模式下 GPU 每帧约 18.6 ms。

**最后装上手机的是提交 `48c524b` 的代码，还没在真机上验证。** 它包含：
- 没绑定的整数顶点属性也算整数，选 `_b_ia<掩码>` 变体，修第一关开头森林里猴子被拉扯 / 看不见；
- 预排布贴图 600 帧没用就回收（`pkg_sweep`），并统计内存；
- 启动时按 `pso_manifest.tsv` 和手机上的 `pso_user.tsv` 后台预编管线；
- 按键脚本加摇杆。

下一次真机测试就跑上面那条「走动加分析器」，在日志里看：
- 有没有「管线预热完成」；
- 有没有「预排布贴图回收」；
- 还缺不缺 `_ia` 变体（缺的会记进 `iattr_masks.tsv`，补编后重出包）；
- 走动时的长帧和热点。

**iPhone 上的已知问题**（玩家实测，2026-10-17；Mac 上没有这些问题）：

| 问题 | 查到的原因 / 打算 |
|---|---|
| 走动卡顿 | 管线在提交线程上同步创建，一个 30–140 ms，走一段有约 250 个长帧。已加清单预编；2026-10-22 加了异步建管线（见 [mac.md](mac.md)），真机还没验证 |
| 进城后贴图乱飞、帧数低 | 怀疑带骨骼的模型选错了变体（用了浮点版），还没核实。先用截图和 `MTL_DEBUG_LAYER` 定位 |
| 内存 | 余量只有约 145 MB。游戏自己的内存池占 2.1 GB；预排布贴图的 GPU 副本累计涨到 271 MB，已加回收。`nvnTextureInitialize` 重新初始化时不释放旧的序号，有泄漏：2026-11-04 已改（参数相同沿用旧序号；不同则等贴图池里没有引用了再放，见 [gfx.md](gfx.md)），待真机看「贴图没 Finalize 就重新初始化」和「放掉旧序号」两行日志与内存统计。付费账号的内存权限能缓解 |
| 其他没做的 | 截图上下颠倒（2026-11-04 已改：出图帧截图在 GL 原点下按行翻一次，待验证）；深度截断（depth clamp）2026-11-04 已接、待真机看效果；缺 `c1_full.tsv`；几何着色器（10 份）跳过；过场视频在 iOS 上只放黑帧（2026-11-04 起 `ios/run.sh build` 带 libvpx 软解，`host/kirby_vp9_vpx.c`；`tools/test_vp9_vpx.sh` 在 Linux 上和 ffmpeg 逐字节对过，手机上还没看速度和画面） |

日志里以「内存 至帧」「内存池 共」开头的两行是内存统计（常驻、余量、离系统上限还有多少，预排布贴图现存 / 累计 / 回收），查内存先看它们。

## 已知事实（2026-10-13 分析，v1.0.0）

- 版本 v1.0.0（没有更新包），SDK 11.4.0。改写规则和地址都按构建号绑定；换版本后地址类结论都得重做。**跟版本有关的常量（地址、构建号）一律写成参数或放在文件开头的表里。**
- 构建号（NSO 头 0x40 起 16 字节）：`main` = `d9ba7db72ffafecd07f5f5535dec0573`，`sdk` = `b39add63421be525d352e576dab438d3`，`subsdk0` = `535249a546887cac875a099e9c8ef367`。
- 模块：`main` 是游戏本体（.text 8.0 MB，含 PhysX 3.4、NintendoWare G3d/Vfx2/Ui2d/Font/Atk2、nn::gfx、NEX 4.6.5）；`sdk` 是 NintendoSDK 11.4 + NVN 驱动；`subsdk0` 是 NintendoSDK_movie。
- 图形只走 NVN，没有 Vulkan；`main` 只导入一个 `nvnBootstrapLoader`。
- 音频走 nn::audio 的 **AudioRenderer**（经 Atk2）。D2R 只实现了 AudioOut，这块是新写的。
- `main` 导入 707 个符号，上游宿主已接管其中 96 个（清单 `analysis/main.imports.txt`）。
- RomFS 9373 个文件、约 5.7 GB；`shader/` 下 24 个 `.bfsha`，`mov/` 是 36 个 VP9 `.webm`。
