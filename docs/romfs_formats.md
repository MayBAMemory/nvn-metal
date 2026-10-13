# RomFS 文件格式摸底（K03）

日期：2026-10-14，v1.0.0。

## 外层格式

| 扩展名 | 文件数 | 字节数 | 用途/魔数 |
|---|---:|---:|---|
| .bfsar | 1 | 384 | NintendoWare 音频归档；FSAR |
| .bfsha | 24 | 614712 | 着色器归档；FSHA |
| .bfstm | 52 | 217670368 | NintendoWare 音频流；FSTM |
| .bin | 653 | 34446306 | HAL XBIN 容器（样本为 Basil 字节码）；分类依路径 |
| .bnvib | 291 | 436840 | 振动数据（据目录名猜测）；无 ASCII 魔数，样本 uint32=4 |
| .cmp | 7201 | 4713251913 | 资源压缩包装；uint32 长度 + Zstd/LZ4 帧 |
| .cmpbin | 430 | 483385269 | 同一压缩包装，常见音频归档/界面 SARC |
| .cndbin | 46 | 831144 | XBIN 参数容器（据目录猜测） |
| .dat | 1 | 862488 | XBIN 容器，HGDF 标签；用途未解析 |
| .msbp | 1 | 6064 | 消息项目数据；MsgPrjBn |
| .msbt | 533 | 4688528 | 消息文本；MsgStdBn |
| .opus | 104 | 317446923 | Nintendo Opus 容器（据扩展名和头部推测），不是 OggS 包装 |
| .webm | 36 | 366620836 | WebM 视频；EBML 1A45DFA3 |

## 文件头抽样

每种扩展名抽 3 个；不足 3 个则全查。仅保留任务要求的 32 字节格式头，不包含资源正文。

