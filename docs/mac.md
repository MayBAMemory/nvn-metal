# 在 Mac 上直接跑（Apple 芯片）

不上手机、不用签名，宿主 `host/kirbyhost` 直接在 Mac 上跑游戏。2026-10-22 第一次在 M5（10 核）上跑通：走到新存档的难易度选择画面，画面和中文都正常；无窗口跑 60 秒稳定 60 Hz（帧时间 p50 16.67 / p95 16.98 / 最大 17.7 ms），GPU 每帧约 1.6 ms，常驻内存约 1.5 GB。之后已经能进关卡正常玩：关卡 60 帧、有声音、过场动画能播、可改键、支持本地双人，下面逐条说明。

要有：`make -C host` 编出的 `host/kirbyhost`；`$KIRBY_WORK`（缺省 `~/kirby-work`）里的 `patched/`、`RomFS/`，以及 **macOS 版着色器 `shlib_mac/`**。

```bash
# 一次性：把 shlib/ 编成 macOS 版 metallib（变换和 iOS 版完全相同，只换编译目标）
python3 -I ios/tools/shlib_ios.py ~/kirby-work/shlib ~/kirby-work/shlib_mac --sdk macosx
make -C host
host/run_mac.sh          # 开窗口玩
host/run_mac.sh 60       # 不开窗口跑 60 秒就停，日志在 ~/kirby-work/mac/runN.txt（测启动、测帧时间用）
```

