# 从自己的游戏导出开始构建

命令都在仓库根目录下跑。

## 需要什么

1. **你自己购买的 Switch 版《星之卡比 探索发现》v1.0.0**（Title ID `01004D300C5AE000`），以及从它导出的 ExeFS 和 RomFS（已解密的目录）。仓库不负责解密：用外部工具导出，例如在你自己的 Switch 上用 nxdumptool。
2. **上游 d2r-mobile 的一份检出，固定在提交 `6a2474d`**：宿主编译的是它的 `dm/host` 通用部分和整个 `dm/gfx`（原样编，不复制），这些文件不在本仓库里。
3. **卡比要用的上游改动**：`patches/d2r-mobile-kirby.patch`（11 个文件、约 630 行，见 [patches/README.md](../patches/README.md)）。补丁是对 `6a2474d` 做的，**套不上上游最新的 `main`**（2026-10-22 核对：上游之后改过其中 6 个文件，补丁冲突）。
4. Mac 上有 Xcode；iOS 出包还要 `brew install xcodegen` 和 `xcodebuild -downloadComponent MetalToolchain`。

```bash
# 上游放在本仓库的兄弟目录，脚本默认就在这儿找；放在别处就设 D2R_MOBILE 指到 d2r-mobile 的根
git clone https://github.com/jhaizhou-ops/d2r-mobile.git ../d2r-mobile
git -C ../d2r-mobile checkout 6a2474d     # 补丁的基线；不要用最新 main
git -C ../d2r-mobile apply ../nvn-metal/patches/d2r-mobile-kirby.patch

# 游戏侧：解包、解 NSO、改写模块、出着色器库，产物都放 ~/kirby-work（不进 git）
python -I tools/import_dump.py --exefs <导出的 ExeFS 目录> --romfs <导出的 RomFS 目录> ~/kirby-work --romfs-out ~/kirby-work/RomFS
# ……之后的改写、着色器库见下一节「全在 Mac 上从头重做」

# Mac 宿主
make -C host            # 产物 host/kirbyhost
make -C host check      # Intel Mac 上只做语法检查

# iPhone
ios/run.sh build && ios/run.sh install && ios/run.sh run
```

`ios/local.env`（样例 `ios/local.env.example`）里填设备、签名和 `KIRBY_WORK` 路径，不进仓库。

## 全在 Mac 上从头重做（不用 Windows）

2026-10-22 在 M5 上照下面做了一遍，每一步的数字都和原来 Windows 上的报告一致：K01 RomFS 9373 个文件；K04 三个模块 STRUCT / EQUIV 全 OK、站点数相同；K06 27239 组全部对上；H3 出库 16985、跳过 10 份几何着色器。当时 K01 用的是仓库自带的 XCI 解包脚本；2026-10-30 起仓库不再带解密代码，改用 `tools/import_dump.py` 导入外部工具导出的目录，核对过 `romfs_list.txt` 和三个模块与原来逐字节相同。

```bash
brew install dotnet spirv-cross llvm xcodegen
python3 -m venv ~/.venvs/kirby && ~/.venvs/kirby/bin/pip install capstone lz4 zstandard unicorn
PY=~/.venvs/kirby/bin/python; W=~/kirby-work; DM=../d2r-mobile/dm
# K01 导入（ExeFS / RomFS 先用外部工具从自己的游戏导出）
$PY -I tools/import_dump.py --exefs <导出的 ExeFS 目录> --romfs <导出的 RomFS 目录> $W --romfs-out $W/RomFS
$PY -I tools/verify_romfs.py $W/RomFS $W/romfs_list.txt
# K04 改写三个模块并核对（核对要 Homebrew 的 LLVM：Xcode 里没有 llvm-objcopy）
export DM_RULESET=native DM_ROOT=$W LLVM_BIN=$(brew --prefix llvm)/bin
for m in main sdk subsdk0; do
  $PY -I $DM/tools/patch_nso.py $W/exefs/$m $W/patched $m
  $PY -I tools/structck_kirby.py $W/patched $m $W/exefs/$m
  $PY -I tools/equiv_kirby.py $W/patched $m $W/exefs/$m --seed 1 --jobs 4 --tsv $W/patched/$m.equiv.tsv
done
# K06 提取着色器（会改写 analysis/shader_index_parts.txt、丢掉 main 那一行，跑完 git checkout 还原）
$PY -I tools/bnsh_extract.py $W/RomFS $W/shaders analysis/kirby_shaders.tsv --inventory docs/cmp_shader_containers.tsv
$PY -I tools/verify_k06.py analysis $W/shaders
$PY -I tools/nso_info.py $W/exefs $W/img && $PY -I tools/main_shaders.py $W/img/main.img $W/shaders
# H3 出着色器库：翻译器按上游源码编（只编它，不跑 setup.sh）
export DOTNET_ROOT=$(brew --prefix dotnet)/libexec
dotnet publish $DM/gfx/tools/dmsh_xlate -c Release -o $W/tools/dmsh_xlate
$PY -I tools/kirby_shlib.py --shaders $W/shaders --out $W/shlib --xlate $W/tools/dmsh_xlate/dmsh_xlate --spirv-cross $(which spirv-cross) --jobs $(sysctl -n hw.ncpu)
```

- Homebrew 的 spirv-cross 是 1.4.363，原来验过的是 1.4.357。整条链自己从头出，前后自洽，没影响。
- 这样重做，真机记下的三份清单（`pso_manifest.tsv`、`uint_masks.tsv`、`iattr_masks.tsv`）就没有了，要重新跑出来。

之后在 Mac 上跑见 [mac.md](mac.md)，装到 iPhone 见 [ios/README.md](../ios/README.md)。
