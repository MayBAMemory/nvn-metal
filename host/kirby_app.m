// 卡比的 macOS App 形态（双击起），取代上游的 app_mac.m（那份按 D2R 写：认 D2R 的资源、只装 main + sdk、存档放「D2R Mobile」）。
// 宿主被包成 .app（tools/mac_app.sh）且没带参数时，启动最前面替玩家把 host/run_mac.sh 那套准备好：
//   窗口开着、有声音；存档、玩过的管线清单、缺的着色器变体清单和 run_mac.sh 共用工作区的 mac/（不在时放 ~/Library/Application Support/Kirby Host）；日志 ~/Library/Logs/Kirby Host/host.log（留上一次的一份）；
//   改写后的游戏程序、macOS 版着色器库、管线清单：包里 Contents/Resources/<名> 有实物就用，没有就看 Resources/paths.txt 里「名=本机路径」
//   （这几样都从游戏内容派生，包里缺省不带，只指向这台机器上 tools/setup_mac.sh 做出来的工作区）；
//   RomFS：paths.txt 的 romfs= 能用就用，否则第一次启动请玩家选（记在偏好里）。
// 另外 kirby_mac_brand()：窗口标题换成 Kirby Host（上游 front_mac.m 写死「D2R Mobile」），命令行起也换。
// 命令行起（host/run_mac.sh、测试脚本）不走 App 形态：不在 .app 包里，或带了参数，一律原样。
// Kirby's macOS App form (started by double-click), replacing upstream's app_mac.m (written for D2R: recognizes D2R's resources, loads only main + sdk, saves under "D2R Mobile").
// When the host is packaged as an .app (tools/mac_app.sh) and started without arguments, the very first thing it does is what host/run_mac.sh would set up:
//   window and sound on; saves, the played-pipeline list and the missing-shader-variant lists shared with run_mac.sh in the work directory's mac/ (~/Library/Application Support/Kirby Host when absent); log in ~/Library/Logs/Kirby Host/host.log (the previous one kept);
//   the rewritten game program, the macOS shader library and the pipeline manifest: the real thing in Contents/Resources/<name> if present, otherwise the "name=local path" line in Resources/paths.txt
//   (all of these are derived from game content, so by default the package carries none of them and points at the work directory tools/setup_mac.sh built on this machine);
//   RomFS: romfs= from paths.txt if usable, otherwise the player is asked on first launch (remembered in preferences).
// Also kirby_mac_brand(): the window title becomes Kirby Host (upstream front_mac.m hard-codes "D2R Mobile"), for command-line starts too.
// Command-line starts (host/run_mac.sh, test scripts) never take the App form: outside an .app package, or with arguments, everything stays as it was.
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#include "dm.h"

#define APP_NAME "Kirby Host"
static NSString *const kRomfsKey = @"RomfsDir";
NSUserDefaults *kirby_defaults(void);   // kirby_input.m：和命令行起共用一份偏好（域 kirbyhost） | kirby_input.m: preferences shared with command-line starts (domain kirbyhost)
static int zh(void) { return [NSLocale.preferredLanguages.firstObject hasPrefix:@"zh"]; }
#define TXT(z, e) (zh() ? @z : @e)

