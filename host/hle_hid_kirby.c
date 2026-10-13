// nn::hid（卡比）：手柄、体感、触屏。取代 D2R 的 dm/host/hle_hid.c（那份和 D2R 的触屏界面、按键脚本绑在一起）。
// 真机上这些接口经 hid 服务的进程间通信和共享内存取数；宿主没有 hid 服务，全部强制接管。
// 输入来源与 D2R 相同：前端键盘写 dm_input_set，手柄（dm/ios/Sources/dm_gamepad.m）写 dm_pad_set；两路按键按位或，摇杆键盘优先。
// 接的手柄跟运行模式走（hle_oe.c dm_opmode）：主机模式 = 1 号全键手柄（Pro 手柄），掌机模式 = 掌机手柄。别的号、别的样式一律「未连接」。
// 体感：给静止状态（重力朝下、无角速度、方向为单位阵）。震动：设备数 0。
// 只在客户端算的接口（ParseVibrationFile、RetrieveVibrationValue、ControllerSupportArg::SetDefault、SetExplainText、
// SixAxisSensorState::GetQuaternion）不接管，照原样跑 SDK 自己的实现。
// nn::hid (Kirby): controller, motion, touchscreen. Replaces D2R's dm/host/hle_hid.c (that one is tied to D2R's touch UI and key scripts).
// On the real device these go through the hid service's IPC and shared memory; the host has no hid service, so all of them are force-taken over.
// Input sources are the same as D2R: the front-end keyboard writes dm_input_set, the controller (dm/ios/Sources/dm_gamepad.m) writes dm_pad_set; buttons are ORed, the keyboard stick wins.
// The connected controller follows the operation mode (hle_oe.c dm_opmode): console = No.1 full-key controller (Pro Controller), handheld = handheld controller. Every other id / style is "not connected".
// Motion: a resting state (gravity down, no angular velocity, identity direction). Vibration: zero devices.
// Client-side-only interfaces (ParseVibrationFile, RetrieveVibrationValue, ControllerSupportArg::SetDefault, SetExplainText,
// SixAxisSensorState::GetQuaternion) are not taken over and run the SDK's own implementation.
#include "hle.h"
#include "dm_input.h"
#include <stdatomic.h>

enum { NPAD_NO1 = 0, NPAD_HANDHELD = 0x20 };
enum { STYLE_FULLKEY = 1u << 0, STYLE_HANDHELD = 1u << 1, STYLE_JOYDUAL = 1u << 2, STYLE_JOYLEFT = 1u << 3, STYLE_JOYRIGHT = 1u << 4 };
enum { ATTR_CONNECTED = 1u << 0 };

// NpadFullKeyState / NpadHandheldState / NpadJoyDualState / NpadJoyLeftState / NpadJoyRightState 布局相同
// NpadFullKeyState / NpadHandheldState / NpadJoyDualState / NpadJoyLeftState / NpadJoyRightState share one layout
typedef struct {
    int64_t sampling;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes;
    uint32_t _pad;
} NpadState;
_Static_assert(sizeof(NpadState) == 0x28, "NpadState");

// SixAxisSensorState（= libnx HidSixAxisSensorState）
// SixAxisSensorState (= libnx HidSixAxisSensorState)
typedef struct {
    int64_t delta_time_ns;
    int64_t sampling;
    float accel[3], gyro[3], angle[3], dir[9];
    uint32_t attributes;
    uint32_t _pad;
} SixAxisState;
_Static_assert(sizeof(SixAxisState) == 0x60, "SixAxisSensorState");

// ControllerSupportResultInfo：前 8 字节 = 人数（int8）+ 3 字节填充 + 选中的手柄号（u32）
// ControllerSupportResultInfo: first 8 bytes = player count (int8) + 3 bytes padding + selected npad id (u32)
typedef struct { int8_t players; uint8_t _pad[3]; uint32_t selected; } SupportResult;

// ---- 输入状态（前端写，游戏取） | Input state (written by the front end, read by the game) ----
static struct { _Atomic uint64_t buttons; _Atomic int32_t lx, ly, rx, ry; } g_in;
static _Atomic uint64_t g_press_latch;                 // 按下沿锁存：两次取手柄之间按下又抬起的键也至少给一次 | press-edge latch: a key pressed and released between two reads is still delivered once
static _Atomic uint64_t g_pad_btn, g_pad_axes, g_pad_latch;
static _Atomic int64_t g_sampling;

void dm_input_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    atomic_fetch_or(&g_press_latch, buttons & ~atomic_load(&g_in.buttons));
    atomic_store(&g_in.buttons, buttons); atomic_store(&g_in.lx, lx); atomic_store(&g_in.ly, ly); atomic_store(&g_in.rx, rx); atomic_store(&g_in.ry, ry);
}
void dm_pad_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    uint64_t old = atomic_exchange(&g_pad_btn, buttons); atomic_fetch_or(&g_pad_latch, buttons & ~old);
    atomic_store(&g_pad_axes, (uint64_t)(uint16_t)lx | (uint64_t)(uint16_t)ly << 16 | (uint64_t)(uint16_t)rx << 32 | (uint64_t)(uint16_t)ry << 48);
}
uint64_t dm_input_samples(void) { return (uint64_t)atomic_load(&g_sampling); }

