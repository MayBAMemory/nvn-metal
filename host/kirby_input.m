// 卡比的 Mac 输入：键位可改、两个玩家各用各的键盘键位和手柄（本地双人），外加「键位设置」窗口（菜单 ⌘,）。
// 上游 Mac 外壳（front_mac.m）的键位写死在代码里、所有手柄合成一份（dm_gamepad.m，为 D2R 单人写的），所以卡比这边自己读：
//   键盘：本地事件监听（先于窗口拿到按键），按每个玩家的键位表换算；带 ⌘ 的组合键放行给系统菜单。
//   手柄：按连接先后编号（手柄 1、2…），每个玩家选一个（缺省 1P = 手柄 1、2P = 手柄 2），120 Hz 轮询。
// 算好后交给 hle_hid_kirby.c 的 kirby_input_set（玩家、来源），那边从此不再用上游合成的那份。2P 有键位或分到的手柄连着，就算 2P 有输入来源，2 号手柄报已连接。
// 设置存在 NSUserDefaults（域固定是 kirbyhost，命令行和 .app 共用）。只在 Mac 上编：iOS 走自己的触屏 / 手柄路线。
// Kirby's Mac input: remappable keys, separate keyboard bindings and controllers for two players (local co-op), plus a "Key Settings" window (menu ⌘,).
// The upstream Mac shell (front_mac.m) hard-codes its keys and merges every controller into one (dm_gamepad.m, written for single-player D2R), so Kirby reads input itself:
//   Keyboard: a local event monitor (sees keys before the window), mapped through each player's binding table; combinations with ⌘ are passed through to the system menus.
//   Controllers: numbered by connection order (controller 1, 2, ...), each player picks one (default 1P = controller 1, 2P = controller 2), polled at 120 Hz.
// The results go to kirby_input_set (player, source) in hle_hid_kirby.c, which then stops using the upstream merged input. 2P counts as having an input source when it has bindings
// or its assigned controller is connected, and the No.2 controller then reports as connected. Settings live in NSUserDefaults (always the domain kirbyhost, shared by the command line and the .app). Mac only: iOS keeps its own touch / controller path.
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include "dm.h"
#include "dm_input.h"
#include "dm_padmap.h"

void kirby_input_set(int player, int source, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);   // hle_hid_kirby.c
void kirby_input_p2_available(int on);                                                                         // hle_hid_kirby.c

// ---- 动作表与缺省键位 | actions and default bindings ----
enum { A_LUP = 16, A_LDOWN, A_LLEFT, A_LRIGHT, A_RUP, A_RDOWN, A_RLEFT, A_RRIGHT, NACT };
#define NOKEY 0xffff
static const struct { const char *name; uint64_t bit; } k_act[NACT] = {
    {"A", DM_BTN_A}, {"B", DM_BTN_B}, {"X", DM_BTN_X}, {"Y", DM_BTN_Y}, {"L", DM_BTN_L}, {"R", DM_BTN_R}, {"ZL", DM_BTN_ZL}, {"ZR", DM_BTN_ZR},
    {"+", DM_BTN_PLUS}, {"−", DM_BTN_MINUS}, {"十字键 上", DM_BTN_UP}, {"十字键 下", DM_BTN_DOWN}, {"十字键 左", DM_BTN_LEFT}, {"十字键 右", DM_BTN_RIGHT},
    {"按左摇杆", DM_BTN_LS}, {"按右摇杆", DM_BTN_RS},
    {"左摇杆 上（走动）", 0}, {"左摇杆 下", 0}, {"左摇杆 左", 0}, {"左摇杆 右", 0}, {"右摇杆 上", 0}, {"右摇杆 下", 0}, {"右摇杆 左", 0}, {"右摇杆 右", 0},
};
// 1P 缺省 = 原来写死的键位（J K U I / Q E Z C / 回车 Tab / 方向键 / F G / WASD）；2P 缺省不占键盘（用手柄 2）
// 1P default = the previously hard-coded keys; 2P by default takes no keyboard keys (uses controller 2)
static const uint16_t k_default[2][NACT] = {
    {38, 40, 32, 34, 12, 14, 6, 8, 36, 48, 126, 125, 123, 124, 3, 5, 13, 1, 0, 2, NOKEY, NOKEY, NOKEY, NOKEY},
    {NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY},
};
static uint16_t g_bind[2][NACT];
static int g_pad_of[2] = {1, 2};   // 每个玩家用第几个手柄（0 = 不用）| which controller each player uses (0 = none)

