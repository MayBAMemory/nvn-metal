# 卡比的声音：不需要移植混音器

2026-10-14，主线程。结论推翻了原计划里的 K10a、K10b、K10c 和 H2（「把 Ryujinx 的音频渲染器移植成 C」），这几项都不用做了。

## 依据（反汇编核对，工具 `tools/sdis_sym.py`、`tools/xref_calls.py`）

1. `nn::audio::OpenAudioRenderer`（sdk+0x4bf20）和 `GetAudioRendererWorkBufferSize`（sdk+0x4be00）按 `AudioRendererParameter+0x2c`（渲染设备）选路：等于 1（CPU）走 `CreateAudioRendererManagerByDfc`，即本进程直接调用 SDK 自带的服务端实现（`nn::audio::server::*` 共 642 个函数）；否则走 `ByHipc`，连系统的 audren 服务。`+0x30`（执行方式）等于 1 时，是手动执行模式。
2. 卡比在 `main+0x4596cc` 一条 `str` 把这两个字段同时写成 1（`0x100000001`），然后在 `main+0x459704` 打开渲染器。
3. `main` 自己调用 `ExecuteAudioRendererRendering`（`main+0x4593a4`、`+0x459490`）。SDK 的实现（`AudioRendererImpl::ExecuteAudioRendererRendering`，sdk+0x5a55c）是：生成命令，再交给 `dsp::CommandListProcessor::Process` 在 CPU 上执行。`Process` 把各类命令都处理掉，却**不调用任何声音输出函数**，所以 DeviceSink 在这种模式下不会自己出声。
4. 游戏另外挂了一个循环缓冲输出（`AddCircularBufferSink`，`main+0x458ff4`）。读取函数（`main+0x458f20` 起）在 `[对象+0x7a0] == 1`、并且挂了回调 `[对象+0x7d0]` 时，调用 `ReadCircularBufferSink` 取出 PCM，再尾调用这个回调。
5. 整个 `main` 里，调用 AudioOut 的只有 `main+0x76a000` 到 `+0x771000` 这一个模块（`OpenDefaultAudioOut` 在 `+0x770314`，`StartAudioOut` 在 `+0x76ac14`，`AppendAudioOutBuffer` 有 5 处）。它先用 `ProcessResamplerBuffer` 重采样，再交给 AudioOut；同一模块也处理视频伴音（`movie::Buffer`）。可以推断，第 4 条里的回调就是这个模块，**有待运行时核实**：开 `DM_AUDIO_DUMP` 看游戏音乐有没有写出来。

**所以混音完全由 SDK 在游戏进程里完成，声音最后经游戏自己的 AudioOut 输出，真机上也是这样。** 除了 AudioOut，游戏没有别的出声途径。宿主只需要提供：
- **AudioOut**：D2R 的 `dm/host/hle_audio.c` 已经接到 CoreAudio，直接共用。
- **几个会连系统服务的入口**：写在 `host/hle_audio_kirby.c`。包括 `OpenDefaultAudioOut`、不带事件的 `OpenAudioOut`、`SetAudioOutVolume`、`GetAudioOutPlayedSampleCount`、`ListAudioDeviceName`、`SetAudioDeviceOutputVolume`。这 6 个名字都已核对，在卡比里有重定位引用。

为此在 `dm/host/hle_audio.c` 里加了两个对外函数 `dm_audio_out_open`、`dm_audio_out_played_samples`，并在 `pull` 里累计已播字节数。D2R 自己不调用它们，行为不变。

## 运行时要留意的（H4）

- 本进程里的混音器（`nn::audio::server`）会不会在某处碰到系统调用或进程间通信，静态看不全，要跑起来才知道。
- `SetAudioOutVolume` 和 `SetAudioDeviceOutputVolume` 目前只记日志、不生效。如果游戏真的用到非 1.0 的值，再接进 AudioUnit。
- AudioOut 只支持 48 kHz（D2R 的限制）。卡比的参数如果是别的采样率，打开时会直接中止，日志里能看到。
- 过场动画的声音（subsdk0 的 movie 库）也走 AudioOut，等过场动画的方案定了再看。

## Ryujinx.Audio

Codex 在 K09 里放进来的 `dm/third_party/ryujinx/src/Ryujinx.Audio/` 暂时用不上，先不提交。以后如果本进程的混音器真跑不通，再拿它当后备方案。