static uint32_t connected_id(void) { return dm_opmode() == 0 ? NPAD_HANDHELD : NPAD_NO1; }
static uint32_t style_of(uint32_t id) {
    if (id != connected_id()) return 0;
    return id == NPAD_HANDHELD ? STYLE_HANDHELD : STYLE_FULLKEY;
}

static void fill(NpadState *s, uint32_t id, uint32_t style) {
    memset(s, 0, sizeof *s);
    s->sampling = atomic_fetch_add(&g_sampling, 1) + 1;
    if (!(style_of(id) & style)) return;   // 这个号没接这种样式：全 0（未连接） | this id does not have this style connected: all zero (not connected)
    s->buttons = atomic_load(&g_in.buttons) | atomic_exchange(&g_press_latch, 0) | atomic_load(&g_pad_btn) | atomic_exchange(&g_pad_latch, 0);
    s->lx = atomic_load(&g_in.lx); s->ly = atomic_load(&g_in.ly); s->rx = atomic_load(&g_in.rx); s->ry = atomic_load(&g_in.ry);
    uint64_t ax = atomic_load(&g_pad_axes); int16_t plx = (int16_t)ax, ply = (int16_t)(ax >> 16), prx = (int16_t)(ax >> 32), pry = (int16_t)(ax >> 48);
    if (!(s->lx || s->ly)) { s->lx = plx; s->ly = ply; }
    if (!(s->rx || s->ry)) { s->rx = prx; s->ry = pry; }
    s->attributes = ATTR_CONNECTED;
}

// ---- 手柄 | Controller ----
static _Atomic uint32_t g_supported_styles = STYLE_FULLKEY | STYLE_HANDHELD;
static _Atomic int g_hold_type = 1;   // NpadJoyHoldType：0 竖握，1 横握（SDK 缺省） | NpadJoyHoldType: 0 vertical, 1 horizontal (SDK default)

HLE_FORCE("_ZN2nn3hid14InitializeNpadEv", "连 hid 服务；宿主从前端取输入", void, kh_InitNpad, void) {}
HLE_FORCE("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", "支持的手柄号；宿主固定", void, kh_SetIds, const uint32_t *ids, uint64_t n) { (void)ids; (void)n; }
// BitFlagSet<32> 按值传在 w 寄存器 | BitFlagSet<32> is passed by value in a w register
HLE_FORCE("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", "支持的手柄样式：记下，查询时原样返回", void, kh_SetStyles, uint32_t styles) {
    atomic_store(&g_supported_styles, styles);
    LOG("hid：游戏支持的手柄样式 %#x", styles);
}
HLE_FORCE("_ZN2nn3hid24GetSupportedNpadStyleSetEv", "支持的手柄样式：返回游戏设的值", uint32_t, kh_GetSupStyles, void) { return atomic_load(&g_supported_styles); }
HLE_FORCE("_ZN2nn3hid15GetNpadStyleSetERKj", "手柄样式查询；跟运行模式走", uint32_t, kh_GetStyle, const uint32_t *id) { return style_of(*id); }
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", "全键手柄状态", void, kh_StFullKey, NpadState *s, const uint32_t *id) { fill(s, *id, STYLE_FULLKEY); }
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", "掌机手柄状态", void, kh_StHandheld, NpadState *s, const uint32_t *id) { fill(s, *id, STYLE_HANDHELD); }
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", "双 Joy-Con 状态：宿主不接这种样式", void, kh_StJoyDual, NpadState *s, const uint32_t *id) { fill(s, *id, STYLE_JOYDUAL); }
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", "左 Joy-Con 状态：宿主不接这种样式", void, kh_StJoyLeft, NpadState *s, const uint32_t *id) { fill(s, *id, STYLE_JOYLEFT); }
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", "右 Joy-Con 状态：宿主不接这种样式", void, kh_StJoyRight, NpadState *s, const uint32_t *id) { fill(s, *id, STYLE_JOYRIGHT); }
HLE_FORCE("_ZN2nn3hid14DisconnectNpadERKj", "断开手柄：宿主手柄常连，忽略", void, kh_Disconnect, const uint32_t *id) { LOG("hid：游戏要求断开手柄 %#x（忽略）", *id); }
HLE_FORCE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", "Joy-Con 握法：记下", void, kh_SetHold, int64_t t) { atomic_store(&g_hold_type, (int)t); }
HLE_FORCE("_ZN2nn3hid18GetNpadJoyHoldTypeEv", "Joy-Con 握法：返回游戏设的值", int64_t, kh_GetHold, void) { return atomic_load(&g_hold_type); }
HLE_FORCE("_ZN2nn3hid20GetNpadJoyAssignmentERKj", "Joy-Con 分配方式：恒为双持（0）", int64_t, kh_GetAssign, const uint32_t *id) { (void)id; return 0; }
HLE_FORCE("_ZN2nn3hid30SetNpadJoyAssignmentModeSingleERKjNS0_17NpadJoyDeviceTypeE", "Joy-Con 单持：宿主不接 Joy-Con，忽略", void, kh_SetSingle, const uint32_t *id, int64_t type) { (void)id; (void)type; }
HLE_FORCE("_ZN2nn3hid18SwapNpadAssignmentERKjS2_", "交换手柄号：忽略", void, kh_Swap, const uint32_t *a, const uint32_t *b) { (void)a; (void)b; }
HLE_FORCE("_ZN2nn3hid21StartLrAssignmentModeEv", "L/R 分配模式：忽略", void, kh_LrStart, void) {}
HLE_FORCE("_ZN2nn3hid20StopLrAssignmentModeEv", "L/R 分配模式：忽略", void, kh_LrStop, void) {}