// 键码 → 名字（美式键盘位置）| key code → name (US layout positions)
static NSString *key_name(uint16_t c) {
    static const struct { uint16_t c; const char *n; } t[] = {
        {0, "A"}, {1, "S"}, {2, "D"}, {3, "F"}, {4, "H"}, {5, "G"}, {6, "Z"}, {7, "X"}, {8, "C"}, {9, "V"}, {11, "B"}, {12, "Q"}, {13, "W"}, {14, "E"}, {15, "R"},
        {16, "Y"}, {17, "T"}, {18, "1"}, {19, "2"}, {20, "3"}, {21, "4"}, {22, "6"}, {23, "5"}, {24, "="}, {25, "9"}, {26, "7"}, {27, "-"}, {28, "8"}, {29, "0"},
        {30, "]"}, {31, "O"}, {32, "U"}, {33, "["}, {34, "I"}, {35, "P"}, {36, "回车"}, {37, "L"}, {38, "J"}, {39, "'"}, {40, "K"}, {41, ";"}, {42, "\\"}, {43, ","},
        {44, "/"}, {45, "N"}, {46, "M"}, {47, "."}, {48, "Tab"}, {49, "空格"}, {50, "`"}, {51, "删除"}, {53, "Esc"}, {54, "右 ⌘"}, {55, "⌘"}, {56, "Shift"},
        {57, "大写锁定"}, {58, "Option"}, {59, "Control"}, {60, "右 Shift"}, {61, "右 Option"}, {62, "右 Control"},
        {65, "小键盘 ."}, {67, "小键盘 *"}, {69, "小键盘 +"}, {71, "小键盘 清除"}, {75, "小键盘 /"}, {76, "小键盘 回车"}, {78, "小键盘 -"}, {81, "小键盘 ="},
        {82, "小键盘 0"}, {83, "小键盘 1"}, {84, "小键盘 2"}, {85, "小键盘 3"}, {86, "小键盘 4"}, {87, "小键盘 5"}, {88, "小键盘 6"}, {89, "小键盘 7"}, {91, "小键盘 8"}, {92, "小键盘 9"},
        {96, "F5"}, {97, "F6"}, {98, "F7"}, {99, "F3"}, {100, "F8"}, {101, "F9"}, {103, "F11"}, {109, "F10"}, {111, "F12"}, {118, "F4"}, {120, "F2"}, {122, "F1"},
        {115, "Home"}, {116, "PgUp"}, {117, "Del"}, {119, "End"}, {121, "PgDn"}, {123, "←"}, {124, "→"}, {125, "↓"}, {126, "↑"},
    };
    if (c == NOKEY) return @"—";
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (t[i].c == c) return @(t[i].n);
    return [NSString stringWithFormat:@"键 %u", c];
}

// ---- 设置的读写 | loading and saving settings ----
// 固定用域 kirbyhost：命令行起时它就是 standardUserDefaults；包成 .app（kirby_app.m）后 standardUserDefaults 换成包名，用固定的域两边才是同一份键位
// Always the domain kirbyhost: for a command-line start that is what standardUserDefaults is; packaged as an .app (kirby_app.m) standardUserDefaults becomes the bundle id, so a fixed domain keeps one set of keys for both
NSUserDefaults *kirby_defaults(void) {
    static NSUserDefaults *u; static dispatch_once_t once;
    dispatch_once(&once, ^{ u = [[NSUserDefaults alloc] initWithSuiteName:@"kirbyhost"] ?: NSUserDefaults.standardUserDefaults; });
    return u;
}
static void prefs_load(void) {
    NSUserDefaults *u = kirby_defaults();
    for (int p = 0; p < 2; p++) {
        NSArray *a = [u arrayForKey:[NSString stringWithFormat:@"KirbyKeys%d", p + 1]];
        for (int i = 0; i < NACT; i++) g_bind[p][i] = a.count == NACT ? (uint16_t)[a[i] unsignedIntValue] : k_default[p][i];
        id v = [u objectForKey:[NSString stringWithFormat:@"KirbyPad%d", p + 1]]; if (v) g_pad_of[p] = [v intValue];
    }
}
static void prefs_save(void) {
    NSUserDefaults *u = kirby_defaults();
    for (int p = 0; p < 2; p++) {
        NSMutableArray *a = [NSMutableArray array]; for (int i = 0; i < NACT; i++) [a addObject:@(g_bind[p][i])];
        [u setObject:a forKey:[NSString stringWithFormat:@"KirbyKeys%d", p + 1]]; [u setInteger:g_pad_of[p] forKey:[NSString stringWithFormat:@"KirbyPad%d", p + 1]];
    }
}

