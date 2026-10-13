// nn::audio（卡比）：补 D2R 的 dm/host/hle_audio.c 没覆盖的几个入口。
// 卡比的声音链（反汇编核对，见 kirby/docs/audio.md）：游戏把 AudioRendererParameter 的「渲染设备 = CPU、执行方式 = 手动」都设成 1
// （main+0x4596cc），SDK 于是在本进程里跑混音器（CreateAudioRendererManagerByDfc，不连 audren 服务）；游戏自己调
// ExecuteAudioRendererRendering 出一帧、ReadCircularBufferSink 读出 PCM，再 AppendAudioOutBuffer 交给 AudioOut。
// 所以混音器不用宿主实现，只要 AudioOut（D2R 已接 CoreAudio）和下面这几处会连系统服务的入口。
// nn::audio (Kirby): fills the few entries D2R's dm/host/hle_audio.c does not cover.
// Kirby's audio chain (checked by disassembly, see kirby/docs/audio.md): the game sets both "rendering device = CPU" and "execution mode = manual"
// of AudioRendererParameter to 1 (main+0x4596cc), so the SDK runs the mixer inside this process (CreateAudioRendererManagerByDfc, no audren service);
// the game itself calls ExecuteAudioRendererRendering for one frame, reads the PCM with ReadCircularBufferSink, then hands it to AudioOut with AppendAudioOutBuffer.
// So the host does not implement the mixer; it only needs AudioOut (D2R already wired to CoreAudio) plus the entries below that would reach system services.
#include "hle.h"
#include <stdatomic.h>

Result dm_audio_out_open(void *g, void *ev, const char *name, const uint8_t *param);   // dm/host/hle_audio.c
uint64_t dm_audio_out_played_samples(const void *g);                                  // dm/host/hle_audio.c

// ---- AudioOut：D2R 只接了带事件的 OpenAudioOut；卡比走 OpenDefaultAudioOut → 不带事件的 OpenAudioOut ----
// ---- AudioOut: D2R only took over OpenAudioOut with an event; Kirby goes OpenDefaultAudioOut → OpenAudioOut without an event ----
HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE", "连 audout 服务；宿主 AudioUnit（hle_audio.c）",
          Result, kh_au_OpenDefault, void *g, const uint8_t *param) { return dm_audio_out_open(g, NULL, "DeviceOut", param); }
HLE_FORCE("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPKcRKNS0_17AudioOutParameterE", "连 audout 服务（不带事件）；宿主 AudioUnit（hle_audio.c）",
          Result, kh_au_Open, void *g, const char *name, const uint8_t *param) { return dm_audio_out_open(g, NULL, name, param); }
// 音量：记下不生效（游戏自己在混音器里调音量；这一层多半恒为 1.0）。第一次和变化时记日志，真遇到用非 1.0 的再接
// Volume: recorded but not applied (the game adjusts volume in its own mixer; at this level it is most likely always 1.0). Logged the first time and on change; wire it up if a non-1.0 value actually shows up
HLE_FORCE("_ZN2nn5audio17SetAudioOutVolumeEPNS0_8AudioOutEf", "音量：记录，暂不生效", void, kh_au_SetVol, void *g, float v) {
    static float last = -1; if (v != last) { LOG("audio::SetAudioOutVolume %p → %.3f（未生效）", g, v); last = v; }
}
HLE_FORCE("_ZN2nn5audio28GetAudioOutPlayedSampleCountEPKNS0_8AudioOutE", "已播放样本数：按交给出声端的字节算", uint64_t, kh_au_Played, const void *g) { return dm_audio_out_played_samples(g); }

// ---- 音频设备（真机经 IAudioDevice 服务）：宿主只有一个输出设备 ----
// ---- Audio device (on the real device through the IAudioDevice service): the host has a single output device ----
// AudioDeviceName = { char name[256] }
HLE_FORCE("_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi", "音频设备列表；宿主 1 个", int, kh_dev_List, char *out, int n) {
    if (n < 1 || !out) return 0;
    memset(out, 0, 256); strcpy(out, "AudioTvOutput"); return 1;
}
HLE_FORCE("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", "设备音量：记录，暂不生效", void, kh_dev_SetVol, const char *name, float v) {
    static float last = -1; if (v != last) { LOG("audio::SetAudioDeviceOutputVolume %s → %.3f（未生效）", name ? name : "?", v); last = v; }
}