// 是不是卡比 v1.0.0 的 RomFS：认 basil/App.bin 和 shader/Copy/Copy.bfsha。选到上一层（导出目录的根）也认，往里找一层
// Whether this is Kirby v1.0.0's RomFS: recognized by basil/App.bin and shader/Copy/Copy.bfsha. Choosing one level up (the dump root) is accepted too, looking one level in
static NSString *romfs_root(NSString *dir, NSString **why) {
    NSFileManager *fm = NSFileManager.defaultManager;
    for (NSString *sub in @[@"", @"RomFS", @"romfs"]) {
        NSString *d = sub.length ? [dir stringByAppendingPathComponent:sub] : dir;
        NSString *a = [d stringByAppendingPathComponent:@"basil/App.bin"], *s = [d stringByAppendingPathComponent:@"shader/Copy/Copy.bfsha"];
        if ([fm fileExistsAtPath:a] && [fm fileExistsAtPath:s]) {
            if (![fm isReadableFileAtPath:a]) { if (why) *why = TXT("这个文件夹读不了（权限？）", "This folder cannot be read (permissions?)"); return nil; }
            return d;
        }
    }
    if (why) *why = [NSString stringWithFormat:TXT("「%@」不是游戏的 RomFS 文件夹（里面应有 basil、shader 等文件夹）", "\"%@\" is not the game's RomFS folder (it should contain basil, shader and other folders)"), dir.lastPathComponent];
    return nil;
}
static void alert(NSString *title, NSString *text) {
    NSAlert *a = [NSAlert new]; a.messageText = title; a.informativeText = text; [a addButtonWithTitle:TXT("好", "OK")]; [a runModal];
}
// 请玩家选 RomFS 文件夹，直到选对或取消。返回 RomFS 根目录，取消返回 nil
// Ask the player for the RomFS folder until it is right or cancelled. Returns the RomFS root, or nil on cancel
static NSString *pick_romfs(NSString *first_why) {
    NSString *why = first_why;
    for (;;) {
        NSOpenPanel *p = [NSOpenPanel openPanel];
        p.canChooseDirectories = YES; p.canChooseFiles = NO; p.allowsMultipleSelection = NO; p.prompt = TXT("用这个", "Use This");
        p.message = why ? [NSString stringWithFormat:TXT("%@。请重新选择。", "%@. Please choose again."), why]
                        : TXT("请选择你自己导出的游戏 RomFS 文件夹（tools/setup_mac.sh 做出来的是 ~/kirby-work/RomFS）", "Choose the game's RomFS folder you dumped yourself (tools/setup_mac.sh puts it in ~/kirby-work/RomFS)");
        if ([p runModal] != NSModalResponseOK) return nil;
        NSString *w = nil, *r = romfs_root(p.URL.path, &w);
        if (r) return r;
        alert(TXT("不是游戏的 RomFS", "Not the game's RomFS"), w); why = w;
    }
}
// 包里的一项：实物在 Contents/Resources/<名> 就用它；没有就看 Resources/paths.txt 里「名=本机路径」那一行（路径不存在返回 nil）
// One package item: the real thing in Contents/Resources/<name> if present; otherwise the "name=local path" line in Resources/paths.txt (nil when that path does not exist)
static NSString *res_item(NSString *name) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *rp = NSBundle.mainBundle.resourcePath, *d = [rp stringByAppendingPathComponent:name];
    if ([fm fileExistsAtPath:d]) return d;
    NSString *txt = [NSString stringWithContentsOfFile:[rp stringByAppendingPathComponent:@"paths.txt"] encoding:NSUTF8StringEncoding error:nil];
    NSString *pre = [name stringByAppendingString:@"="];
    for (NSString *ln in [txt componentsSeparatedByString:@"\n"])
        if ([ln hasPrefix:pre]) { d = [ln substringFromIndex:pre.length]; return [fm fileExistsAtPath:d] ? d : nil; }
    return nil;
}
static void setenv_default(const char *k, NSString *v) { if (v && !getenv(k)) setenv(k, v.fileSystemRepresentation, 1); }

