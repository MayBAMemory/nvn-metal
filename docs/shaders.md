# H3 卡比的着色器库

2026-10-14，主线程。

## 一句话

卡比的着色器**按机器码定键**，从 K06 提取的结果直接出成 D2R 回放层能加载的「B 路线」库：**16985 份**，全部生成成功。其中 16936 份来自 RomFS，49 份是 `main` 程序里内嵌的。几何着色器（10 份）跳过。

## 怎么出库

```powershell
$env:DOTNET_ROOT = "C:\Users\hy367\scoop\apps\dotnet-sdk\current"
python -I tools/main_shaders.py D:\kirby-work\img\main.img D:\kirby-work\shaders   # main 内嵌的 49 份，只需跑一次
python -I tools/kirby_shlib.py --shaders D:\kirby-work\shaders --out D:\kirby-work\shlib
```

约 10 分钟。产物：
- `D:\kirby-work\shlib\b-<V>\<键>_b.{metallib,metal,json}`
- `shader_map.tsv`
- `build.tsv`

`V` 取自 `dm/gfx/nvn/dmsh_chain.h`。这个版本号一变（例如在 Mac 上跑 `setup.sh` 时按本机工具重新登记），就要重新出库。

运行时把宿主指过去：`DM_NVN_SHMAP=<out>/shader_map.tsv`、`DM_MSL_DIR=<out>`。

## 和 D2R 的不同

| 项 | D2R | 卡比 | 依据 |
|---|---|---|---|
| 键 | 控制段 0x880 字节的 sha256 前 16 位 | **机器码 `[0, ctrl+0x704)` 的 sha256 前 16 位** | 全部 27239 个组合核对过：`ctrl+0x704` 等于机器码长度，哈希等于 K06 的 `code_hash`。翻译只看机器码（SPH、指令、c1 都在里面），所以同一份机器码只出一份：27239 → 16936。这样也绕开了 766 个控制段键冲突，以及 `SetShadersExt` 在运行时改写控制段的问题 |
| 运行时算键 | `nvn_mtl.m` 的 `prog_get` | 同一处，新增 `shader_key_of()`：`dm_game_d2r()` 为 0 时按机器码算。图形单测不链宿主，用弱定义，按 D2R 处理 | |
| 贴图句柄 | 分离表 `c[2][0x168 + 8r]` / `[0x568 + 8r]` | **组合表 `c[2][0x20 + 8r]`（32 槽）**，采样器号取同一句柄的高位（回放层已有的 `samp_tex`） | K07：61466 处引用全部落在这里 |
| 作答表 | `shader_facts.tsv`（按 PC 版反射人工维护） | **不需要** | K07：翻译器没有问过贴图类型和格式 |
| 计算着色器的线程组 | 取自 PC 版反射 | **`ctrl+0x748/0x74c/0x750` = X/Y/Z，`ctrl+0x754` = 共享内存字节数** | 对比全部 72 个计算控制段得出：主要是 (32,1,1)，另有 (8,5,1)、(8,5,4)；共享内存 0x280 到 0x680。53 个计算着色器用到共享内存，K07 试翻译时按 0 处理是错的，正式出库已改正 |
| 翻译选项 | `debug` 开、`reduced` 开 | 相同 | 开 `debug` 顺带解决了 K07 里那 3 个顶点着色器的翻译器异常 |
| `main` 内嵌着色器 | 4 个计算核，手写 MSL（`nvn_builtin.h`） | `main` 里 3 个 BNSH 容器（`main+0xa07000`、`+0xa17000`、`+0xa1f000`），49 个新组合，**直接走同一条翻译链** | `tools/main_shaders.py`，索引在 `analysis/kirby_shaders.main.tsv` |
| 没有 Metal 编译器 | `nometal_run.py`：库文件里放源码，设备上现编 | 同样做法 | |

## 还没做 / 已知缺口

1. **alpha 测试**（卡比 4 个调用点）。计划：出库时给每个片元着色器末尾注入一段用 Metal function constant 控制的 alpha 测试（`[[function_constant]]` 传比较函数和参考值；函数为 Always 时编译器直接删掉这段代码）。回放层建片元函数时，按当前 `color_st.alpha_func` 和 `SetAlphaRef` 的值传常量，并把这两项加进管线缓存键。要同时改出库脚本和 `nvn_mtl.m`，放到 H4 能运行验证时再做。另一种做法是用翻译器自带的 `AlphaTestOp` 出变体。
2. **几何着色器**：10 份，只出现在「塞满变形」的倾卸车和电梯两种形态（`gfx/Actor/Chara/Kirby/DeformKirby/{DumpCar,Elevator}`）。spirv-cross 不能把它们转成 MSL。运行时这类程序会报「不在映射表里」。要先确认少了几何阶段之后，管线还能不能建起来（顶点着色器的输出要和片元着色器的输入对得上）。
3. **苹果编译器没验过**：在 Windows 上只走到 MSL。哪份 MSL 编不过，要到设备上现编、或者在装了 Xcode 的 Mac 上编时才知道。
4. **`c1_full.tsv`**（c1 常数段下标是否有界）没有生成。回放层找不到这个文件时，会保守地给每个着色器的 c1 单独分配 64 KiB（功能正确，只是多占内存）。以后用 `dm/gfx/tools/c1_guard.py` 补上。
5. **预热清单 `pso_manifest.tsv`**：没有。第一次运行时着色器和管线都要现建，以后可以用 `DM_PSO_RECORD` 录一份。
