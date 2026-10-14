# kirby/ios — 卡比的 iOS App

把卡比宿主（`kirby/host` + 共用的 `dm/host`、`dm/gfx`）和改写后的三个模块装进一个 iPhone App。做法和 D2R 的 `dm/ios` 相同，原理先读 [../../dm/ios/README.zh-CN.md](../../dm/ios/README.zh-CN.md)「构建时把什么装进 App」。这里只写卡比不同的地方和怎么用。

**Intel Mac 也能用**：出包、签名、装机都在 Mac 上做，游戏在手机上跑，Mac 是什么芯片无关紧要。

## 组成

| 文件 | 内容 |
|---|---|
| `Sources/main.m` | 入口：日志写到 `Documents/boot.txt`，找 RomFS，设宿主的环境变量，场景到前台后在 64 MB 栈的线程上调 `dm_main`（`kirby_main.c` 以 `-Dmain=dm_main` 编入） |
| `Sources/front_ios_kirby.m` | `front.h` 的 iOS 实现：16:9 居中的 Metal 视图、触屏、+/− 小钮、屏幕手柄、生命周期 |
| `tools/gen_build.py` | 从 `kirby/host/Makefile` 展开的编译命令取源文件和头路径（改自 `dm/ios/tools/gen_build.py`） |
| `tools/shlib_ios.py` | 把 Windows 上出的着色器库编成 iOS 版 metallib（编译参数用 `dm/ios/tools/ios_metal.py` 那一份） |
| `run.sh` | 出包 / 编着色器 / 装机 / 推 RomFS / 启动拉日志 |
| `project.yml` | XcodeGen 工程描述；工程文件和 `Gen/` 不进仓库，每次构建重新生成 |

模块内嵌直接用 D2R 的 `dm/ios/tools/gen_embed.py`，没复制。

和 D2R 的 App 相比，下面这些一概不要：触屏操作界面（摇杆、技能钮、面板点击）、资源下载、覆盖层、iCloud 存档、补充包、测试探针。

**操作**：
- 有实体手柄（Xbox、PS、Switch Pro 等）就用它，连上即用。
- 没有的话，屏幕上会显示系统自带的屏幕手柄：左摇杆，A/B/X/Y，L/R。
- 右上角有 + / − 两个小钮。

## 准备（一次）

1. 装工具：`brew install xcodegen`，`xcodebuild -downloadComponent MetalToolchain`（约 690 MB）。
2. 从 Windows 拷游戏数据到 `~/kirby-work/`（或在 `local.env` 里用 `KIRBY_WORK` 指到别处）：

   | Windows | Mac | 大小 |
   |---|---|---|
   | `D:\kirby-work\patched\` 里的 `main/sdk/subsdk0` 各三个文件：`.patched.bin`、`.fixups.tsv`、`.gates.tsv` | `~/kirby-work/patched/` | 几十 MB |
   | `D:\kirby-work\shlib\`（`shader_map.tsv` 和 `b-<V>\`） | `~/kirby-work/shlib/` | |
   | `D:\kirby-work\RomFS\` | `~/kirby-work/RomFS/` | 6.1 GB |

   不用拷密钥、XCI 和 ExeFS。
3. 签名：Xcode → Settings → Accounts 里登录 Apple ID。然后把 `local.env.example` 复制成 `local.env`，填好这几项：
   - `KIRBY_TEAM_ID`：团队号。
   - `KIRBY_BUNDLE_ID`：自己起的包名。
   - `KIRBY_DEVICE`：手机的设备标识，用 `xcrun devicectl list devices` 查。
   - `KIRBY_FREE_ACCOUNT`：免费账号填 1。

## 使用

```bash
kirby/ios/run.sh shaders          # Windows 的着色器库 → iOS metallib（约 1.7 万个，第一次要一阵子；以后只编变了的）
kirby/ios/run.sh build            # 出包
kirby/ios/run.sh install          # 装到手机
kirby/ios/run.sh romfs            # 推 RomFS（只需一次；App 先装过才有容器）
kirby/ios/run.sh run 300          # 启动，并把手机上的 Documents/boot.txt 持续拉回来显示
kirby/ios/run.sh log              # 只拉一次日志
```

没有游戏文件时：`KIRBY_NO_GAME=1 kirby/ios/run.sh build` 只编外壳和宿主，用来检查编译、链接和签名。这样出的包装上也起不来。

RomFS 也可以不用 `run.sh romfs`：手机连 Mac，在 Finder 里选中手机 →「文件」→ 把整个 RomFS 文件夹拖进 Kirby Host。App 会在文稿目录下 3 层以内找根目录里有 `gfx/` 和 `shader/` 的文件夹。

## 已知限制和坑

- **构建按 target，不给 `-destination`**：Xcode 26 给任何真机或通用目标构建都要先装「iOS 平台」组件，也就是模拟器运行时，约 8 GB，否则报 `iOS 26.5 is not installed`。按 target 加 `-sdk iphoneos` 构建不查这一项，产物一样。
  实测（2026-10-14，免费个人团队）：按 target 构建、自动签名时，Xcode 照样把连着的手机登记进了描述文件，不用另外处理。如果以后报「团队里没有设备」，在 Xcode 里打开 `KirbyIOS.xcodeproj`，对着手机构建一次就能登记。
- **免费账号**：签不了「提高内存上限」和「扩展虚拟寻址」两项权限。D2R 不带这两项也能跑；卡比够不够要实测。另外，描述文件 7 天过期，过期了要重新 build、install。
- **手机系统比 Xcode 新**：手机 iOS 27.2、Xcode 26.5，实测装机、启动、拉日志都正常。
- **着色器**：`shaders` 报失败的，记在 `~/kirby-work/shlib_ios/b-<V>/failed.tsv`，其余照常可用。运行时用到编不过的着色器，日志里会有「【红】加载着色器失败」。
- **还没接**：
  - alpha 测试和几何着色器（`docs/shaders.md`，留给 H4）。
  - 过场动画只报 0 条轨道（`hle_movie_kirby.c`）。
  - 进后台只做了暂停音频和 GPU。
