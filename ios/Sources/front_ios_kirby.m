// 卡比的 iOS 前端：front.h 的 iOS 实现，外加手柄、触屏和生命周期。
// 参照 dm/ios/Sources/front_ios.m（D2R），只留卡比用得上的部分：D2R 那份有 1600 行，大半是 D2R 的触屏界面（摇杆、技能钮、面板点击、底栏），这里一概不要。
//   · 画面：一个 CAMetalLayer 视图，按 16:9 居中，四周留黑边。
//   · 手柄：dm/ios/Sources/dm_gamepad.m（宿主 Makefile 已编入），连上即用。没有实体手柄时显示系统的屏幕手柄 GCVirtualController，
//     它对 dm_gamepad.m 来说就是一个普通手柄，不用另写输入代码。实体手柄一连上，屏幕手柄就收起。
//   · 屏幕右上角另有 + / − 两个小钮（屏幕手柄没有这两个键）。
//   · 触屏：原样交给宿主 dm_touch_set（卡比用 GetTouchScreenState 读）。
//   · 生命周期：照 front_mac.m 的口径——失活 2、进后台 3 并暂停音频与 GPU、回前台恢复。
// Kirby's iOS front end: the iOS implementation of front.h, plus controller, touch and lifecycle.
// Modelled on dm/ios/Sources/front_ios.m (D2R), keeping only what Kirby uses: that file is 1600 lines, mostly D2R's touch UI (stick, skill buttons, panel taps, bottom bar), none of which is wanted here.
//   · Picture: one CAMetalLayer view, 16:9 centred with black borders.
//   · Controller: dm/ios/Sources/dm_gamepad.m (already built in by the host Makefile) works as soon as a controller connects. With no physical controller the
//     system on-screen controller GCVirtualController is shown; to dm_gamepad.m it is just another controller, so no extra input code. It hides once a physical one connects.
//   · Two small + / − buttons at the top right (the on-screen controller has neither key).
//   · Touch: passed as is to the host's dm_touch_set (Kirby reads it with GetTouchScreenState).
//   · Lifecycle: same rules as front_mac.m -- resign active 2, background 3 with audio and GPU paused, foreground restores.
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <GameController/GameController.h>
#include <signal.h>
#include <stdatomic.h>
#include <math.h>
#include "dm.h"
#include "hle.h"
#include "front.h"
#include "dm_input.h"

void dm_gamepad_start(void (*on_mode)(int pad)); void dm_gamepad_reset(void);   // dm/ios/Sources/dm_gamepad.m
void dm_gfx_suspend(int on);

static CAMetalLayer *g_layer;
int dm_front_navigation(void) { return 1; }   // dm_gamepad.m：非零 = 不把 ZR 拿去做 D2R 的回城（同 app_mac.m） | dm_gamepad.m: non-zero = do not take ZR for D2R's town portal (same as app_mac.m)

// ---------- 画面与触屏 | picture and touch ----------
@interface KirbyMetalView : UIView
@end
@implementation KirbyMetalView {
    UITouch *_slot[16];   // 强引用；所有结束 / 取消路径都清槽 | strong refs; every end / cancel path clears the slot
}
+ (Class)layerClass { return CAMetalLayer.class; }
- (instancetype)initWithFrame:(CGRect)f {
    if (!(self = [super initWithFrame:f])) return nil;
    self.multipleTouchEnabled = YES;
    return self;
}
- (void)push {
    dm_touch t[16]; int n = 0; CGSize s = self.bounds.size;
    for (uint32_t i = 0; i < 16; i++) if (_slot[i]) {
        CGPoint p = [_slot[i] locationInView:self];
        t[n++] = (dm_touch){i, (float)fmin(fmax(p.x / s.width, 0), 0.9999), (float)fmin(fmax(p.y / s.height, 0), 0.9999)};
    }
    dm_touch_set(t, n);
}
- (void)touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e {
    for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (!_slot[i]) { _slot[i] = t; break; }
    [self push];
}
- (void)touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self push]; }
- (void)drop:(NSSet<UITouch *> *)ts { for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (_slot[i] == t) _slot[i] = nil; [self push]; }
- (void)touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self drop:ts]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self drop:ts]; }
@end

