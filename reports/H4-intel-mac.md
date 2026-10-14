# H4-mac：Intel Mac 上的编译 / 链接检查

2026-10-14，Intel Mac 上的 Claude。按 `kirby/docs/intel_mac.md` 的步骤执行。

## 结论

**卡比宿主 `kirbyhost` 按 arm64 编译 + 链接一次通过：0 个编译错误，0 个链接错误，源码一行没改。** H1 拆分时用正则推出来的依赖关系是对的：`kirby_glue.c` 的垫片已经齐全，没有缺符号，也没有重复定义。

## 环境

| 项 | 值 |
|---|---|
| 系统 | macOS 26.7.1（25G241），Intel x86-64 |
| Xcode | 26.5（17F42），SDK 26.5 |
| 编译器 | Apple clang 21.0.0（clang-2100.1.1.101） |

## 复现

```bash
make -C kirby/host check                  # 退出码 0，0 error，32 warning
make -C kirby/host ARCH="-arch arm64"     # 退出码 0
file kirby/host/kirbyhost                 # Mach-O 64-bit executable arm64，约 1.0 MB
```

`check` 会逐个检查 49 个源文件（`.S` 除外），其中包括 Windows 上的 `syntax_check.py` 查不到的 `.m` 文件：`nvn_mtl.m`、`nvn_refl.m`、`nvn_deswizzle_gpu.m`、`front_mac.m`、`app_mac.m`、`memstat.m`、`dm_gamepad.m`。

## 额外做的检查

1. **HLE 名字有没有重复**（`docs/host_split.md` 的风险里提到，链接器查不出来，装载时才会中止）：把参与构建的 49 个文件里的 `HLE("…")` / `HLE_FORCE("…")` 全部扫了一遍，共 314 处登记，名字两两不同。K08 的 `hle_offline.c` 和 D2R 的 `hle_net.c`、`hle_friends.c`、`hle_misc.c`、`hle_account.c` **没有重名**。
2. **D2R 的文件改坏了没有**：`make -C dm/host` 要用游戏文件生成表头（`dm_vault_tab.h`、`dm_hud_exp.h`、`dm_bright_levels.inc`），这台机器上没有，所以编不了，intel_mac.md 也不要求编过。我改成对 D2R 专属文件（`main.c`、`hle_hid.c`、`hle_movie.c`、`dm_*.c` 等）逐个做 `-arch arm64 -fsyntax-only`。只有 `diag.c`、`dm_bright.c`、`dm_vault_ui.c` 三个文件报错，而且都是缺上面这几个生成出来的表头。定义 `dm_game_d2r()` 的 `main.c` 能通过。共用文件（`hle_oe.c`、`hle_svc.c`、`hle_time.c`、`hle_fs.c`、`loader.c`）在 kirbyhost 的构建里已经编过了。

## 剩下的警告（没改，说明原因）

32 个警告，31 个在 `dm/gfx/nvn/nvn_mtl.m`，1 个在 `dm/host/hle.c`（`g_cache` 没用到）。都是 D2R 共用代码，按规矩不改行为，也不为了消警告去碰它：

- **Metal 4 和旧 Metal 编码器的协议类型混用**（约 28 个，如第 543、1469、2673、4362–4511 行）：同一个变量有时声明成 `id<MTLRenderCommandEncoder>`，传进去的却是 `id<MTL4RenderCommandEncoder>`。这是 D2R 的有意写法：两种路线共用一套 `enc_*` 包装，Objective-C 运行时按对象的真实类分派消息，类型只影响编译期检查。D2R 在 Apple 芯片上就是这样跑的。
- **第 2130 行** `NSString *zh = g_shmap[k]`：`g_shmap` 的泛型标注写成了 `NSArray *`，但第 1866 行实际存进去的是字符串（`c[2]`），所以运行时没有问题。这个问题从 D2R 首个公开版本就有，错的是标注。
- **第 783、791、2118、2464 行**：返回类型上的 `__unsafe_unretained` 被 ARC 忽略，不影响运行。

## 还剩什么

- 没有运行过。Intel Mac 执行不了 arm64 的 `kirbyhost`（Rosetta 只能把 x86-64 翻成 arm64，反过来不行），而且宿主是直接执行游戏的 aarch64 代码。下一步要在 Apple 芯片 Mac 上按 `docs/host_split.md` 文末「验证」第 2 条去跑。
- D2R 的完整构建（`make -C dm/host`）要有 D2R 的游戏文件才能编，这台机器上验证不了。
- `kirby/.gitignore` 加了 `host/kirbyhost` 和 `host/*.dSYM/`，构建产物不会被误提交。
