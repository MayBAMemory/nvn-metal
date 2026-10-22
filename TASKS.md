# 卡比移植：任务清单

先读 [README.md](README.md)（路径、已知事实、规矩）。

**难度和建议的模型**
- 低：Haiku / Sonnet 都行。
- 中：建议 Sonnet。
- 高：留给 Opus（主线程）。

**状态**：待做 / 进行中 / 完成 / 卡住（原因写进报告）。

## 总表

| 号 | 任务 | 难度 | 依赖 | 状态 |
|---|---|---|---|---|
| K01 | 完整解出 RomFS | 低 | — | 完成 |
| K02 | 导入符号表格化，对照 D2R 已接管的接口 | 低 | — | 完成 |
| K03 | RomFS 文件格式摸底（重点是 `.cmp`） | 低 | K01 | 完成 |
| K04 | 用改写器改写卡比的三个模块，在 Windows 上跑等价核对 | 中 | — | 完成 |
| K05 | NVN 调用面：卡比实际用到哪些 NVN 函数 | 中 | — | 完成 |
| K06 | 定位并提取着色器 | 中 | K01, K03 | 完成 |
| K07 | 在 Windows 上搭着色器翻译链并试翻译 | 中 | K06 | 完成（主线程）：非几何着色器 100% 能翻成 MSL，见 reports/K07.md |
| K08 | 离线服务桩：联网、amiibo、商城等接口的清单与桩代码草稿 | 低 | K02 | 完成 |
| K09 | AudioRenderer 资料盘点与接口清单 | 中 | K02 | 取消：主线程查明不需要（docs/audio.md） |
| K10a | AudioRenderer DSP 移植：解码与重采样 | 中 | K09 | 取消：不需要（docs/audio.md） |
| K10b | AudioRenderer DSP 移植：混音、音量渐变、双二阶滤波 | 中 | K09 | 取消：不需要（docs/audio.md） |
| K10c | AudioRenderer DSP 移植：效果器（延迟、混响、I3DL2、Aux） | 中 | K09 | 取消：不需要（docs/audio.md） |
| H1 | 宿主拆分：通用部分 / D2R 专属部分，加卡比的构建配置 | 高 | — | 完成：Intel Mac 上 arm64 编译 + 链接 0 错误（reports/H4-intel-mac.md）；运行要 Apple 芯片 Mac |
| H2 | AudioRenderer：参数更新解析、命令生成、接进宿主 | 高 | K10* | 完成：改为补 6 个入口（host/hle_audio_kirby.c），Mac 编译链接通过 |
| H3 | 着色器作答表（facts）方案 | 高 | K07 | 完成：按机器码定键出库 16985 份，不需要作答表（docs/shaders.md）；alpha 测试与几何着色器留 H4 |
| H4 | Mac / iOS 上启动调试，直到出第一帧 | 高 | H1, K04 | 主线程（要 Mac） |
| I1 | 卡比的 iOS 外壳（kirby/ios）：出包、签名、装机、推 RomFS、拉日志 | 高 | H1 | 完成：真机（iOS 27.2）签名、装机、启动、拉日志已走通；待拷来游戏数据后跑带游戏内容的包（reports/I1.md） |
| U1 | 决定开发设备（Apple 芯片 Mac，或 Intel Mac 加 iPhone） | — | — | 用户 |
| U2 | 决定游戏版本：是否从主机导出最新更新包 | — | — | 用户（越早越好） |

可以并行的组：
- 第一批 K01、K02、K04、K05（已完成）。
- **第二批 K06、K08、K09，三个互不依赖，可以同时开**。K07 等 K06 做完；K10a、K10b、K10c 等 K09 做完，这三个之间也能并行。

## 验收记录

