// 60 帧：卡比关卡里是 30 帧，不是图形跟不上，是游戏自己的帧率框架在等。
// 主循环每帧末尾（main+0x53311c 起）按 GetSystemTick 数「过了几个 1/60 秒」，没到「至少几拍」就 SleepThread(10 µs) 再数；
// 关卡里框架处在模式 2（可变帧率），至少几拍 = 框架 +0x3c = 2，所以 30 帧。
// 可变帧率模式的时间步长是实测的：最近 4 帧（+0x44）实际过的拍数取平均（+0x78），上限 3（+0x34），写进 +0x6c，
// 游戏逻辑按「+0x6c × 1/60 秒」推进（main+0x532a44 起；视频播放器 main+0x7721f0 也读它）。
// 所以把「至少几拍」改成 1：帧每拍出一张，实测倍数自然变成 1.0，逻辑每帧走 1/60 秒，速度不变。
// 实测（M5，同一存档同一按键脚本）：帧时间 p50 33.33 → 16.67 ms，+0x6c 2.000 → 1.000，GPU 每帧约 4 ms。
// 缺省：Mac 开（60 帧），iOS 关（真机 1080p 下 GPU 每帧约 18.6 ms，硬上 60 会忽快忽慢）。KIRBY_FPS=30 / 60 强制。
// 改的是数据不是代码，iOS 上代码段不可写也能用。每帧在游戏主线程读手柄时（hle_hid_kirby.c fill）检查一次。
// 60 fps: Kirby's levels run at 30 not because graphics cannot keep up but because the game's own frame-rate framework waits.
// At the end of every frame (from main+0x53311c) the main loop counts "how many 1/60 s have passed" via GetSystemTick and, until "at least N ticks", does SleepThread(10 µs) and counts again;
// in levels the framework is in mode 2 (variable frame rate) with at-least-N = framework +0x3c = 2, hence 30 fps.
// Variable-rate mode measures its time step: the average of the ticks actually elapsed over the last 4 frames (+0x44), capped at 3 (+0x34), goes to +0x6c,
// and game logic advances by "+0x6c × 1/60 s" (from main+0x532a44; the video player at main+0x7721f0 reads it too).
// So setting at-least-N to 1 makes a frame come every tick, the measured multiplier becomes 1.0 by itself, logic advances 1/60 s per frame, and speed is unchanged.
// Measured (M5, same save, same key script): frame time p50 33.33 → 16.67 ms, +0x6c 2.000 → 1.000, about 4 ms of GPU per frame.
// Default: on for Mac (60 fps), off for iOS (on device at 1080p the GPU takes ~18.6 ms per frame, forcing 60 would judder). KIRBY_FPS=30 / 60 forces it.
// It changes data, not code, so it also works on iOS where the code segment is not writable. Checked once per frame when the game reads the controller on its main thread (hle_hid_kirby.c fill).
#include "dm.h"
#include <TargetConditionals.h>

// 跟版本绑定的地址（v1.0.0，main 构建号 d9ba7db7…）| version-bound addresses (v1.0.0, main build id d9ba7db7…)
#define FW_GLOBAL   0xa61e58   // main 里存「帧率框架持有者」指针的全局；框架 = [持有者 + 8] | global in main holding the frame-rate framework's owner; framework = [owner + 8]
#define FW_MIN_TICK 0x3c       // 模式 2 的「每帧至少几拍」| mode 2's "at least N ticks per frame"

static int fps60(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("KIRBY_FPS");
#if TARGET_OS_OSX
        on = !(e && atoi(e) == 30);
#else
        on = e && atoi(e) == 60;
#endif
        LOG("帧率：%s（KIRBY_FPS=30 / 60 可改）", on ? "60 帧（关卡里可变帧率模式最少 1 拍）" : "原版 30 帧");
    }
    return on;
}

void kirby_fps_tick(void) {
    if (!fps60() || !g_nmod || strcmp(g_mod[0].name, "main")) return;
    uint64_t owner = *(const uint64_t *)(g_mod[0].base + FW_GLOBAL);
    int *min_tick = owner ? (int *)(*(uint8_t **)(owner + 8) + FW_MIN_TICK) : NULL;
    if (!owner || !*(uint8_t **)(owner + 8)) return;
    if (*min_tick == 2) { *min_tick = 1; static int n; if (n++ < 3) LOG("帧率：可变帧率模式最少拍数 2 → 1"); }
}
