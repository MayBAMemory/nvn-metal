// 卡比宿主的垫片：共用的 dm/host、dm/gfx 文件会调到一些定义在 D2R 专属文件里的函数（diag.c、dm_*.c、hle_tact.c、touch_mac.c 等），
// 卡比不编这些文件，这里给出中性的实现。清单由 kirby/docs/host_split.md 的依赖扫描得出；链接报缺符号时往这里补，并在文档里记一笔。
// 原则：不碰游戏内存，不写任何 D2R 地址；返回「没有这项功能」的值。
// 图形层 nvn_mtl.m / hle_nvn.c 已有弱定义缺省的（dm_bright_k、dm_fps_setting、dm_prof_ribbon_ns、dm_shot_written、dm_perf_present、
// dm_longframe_note、dm_thermal_now、dm_brightness_now）这里不重复定义。
// Kirby host shims: shared dm/host and dm/gfx files call some functions defined in D2R-only files (diag.c, dm_*.c, hle_tact.c, touch_mac.c etc.).
// Kirby does not build those files, so neutral implementations are given here. The list comes from the dependency scan in kirby/docs/host_split.md; when linking reports a missing symbol, add it here and note it in the doc.
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
const char *dm_ui(const char *key) { return key; }    // 宿主界面文案：没有表就原样给键名 | host UI strings: with no table, return the key itself
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