| 路径 | 开头 32 字节（hex） |
|---|---|
| snd/Kirby15/Kirby15.bfsar | `46 53 41 52 ff fe 40 00 00 00 00 03 80 01 00 00 03 00 00 00 00 20 00 00 40 00 00 00 60 00 00 00` |
| shader/BlendShape/BlendShape.bfsha | `46 53 48 41 20 20 20 20 00 00 08 00 ff fe 0c 00 f8 04 00 00 00 00 78 04 00 50 00 00 b8 50 00 00` |
| shader/Copy/Copy.bfsha | `46 53 48 41 20 20 20 20 00 00 08 00 ff fe 0c 00 84 03 00 00 00 00 48 03 00 50 00 00 a0 50 00 00` |
| shader/DecalShadow/DecalShadow.bfsha | `46 53 48 41 20 20 20 20 00 00 08 00 ff fe 0c 00 2e 06 00 00 00 00 78 05 00 60 00 00 b8 60 00 00` |
| snd/Kirby15/stream/BH_Quest1.dspadpcm.bfstm | `46 53 54 4d ff fe 40 00 00 00 00 02 a0 a1 37 00 03 00 00 00 00 40 00 00 40 00 00 00 00 01 00 00` |
| snd/Kirby15/stream/CF_Title1.dspadpcm.bfstm | `46 53 54 4d ff fe 40 00 00 00 00 02 20 41 13 00 03 00 00 00 00 40 00 00 40 00 00 00 00 01 00 00` |
| snd/Kirby15/stream/DP_Title1.dspadpcm.bfstm | `46 53 54 4d ff fe 40 00 00 00 00 02 20 3c 13 00 03 00 00 00 00 40 00 00 40 00 00 00 00 01 00 00` |
| basil/App.bin | `58 42 49 4e 34 12 02 00 f2 29 06 00 e9 fd 00 00 07 00 02 00 28 00 00 00 24 00 00 00 a1 01 00 00` |
| basil/Scn.bin | `58 42 49 4e 34 12 02 00 4a b2 3f 01 e9 fd 00 00 07 00 02 00 14 04 00 00 24 00 00 00 cc 23 00 00` |
| basil/Seq.bin | `58 42 49 4e 34 12 02 00 92 86 06 00 e9 fd 00 00 07 00 02 00 18 00 00 00 24 00 00 00 25 01 00 00` |
| hidvib/Scn/Step/CharaBandanaWaddleDee/Charge2.bnvib | `04 00 00 00 03 00 c8 00 00 03 00 00 01 77 03 96 2a 76 69 a1 50 8a 64 96 52 88 3d 9c 7d 83 39 99` |
| hidvib/Scn/Step/CharaBandanaWaddleDee/Charge3.bnvib | `04 00 00 00 03 00 c8 00 e4 03 00 00 00 76 05 9c 17 76 38 a6 3a 76 56 9b 31 78 5e 9c 5e 86 37 98` |
| hidvib/Scn/Step/CharaBandanaWaddleDee/GrandThrustCancelJump.bnvib | `04 00 00 00 03 00 c8 00 6c 06 00 00 03 76 0b 96 0b 76 22 a1 0f 76 32 a1 1e 77 3a 9b 4d 80 22 99` |
| font/Region/STD/Ext-Operation-OLM.bffnt.cmp | `84 24 c0 00 28 b5 2f fd 80 60 84 24 c0 00 a4 13 00 16 53 3f 32 60 8b a3 00 a0 f9 00 38 01 c8 77` |
| font/Region/STD/Ext-Operation.bffnt.cmp | `78 23 10 00 28 b5 2f fd a0 78 23 10 00 a4 20 02 7a e5 2a a7 51 10 20 48 65 01 e4 df 08 b0 2c a9` |
| font/Region/STD/Ext-OtherColor.bffnt.cmp | `84 22 80 00 28 b5 2f fd 80 60 84 22 80 00 c4 07 00 d2 cc 29 2d 70 6b 82 00 2c 36 5f 55 d1 95 8d` |
| lyt/Resident/StoryLevelTitle/Estory/Body.arc.cmpbin | `fc c0 0e 00 28 b5 2f fd a0 fc c0 0e 00 14 a1 01 1a 1b 9a 71 58 00 cc c9 db 08 b8 66 f7 fd b8 98` |
| lyt/Resident/StoryLevelTitle/Lv1/Body.arc.cmpbin | `b0 f0 0e 00 28 b5 2f fd a0 b0 f0 0e 00 7c ee 01 4a 7f 6e 83 59 00 ce b9 dc 08 84 a0 63 f1 c9 cb` |
| lyt/Resident/StoryLevelTitle/Lv2/Body.arc.cmpbin | `7c d2 0e 00 28 b5 2f fd a0 7c d2 0e 00 24 26 02 1a b0 f2 8d 58 00 10 b2 a3 04 9c 0b 01 e8 2c 60` |
| param/StageScreenShot/Param.cndbin | `58 42 49 4e 34 12 04 00 74 fc 00 00 e9 fd 00 00 74 fc 00 00 74 27 00 00 00 00 00 00 24 00 00 00` |
| param/Step/Chara/BossChimeraPerfect/ActionParam.cndbin | `58 42 49 4e 34 12 04 00 49 80 00 00 e9 fd 00 00 4c 80 00 00 74 27 00 00 00 00 00 00 24 00 00 00` |
| param/Step/Chara/BossChimeraPerfect/CameraParam.cndbin | `58 42 49 4e 34 12 04 00 d9 93 00 00 e9 fd 00 00 dc 93 00 00 74 27 00 00 00 00 00 00 24 00 00 00` |
| fdg/Archive.dat | `58 42 49 4e 34 12 04 00 0c 29 0d 00 e9 fd 00 00 0c 29 0d 00 48 47 44 46 03 00 00 00 28 00 00 00` |
| msg/Kirby15/Kirby15.msbp | `4d 73 67 50 72 6a 42 6e ff fe 00 00 00 04 05 00 00 00 b0 17 00 00 00 00 00 00 00 00 00 00 00 00` |
| msg/Kirby15/CN_Chinese/AbilityList.msbt | `4d 73 67 53 74 64 42 6e ff fe 00 00 01 03 02 00 00 00 90 18 00 00 00 00 00 00 00 00 00 00 00 00` |
| msg/Kirby15/CN_Chinese/AdviceWindow.msbt | `4d 73 67 53 74 64 42 6e ff fe 00 00 01 03 02 00 00 00 70 08 00 00 00 00 00 00 00 00 00 00 00 00` |
| msg/Kirby15/CN_Chinese/Arena.msbt | `4d 73 67 53 74 64 42 6e ff fe 00 00 01 03 02 00 00 00 20 2e 00 00 00 00 00 00 00 00 00 00 00 00` |
| snd/Kirby15/stream/Ec_MetaWind0.opus | `01 00 00 80 18 00 00 00 00 02 00 00 80 bb 00 00 20 00 00 00 00 00 00 00 00 00 00 00 78 00 00 00` |
| snd/Kirby15/stream/K15_ArenaFight1_Stest.marker.opus | `01 00 00 80 18 00 00 00 00 02 00 00 80 bb 00 00 20 00 00 00 00 00 00 00 00 00 00 00 78 00 00 00` |
| snd/Kirby15/stream/K15_ArenaFight2_Stest.marker.opus | `01 00 00 80 18 00 00 00 00 02 00 00 80 bb 00 00 20 00 00 00 00 00 00 00 00 00 00 00 78 00 00 00` |
| mov/EventMovie/Ending.webm | `1a 45 df a3 9f 42 86 81 01 42 f7 81 01 42 f2 81 04 42 f3 81 08 42 82 84 77 65 62 6d 42 87 81 04` |
| mov/EventMovie/FirstCarDeform.webm | `1a 45 df a3 9f 42 86 81 01 42 f7 81 01 42 f2 81 04 42 f3 81 08 42 82 84 77 65 62 6d 42 87 81 04` |
| mov/EventMovie/Opening.webm | `1a 45 df a3 9f 42 86 81 01 42 f7 81 01 42 f2 81 04 42 f3 81 08 42 82 84 77 65 62 6d 42 87 81 04` |

## CMP/CMPBIN 格式与全量分布