// + / − 小钮：按下置位、抬起清位，经 dm_input_set 与手柄按位或 | + / − buttons: set on press, clear on release, ORed with the controller via dm_input_set
static _Atomic uint64_t g_btn;
@interface KirbyKey : UIButton
@property (assign) uint64_t bit;
@end
@implementation KirbyKey
- (void)down { atomic_fetch_or(&g_btn, self.bit); dm_input_set(atomic_load(&g_btn), 0, 0, 0, 0); }
- (void)up { atomic_fetch_and(&g_btn, ~self.bit); dm_input_set(atomic_load(&g_btn), 0, 0, 0, 0); }
@end
static KirbyKey *key_make(NSString *title, uint64_t bit) {
    KirbyKey *b = [KirbyKey buttonWithType:UIButtonTypeSystem]; b.bit = bit;
    [b setTitle:title forState:UIControlStateNormal]; b.titleLabel.font = [UIFont boldSystemFontOfSize:22];
    [b setTitleColor:[UIColor colorWithWhite:1 alpha:0.8] forState:UIControlStateNormal];
    b.backgroundColor = [UIColor colorWithWhite:0.15 alpha:0.45]; b.layer.cornerRadius = 22;
    [b addTarget:b action:@selector(down) forControlEvents:UIControlEventTouchDown];
    [b addTarget:b action:@selector(up) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
    return b;
}

// 画质钮：掌机 720p ⇄ 主机 1080p。dm_set_opmode 像 Switch 插拔底座一样通知游戏（hle_oe.c），游戏自己切渲染分辨率；选择存进 NSUserDefaults，下次启动沿用（main.m）
// Quality button: handheld 720p <-> docked 1080p. dm_set_opmode notifies the game like docking / undocking a Switch (hle_oe.c) and the game switches its render resolution itself; saved in NSUserDefaults for the next launch (main.m)
@interface KirbyQuality : UIButton
@end
@implementation KirbyQuality
- (void)refresh { [self setTitle:dm_opmode() ? @"1080p" : @"720p" forState:UIControlStateNormal]; }
- (void)tap {
    int m = !dm_opmode(); dm_set_opmode(m);
    [NSUserDefaults.standardUserDefaults setInteger:m forKey:@"kirby_opmode"];
    LOG("画质：切到 %s", m ? "主机 1080p" : "掌机 720p"); [self refresh];
}
@end
@interface KirbyBox : UIView
@property (strong) KirbyMetalView *game;
@property (strong) UIView *keys;
@end
@implementation KirbyBox
- (void)layoutSubviews {   // 16:9 居中；转屏 / 尺寸变化时重排 | 16:9 centred; re-laid out on rotation / size change
    [super layoutSubviews];
    CGRect b = self.bounds; CGFloat w = b.size.width, h = b.size.height;
    if (w / h > 16.0 / 9) w = round(h * 16 / 9); else h = round(w * 9 / 16);
    self.game.frame = CGRectMake(round((b.size.width - w) / 2), round((b.size.height - h) / 2), w, h);
    if (self.window.windowScene.screen) g_layer.contentsScale = self.window.windowScene.screen.nativeScale;   // 进窗口后才知道是哪块屏 | the screen is known only once in a window
    CGFloat sc = g_layer.contentsScale ?: 1;
    CGSize px = CGSizeMake(w * sc, h * sc);
    if (!CGSizeEqualToSize(px, g_layer.drawableSize)) { g_layer.drawableSize = px;
        if (self.window) LOG("画面：窗口 %.0fx%.0f 点，游戏视图 %.0fx%.0f 点（16:9 居中），出图 %.0fx%.0f 像素", b.size.width, b.size.height, w, h, px.width, px.height); }
    UIEdgeInsets in = self.safeAreaInsets;
    self.keys.frame = CGRectMake(b.size.width - in.right - 212, in.top + 12, 200, 44);
}
@end

UIView *front_ios_view(CGRect frame) {
    KirbyBox *box = [[KirbyBox alloc] initWithFrame:frame]; box.backgroundColor = UIColor.blackColor;
    KirbyMetalView *v = [[KirbyMetalView alloc] initWithFrame:frame]; box.game = v; [box addSubview:v];
    g_layer = (CAMetalLayer *)v.layer;
    g_layer.device = MTLCreateSystemDefaultDevice();
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    UIView *keys = [[UIView alloc] initWithFrame:CGRectZero]; box.keys = keys;
    KirbyKey *minus = key_make(@"−", DM_BTN_MINUS), *plus = key_make(@"+", DM_BTN_PLUS);
    KirbyQuality *q = [KirbyQuality buttonWithType:UIButtonTypeSystem];
    q.titleLabel.font = [UIFont boldSystemFontOfSize:15]; [q setTitleColor:[UIColor colorWithWhite:1 alpha:0.8] forState:UIControlStateNormal];
    q.backgroundColor = [UIColor colorWithWhite:0.15 alpha:0.45]; q.layer.cornerRadius = 22; q.frame = CGRectMake(0, 0, 76, 44);
    [q addTarget:q action:@selector(tap) forControlEvents:UIControlEventTouchUpInside]; [q refresh];
    minus.frame = CGRectMake(88, 0, 44, 44); plus.frame = CGRectMake(156, 0, 44, 44);
    [keys addSubview:q]; [keys addSubview:minus]; [keys addSubview:plus]; [box addSubview:keys];
    [box setNeedsLayout]; [box layoutIfNeeded];
    return box;
}
void *front_metal_layer(void) { return (__bridge void *)g_layer; }

// 卡比走自己的出图尺寸；这个值只有共用文件里 D2R 专属的分支在用。按 Switch 原生给：主机 1920x1080、掌机 1280x720。DM_GAME_RES=宽x高 覆盖主机档
// Kirby picks its own output size; this value is only used by D2R-only branches in shared files. Switch native: docked 1920x1080, handheld 1280x720. DM_GAME_RES=WxH overrides docked
uint64_t front_game_res(int docked) {
    unsigned w, h; const char *e = getenv("DM_GAME_RES");
    if (docked && e && sscanf(e, "%ux%u", &w, &h) == 2) return (uint64_t)h << 32 | w;
    return docked ? 1080ull << 32 | 1920 : 720ull << 32 | 1280;
}

// ---------- 手柄：实体手柄优先，没有就显示屏幕手柄 | controller: physical first, otherwise the on-screen one ----------
static GCVirtualController *g_vpad; static BOOL g_vpad_on;
static void vpad_update(void) {
    if (!g_vpad) return;
    BOOL phys = NO;
    for (GCController *c in GCController.controllers) if (c != g_vpad.controller && c.extendedGamepad) phys = YES;
    if (phys && g_vpad_on) { [g_vpad disconnect]; g_vpad_on = NO; LOG("手柄：已连实体手柄，收起屏幕手柄"); }
    else if (!phys && !g_vpad_on) {
        g_vpad_on = YES;
        [g_vpad connectWithReplyHandler:^(NSError *err) { if (err) { g_vpad_on = NO; LOG("【红】屏幕手柄显示失败：%s", err.localizedDescription.UTF8String); } else LOG("手柄：没有实体手柄，显示屏幕手柄"); }];
    }
}
static void pad_install(void) {   // 主线程 | main thread
    dm_gamepad_start(NULL);
    if (getenv("KIRBY_NO_VPAD")) return;
    GCVirtualControllerConfiguration *cfg = [GCVirtualControllerConfiguration new];
    cfg.elements = [NSSet setWithArray:@[GCInputLeftThumbstick, GCInputButtonA, GCInputButtonB, GCInputButtonX, GCInputButtonY,
                                         GCInputLeftShoulder, GCInputRightShoulder]];
    g_vpad = [[GCVirtualController alloc] initWithConfiguration:cfg];
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { (void)n; vpad_update(); }];
    [nc addObserverForName:GCControllerDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { (void)n; vpad_update(); }];
    vpad_update();
}

