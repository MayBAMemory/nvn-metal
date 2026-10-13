# 卡比的图形层：新增接口与遗留事项

2026-10-14，主线程。

## 已做（`dm/gfx/nvn/hle_nvn.c` 末尾「卡比经 nn::gfx 用到」一节）

K05 列出的 53 个函数中，有 46 个已经写好，另外 4 个 D2R 本来就是空实现。分组如下：

- **缓冲对象**（10 个）：`nvnBuffer*`、`nvnBufferBuilder*`。新增对象布局 `buf_t` / `buf_builder_t`（`nvn_obj.h`，有 FITS 检查），GPU 地址等于 CPU 地址。
- **取属性、调试标签、内存池映射、设备句柄**：都是简单实现。组合句柄的写法和 `GetSeparate*Handle` 一致：贴图号 | 采样器号 << 20。
- **`SetShadersExt`**：数组每项 0x18 字节，依据是 `main+0x51d284`。取前两项，转成普通的 `SetShaders`。
- **`DrawArraysInstanced`**：`p_draw` 加了 `instances` 和 `base_instance` 两个字段，回放层（`nvn_mtl.m` 的 `draw()`）在实例数不为 0 时走实例化绘制。D2R 的记录里这两个字段为 0，行为不变。
- **`DrawElementsBaseVertex`**、**`BindTexture` / `BindImage` / `BindVertexBuffer`（单个）**、**`CallCommands`**：沿用现有的命令格式。
- **只改 GPU 固定功能、回放层还没有对应的**（深度钳制、光栅化丢弃、保守光栅化、采样掩码、线宽、拷贝图像跨距、逻辑运算、取最小 / 最大值的采样器）：遇到非缺省值时报红一次，运行时日志里能看到卡比到底用没用。
- **视图的通道重排和深度模板模式**：已经记进 `view_t`（新增字段，默认 0），回放层还没用上。

**还没做，被调用时报红并给出名字**：`nvnCommandBufferDownsample`、`nvnTextureWriteTexels`、`nvnTextureFlushTexels`。原因：`WriteTexels` 要把线性数据按块线性排布写进游戏内存，目前只有反向的解排布实现。

以上全部**没有编译过**。验证方法见 `docs/host_split.md` 文末。

## 留给 H3（着色器）的

1. **alpha 测试**：卡比有 4 个调用点。Metal 没有固定功能的 alpha 测试，要按 `color_st.alpha_func` 和 alpha 参考值，在片元着色器末尾补一个 `discard`。做法有两种：出着色器变体，或者用常量缓冲传函数和参考值。`SetAlphaRef` 目前是空实现。
2. **`SetShadersExt` 会改控制段**：nn::gfx 在调用之前，会把一段数据 `memcpy` 进控制段（`main+0x51d2bc` 到 `+0x51d32c`，按控制段里 `+0x14`、`+0x1c`、`+0x6f0`、`+0x7c8`、`+0x7e8` 这几处的偏移和长度）。所以**这类着色器运行时的控制段，和 K06 从资源文件里提取到的不一样**，按控制段算出的键会对不上。H3 定键方案时必须考虑这一点：要么只哈希不会被改写的部分，要么离线时也把同样的改写做一遍。
3. **K06 的 766 个控制段键冲突**：见 TASKS.md 的 K06 决定。
4. **几何着色器**（10 个键）：Metal 没有几何着色器阶段。

## 留给 H4（运行时）的

- 通道重排要在回放层建贴图视图时生效（`MTLTextureSwizzleChannels`）；深度钳制可以用 `setDepthClipMode`。看到对应的报红再接。
- `GetCommandMemoryUsed` 是近似值（本次录制已用的字节数）。如果游戏拿它来决定什么时候补命令内存，要再核对。