// ---- 键盘 | keyboard ----
static uint8_t g_down[256];
static void keys_publish(void) {
    for (int p = 0; p < 2; p++) {
        uint64_t b = 0; int d[8] = {0};
        for (int i = 0; i < NACT; i++) { uint16_t c = g_bind[p][i]; if (c == NOKEY || c > 255 || !g_down[c]) continue; if (i < A_LUP) b |= k_act[i].bit; else d[i - A_LUP] = 1; }
        kirby_input_set(p, 0, b, (d[3] - d[2]) * 32767, (d[0] - d[1]) * 32767, (d[7] - d[6]) * 32767, (d[4] - d[5]) * 32767);
    }
}
static void keys_clear(void) { memset(g_down, 0, sizeof g_down); keys_publish(); }

// ---- 手柄 | controllers ----
static NSMutableArray<GCController *> *g_pads;   // 连接先后 | in connection order
static dispatch_source_t g_poll;
static int32_t axis(float v) { if (v > 1) v = 1; if (v < -1) v = -1; return (int32_t)lroundf(v * DM_STICK_MAX); }
static NSString *pad_label(int n) {   // 手柄 n（1 起）| controller n (from 1)
    if (n <= 0) return @"不用手柄";
    GCController *c = n <= (int)g_pads.count ? g_pads[n - 1] : nil;
    return c ? [NSString stringWithFormat:@"手柄 %d：%@", n, c.vendorName ?: c.productCategory ?: @"?"] : [NSString stringWithFormat:@"手柄 %d（未连接）", n];
}
static void pads_poll(void) {
    int p2 = 0;
    for (int p = 0; p < 2; p++) {
        int n = g_pad_of[p]; GCController *c = n > 0 && n <= (int)g_pads.count ? g_pads[n - 1] : nil; GCExtendedGamepad *g = c.extendedGamepad;
        if (!g) { kirby_input_set(p, 1, 0, 0, 0, 0, 0); continue; }
        if (p == 1) p2 = 1;
        uint64_t b = dm_padmap_face(dm_padmap_kind(c.productCategory.UTF8String, c.vendorName.UTF8String), g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed);
        if (g.leftShoulder.pressed) b |= DM_BTN_L; if (g.rightShoulder.pressed) b |= DM_BTN_R; if (g.leftTrigger.pressed) b |= DM_BTN_ZL; if (g.rightTrigger.pressed) b |= DM_BTN_ZR;
        if (g.buttonMenu.pressed) b |= DM_BTN_PLUS; if (g.buttonOptions.pressed) b |= DM_BTN_MINUS;
        if (g.dpad.up.pressed) b |= DM_BTN_UP; if (g.dpad.down.pressed) b |= DM_BTN_DOWN; if (g.dpad.left.pressed) b |= DM_BTN_LEFT; if (g.dpad.right.pressed) b |= DM_BTN_RIGHT;
        if (g.leftThumbstickButton.pressed) b |= DM_BTN_LS; if (g.rightThumbstickButton.pressed) b |= DM_BTN_RS;
        int32_t a[4] = {axis(g.leftThumbstick.xAxis.value), axis(g.leftThumbstick.yAxis.value), axis(g.rightThumbstick.xAxis.value), axis(g.rightThumbstick.yAxis.value)};
        for (int k = 0; k < 4; k += 2) if (hypot(a[k], a[k + 1]) < 0.15 * DM_STICK_MAX) a[k] = a[k + 1] = 0;   // 死区 15% | 15% dead zone
        kirby_input_set(p, 1, b, a[0], a[1], a[2], a[3]);
    }
    for (int i = 0; i < NACT && !p2; i++) p2 = g_bind[1][i] != NOKEY;
    kirby_input_p2_available(p2);
}