**2026-10-14 主线程验收 K01–K05：全部通过。** 抽查了 RomFS 的文件数和字节数、三个模块 structck 与 equiv 日志的最后一行，并核对了提交内容里没有密钥。这几个结论对后面有用：
- 改写器不用改。`main` 和 `subsdk0` 只有 x18 一类改写点；svc、计时器和线程寄存器只出现在 `sdk`。
- 卡比实际调用的 NVN 函数有 204 个，其中 D2R 已实现 151 个，需新增或复核 53 个。新增的大多是简单的 getter / setter 和调试标签，图形层的缺口比原先估计的小得多。
- 着色器的规模大：3554 个容器，19412 个 BNSH 头（未去重）。K06 一定要先去重。
- `main` 导入了 `glslc*`（NVN 的运行时 GLSL 编译器），但没有任何模块提供。已并入 K08：先做成返回失败的桩，同时要查清游戏会不会真的调用它。
- `sdk` 里有 27 个 svc 编号 D2R 没有分派，包括 0x21 SendSyncRequest（进程间通信）。这些归 H1 和 H4 处理，不要单独领。

---

## K01 完整解出 RomFS ｜ 低

**背景**：`tools/xci_extract.py` 已经能解 XCI、解出 ExeFS、列出 RomFS 清单，但还不能解出 RomFS 文件本身。

**要做**
1. 给 `xci_extract.py` 加 `--romfs-out <目录>` 参数，把程序 NCA（type=0）第 1 段 RomFS 的全部文件按原路径写出来。
2. 文件最大有几百 MB，要**分块流式读写**（比如每次 8 MB），不能整个读进内存。`NcaSection.read` 已经支持任意偏移读取（AES-CTR）。
3. 文件内容的位置：RomFS 头里的 `data_off` 加上文件条目里的偏移，都相对于 IVFC 第 6 级数据区（`romfs_list` 里的 `lvl_off`）。现有代码解析了这个偏移，但没有用上。
4. 打印进度。重跑时，已存在且大小相同的文件跳过。

**产出**：`D:\kirby-work\RomFS\...`；更新后的 `xci_extract.py`；报告 `reports/K01.md`。

**验收**
- 文件数是 9373，总字节数是 6,140,261,775。
- 抽查：`shader/` 下 `.bfsha` 的前 4 字节应为 `FSHA`；`mov/` 下 `.webm` 的前 4 字节应为 `1A 45 DF A3`；`msg/` 下 `.msbt` 应以 `MsgStdBn` 开头。
- 选做：校验 IVFC 哈希树（参考 `D:\swtich\脚本\bktr_extract.py` 的写法）。

---

## K02 导入符号表格化，对照 D2R 已接管的接口 ｜ 低

**背景**：`analysis/main.imports.txt` 和 `analysis/subsdk0.imports.txt` 是改编过的（mangled）符号名。D2R 宿主用 `HLE("名字", ...)` 和 `HLE_FORCE("名字", ...)` 登记它接管的接口（`dm/host/*.c`、`dm/gfx/**/*.c`，共约 200 个）。