// main() 最前面调。是 App 形态就把环境与参数备好并返回 1；不是返回 0、什么都不动
// Called at the very start of main(). In App form it prepares the environment and arguments and returns 1; otherwise it returns 0 and touches nothing
int dm_app_bootstrap(int *argc, char ***argv) {
    @autoreleasepool {
        NSString *bp = NSBundle.mainBundle.bundlePath;
        if (*argc > 1 || ![bp.pathExtension isEqualToString:@"app"]) return 0;
        NSFileManager *fm = NSFileManager.defaultManager;
        NSString *sup = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@APP_NAME];
        if (getenv("KIRBY_APP_SUPPORT")) sup = @(getenv("KIRBY_APP_SUPPORT"));   // 测试：存档等放到指定目录，不碰玩家那份 | test: saves etc. in a given directory, leaving the player's own alone
        NSString *logd = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Logs/" APP_NAME];
        // 存档和清单：paths.txt 的 state=（工作区的 mac/，和 host/run_mac.sh 同一份）能用就用，双击和命令行玩的是同一个存档；否则放 sup
        // saves and lists: state= from paths.txt (the work directory's mac/, the same one host/run_mac.sh uses) when usable, so double-click and command line share one save; otherwise sup
        NSString *state = getenv("KIRBY_APP_SUPPORT") ? sup : (res_item(@"state") ?: sup);
        NSString *save = [state stringByAppendingPathComponent:@"save"];
        for (NSString *d in @[sup, logd, save]) [fm createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
        // 双击起没有终端：日志落文件，上一次的留一份 | a double-click start has no terminal: the log goes to a file, the previous one is kept
        NSString *lf = [logd stringByAppendingPathComponent:@"host.log"], *prev = [logd stringByAppendingPathComponent:@"host.prev.log"];
        [fm removeItemAtPath:prev error:nil]; [fm moveItemAtPath:lf toPath:prev error:nil];
        if (!isatty(2)) { freopen(lf.fileSystemRepresentation, "w", stderr); setvbuf(stderr, NULL, _IOLBF, 0); }
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular]; [NSApp activateIgnoringOtherApps:YES];
        NSString *prod = res_item(@"product"), *shlib = res_item(@"shlib");
        NSString *shmap = [shlib stringByAppendingPathComponent:@"shader_map.tsv"];
        if (!prod || ![fm fileExistsAtPath:[prod stringByAppendingPathComponent:@"main.patched.bin"]] || !shlib || ![fm fileExistsAtPath:shmap]) {
            LOG("【红】App：找不到改写后的游戏程序（%s）或着色器库（%s）", prod.UTF8String ?: "无", shlib.UTF8String ?: "无");
            alert(TXT("缺游戏程序或着色器库", "Game program or shader library missing"),
                  TXT("这个 App 只指向构建它的那台 Mac 上的工作区（~/kirby-work）。工作区被移走或删了：重跑 tools/setup_mac.sh。",
                      "This App only points at the work directory (~/kirby-work) on the Mac that built it. If that was moved or deleted, run tools/setup_mac.sh again."));
            exit(1);
        }
        NSUserDefaults *ud = kirby_defaults();
        NSString *why = nil, *rom = nil, *saved = [ud stringForKey:kRomfsKey];
        if (getenv("KIRBY_APP_ROMFS")) {   // 测试（没人点对话框时用）：直接给 RomFS；不对就写日志退出，不弹框、不记偏好 | test (nobody to click the dialog): give the RomFS directly; if wrong, log and exit, no dialog, nothing saved
            rom = romfs_root(@(getenv("KIRBY_APP_ROMFS")), &why);
            if (!rom) { LOG("【红】App：KIRBY_APP_ROMFS 不对：%s", why.UTF8String); exit(2); }
        } else {
            NSString *built = res_item(@"romfs");
            if (saved) rom = romfs_root(saved, &why);
            if (!rom && built) rom = romfs_root(built, NULL);
            if (!rom) {
                rom = pick_romfs(saved ? [NSString stringWithFormat:TXT("上次用的 RomFS 文件夹不能用了：%@", "The RomFS folder used last time is no longer usable: %@"), why] : nil);
                if (!rom) { LOG("App：没有选 RomFS 文件夹，退出"); exit(0); }
                [ud setObject:rom forKey:kRomfsKey];
            }
        }
        setenv("DM_WINDOW", "1", 1);
        if (!getenv("DM_LANG")) setenv("DM_LANG", zh() ? "zh-Hans" : "en", 1);   // 菜单文案（kirby_glue.c dm_ui）跟系统语言 | menu text (kirby_glue.c dm_ui) follows the system language
        setenv_default("DM_AUDIO", @"1");   // 同 run_mac.sh 开窗口玩 | as run_mac.sh does for windowed play
        setenv_default("DM_ROMFS", rom); setenv_default("DM_SAVE", save);   // 玩过的管线 pso_user.tsv 写在 DM_SAVE 的上一层 = state | the played-pipeline list pso_user.tsv goes in DM_SAVE's parent = state
        setenv_default("DM_NVN_SHMAP", shmap); setenv_default("DM_MSL_DIR", shlib);
        setenv_default("DM_PSO_MANIFEST", res_item(@"pso_manifest.tsv"));
        [fm changeCurrentDirectoryPath:state];   // 缺的着色器变体清单（uint_masks.tsv、iattr_masks.tsv）写在当前目录 | missing-variant lists are written to the current directory
        static char *av[6]; av[0] = (*argv)[0]; av[1] = strdup(prod.fileSystemRepresentation); av[2] = "main"; av[3] = "sdk"; av[4] = "subsdk0"; av[5] = NULL;
        *argv = av; *argc = 5;
        LOG("App：存档 %s；RomFS %s；程序 %s；着色器库 %s；日志 %s", save.UTF8String, rom.UTF8String, prod.UTF8String, shlib.UTF8String, lf.UTF8String);
        return 1;
    }
}

// 窗口标题：上游 front_mac.m 建窗口时写死「D2R Mobile」。这里在主线程循环跑起来（窗口已建好）后改掉；无窗口时什么都不做
// Window title: upstream front_mac.m hard-codes "D2R Mobile" when it creates the window. This changes it once the main run loop is running (the window exists); nothing happens headless
void kirby_mac_brand(void) {
    if (!getenv("DM_WINDOW")) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        for (NSWindow *w in NSApp.windows) if ([w.title isEqualToString:@"D2R Mobile"]) w.title = @APP_NAME;
    });
}
#else
void kirby_mac_brand(void) {}
#endif