- 键位：缺省 WASD 左摇杆；J K U I = A B X Y；Q E = L R；Z C = ZL ZR；回车 = +；Tab = −；方向键 = 十字键。**可以改**：菜单「键位设置…」（⌘,），第一次启动会自动弹出；点一格再按新键，Esc 取消，删除键清空，同一个键只能绑一处，设置自动保存（NSUserDefaults，域 kirbyhost）。`KIRBY_SHOW_KEYS=1` 强制弹出。实现在 `host/kirby_input.m`：卡比自己读键盘和手柄，不再用上游写死的键位和「所有手柄合成一份」。
- **本地双人**：键位设置里 1P、2P 各有一套键位、各选一个手柄（按连接先后编号，缺省 1P = 手柄 1、2P = 手柄 2；2P 缺省不占键盘）。2P 有键位或分到的手柄连着，游戏查 2 号手柄就报已连接，系统的手柄设置小程序回 2 人。游戏里从暂停菜单「双人游玩」开始；剧情开头（「初始之地」）这一项是灰的，要往后推进才开放，所以实际加入流程还没测到。按键脚本里键名前加 `2` 是 2P（如 `2A@30`），2P 在它第一次按键前 2 秒「插上」。
- 存档在 `~/kirby-work/mac/save`。运行时缺的变体清单（`uint_masks.tsv`、`iattr_masks.tsv`）和管线清单 `pso_user.tsv` 写在 `~/kirby-work/mac/`；合并进 `~/kirby-work/` 下的同名清单后，重跑上面的编译命令就只补编缺的。
- 为什么要单独编一份：Mac 上的宿主没有 `DM_SHELL`，读的是真正编好的 metallib（`newLibraryWithData`）；`kirby_shlib.py` 出的 `_b.metallib` 里装的是 MSL 源码，`shlib_ios/` 是 iOS 目标的，都不能直接用。
- 编译要很久：约 3.8 万次 `metal` 编译（主库 16985 + 全整数输出 `_u` 约 8500 + 部分整数输出 `_um` 约 4400 + 整数属性 `_ia` 约 2300），每次约 1 秒，几乎全花在编译器的优化阶段（`xcrun` 的开销、合并成一个进程批量编都量过，省不了）。M5 上 62 分钟。**编着色器时别测性能**：它占满所有核，系统负载到 100 左右，测出来的长帧不能算数。
- **新动作 / 新敌人第一次出现时的卡顿**：预编的只是着色器函数（AIR），真正在 GPU 上跑的「管线」（顶点 + 片元 + 顶点格式 + 渲染目标格式 + 混合）要到游戏第一次用这个组合时才知道，当场由 Metal 编成 M5 的机器码；原来提交线程会停下等它编完。第一次玩 6 分钟：90 个超过 200 ms 的长帧里 85 个在建管线，共约 64 秒，最长一次 3.6 秒。现在默认**异步建管线**：没编过的丢给后台编，这次绘制跳过、不等，代价是那个东西第一次出现时少画一两帧。A/B（同一份存档副本、同一份按键脚本、关预热、各 100 秒）：建管线卡住的总时间 527 → 79 ms，>200 ms 的帧 2 → 0，最长一帧 348 → 94 ms。剩下的是合并通道 / 特化片元的管线，仍是同步。`DM_PSO_ASYNC=0` 退回原来的做法。
- 玩过的管线记在 `~/kirby-work/mac/pso_user.tsv`，下次启动在后台先编好，同一处第二次基本不卡。
- **共享管线清单**（2026-11-04）：`run_mac.sh` / `mac_app.sh` 每次把仓库的 `pso/shared.tsv` 和工作区的 `pso_manifest.tsv`（真机记下的）合成 `shlib_mac/pso_manifest.tsv`，宿主启动时连同本机的 `pso_user.tsv` 一起后台预编；iOS 的 `ios/run.sh build` 也把共享清单带进 App。清单只有着色器键和管线格式（`S` / `C` / `R` 三种行），不含游戏内容。愿意分享自己玩出来的：`python3 -I tools/pso_merge.py add ~/kirby-work/mac/pso_user.tsv ~/kirby-work/pso_manifest.tsv`，它逐行按格式严格校验、去重排序后写进 `pso/shared.tsv`，再提交 PR。原来 `run_mac.sh` 设 `DM_PSO_MANIFEST`，宿主就只读那一份、本机的 `pso_user.tsv` 反而不预编了，现在不设。
- **声音**：游戏的音响系统（Atk2）用的渲染器本来交给系统 DSP 跑，宿主把它改成在本进程用 CPU 渲染，而这种模式下最后一步「设备输出」（DeviceSink）不出声，所以以前整局都没声音。现在游戏 `AddDeviceSink`（`MainAudioOut`，5.1 声道）时，宿主在同一个最终混音上加挂一个循环缓冲输出，每执行一帧就读出来、缩混成立体声送进 AudioUnit（`host/hle_audio_kirby.c`「设备输出旁路」）。`run_mac.sh` 开窗口时缺省 `DM_AUDIO=1`（宿主在 Mac 上缺省静音，无窗口测试仍静音）。`KIRBY_SOUND_BYPASS=0` 关旁路，`KIRBY_SOUND_DUMP=文件` 导出原始 6 声道数据。两个坑：循环缓冲输出要等它的内存池真挂上（状态 1）再加；交给它的大小必须是整数帧（SDK 算的 23040），按 4 KB 取整的 24576 会让最后一帧越界写坏宿主的堆。
- **画面偏暗、偏浓**：卡比的交换链是 sRGB 格式，出图时采样得到线性值，原样写进 Mac 窗口（非 sRGB）少了一次编码。出图着色器现在在这种情况下重新编码 sRGB（上游 `nvn_mtl.m` 的 `fs_enc`，D2R 不受影响）。
- **过场动画和右下角的教程视频**：`mov/EventMovie/`（4 段，1920×1080 VP9 + Vorbis 音轨）和 `mov/GuideMovie/`（30 多段，480×270 VP9，无声）。Mac 上用 VideoToolbox 硬解 VP9（M5 上 1080p 每帧约 2.8 ms），宿主自己解 WebM（`host/kirby_webm.c`、`host/kirby_vp9.c`），按游戏的 movie:: 协议交 NV12 帧（`host/hle_movie_kirby.c` 开头有协议说明）。过场的 Vorbis 音轨也报给游戏：`host/kirby_vorbis.c` 用 stb_vorbis（`host/third_party/stb_vorbis.c`，公有领域 / MIT）解成 16 位 PCM，游戏自己经 AudioOut 出声（Mac 上要 `DM_AUDIO=1`，`run_mac.sh` 开窗口时已设）。iOS 没有 VP9 解码入口，仍是空播。
- **60 帧（Mac 缺省开）**：关卡里的 30 帧不是图形跟不上，是游戏自己的帧率框架在等——可变帧率模式规定「每帧至少等 2 拍（1/60 秒）」。这个模式的时间步长本来就是实测的（最近 4 帧实际过了几拍取平均，游戏逻辑按「倍数 × 1/60 秒」走），所以把「至少 2 拍」改成 1 拍，帧就每拍出一张，倍数自然变成 1.0，速度不变。实测帧时间 p50 33.33 → 16.67 ms，游戏自己的步长倍数 2.000 → 1.000，GPU 每帧约 4 ms。改的是数据不是代码，见 `host/kirby_fps.c`。`KIRBY_FPS=30` 退回原版。iOS 缺省仍是 30（真机 GPU 每帧约 18.6 ms）。
- **双击起的 App**：`tools/mac_app.sh`（`tools/setup_mac.sh` 最后一步自动做）出 `~/kirby-work/Kirby Host.app`，可以拖进「应用程序」或程序坞。包里只有宿主程序和 `Resources/paths.txt`，改写后的程序、`shlib_mac/`、管线清单和 RomFS 都指向这台机器的工作区，所以只能在构建它的 Mac 上用，**不要分享**。双击起时和 `run_mac.sh` 开窗口玩一样（有声音、60 帧），存档、`pso_user.tsv`、缺的变体清单也用同一个 `~/kirby-work/mac/`；日志在 `~/Library/Logs/Kirby Host/host.log`（留上一次的 `host.prev.log`）。工作区找不到 RomFS 时弹框让你选一次。实现在 `host/kirby_app.m`，取代上游按 D2R 写的 `app_mac.m`。键位存在偏好域 `kirbyhost`，命令行和 App 共用。
- 窗口标题是「Kirby Host」，菜单是中文（系统语言是英文时为英文）：应用菜单「键位设置…」（⌘,）、「退出」（⌘Q），「显示 → 全屏」（⌃⌘F），「帮助 → 键位设置…」。上游 Mac 外壳写死「D2R Mobile」、菜单文案从 D2R 的表里取，卡比这边在 `host/kirby_app.m`、`host/kirby_glue.c`（`dm_ui`）、`host/kirby_input.m` 里换掉，没改上游。

命令都在仓库根目录下跑。准备 `patched/`、`RomFS/`、`shlib/` 的完整流程见 [build.md](build.md)。