// ---- 键位设置窗口 | key settings window ----
@interface KirbyKeysWindow : NSObject <NSWindowDelegate>
@property(strong) NSWindow *win;
@property(strong) NSMutableArray<NSButton *> *cells;   // [玩家 * NACT + 动作] | [player * NACT + action]
@property(strong) NSArray<NSPopUpButton *> *padMenus;
@property(strong) NSTextField *hint;
@property int capP, capA;                               // 正在等新键：玩家 / 动作（-1 = 没在等）| waiting for a new key: player / action (-1 = not waiting)
@end
static KirbyKeysWindow *g_ui;
@implementation KirbyKeysWindow
- (void)refresh {
    for (int p = 0; p < 2; p++) for (int i = 0; i < NACT; i++) {
        NSButton *b = self.cells[p * NACT + i];
        b.title = p == self.capP && i == self.capA ? @"按新键…" : key_name(g_bind[p][i]);
    }
    for (int p = 0; p < 2; p++) {
        NSPopUpButton *m = self.padMenus[p]; [m removeAllItems];
        for (int n = 0; n <= 4; n++) [m addItemWithTitle:pad_label(n)];
        [m selectItemAtIndex:g_pad_of[p]];
    }
    self.hint.stringValue = self.capA >= 0 ? @"按下新的键（Esc 取消，删除键 = 清空这一格）" : @"点一格改键位。同一个键只能绑一处；设置自动保存。";
}
- (void)pick:(NSButton *)b { self.capP = (int)b.tag / NACT; self.capA = (int)b.tag % NACT; [self refresh]; }
- (void)padChanged:(NSPopUpButton *)m { g_pad_of[m.tag] = (int)m.indexOfSelectedItem; prefs_save(); pads_poll(); [self refresh]; }
- (void)reset:(id)s { (void)s; memcpy(g_bind, k_default, sizeof g_bind); g_pad_of[0] = 1; g_pad_of[1] = 2; self.capA = -1; prefs_save(); keys_clear(); pads_poll(); [self refresh]; }
// 正在等新键时拿走这次按键 | while waiting for a new key, take this key press
- (BOOL)capture:(uint16_t)code {
    if (self.capA < 0) return NO;
    int p = self.capP, a = self.capA; self.capA = -1;
    if (code == 53) { [self refresh]; return YES; }                              // Esc：取消 | Esc: cancel
    if (code == 51) g_bind[p][a] = NOKEY;                                        // 删除键：清空 | Delete: clear
    else { for (int q = 0; q < 2; q++) for (int i = 0; i < NACT; i++) if (g_bind[q][i] == code) g_bind[q][i] = NOKEY; g_bind[p][a] = code; }
    prefs_save(); keys_clear(); pads_poll(); [self refresh]; return YES;
}
- (void)windowWillClose:(NSNotification *)n { (void)n; self.capA = -1; }
- (void)build {
    self.capA = -1; self.cells = [NSMutableArray array];
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 520, 760) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    w.title = @"键位设置"; w.releasedWhenClosed = NO; w.delegate = self; self.win = w;
    NSMutableArray *rows = [NSMutableArray array];
    NSTextField *(^label)(NSString *, BOOL) = ^NSTextField *(NSString *s, BOOL bold) { NSTextField *t = [NSTextField labelWithString:s]; if (bold) t.font = [NSFont boldSystemFontOfSize:13]; return t; };
    [rows addObject:@[label(@"", NO), label(@"1P", YES), label(@"2P", YES)]];
    NSMutableArray *menus = [NSMutableArray array];
    for (int p = 0; p < 2; p++) { NSPopUpButton *m = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO]; m.tag = p; m.target = self; m.action = @selector(padChanged:); [menus addObject:m]; }
    self.padMenus = menus;
    [rows addObject:@[label(@"手柄", NO), menus[0], menus[1]]];
    for (int i = 0; i < NACT; i++) {
        NSMutableArray *r = [NSMutableArray arrayWithObject:label(@(k_act[i].name), NO)];
        for (int p = 0; p < 2; p++) { NSButton *b = [NSButton buttonWithTitle:@"" target:self action:@selector(pick:)]; b.tag = p * NACT + i; [r addObject:b]; }
        [rows addObject:r];
    }
    for (int p = 0; p < 2; p++) for (int i = 0; i < NACT; i++) [self.cells addObject:rows[2 + i][1 + p]];
    NSGridView *grid = [NSGridView gridViewWithViews:rows]; grid.rowSpacing = 4; grid.columnSpacing = 12;
    [grid columnAtIndex:1].width = 170; [grid columnAtIndex:2].width = 170;
    self.hint = [NSTextField wrappingLabelWithString:@""];
    NSButton *reset = [NSButton buttonWithTitle:@"恢复默认" target:self action:@selector(reset:)];
    NSStackView *stack = [NSStackView stackViewWithViews:@[grid, self.hint, reset]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical; stack.alignment = NSLayoutAttributeLeading; stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    w.contentView = stack; [self refresh]; [w center];
}
- (void)show:(id)s { (void)s; if (!self.win) [self build]; [self refresh]; [self.win makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES]; }
@end

