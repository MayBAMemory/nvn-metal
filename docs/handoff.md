# 换一台电脑接着做

git 里只有代码。下面这几样要另外准备或从旧电脑拷过来。

## 1. 代码

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

## 2. 本机工具

| 要什么 | 做什么用 | 怎么装 |
|---|---|---|
| macOS + Xcode 26（用过 26.5） | 图形层用到 Metal 4，要 26 版 SDK | App Store；装完开一次 Xcode 同意许可 |
| xcodegen | `ios/run.sh build` 生成工程 | `brew install xcodegen` |
| Metal 工具链 | 编 iOS 版 metallib | `xcodebuild -downloadComponent MetalToolchain`（约 690 MB） |
| Python 3 + `capstone lz4 zstandard unicorn` | `tools/` 里的分析和改写核对；单纯出包装机只用标准库 | `python3 -m pip install capstone lz4 zstandard unicorn` |
| Xcode 里登录 Apple ID | 签名 | Xcode → Settings → Accounts |

- Intel Mac 和 Apple 芯片的 Mac 都能出包、装机，游戏在手机上跑。只有想在 Mac 上直接跑 `host/kirbyhost` 才要 Apple 芯片。
- 从零重出着色器库（`shlib/`）原来用 Windows 那套：.NET SDK 10、SPIRV-Cross、ninja、LLVM，见 [reports/K07.md](../reports/K07.md)、[docs/shaders.md](shaders.md)。现在 Mac 上也能做，见 README 的「[从自己的游戏导出开始，全在 Mac 上从头重做](../README.md#从自己的游戏导出开始全在-mac-上从头重做不用-windows)」。拷旧电脑的 `shlib/` 就用不上它们。

## 3. 工作区 `~/kirby-work`（不进 git，从旧电脑拷）

用移动硬盘：`rsync -a --exclude build --exclude run ~/kirby-work/ /Volumes/<盘>/kirby-work/`，到新电脑再拷回 `~/kirby-work`。放在别处的话，在 `ios/local.env` 里设 `KIRBY_WORK`。

| 内容 | 大小 | 必须吗 | 说明 |
|---|---|---|---|
| `patched/` 里 `main`、`sdk`、`subsdk0` 各三个文件：`.patched.bin`、`.fixups.tsv`、`.gates.tsv` | 34 MB | **必须** | 改写后的游戏模块，`build` 直接嵌进 App |
| `shlib_ios/`（`shader_map.tsv` 和 `b-15cf7ca29eaf/`） | 462 MB | **必须**（或者拷 `shlib/` 再重编） | 编好的 iOS 版 metallib。没有它就要跑 `ios/run.sh shaders`，从 `shlib/` 重编约 1.7 万个，要很久 |
| `shlib/` | 512 MB | 建议 | `ios/run.sh shaders` 的输入（Windows 出的着色器库）。补编整数变体时要用 |
| `pso_manifest.tsv` | 1 MB | **必须** | 真机记下的管线清单（约 2000 条），`build` 带进 App，启动时后台预编，走动少卡顿 |
| `uint_masks.tsv`、`iattr_masks.tsv` | 很小 | **必须** | 真机记下的整数输出 / 整数顶点属性变体，`ios/run.sh shaders` 照着补编；丢了会重新出现模型拉伸 |
| `RomFS/` | 5.7 GB | 看情况 | 手机上的 App 里已经有一份。只要新电脑用**同一个包名、同一个 Apple ID** 覆盖安装就不用再推；换包名或删过 App 才要 `ios/run.sh romfs` |
| `exefs/`、`img/` | 43 MB | 可选 | 原始模块和映像。重跑改写（`dm/tools/patch_nso.py`）、等价核对和 `tools/` 的分析时才要 |
| `run/` | — | 不用 | 旧日志和截图 |
| `build/` | 1.4 GB | 不用 | 每次 `build` 重新生成 |

完全没有旧工作区时，从自己的游戏导出重做一遍：K01（`tools/import_dump.py` 导入 ExeFS / RomFS）→ K04（改写三个模块）→ K06（提取着色器）→ K07 / H3（Windows 上翻译出库）→ `ios/run.sh shaders`。命令都在各自的报告里。这样做真机记下的三份清单也就没了。

## 4. 手机和签名

- 手机：iPhone 16 Pro Max，iOS 27.2。App 叫 Kirby Host，包名 `io.github.maybamemory.kirbyhost`。RomFS 已经推进 App 容器的 `Library/Application Support/romfs`。
- `ios/local.env` 不进 git：从旧电脑拷，或者照 `ios/local.env.example` 重填。旧电脑上这份在 `~/nvn-metal/ios/local.env`（更早在 `~/d2r-mobile/kirby/ios/local.env`）。
  - `KIRBY_BUNDLE_ID` 沿用 `io.github.maybamemory.kirbyhost`，这样新电脑装上去是覆盖安装，RomFS 和存档都在。
  - `KIRBY_TEAM_ID`：Xcode 登录同一个 Apple ID 后的团队号。换了 Apple ID，团队号就变了，覆盖装会被拒，只能删掉重装、重推 RomFS。
  - `KIRBY_DEVICE`：`xcrun devicectl list devices` 的 Identifier 列，每台电脑看到的可能不同，要重查。
  - `KIRBY_FREE_ACCOUNT=1`：现在用的是免费 Apple ID。描述文件 7 天过期，过期要重新 `build` + `install`；每台手机最多 3 个自签 App。
- 新电脑第一次签：Xcode 会给这台电脑生成新的开发证书。手机要连着，自动签名才会把设备登记进描述文件。

**以后换成付费开发者账号**：把 `KIRBY_FREE_ACCOUNT` 清空。这样会带上「提高内存上限」「扩展虚拟寻址」两项权限，卡比内存紧（见 [README](../README.md) 的「iPhone 端进度」），这两项很要紧。