**要做**
1. 装 LLVM（`scoop install llvm`），用 `llvm-cxxfilt` 还原符号名。
2. 用 `tools/imports.py` 出 `sdk` 和 `subsdk0` 的导出表（映像在 `D:\kirby-work\img\`）。
3. 生成 `analysis/imports_main.tsv` 和 `analysis/imports_subsdk0.tsv`，每个导入一行，列为：
   - `mangled`、`demangled`
   - `ns`：命名空间，取到二级，如 `nn::audio`、`nn::os::detail`；C 函数记为 `libc`
   - `defined_in`：在哪个模块的导出里能找到（`sdk` / `subsdk0` / `main` / `none`）
   - `d2r`：D2R 是否接管了它（`HLE` / `HLE_FORCE` / 空），以及在哪个文件
4. 反过来也查一遍：D2R 接管的每个名字，在卡比的 `sdk` 导出或 `main` 导入里存不存在。生成 `analysis/d2r_hle_in_kirby.tsv`，列为：名字、D2R 文件、卡比里存在与否。
5. 报告里写汇总：按 `ns` 统计数量；`defined_in=none` 的完整列表（这些导入没有任何模块提供，宿主必须给）；D2R 的接管能直接沿用多少个。

**验收**：所有 TSV 行数和导入数一致（main 707，subsdk0 657）；`defined_in=none` 的每一项都在报告里列出。

---

## K03 RomFS 文件格式摸底 ｜ 低

**背景**：游戏运行时自己会解压资源，所以搞清格式**不是**运行的前提。这一步是给 K06（找着色器）和以后的分析用的。

**要做**
1. 每种扩展名抽 3 到 5 个文件，记下开头 32 字节和可以认出的魔数。
2. 重点是 `.cmp`（7201 个，4.5 GB）和 `.cmpbin`（430 个）：判断压缩格式。依次试 zstd（魔数 `28 B5 2F FD`）、zlib、raw deflate、LZ4 block 和 frame、Yaz0、LZ77 变体；也看看头部有没有「解压后大小」字段。可以搜一下社区对这款游戏的资料（HAL 研究所的文件格式）。
3. 认出来以后，写 `tools/cmp_unpack.py <输入> <输出>`，并解几个样本，看里面是什么容器（`BFRES`、`SARC`、`BNTX`、`FSHA`、`BNSH`……）。
4. 统计全部 `.cmp` 解压后的内层魔数分布。只读头部，不用把文件全解到盘上。

**产出**：`docs/romfs_formats.md`（每种扩展名：用途猜测、魔数、压缩、内层格式）；`tools/cmp_unpack.py`；报告。

**验收**：`.cmp` 的格式确定下来，并说清楚哪些 `.cmp` 里面装着着色器（`FSHA` / `BNSH`）。

---

## K04 用改写器改写卡比的三个模块 ｜ 中

**背景**：`dm/tools/patch_nso.py` 是通用的改写器，要点见文件开头的注释。`DM_RULESET=native`（缺省）不带任何 D2R 专属的剪口（CUTS）和观测门（OBS）。等价核对工具在 `dm/verify/`：`structck.py` 做结构核对，`equiv.py` 用 Unicorn 逐处试验改写前后是否等价；用法见 `dm/verify/README.zh-CN.md`。`regress.sh` 是 bash 脚本，在 Windows 上照着它的内容直接调 Python 就行。

**要做**
1. `python -m pip install lz4 capstone unicorn`
2. 新建一个 `kirby/out/`（加进 `.gitignore`）或者用 `D:\kirby-work\patched\`，对 `main`、`sdk`、`subsdk0` 分别运行：
   `python dm/tools/patch_nso.py D:\kirby-work\exefs\<模块> <输出目录> <模块>`
   出错时，先确认是不是 D2R 的构建号断言或硬编码地址造成的。**不许改 `dm/tools/patch_nso.py`**：复制成 `tools/patch_nso_kirby.py`，只去掉 D2R 专属的部分，并在报告里逐条说明改了什么、为什么。
3. 对三个模块跑 `structck.py` 和 `equiv.py`。
4. 报告里写：每个模块各类改写点（x18、tpidr_el0、tpidrro_el0、svc 按编号、cntpct_el0）的数量，以及卡比用到了哪些 D2R 没用过的 svc 编号（D2R 的列表见 `dm/host/hle_svc.c` 和 `dm/host/hle_thread.c` 里的分派）。

**验收**：三个模块都改写成功，`structck` 和 `equiv` 全部通过。没通过的项逐条列出原因。

---

## K05 NVN 调用面 ｜ 中

**背景**：`dm/gfx/tools/nvn_slots.py` 用来找游戏主程序的 NVN 加载函数（按名字取地址，存进全局槽），再扫全部 .text，统计每个槽有多少调用点。D2R 实现了 216 个（`dm/gfx/nvn/nvn_slots.tsv`），其中 13 个是空实现（`dm/gfx/nvn/hle_nvn.c` 里的 `g_nops`）。卡比的 `main` 里有 524 个 `nvn*` 名字串。

**要做**
1. 复制成 `tools/nvn_slots_kirby.py`，把 `LOADER` 和 `TEXT_END` 改成参数。
2. 在卡比的 `main` 映像（`D:\kirby-work\img\main.img`）里定位加载函数：先找 `nvnDeviceInitialize` 这类名字串的地址，再找用 `adrp` + `add` 引用这些名字的那个大函数。
   注意：卡比走的是 nn::gfx 的 NVN 后端，取址的写法可能和 D2R 不同，比如存进结构体而不是全局槽，或者分成多个加载函数。要是模式对不上，就如实描述实际的代码形态（附反汇编片段），不要硬套。
3. 输出 `analysis/nvn_slots_kirby.tsv`（名字、槽、调用点数、首个调用点）。
4. 和 D2R 对比，输出 `analysis/nvn_new_needed.tsv`：卡比有调用点、D2R 没实现（或只是空实现）的函数，按调用点数排序，并按对象分组（Device、Queue、CommandBuffer、Texture、Sampler、Program、Memory、Sync……）。

**验收**：报告给出「卡比要用的 NVN 函数总数 / D2R 已实现 / 需新增」三个数，外加需新增的完整清单。

---

## K06 定位并提取着色器 ｜ 中

**背景**：D2R 的着色器翻译链（`dm/gfx/tools/dmsh_build.py`）以「控制段（0x880 字节）的 sha256 前 16 位十六进制」作为每个着色器的键，输入是 NVN 的控制段加机器码。卡比的着色器装在 nn::g3d 的 `.bfsha`（ResShaderArchive）里，里面嵌着 nn::gfx 的 `BNSH`（ResShaderFile），BNSH 的每个变体里才是 NVN 的控制段和机器码。

**要做**
1. 先读懂 `dmsh_build.py`、`shader_map.py`、`maxwell_refs.py`，弄清 D2R 是从什么样的输入里取控制段和机器码的。报告里写清楚输入格式。
2. 找出 RomFS 里所有着色器：`shader/*.bfsha` 的 24 个，加上 K03 的名单 `docs/cmp_shader_containers.tsv`（3554 个容器，偏移相对于解压后的内容）。解压用 `tools/cmp_unpack.py` 里的函数，在内存里解压后直接解析，**不要把 11 GB 解压结果落盘**。有 326 个容器只有 BNSH、没有 FSHA，这些要直接解析 BNSH。
   一共约 19412 个 BNSH 头，大量是重复的。按键去重后，每个键只写一份文件；`kirby_shaders.tsv` 里记下每个键第一次出现的位置和总出现次数。
3. 写 `tools/bnsh_extract.py`：解析 BFSHA 和 BNSH，把每个着色器变体的控制段和机器码写到 `D:\kirby-work\shaders\<键>.ctrl` 和 `<键>.code`。键的算法和 D2R 一致。
   格式资料：社区 Switch-Toolbox 的 BfshaLibrary 和 BNSH 解析代码、各类 nn::gfx 逆向资料。参考可以，**不要整段复制有许可证限制的代码**。
4. 输出 `analysis/kirby_shaders.tsv`，列为：键、阶段（顶点 / 片元 / 计算 / 几何 / 曲面细分）、来源文件、程序名或变体号、机器码大小。

**验收**：报告给出着色器总数、按阶段的分布、去重后的数量。**特别要说明有没有几何着色器和曲面细分着色器**：Metal 没有几何着色器，有的话得另想办法。

**主线程决定（2026-10-14，针对报告里 766 个控制段键冲突）**：同意 Codex 的方案，另加两条补充。
1. 键算法不变。无冲突的键写 `<键>.ctrl` 和 `<键>.code`；冲突的键写一份 `<键>.ctrl`，各份机器码分别写成 `<键>.<机器码 sha256 前 16 位>.code`。
2. **补充一**：`kirby_shaders.tsv`（或另出一张索引表）给**每一个**「控制段 + 机器码」组合都记一行，共 27239 行，列为：控制段键、机器码 sha256 前 16 位、文件名、首次出现位置、出现次数、是否冲突。无冲突的键也要记机器码哈希。原因：运行时最可能统一改用「控制段 + 机器码」的复合键（H3 决定），到时下游要能直接切换，不必重新提取。
3. **补充二**：报告里写清楚冲突那 766 个键里，两份机器码的差异落在哪里（指令区，还是机器码末尾的常数段 / c1），各给几个例子。这决定 H3 能不能只哈希指令区。
4. 运行时怎么区分这些着色器，归 H3，不要在 K06 或 K07 里实现。
5. 几何着色器（10 个键，出现 50 次）记下即可，归 H3 和 H4。

---

## K07 在 Windows 上搭着色器翻译链并试翻译 ｜ 中

**背景**：翻译链是 `dmsh_xlate`（C#，引用 `dm/third_party/ryujinx`）→ spirv-cross 1.4.357.0 → MSL。Windows 上没有苹果的 Metal 编译器，只做到 MSL 这一步。工具的安装和编译见 `docs/NO_XCODE.zh-CN.md`，干转工具是 `dm/gfx/tools/dmsh_dryrun.py`。

**要做**
1. 按 `docs/NO_XCODE.zh-CN.md` 编出 `dmsh_xlate` 和 spirv-cross 1.4.357.0。
2. 对 K06 提取出的着色器做干转（只看能不能翻）。D2R 的作答表 `shader_facts.tsv` 里没有卡比的键，翻译器问到这些事实时，先用合理的缺省值顶上（比如贴图一律按 2D、分量一律按 float），并把被问到的每一项都记下来。
3. 统计成功率；失败的按原因分类，每类给 1 到 2 个样本键。

**产出**：`analysis/xlate_dryrun.tsv`（键、阶段、成败、原因）；报告里写清楚作答表要回答哪些问题、各有多少项（交给主线程 H3 定方案）。

**验收**：全量干转跑完，有成败统计和失败原因分类。

---

## K08 离线服务桩 ｜ 低

**背景**：游戏完全离线运行。D2R 的做法见 `dm/host/hle_net.c`、`hle_friends.c`、`hle_misc.c`：联网类接口统一回「不可用」，让游戏走离线分支。卡比还导入了 `nn::bcat`（20 个）、`nn::prepo`（14）、`nn::nfp`（amiibo，11）、`nn::web`（9）、`nn::ssl`（2）、`nn::socket`（28，D2R 接管了 1 个），以及 NEX（静态链接在 `main` 里，底层走 socket）。

**要做**（依赖 K02 的表）
1. 列出上面这些命名空间里卡比导入的全部函数，加上 `nn::nifm`、`nn::nsd`、`nn::err`、`nn::pdm` 里 D2R 没覆盖的。
   另外加上 8 个 `glslc*` 导入（K02 报告中 `defined_in=none` 的那几个）：桩一律返回失败（`glslcInitialize` 返回 0，`glslcCompile*` 返回 0 且不写输出）。另外，在 `main` 映像里找出它们的调用点（PLT 或 GOT 的引用者），判断游戏是只在调试分支里调用，还是在正常路径里调用，在报告里说明。
2. 对每个函数，按 SDK 语义和 Switchbrew / Ryujinx 的资料，决定桩的行为：返回什么 Result 码、输出参数怎么填、是否需要假句柄。原则：
   - 让游戏认为「没联网 / 没有 amiibo / 功能不可用」；
   - 不要返回会让游戏 abort 的错误码；
   - 每条都要写依据。
3. 输出 `docs/offline_stubs.tsv`（函数、demangle 后的名字、行为、依据）。
4. 按 `dm/host/hle.h` 里 `HLE` 宏的写法起草 `host/hle_offline.c`。**卡比宿主会同时编入 D2R 的 `hle_net.c`、`hle_friends.c`、`hle_misc.c`、`hle_account.c`（见 `docs/host_split.md`）**，这几个文件已经接管的名字一律不能再写，否则强制接管表重名，装载时会中止。写之前先用 `analysis/d2r_hle_in_kirby.tsv` 把它们排除掉；判断某个强制接管会不会命中，以 `D:\kirby-work\reloc_refs.txt` 为准（生成方法见 `docs/host_split.md`）（被重定位引用的才会命中）。这个文件暂时编译不了（要 Mac），所以写完要对照 D2R 现有文件逐行核对宏的用法和类型。

**验收**：清单覆盖到上面所有命名空间的全部导入；C 草稿每个函数都有依据注释。

---

## K09 AudioRenderer 资料盘点 ｜ 中

**背景**：卡比用 NintendoWare Atk2 → nn::audio AudioRenderer。真机上，游戏每帧把一大块「参数更新」交给系统里的音频渲染服务，由服务按命令混音输出。宿主没有这个服务，得自己实现，参考 Ryujinx 的 `Ryujinx.Audio`（MIT 许可，Ryubing 维护的版本，和 `dm/third_party/ryujinx` 同为 1.3.2 版本线）。

**要做**
1. 取得 Ryujinx 1.3.2 的 `src/Ryujinx.Audio` 源码，原样放进 `dm/third_party/ryujinx/src/Ryujinx.Audio/`（仅此一处允许改 `dm/`：新增第三方目录），README 里注明来源和版本。
2. 用 K02 的表列出卡比导入的全部 `nn::audio` 函数（约 128 个），逐个对应到 Ryujinx 里的实现位置，或说明是纯客户端逻辑（SDK 自己算，不经服务）。
3. 确定卡比用的 AudioRenderer 修订版本：在 `sdk` 映像里找 `REV` 魔数（形如 `52 45 56 3x`），对照 Ryujinx 的 `BehaviourContext` 修订表，列出这个修订打开了哪些功能。
4. 写 `docs/audio_renderer.md`，内容包括：
   - 参数更新的数据布局（头部和各段：内存池、语音、混音、效果器、Sink、Splitter、性能）；
   - 卡比可能用到的 DSP 命令列表；
   - 按 K10a、K10b、K10c 拆好的移植范围，每块对应 Ryujinx 的哪些文件。

**验收**：K10 的三个任务能直接按这份文档开工。

---

## K10a、K10b、K10c AudioRenderer DSP 移植 ｜ 中

**背景**：把 Ryujinx 的 DSP 部分（`Ryujinx.Audio/Renderer/Dsp/`）移植成纯 C 放进 `kirby/audio/`。纯 C 不依赖苹果，Windows 上用 scoop 装的 gcc 就能编译和测试。

**分工**（具体范围以 K09 的文档为准）
- **a**：PCM16 / PCM float / ADPCM 解码，重采样器（ResamplerHelper），数据源命令。
- **b**：混音（Mix、MixRamp）、音量与渐变、双二阶滤波（Biquad）、Upsample、Depop。
- **c**：效果器（Delay、Reverb、Reverb3d / I3DL2、Aux、Limiter、Compressor），只做 K09 判定卡比会用到的。

**做法（金标准对拍）**
1. 先在 `kirby/audio/golden/` 写一个小 C# 控制台程序，直接引用 Ryujinx 的 DSP 类，用固定的随机输入生成参考输出，存成二进制文件。生成的数据放进 `D:\kirby-work\audio_golden\`，不进 git。
2. C 实现读同样的输入，输出必须和参考**逐样本一致**。浮点路径允许 1e-6 以内的误差，并在报告里说明。
3. 风格：C11，不用全局可变状态，每个命令一个函数，状态结构体由调用方持有。文件开头注明「移植自 Ryujinx（MIT），对应哪个源文件」。

**验收**：`make -C kirby/audio test`（或一个 `test.ps1`）全部通过；报告列出已移植的命令和对拍的样本数。

---

## 主线程任务（不要领）

- **H1**：把 `dm/host` 拆成通用宿主和 D2R 专属功能（`dm_*.c`、`hle_tact.c`、`hle_granny.c`、触屏界面），给卡比单独做构建配置。
- **H2**：AudioRenderer 的参数更新解析和命令生成，接到宿主的 CoreAudio 输出上。
- **H3**：着色器作答表的方案。D2R 的表是按键人工和半自动维护的，卡比的着色器多得多，可能要改成在运行时记录事实、再离线回填。
- **H4**：在 Mac 上启动调试：装载、SDK 初始化、`nnMain`、逐个补未实现的接口、第一帧。

## 用户的待办

- **U1**：开发设备。Apple 芯片的 Mac（M1 起，能装 macOS 26）可以本机跑、挂调试器，迭代最快；只有 Intel Mac 的话，每一轮都要装到 iPhone 上测。
- **U2**：版本。现在只有 v1.0.0；如果打算玩更新后的版本，尽早从主机导出更新包，因为 K04、K05 的结论都和构建号绑定。
