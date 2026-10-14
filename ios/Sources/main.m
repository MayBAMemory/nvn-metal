// 卡比 iOS 外壳的入口：开日志、找 RomFS、设好宿主要读的环境变量，场景到前台后在 64 MB 栈的线程上调 dm_main
// （kirby/host/kirby_main.c 以 -Dmain=dm_main 编入）。改写后的模块映像在构建时就编进了可执行文件（dm/ios/tools/gen_embed.py），
// 装载器在 iOS 上只把这些段 remap 回原布局，不在运行时写代码页——iOS 不许。
// 参照 dm/ios/Sources/main.m（D2R）的 boot_prepare / boot_thread；D2R 的存档同步、资源下载、覆盖层、补充包、测试探针都不要。
// Entry of Kirby's iOS shell: opens the log, finds the RomFS, sets the environment variables the host reads, and once the scene is in the foreground calls
// dm_main on a 64 MB-stack thread (kirby/host/kirby_main.c is built in with -Dmain=dm_main). The rewritten module images are compiled into the executable at
// build time (dm/ios/tools/gen_embed.py); on iOS the loader only remaps those segments back to the original layout and never writes code pages at run time -- iOS forbids that.
// Modelled on boot_prepare / boot_thread in dm/ios/Sources/main.m (D2R); D2R's save sync, asset download, overlays, legacy pack and test probes are all left out.
#import <UIKit/UIKit.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

int dm_main(int argc, char **argv);
void dm_host_preload_start(int argc, char **argv);
void dm_host_shutdown(const char *why);
UIView *front_ios_view(CGRect frame);
void front_ios_lifecycle_install(void);

static void plog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void plog(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fputs("[kirby] ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}
static NSString *docs(void) { return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0]; }

// 日志：Documents/boot.txt（上一轮改名成 boot.previous.txt）。标准错误 / 输出都重定向进来：宿主的 LOG、NSLog 都在里面。
// Log: Documents/boot.txt (the previous run is renamed boot.previous.txt). stderr / stdout are redirected into it: the host's LOG and NSLog both land there.
static void log_open(void) {
    NSString *cur = [docs() stringByAppendingPathComponent:@"boot.txt"], *prev = [docs() stringByAppendingPathComponent:@"boot.previous.txt"];
    rename(cur.fileSystemRepresentation, prev.fileSystemRepresentation);
    int fd = open(cur.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { dup2(fd, 2); dup2(fd, 1); close(fd); }
    setvbuf(stderr, NULL, _IONBF, 0); setvbuf(stdout, NULL, _IOLBF, 0);
    NSDictionary *info = NSBundle.mainBundle.infoDictionary;
    plog("boot: 卡比宿主 iOS 版，构建 %s，系统 %s，设备 %s", [info[@"CFBundleVersion"] UTF8String], UIDevice.currentDevice.systemVersion.UTF8String,
         UIDevice.currentDevice.model.UTF8String);
}

// RomFS：根目录下要有 gfx/ 和 shader/（卡比 RomFS 的两个大目录，见 kirby/README.md）。按序找，先到先得：
//   1. Library/Application Support/romfs   开发时用 run.sh romfs 从 Mac 推进去
//   2. Documents 里 3 层以内任何一个合格的文件夹（Finder / 「文件」App 拷进来的，文件夹叫什么都行）
// RomFS: its root must contain gfx/ and shader/ (the two big directories of Kirby's RomFS, see kirby/README.md). Searched in order, first hit wins:
//   1. Library/Application Support/romfs   pushed from the Mac with run.sh romfs during development
//   2. any qualifying folder within 3 levels of Documents (copied in via Finder / the Files app; the folder name does not matter)
static BOOL romfs_ok(NSString *d) {
    BOOL a = NO, b = NO; NSFileManager *fm = NSFileManager.defaultManager;
    return [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"gfx"] isDirectory:&a] && a &&
           [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"shader"] isDirectory:&b] && b;
}
static NSString *romfs_find_in(NSString *dir, int depth) {
    if (romfs_ok(dir)) return dir;
    if (depth <= 0) return nil;
    for (NSString *n in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil] sortedArrayUsingSelector:@selector(compare:)]) {
        if ([n hasPrefix:@"."] || [@[@"save", @"temp"] containsObject:n]) continue;
        BOOL isdir = NO; NSString *p = [dir stringByAppendingPathComponent:n];
        if ([NSFileManager.defaultManager fileExistsAtPath:p isDirectory:&isdir] && isdir) { NSString *r = romfs_find_in(p, depth - 1); if (r) return r; }
    }
    return nil;
}
static NSString *romfs_locate(void) {
    NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)[0];
    NSString *dev = [support stringByAppendingPathComponent:@"romfs"];
    [NSFileManager.defaultManager createDirectoryAtPath:dev withIntermediateDirectories:YES attributes:nil error:nil];
    [[NSURL fileURLWithPath:dev] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];   // 6 GB 不进 iCloud 备份 | keep 6 GB out of iCloud backup
    if (romfs_ok(dev)) return dev;
    return romfs_find_in(docs(), 3);
}

