# 这份卡比移植从哪来

代码不是凭空写的，底座是 [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) —— 《暗黑破坏神 II：狱火重生》的 iOS 兼容层（MIT，版权所有 (c) 2026 jhaizhou）。它提供了：

- `dm/host`：通用宿主（装载器、HLE 表、线程、内存、崩溃处理、前端）
- `dm/gfx`：NVN → Metal 的完整重写，以及着色器回放层
- `dm/ios`：出包、嵌入游戏模块、Metal 工具链脚本

这些文件**不在本仓库里**，本仓库只放卡比相关的部分（宿主入口、卡比专属 HLE、iOS 外壳、工具、分析表和报告）。构建时按原样编上游的那些文件，见 `host/Makefile` 开头的说明。

## patches/d2r-mobile-kirby.patch

卡比能跑起来，上游有 11 个文件需要小改（约 670 行）：图形侧的整数顶点属性 / 整数颜色附件 / 管线与贴图细节 / 异步建管线 / 深度钳制 / 贴图重新初始化不泄漏，宿主侧的文件、音频、时间、装载与 HLE 注册。补丁基于上游提交 `6a2474d`（2026-10-13）生成，套不上之后的 `main`（上游又改过其中 6 个文件）：

```bash
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile checkout 6a2474d
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch
```

补丁里的改动同样以 MIT 发布；上游代码的版权仍归 jhaizhou。改了 `dm/` 之后重新生成：`git -C ../d2r-mobile add -N dm && git -C ../d2r-mobile diff 6a2474d -- dm > patches/d2r-mobile-kirby.patch`。如果上游以后合并了这些改动，补丁可以删掉。

## patches/d2r-mobile-kirby-fae8c68.patch（跟上上游 main，准备好了、没验过）

2026-11-04 把上面那份补丁移到上游 `main` 的 `fae8c68`（2026-11-02，比 `6a2474d` 多 99 个提交，加了安卓 / 鸿蒙、翻译线程、录制副本等）。现在的构建仍用上面那份；这份是给换基线时用的：

```bash
git -C ../d2r-mobile checkout fae8c68
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby-fae8c68.patch
```

- 三方合并后 10 处冲突逐处手工合：`hle_audio.c`（上游换成跨平台声卡句柄，保留卡比要的 `played` 计数）；`hle_nvn.c`（上游的录制副本 + 卡比的贴图重新初始化：沿用序号时沿用副本，换序号时让旧副本退役；提交线程标记挪到上游新的提交入口；卡比的 NVN 接口块去掉上游已实现的 `SetCopyImageStride`）；`nvn_mtl.m`（池查找的最近命中缓存加上游的驻留登记；着色器对象两边的字段都要；深度钳制接进上游新的「同值不重设」：`FX_SET` 第 8 位；绕序照旧按 GL 原点翻；出图管线照旧带 sRGB 重编码）。
- 着色器链版本没变（`15cf7ca29eaf`），现有 `shlib*` 能接着用。
- 核过的：在干净的 `fae8c68` 上套用；`tools/syntax_check.py` 结果和 `6a2474d` 上一样（新多出的 `hle_audio.c` 失败只是替身头缺 CoreAudio 声明）；`tools/link_scan.py` 0 个缺符号（新调到的 16 个 D2R 专属函数已在 `host/kirby_glue.c` 补了中性实现，`6a2474d` 上没人调、无害）。
- **没验过的**：Mac 上编译链接、跑游戏；上游的改写器 `dm/tools/patch_nso.py` 改了 190 行（多是安卓 / 鸿蒙，也有通用的栈帧处理），换基线要重跑 K04、看 `main` / `sdk` / `subsdk0` 的改写结果和现在是不是逐字节一样，不一样就重跑结构 / 等价核对；上游的翻译线程、录制副本对卡比的影响（性能、正确性）要实测。全部过了再把 `docs/build.md`、`tools/setup_mac.sh` 里的 `6a2474d` 换掉。

## 不含游戏内容

本仓库和补丁都不含游戏程序、美术、文字、数据表或着色器。`analysis/`、`docs/` 里只有符号名、地址、键、尺寸、计数这类为互操作而记录的事实。
