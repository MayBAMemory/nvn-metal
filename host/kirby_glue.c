// 卡比宿主的垫片：共用的 dm/host、dm/gfx 文件会调到一些定义在 D2R 专属文件里的函数（diag.c、dm_*.c、hle_tact.c、touch_mac.c 等），
// 卡比不编这些文件，这里给出中性的实现。清单由 docs/host_split.md 的依赖扫描得出；链接报缺符号时往这里补，并在文档里记一笔。
// 原则：不碰游戏内存，不写任何 D2R 地址；返回「没有这项功能」的值。
// 图形层 nvn_mtl.m / hle_nvn.c 已有弱定义缺省的（dm_bright_k、dm_fps_setting、dm_prof_ribbon_ns、dm_shot_written、dm_perf_present、
// dm_longframe_note、dm_thermal_now、dm_brightness_now）这里不重复定义。
// Kirby host shims: shared dm/host and dm/gfx files call some functions defined in D2R-only files (diag.c, dm_*.c, hle_tact.c, touch_mac.c etc.).
// Kirby does not build those files, so neutral implementations are given here. The list comes from the dependency scan in docs/host_split.md; when linking reports a missing symbol, add it here and note it in the doc.
// Rules: never touch game memory, never write any D2R address; return the "this feature does not exist" value.
// Symbols that already have weak defaults in the graphics layer nvn_mtl.m / hle_nvn.c (dm_bright_k, dm_fps_setting, dm_prof_ribbon_ns, dm_shot_written, dm_perf_present,
// dm_longframe_note, dm_thermal_now, dm_brightness_now) are not redefined here.
#include "dm.h"
#include "front.h"

// ---- diag.c：启动计时与诊断 | diag.c: boot timing and diagnostics ----
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
void dm_prof_mark(const char *seg) { if (g_flag.bootprof) LOG("[启动] %.1f ms  %s", (now_ns() - g_t0_ns) / 1e6, seg); }
int dm_ingame_now(void) { return 0; }                 // D2R「在局内」；卡比不用 | D2R "in a run"; unused by Kirby
int dm_request_save(void) { return 0; }               // D2R 退出前请求存盘；卡比自己管存档 | D2R save request before exit; Kirby manages its own saves
int dm_unpause_for_exit(void) { return 0; }
void dm_quiet_difficulty_msg(int on) { (void)on; }
uint64_t dm_server_tick(void) { return UINT64_MAX; }   // 没有服务端拍号 | no server tick
void uimsg_subscribe(void (*fn)(const char *cat, const char *act, const char *arg)) { (void)fn; }   // D2R 的界面消息总线；卡比没有 | D2R's UI message bus; Kirby has none
void dm_input_sampled_right(int32_t *rx, int32_t *ry) { *rx = 0; *ry = 0; }

// ---- dm_i18n.c、dm_custom.c：语言与自定义页 | language and custom page ----
// 宿主界面文案：上游 Mac 外壳（front_mac.m）的菜单和提示框按键名取字；卡比只给它用到的这几条，别的原样给键名。
// 语言看 DM_LANG（.app 起时 kirby_app.m 按系统首选语言设），没设看 LANG；en 开头用英文，否则中文
// Host UI strings: the upstream Mac shell (front_mac.m) looks up its menu and dialog text by key; Kirby supplies only the ones it uses and returns other keys as they are.
// Language: DM_LANG (set from the system's preferred language by kirby_app.m for .app starts), else LANG; English when it starts with en, otherwise Chinese
static const struct { const char *k, *zh, *en; } k_ui[] = {
    {"dmAppQuit", "退出 Kirby Host", "Quit Kirby Host"},
    {"dmAppMenuView", "显示", "View"},
    {"dmAppFullscreen", "全屏", "Toggle Full Screen"},
    {"dmAppMenuHelp", "帮助", "Help"},
    {"dmAppKeysTitle", "键位设置…", "Key Settings…"},   // 卡比的输入装好后这一项改为打开键位设置窗口（kirby_input.m） | once Kirby's input is installed this item opens the key settings window (kirby_input.m)
    {"dmAppKeysText", "缺省键位：WASD 左摇杆；J K U I = A B X Y；Q E = L R；Z C = ZL ZR；回车 = +；Tab = −；方向键 = 十字键。\n\n改键：菜单「键位设置…」（⌘,）。",
                      "Default keys: WASD left stick; J K U I = A B X Y; Q E = L R; Z C = ZL ZR; Return = +; Tab = −; arrows = D-pad.\n\nRemap: menu \"Key Settings…\" (⌘,)."},
    {"dmAppOK", "好", "OK"},
};
const char *dm_ui(const char *key) {
    const char *l = getenv("DM_LANG"); if (!l || !*l) l = getenv("LANG");
    int en = l && !strncmp(l, "en", 2);
    for (unsigned i = 0; i < sizeof k_ui / sizeof *k_ui; i++) if (!strcmp(k_ui[i].k, key)) return en ? k_ui[i].en : k_ui[i].zh;
    return key;
}
int dm_sys_lang(void) { return 8; }                   // 只在 D2R 的设置文件逻辑里用（已由 dm_game_d2r 挡住） | only used by D2R's settings-file logic (already gated by dm_game_d2r)
int dm_custom_lang_explicit(void) { return -1; }      // 「没设过」 | "never set"

// ---- dm_uiscreen.c：画面尺寸 | screen size ----
uint64_t dm_game_res(int docked) { return front_game_res(docked); }   // 游戏出图分辨率直接问前端 | ask the front end for the render resolution directly
int dm_screen_real(int *w, int *h) { *w = *h = 0; return 0; }
void dm_ui_rect(int *x, int *y, int *w, int *h) { *x = *y = *w = *h = 0; }
int dm_uiscreen_ingame(void) { return 0; }
void dm_uiscreen_res_reread(void) {}

// ---- dm_cursor.c、dm_freetp.c、dm_padmode.c：D2R 的界面功能 | D2R UI features ----
void dm_ui_text_entered(const char *text) { (void)text; }
int dm_freetp_zr_free(int zl) { (void)zl; return 0; }
void dm_tp_request(void) {}
void dm_padmode_request(int pad) { (void)pad; }

// ---- touch_mac.c：Mac 上用鼠标模拟触点（D2R 的触屏测试） | Mac mouse-as-touch (D2R touch testing) ----
void dm_touch_host_live(int op, float nx, float ny) { (void)op; (void)nx; (void)ny; }
void dm_touch_host_live_enable(void) {}

// ---- hle_tact.c、hle_granny.c：D2R 的资源存储与动画库 | D2R resource storage and animation library ----
uint64_t svc_host_call(int num, uint64_t *regs) { (void)num; (void)regs; return 0; }   // 0x80 起的宿主调用只由 D2R 的剪口产生 | host calls from 0x80 up are only produced by D2R cuts
void tact_prefetch_stop(void) {}
void granny_prof_dump(void) {}