static char *g_argv[8]; static int g_argc;
static NSString *g_romfs;
static BOOL boot_prepare(void) {
    struct rlimit rl; if (!getrlimit(RLIMIT_NOFILE, &rl)) { rlim_t was = rl.rlim_cur; rl.rlim_cur = rl.rlim_max < 4096 ? rl.rlim_max : 4096; setrlimit(RLIMIT_NOFILE, &rl);
        plog("boot: 文件描述符软上限 %llu → %llu", (unsigned long long)was, (unsigned long long)rl.rlim_cur); }
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"dm"], *doc = docs();
    g_romfs = romfs_locate();
    if (!g_romfs) { plog("boot: 🔴 没找到 RomFS（根目录要有 gfx/ 和 shader/）"); return NO; }
    plog("boot: RomFS %s", g_romfs.fileSystemRepresentation);
    for (NSString *d in @[@"save", @"temp"]) [fm createDirectoryAtPath:[doc stringByAppendingPathComponent:d] withIntermediateDirectories:YES attributes:nil error:nil];
    setenv("DM_ROMFS", g_romfs.fileSystemRepresentation, 0);
    setenv("DM_SAVE", [doc stringByAppendingPathComponent:@"save"].fileSystemRepresentation, 0);
    setenv("DM_TEMP", [doc stringByAppendingPathComponent:@"temp"].fileSystemRepresentation, 0);
    setenv("DM_NVN_SHMAP", [res stringByAppendingPathComponent:@"shader_map.tsv"].fileSystemRepresentation, 0);
    setenv("DM_MSL_DIR", [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"msl"].fileSystemRepresentation, 0);
    // 工作目录 = Documents：环境变量里的相对路径（截图名、日志名等）都落在这里 | cwd = Documents: relative paths in env vars (screenshot names, log names...) land here
    if (chdir(doc.fileSystemRepresentation)) plog("boot: 🔴 chdir Documents 失败 %s", strerror(errno));
    g_argv[0] = "kirbyhost"; g_argv[1] = strdup(res.fileSystemRepresentation); g_argv[2] = "main"; g_argv[3] = "sdk"; g_argv[4] = "subsdk0"; g_argc = 5;
    return YES;
}
static void *boot_thread(void *arg) {
    (void)arg;
    plog("boot: dm_main %s", g_argv[1]);
    int rc = dm_main(g_argc, g_argv);
    plog("boot: dm_main 返回 %d", rc);
    plog("DONE");
    return NULL;
}
static int g_host_up;
static void host_start(void) {   // 主线程；场景第一次到前台时调 | main thread; called when the scene first comes to the foreground
    if (g_host_up) return; g_host_up = 1;
    // 宿主装载（重定位 / 强制接管）先在后台线程跑，与界面收尾并行；dm_main 进来会等它 | host loading runs on a background thread in parallel with the UI; dm_main waits for it
    dm_host_preload_start(g_argc, g_argv);
    front_ios_lifecycle_install();
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64 << 20);
    pthread_t t; if (pthread_create(&t, &a, boot_thread, NULL)) plog("boot: 🔴 起宿主线程失败");
}

// ---------- 没找到 RomFS 时的说明页 | instructions page when no RomFS is found ----------
@interface KirbyInfoVC : UIViewController
@end
@implementation KirbyInfoVC
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor;
    UILabel *l = [UILabel new]; l.numberOfLines = 0; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:15];
    l.text = @"没找到游戏资源（RomFS）。\n\n"
              "办法一（开发）：在 Mac 上运行  kirby/ios/run.sh romfs <RomFS 目录>\n"
              "办法二：用 Finder 或「文件」App 把整个 RomFS 文件夹拷进本 App 的文稿目录（文件夹里要有 gfx 和 shader）。\n\n"
              "拷好后把 App 完全退出再打开。";
    l.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:l];
    [NSLayoutConstraint activateConstraints:@[[l.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor], [l.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
                                              [l.widthAnchor constraintLessThanOrEqualToAnchor:self.view.widthAnchor multiplier:0.85]]];
}
@end

@interface KirbyGameVC : UIViewController
@end
@implementation KirbyGameVC
- (void)loadView { self.view = front_ios_view(CGRectMake(0, 0, 1280, 720)); }   // 先给个占位尺寸，进窗口后按实际大小重排（KirbyBox） | placeholder size; re-laid out to the real size once in the window
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }   // 边缘手势先给游戏，第二次滑才给系统 | edge swipes go to the game first
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)opts {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = g_romfs ? [KirbyGameVC new] : [KirbyInfoVC new];
    [self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene *)scene { if (g_romfs) host_start(); }
@end

@interface App : UIResponder <UIApplicationDelegate>
@end
@implementation App
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts { log_open(); boot_prepare(); return YES; }
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)s options:(UISceneConnectionOptions *)o {
    UISceneConfiguration *c = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:s.role]; c.delegateClass = SceneDelegate.class; return c;
}
- (void)applicationWillTerminate:(UIApplication *)app { if (g_host_up) dm_host_shutdown("App 终止"); }
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(App.class)); }
}