void front_run(void (*guest)(void)) {
    static char alt[65536];   // 信号备用栈是每线程的：客体线程栈溢出时崩溃打印也要能跑 | per-thread alt signal stack, so the crash printout still runs on a guest stack overflow
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt}; sigaltstack(&ss, 0);
    // 本线程由 UIKit 主线程派生，继承了它的信号屏蔽字；宿主的诊断信号（SIGUSR2 采样、SIGALRM 看门狗）要收得到，全部解除（同 front_ios.m）
    // This thread inherits the UIKit main thread's signal mask; the host's diagnostic signals (SIGUSR2 sampling, SIGALRM watchdog) must get through, so unblock all (same as front_ios.m)
    sigset_t all; sigfillset(&all); pthread_sigmask(SIG_UNBLOCK, &all, NULL);
    dispatch_async(dispatch_get_main_queue(), ^{ pad_install(); });
    guest();
}

// ---------- 软键盘输入框（front.h）：主线程弹框，调用线程阻塞到确定 / 取消 | text input box: main thread shows it, caller blocks until OK / Cancel ----------
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!g_layer || out_cap <= 0) return -1;
    (void)max_chars;   // 截断由 hle_swkbd.c 统一做 | truncation is done by hle_swkbd.c
    NSString *t = title && *title ? @(title) : @"输入";
    NSString *msg = [NSString stringWithFormat:@"%s%s%s", sub ? sub : "", sub && *sub && guide && *guide ? "\n" : "", guide ? guide : ""];
    NSString *init = initial ? @(initial) : @"";
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block int rc = 0; __block NSString *text = nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *a = [UIAlertController alertControllerWithTitle:t message:msg.length ? msg : nil preferredStyle:UIAlertControllerStyleAlert];
        [a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.text = init; f.clearButtonMode = UITextFieldViewModeWhileEditing; }];
        [a addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x) { (void)x; rc = 0; dispatch_semaphore_signal(done); }]];
        [a addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void)x; text = a.textFields.firstObject.text ?: @""; rc = 1; dispatch_semaphore_signal(done); }]];
        UIViewController *vc = nil;
        for (UIScene *s in UIApplication.sharedApplication.connectedScenes) if ([s isKindOfClass:UIWindowScene.class]) for (UIWindow *w in ((UIWindowScene *)s).windows) if (w.isKeyWindow) vc = w.rootViewController;
        while (vc.presentedViewController) vc = vc.presentedViewController;
        if (!vc) { rc = -1; dispatch_semaphore_signal(done); return; }
        [vc presentViewController:a animated:YES completion:nil];
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (rc == 1) { snprintf(out, (size_t)out_cap, "%s", text.UTF8String ?: ""); }
    return rc;
}

// ---------- 生命周期（口径同 front_mac.m：失活 2 / 后台 3 / 前台 1） | lifecycle (same rules as front_mac.m) ----------
void front_ios_lifecycle_install(void) {
    UIApplication.sharedApplication.idleTimerDisabled = YES;   // 玩的时候不自动锁屏 | no auto-lock while playing
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *q = NSOperationQueue.mainQueue;
    [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:q usingBlock:^(NSNotification *n) { (void)n; dm_focus_set(2); dm_gamepad_reset(); atomic_store(&g_btn, 0); dm_input_set(0, 0, 0, 0, 0); }];
    [nc addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:q usingBlock:^(NSNotification *n) { (void)n; dm_focus_set(3); dm_audio_suspend(1); dm_gfx_suspend(1); LOG("生命周期：进后台，暂停音频与 GPU"); }];
    [nc addObserverForName:UIApplicationWillEnterForegroundNotification object:nil queue:q usingBlock:^(NSNotification *n) { (void)n; dm_gfx_suspend(0); dm_audio_suspend(0); dm_focus_set(2); LOG("生命周期：回前台"); }];
    [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:q usingBlock:^(NSNotification *n) { (void)n; dm_focus_set(1); }];
}