static void install(void) {
    prefs_load(); g_pads = [NSMutableArray array]; for (GCController *c in GCController.controllers) [g_pads addObject:c];
    g_ui = [KirbyKeysWindow new]; g_ui.capA = -1;
    // 菜单：应用菜单里加「键位设置…」⌘, | menu: "Key Settings..." ⌘, in the application menu
    NSMenu *app = NSApp.mainMenu.itemArray.firstObject.submenu;
    if (app) { NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:@"键位设置…" action:@selector(show:) keyEquivalent:@","]; it.target = g_ui; [app insertItem:it atIndex:0]; }
    // 「帮助」里上游那项（keys:）弹的是写死的键位对照；改成打开卡比的键位设置 | upstream's Help item (keys:) shows a fixed key list; point it at Kirby's key settings instead
    for (NSMenuItem *top in NSApp.mainMenu.itemArray)
        for (NSMenuItem *it in top.submenu.itemArray)
            if (it.action == @selector(keys:)) { it.title = @"键位设置…"; it.action = @selector(show:); it.target = g_ui; }
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged handler:^NSEvent *(NSEvent *e) {
        uint16_t c = e.keyCode; if (c > 255) return e;
        if (e.type == NSEventTypeFlagsChanged) {   // 修饰键单独按：按 / 抬看对应的标志位 | modifiers on their own: down / up from the matching flag
            NSEventModifierFlags f = e.modifierFlags; int down =
                (c == 56 || c == 60) ? !!(f & NSEventModifierFlagShift) : (c == 59 || c == 62) ? !!(f & NSEventModifierFlagControl) :
                (c == 58 || c == 61) ? !!(f & NSEventModifierFlagOption) : (c == 55 || c == 54) ? !!(f & NSEventModifierFlagCommand) : (c == 57) ? !!(f & NSEventModifierFlagCapsLock) : 0;
            if (down && [g_ui capture:c]) return nil;
            g_down[c] = (uint8_t)down; keys_publish(); return e;
        }
        if (e.type == NSEventTypeKeyDown && [g_ui capture:c]) return nil;
        if (e.modifierFlags & NSEventModifierFlagCommand) return e;              // ⌘ 组合键归菜单 | ⌘ combinations belong to the menus
        if (e.type == NSEventTypeKeyDown && e.isARepeat) return g_ui.win.isKeyWindow ? nil : e;
        g_down[c] = e.type == NSEventTypeKeyDown; keys_publish();
        return g_ui.win.isKeyWindow ? nil : e;                                    // 设置窗口在前时不把键交给它（免得按钮被空格 / 回车触发）| keep keys away from the settings window's buttons
    }];
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { (void)n; keys_clear(); }];
    [nc addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
        GCController *c = n.object; if (![g_pads containsObject:c]) [g_pads addObject:c]; LOG("输入：手柄 %lu 连上 %s", (unsigned long)g_pads.count, c.vendorName.UTF8String ?: "?"); if (g_ui.win) [g_ui refresh]; }];
    [nc addObserverForName:GCControllerDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
        GCController *c = n.object; NSUInteger i = [g_pads indexOfObject:c]; if (i != NSNotFound) { [g_pads removeObjectAtIndex:i]; LOG("输入：手柄 %lu 断开 %s", (unsigned long)i + 1, c.vendorName.UTF8String ?: "?"); }
        if (g_ui.win) [g_ui refresh]; }];
    dispatch_source_t t = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(t, DISPATCH_TIME_NOW, NSEC_PER_SEC / 120, NSEC_PER_MSEC); dispatch_source_set_event_handler(t, ^{ pads_poll(); }); dispatch_resume(t);
    g_poll = t;   // 一直持有，定时器才不会被释放 | keep a reference so the timer is not released
    keys_publish(); pads_poll();
    // 第一次用（还没存过键位）就弹出键位设置，让人知道键在哪；KIRBY_SHOW_KEYS=1 强制弹出 | first use (no saved keys yet): show the key settings so people know where the keys are; KIRBY_SHOW_KEYS=1 forces it
    const char *sk = getenv("KIRBY_SHOW_KEYS");
    if (![kirby_defaults() arrayForKey:@"KirbyKeys1"] || (sk && *sk == '1')) { prefs_save(); [g_ui show:nil]; }
    LOG("输入：卡比自己的键位 / 手柄分配已接管（键位设置：菜单 ⌘,）；1P 手柄 %d、2P 手柄 %d", g_pad_of[0], g_pad_of[1]);
}
// 游戏初始化手柄时调（客体线程）：交给主线程装；没有主线程循环（无窗口测试）就永远不装，照旧用上游的输入
// Called when the game initializes controllers (guest thread): installed on the main thread; without a main run loop (headless tests) it never installs and the upstream input stays in use
void kirby_input_start(void) {
    static int once; if (once++ || !getenv("DM_WINDOW")) return;
    dispatch_async(dispatch_get_main_queue(), ^{ install(); });
}
#else
void kirby_input_start(void) {}
#endif