偏移 0x00 为 little-endian uint32 解压后长度；0x04 开始是标准 Zstandard（28 B5 2F FD）或 LZ4 frame（04 22 4D 18）。Zstd frame content size（存在时）必须与前缀相等；LZ4 frame 的长度字段存在时亦核对。

| 扩展名 | 压缩 | 文件数 |
|---|---|---:|
| .cmp | lz4 | 2080 |
| .cmp | zstd | 5121 |
| .cmpbin | lz4 | 354 |
| .cmpbin | zstd | 76 |

| 扩展名 | 内层前 4 字节 | 判定 | 文件数 |
|---|---|---|---:|
| .cmp | `36 f8 1a 1e` | BFTTF（字体；内层编码未解析） | 45 |
| .cmp | `46 46 4e 54` | BFFNT 字体 | 4 |
| .cmp | `46 52 45 53` | BFRES（模型/材质/贴图容器） | 4287 |
| .cmp | `53 41 52 43` | SARC（资源归档） | 486 |
| .cmp | `56 46 58 42` | NintendoWare 特效容器 | 299 |
| .cmp | `58 42 49 4e` | XBIN（样本偏移 0x14 为 YAML） | 2080 |
| .cmpbin | `46 53 41 52` | BFSAR 音频归档 | 354 |
| .cmpbin | `53 41 52 43` | SARC（资源归档） | 76 |

- .cmp 声明的总解压长度：11433388448 字节。
- .cmpbin 声明的总解压长度：646258134 字节。

全量文件头全部落在以上两种标准帧，因此不需要按 zlib/raw deflate/LZ4 block/Yaz0/LZ77 强行解码；这些格式没有出现匹配的包装头。

## 完整解压样本

| 压缩包装 | codec | 内层头 | 输出 | 解压字节 |
|---|---|---|---|---:|
| ubin/Step/Map/Arena/Arena/Step01/Toy_Decor.bin.cmp | lz4 | `5842494e` | `D:\kirby-work\unpacked_samples\01_cmp_lz4.bin` | 80 |
| font/ScalableFontBin/K15-LocalCharacter-M.bfttf.cmp | zstd | `36f81a1e` | `D:\kirby-work\unpacked_samples\02_cmp_zstd.bin` | 19340 |
| font/Region/STD/Ext-OtherGray.bffnt.cmp | zstd | `46464e54` | `D:\kirby-work\unpacked_samples\03_cmp_zstd.bin` | 139480 |
| gfx/Actor/Item/MaxTomato/Motion.bfres.cmp | zstd | `46524553` | `D:\kirby-work\unpacked_samples\04_cmp_zstd.bin` | 688 |
| lyt/Step/AimInfo/Body.arc.cmp | zstd | `53415243` | `D:\kirby-work\unpacked_samples\05_cmp_zstd.bin` | 568 |
| ptcl/Actor/Gimmick/MoveGate.ptcl.cmp | zstd | `56465842` | `D:\kirby-work\unpacked_samples\06_cmp_zstd.bin` | 204800 |
| snd/Kirby15/HeroIce.bfsar.cmpbin | lz4 | `46534152` | `D:\kirby-work\unpacked_samples\07_cmpbin_lz4.bin` | 704 |
| lyt/Resident/WipeBg/BaseCstage/Body.arc.cmpbin | zstd | `53415243` | `D:\kirby-work\unpacked_samples\08_cmpbin_zstd.bin` | 259264 |

## 着色器容器

文件开头没有 FSHA/BNSH 不表示容器里没有着色器。为 K06 另做 FRES/SARC/VFXB 的分块流式扫描，定位带有效 8 字节签名（FSHA 后四空格，BNSH 后四零字节）、0x0c little-endian BOM 且声明长度在容器内的 FSHA/BNSH 头；只给位置，不提取代码或猜测变体布局。完整名单见 cmp_shader_containers.tsv（每个源文件一行，偏移为解压后字节位置，以分号分隔）。

- 容器扫描：5148 个，11153371148 字节；未写出全量解压资源。
- 带着色器头的压缩文件：3554 个；FSHA 头 3397 个，BNSH 头 19412 个。

头部扫描不能替代 K06 对指针、文件长度和着色器变体的结构解析；这里的内嵌位置供解析器核对。XBIN 参数/字体/FSAR 音频不属于此次着色器容器扫描范围。

## 依据与限制

- [Zstandard 官方帧格式](https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md)：用于核对帧魔数和长度字段。
- [LZ4 官方帧格式](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md)：用于核对 LZ4 frame 包装。
- [Khangaroo 的 Star Allies CMP 工具](https://gist.github.com/khang06/49d0fe124025a87ba661ebcf33964ef3)：旧作采用 4 字节长度加 LZ4；这里只参考事实，未复制实现。当前游戏使用两种 codec，由本地全量核对确认。
- [MintWorkshop](https://github.com/firubii/MintWorkshop)：作者列出本作 Basil 7.0.2；本地 Basil XBIN 头部的 7/0/2 字段与其一致。其他 XBIN 用途仍属目录推测。
- BFTTF 的内层编码及其是否加密未解析；不影响 CMP 外层格式判定或 K06 着色器定位。
