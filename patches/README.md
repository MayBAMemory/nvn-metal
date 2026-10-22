# 这份卡比移植从哪来

代码不是凭空写的，底座是 [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) —— 《暗黑破坏神 II：狱火重生》的 iOS 兼容层（MIT，版权所有 (c) 2026 jhaizhou）。它提供了：

- `dm/host`：通用宿主（装载器、HLE 表、线程、内存、崩溃处理、前端）
- `dm/gfx`：NVN → Metal 的完整重写，以及着色器回放层
- `dm/ios`：出包、嵌入游戏模块、Metal 工具链脚本

这些文件**不在本仓库里**，本仓库只放卡比相关的部分（宿主入口、卡比专属 HLE、iOS 外壳、工具、分析表和报告）。构建时按原样编上游的那些文件，见 `host/Makefile` 开头的说明。

## patches/d2r-mobile-kirby.patch

卡比能跑起来，上游有 11 个文件需要小改（约 520 行）：图形侧的整数顶点属性 / 整数颜色附件 / 管线与贴图细节，宿主侧的文件、音频、时间、装载与 HLE 注册。补丁基于上游 `main` 分支生成：

```bash
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch
```

补丁里的改动同样以 MIT 发布；上游代码的版权仍归 jhaizhou。如果上游以后合并了这些改动，补丁可以删掉。

## 不含游戏内容

本仓库和补丁都不含游戏程序、美术、文字、数据表或着色器。`analysis/`、`docs/` 里只有符号名、地址、键、尺寸、计数这类为互操作而记录的事实。