// 手柄设置界面（系统小程序）：直接回「1 人，用当前接着的手柄」
// Controller support applet (system applet): answer "1 player, using the currently connected controller" directly
HLE_FORCE("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE", "手柄设置小程序：回 1 人、当前手柄", Result, kh_ShowSupport, SupportResult *out, const void *arg) {
    (void)arg;
    if (out) { out->players = 1; out->_pad[0] = out->_pad[1] = out->_pad[2] = 0; out->selected = connected_id(); }
    LOG("hid：手柄设置小程序 → 1 人，手柄 %#x", connected_id());
    return RES_OK;
}
HLE_FORCE("_ZN2nn3hid24ShowControllerStrapGuideEv", "腕带说明小程序：直接返回", Result, kh_ShowStrap, void) { return RES_OK; }

// ---- 体感 | Motion ----
// 句柄 = 样式序号 | 手柄号 << 8 | 序号 << 16（只在宿主内部认，游戏原样传回）
// Handle = style index | npad id << 8 | index << 16 (only interpreted inside the host; the game passes it back unchanged)
HLE_FORCE("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", "体感句柄：按请求数给",
          int, kh_SixHandles, uint32_t *out, int count, const uint32_t *id, uint32_t style) {
    int n = style == STYLE_JOYDUAL ? 2 : 1; if (n > count) n = count;
    for (int i = 0; i < n; i++) out[i] = (uint32_t)__builtin_ctz(style ? style : 1) | (*id & 0xff) << 8 | (uint32_t)i << 16;
    return n;
}
HLE_FORCE("_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", "体感开始：宿主给静止状态", void, kh_SixStart, const uint32_t *h) { (void)h; }
HLE_FORCE("_ZN2nn3hid17StopSixAxisSensorERKNS0_19SixAxisSensorHandleE", "体感停止", void, kh_SixStop, const uint32_t *h) { (void)h; }
HLE_FORCE("_ZN2nn3hid21IsSixAxisSensorAtRestERKNS0_19SixAxisSensorHandleE", "体感静止：恒为是", unsigned, kh_SixRest, const uint32_t *h) { (void)h; return 1; }
HLE_FORCE("_ZN2nn3hid21GetSixAxisSensorStateEPNS0_18SixAxisSensorStateERKNS0_19SixAxisSensorHandleE", "体感状态：静止", void, kh_SixState, SixAxisState *s, const uint32_t *h) {
    (void)h;
    static _Atomic int64_t seq;
    memset(s, 0, sizeof *s);
    s->delta_time_ns = 5000000;   // 5 ms 采样间隔 | 5 ms sampling interval
    s->sampling = atomic_fetch_add(&seq, 1) + 1;
    s->accel[2] = -1.0f;          // 1 g 朝下 | 1 g pointing down
    s->dir[0] = s->dir[4] = s->dir[8] = 1.0f;
    s->attributes = ATTR_CONNECTED;
}

// ---- 震动：设备数 0 | Vibration: zero devices ----
HLE_FORCE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",
          "震动设备；宿主 0 个", int, kh_VibHandles, void *out, int max, const uint32_t *id, uint32_t style) { (void)out; (void)max; (void)id; (void)style; return 0; }
HLE_FORCE("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", "震动；宿主不支持", void, kh_VibInit, const void *h) { (void)h; }
HLE_FORCE("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", "震动；宿主不支持", void, kh_VibSend, const void *h, const void *v) { (void)h; (void)v; }

// ---- 触屏：与 D2R 共用 hle_touch.c 的 touch_fill；卡比调的是单数版 GetTouchScreenState<16> ----
// ---- Touchscreen: shares touch_fill in hle_touch.c with D2R; Kirby calls the singular GetTouchScreenState<16> ----
int touch_fill(void *out, uint64_t now_ns);
HLE_FORCE("_ZN2nn3hid21InitializeTouchScreenEv", "连 hid 服务；触屏由前端另接", void, kh_InitTouch, void) {}
HLE_FORCE("_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE", "触屏状态：前端触点（hle_touch.c）", void, kh_GetTouch, void *out) { touch_fill(out, guest_ns()); }
