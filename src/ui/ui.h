#ifndef UTGARD_UI_H
#define UTGARD_UI_H

/* Shared by the ui_*.c modules and main.c: the window is one program split
   by concern, so its state and helpers are visible to all of them. */

#define COBJMACROS
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <strsafe.h>
#include "fileio.h"
#include <string.h>
#include "zapret.h"
#include "link.h"
#include "profiles.h"
#include "ask.h"
#include "net.h"
#include "genconf.h"
#include "singbox.h"
#include "apps.h"
#include "pick.h"
#include "settings.h"
#include "autostart.h"
#include "shellopen.h"
#include "update.h"
#include "version.h"
#include "tray.h"
#include "lists.h"
#include <stdlib.h>
#include <time.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <windowsx.h>

/* ---- palette ------------------------------------------------------- */

#define CLR_BG        RGB(0x1F, 0x21, 0x34)
#define CLR_FOOTER    RGB(0x1A, 0x1C, 0x2C)
#define CLR_SURFACE   RGB(0x2C, 0x2E, 0x40)
#define CLR_LINE      RGB(0x3A, 0x3C, 0x4C)
#define CLR_TEXT      RGB(0xE8, 0xEA, 0xF0)
#define CLR_MUTED     RGB(0x88, 0x9E, 0xA8)
#define CLR_ACCENT    RGB(0xFF, 0xDE, 0x7D)
#define CLR_ACCENT_LO RGB(0xD9, 0xBB, 0x63)   /* accent, pressed */
#define CLR_ACCENT_HI RGB(0xFF, 0xE9, 0xA6)   /* accent, under the pointer */
#define CLR_OK        RGB(0xBA, 0xD8, 0x9A)
#define CLR_WARN      RGB(0xA8, 0x6B, 0x86)
#define CLR_WARN_LO   RGB(0x8F, 0x5B, 0x72)   /* warn, pressed: as accent, x0.85 */
#define CLR_WARN_HI   RGB(0xC5, 0x9B, 0xAE)   /* warn, under the pointer: 1/3 to white */
/* ---- control ids ---------------------------------------------------- */

#define ID_TAB_UTGARD 101
#define ID_TAB_ZAPRET 102
#define ID_TOGGLE     201
#define ID_PICK_PATH  301
#define ID_STRATEGIES 302
#define ID_ZAP_START  303
#define ID_ZAP_STOP   304
#define ID_ZAP_RESTART 305
#define ID_PROFILES   401
#define ID_PROF_ADD   402
#define ID_PROF_DEL   403
#define ID_PROF_SUB   404
#define ID_EDIT_HOSTS 405
#define ID_EDIT_APPS  406
#define ID_ZAP_FIX    407
#define ID_ZAP_GAME   408
#define ID_ZAP_IPSET  409
#define ID_ZAP_IPUPD  410
#define ID_ZAP_HOSTS  411
#define ID_APPS_LIST  501
#define ID_APPS_BACK  502
#define ID_APPS_PICK  503
#define ID_APPS_MANUAL 504
#define ID_HOSTS_EDIT 601
#define ID_HOSTS_BACK 602
#define ID_HOSTS_TIDY 603
#define ID_HOSTS_SAVE 604
#define ID_PICK_SEARCH 701
#define ID_PICK_LIST  702
#define ID_PICK_BACK  703
#define ID_PICK_SAVE  704
#define TIMER_PICK    2
#define TIMER_SUB     3     /* checks once a minute whether the subscription is due */
/* The manual editor: two sections, each a stack of rows made of a field,
   a plus and a minus. The rows are created once and shown as needed, so
   adding or removing one never destroys what is typed in the others. */
#define ED_ROWS        6
#define ID_ED_NAME     800     /* + row */
#define ID_ED_NPLUS    820
#define ID_ED_NMINUS   840
#define ID_ED_PATH     860
#define ID_ED_PPLUS    880
#define ID_ED_PMINUS   900
#define ID_ED_BACK     920
#define ID_ED_SAVE     921
#define ID_SET_OPEN    950
#define ID_SET_MTU     951
#define ID_SET_LOG     952
#define ID_SET_BACK    953
#define ID_SET_SAVE    954
#define ID_SET_STACK   955
#define ID_SET_DNS     956
#define ID_SET_TRAY    957
#define ID_SET_SUB     958
#define ID_SET_AUTO    961
#define ID_SET_UPD     962
#define ID_SET_UPD_NOW 963
#define ID_SET_V_UTGARD  964   /* version links on the settings page */
#define ID_SET_V_SINGBOX 965
#define ID_SET_V_AWG     966
#define ID_PING_NOW    959
#define ID_ZAP_LIST    960
#define LIST_TEXT_MAX 262144
/* the fetch thread hands its result back through the message queue */
#define WM_APP_SUB_DONE  (WM_APP + 1)
#define WM_APP_PING_ONE  (WM_APP + 2)
#define WM_APP_PING_DONE (WM_APP + 3)
#define WM_APP_INSTALL     (WM_APP + 4)
#define WM_APP_INSTALL_ASK (WM_APP + 5)
#define WM_APP_EXC_DONE    (WM_APP + 6)
#define WM_APP_EXC_START   (WM_APP + 7)
#define WM_APP_TRAY        (WM_APP + 8)     /* notification-area icon events */
#define WM_APP_SHOW        (WM_APP + 9)     /* a second launch asks us to show */
#define WM_APP_UPD_DONE    (WM_APP + 11)    /* release check finished */
#define WM_APP_JOB_STAGE   (WM_APP + 30)    /* a job moved on: lp is a string literal */
#define WM_APP_JOB_DONE    (WM_APP + 10)    /* a background job finished */
#define ID_TRAY_OPEN   1901
#define ID_TRAY_VPN    1902
#define ID_TRAY_EXIT   1903
#define TIMER_STATUS  1
/* undocumented in mingw's dwmapi.h; documented by Microsoft for Win11 22000+ */
#define UTG_DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define UTG_DWMWA_CAPTION_COLOR           35
/* ---- layout --------------------------------------------------------- */

#define TABS_H   S(40)
#define FOOTER_H S(38)
#define PAD      S(18)
#define APP_ZONE_TOGGLE 96
#define APP_ZONE_DELETE 84

/* button kinds, stored in GWLP_USERDATA */
enum { BK_TAB = 0, BK_PRIMARY, BK_SECONDARY, BK_DANGER, BK_CHECK, BK_LINK };
/* pages */
enum { PAGE_UTGARD = 0, PAGE_ZAPRET, PAGE_APPS, PAGE_HOSTS, PAGE_PICK, PAGE_EDIT,
       PAGE_SETTINGS };
/* The list page serves two files: the VPN site list, compiled for sing-box,
   and zapret's own list-general-user.txt, which zapret reads as plain text. */
enum { HOSTS_VPN = 0, HOSTS_ZAPRET };
/* A copy of the server names, taken on the UI thread, for workers that need
   to resolve them without touching g_prof. */
typedef struct {
    char host[PROFILES_MAX][256];
    int  count;
} host_snapshot;
/* ---- background jobs ---------------------------------------------------
   Anything that can take seconds - starting and stopping sing-box, compiling
   the site list, zapret's service operations, downloads - runs on a worker
   thread. The worker sees only what was copied into its job on the UI
   thread, never the window's globals, and reports back with a message. One
   job at a time; while it runs the action buttons are greyed and the status
   line says what is happening. */

typedef struct long_job long_job;
typedef void (*job_work)(long_job *j);            /* worker thread */
typedef void (*job_done)(HWND hwnd, long_job *j); /* UI thread */
struct long_job {
    HWND          hwnd;
    job_work      work;
    job_done      done;
    int           ok;
    wchar_t       msg[SB_MSG_MAX];
    int           flag;                  /* job-specific switch */
    int           n1, n2;                /* job-specific results */
    wchar_t       dir[ZAPRET_PATH_MAX];
    wchar_t       name[ZAPRET_NAME_MAX];
    wchar_t       temp[MAX_PATH * 2];
    char         *text;                  /* heap, job-specific */
    void         *extra;                 /* heap, job-specific */
    size_t        extra_size;            /* wiped before free: may hold secrets */
    host_snapshot hosts;
    char          ips[64][16];
};
typedef struct {
    HWND    hwnd;
    wchar_t url[2048];
    char   *body;
    size_t  len;
    wchar_t err[256];
    int     ok;
    int     silent;     /* automatic refresh: no windows either way */
} sub_job;
/* ---- first run: fetch sing-box -------------------------------------- */

/* resume: what to do once an AmneziaWG download succeeds - 0 nothing,
   1 switch the VPN on, 2 restart it with the chosen profile. */
typedef struct { HWND hwnd; wchar_t msg[SB_MSG_MAX]; int ok; int awg; int resume; } install_job;
typedef struct {
    HWND          hwnd;
    wchar_t       dir[ZAPRET_PATH_MAX];
    host_snapshot hosts;          /* the worker's own copy */
    int           total;
    int           present;
} exc_job;
/* ---- update check ------------------------------------------------------ */

typedef struct {
    HWND    hwnd;
    int     manual;     /* the button, not the start-up check: report everything */
    int     ok;
    char    tag[32];
    wchar_t err[256];
} upd_job;

/* ---- shared state (defined in main.c) ---- */

extern int g_dpi;
extern int g_page;
extern HFONT g_font, g_font_big, g_font_small;
extern HBRUSH g_brush_bg, g_brush_footer, g_brush_surface, g_brush_line;
extern HWND g_tab_utgard, g_tab_zapret, g_toggle, g_pick_path, g_list;
extern HWND g_zap_start, g_zap_stop, g_zap_restart;
extern HWND g_plist, g_prof_add, g_prof_del, g_prof_sub;
extern int g_sub_busy;
extern int g_ping[PROFILES_MAX];
extern int g_ping_gen;
extern int g_ping_busy;
extern int g_vpn_on;
extern int g_installing;        /* 1 sing-box, 2 AmneziaWG being downloaded */
extern int g_awg_lost;          /* VPN on, AmneziaWG profile, tunnel gone */
extern int g_awg_ready;         /* the AmneziaWG core is downloaded and intact */
extern HWND g_btn_hosts, g_btn_apps, g_zap_fix;
extern HWND g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_tip;
extern HWND g_alist, g_app_back, g_app_pick, g_app_manual;
extern WNDPROC g_alist_prev;
extern app_entry g_appv[APPS_MAX];
extern int g_appv_n;
extern int g_app_hover_item;
extern int g_app_hover_zone;
extern HWND g_hedit, g_h_back, g_h_tidy, g_h_save;
extern int g_hosts_mode;
extern HWND g_pk_search, g_pk_list, g_pk_back, g_pk_save;
extern pick_proc g_pk_all[PICK_MAX];
extern int g_pk_view[PICK_MAX];
extern int g_pk_view_n;
extern int g_pk_checked_n;
extern HWND g_ed_name[ED_ROWS], g_ed_nplus[ED_ROWS], g_ed_nminus[ED_ROWS];
extern HWND g_ed_path[ED_ROWS], g_ed_pplus[ED_ROWS], g_ed_pminus[ED_ROWS];
extern HWND g_ed_back, g_ed_save;
extern int g_ed_ncount, g_ed_pcount;
extern wchar_t g_ed_orig[APPS_NAME_MAX];
extern app_settings g_set;
extern HWND g_set_open, g_set_mtu, g_set_log, g_set_back, g_set_save;
extern HWND g_set_upd, g_set_upd_now;
extern HWND g_set_v_utgard, g_set_v_singbox, g_set_v_awg;
extern HWND g_set_stack, g_set_dns, g_set_tray, g_set_auto, g_set_sub, g_ping_now, g_zap_list;
extern long long g_sub_retry;
extern int g_exc_known, g_exc_present;
extern int g_zap_dirty;
extern int g_busy;
extern const wchar_t *g_busy_text;
extern int g_host_count, g_app_count;
extern profile_store g_prof;
extern HFONT g_font_mono;
extern zapret_info g_zap;
extern zapret_status g_status;
extern int g_count;
extern int g_upd_pending;

/* ---- ui_draw.c ---- */

int S(int v);
void fonts_create(void);
void fonts_destroy(void);
void brushes_create(void);
void brushes_destroy(void);
int list_hot_row(HWND list);
void list_hover_attach(HWND list);
void list_hover_resync(HWND list);
HWND make_button_on(HWND parent, const wchar_t *text, int id, int kind, COLORREF backdrop);
HWND make_button(HWND parent, const wchar_t *text, int id, int kind);
void fill(HDC dc, int x, int y, int w, int h, HBRUSH br);
void text_at(HDC dc, int x, int y, int w, int h, const wchar_t *s, COLORREF color, HFONT font, UINT flags);
void dot(HDC dc, int cx, int cy, int r, COLORREF color);
void rounded(HDC dc, const RECT *r, COLORREF fillc, COLORREF border);
void draw_button(const DRAWITEMSTRUCT *d);

/* ---- ui_layout.c ---- */

void layout(HWND hwnd);

/* ---- ui_paint.c ---- */

void on_paint(HWND hwnd);
void draw_strategy(const DRAWITEMSTRUCT *d);
void draw_profile(const DRAWITEMSTRUCT *d);
int app_zone_at(int x, int width);
void draw_app_row(const DRAWITEMSTRUCT *d);
void draw_pick_row(const DRAWITEMSTRUCT *d);

/* ---- ui_common.c ---- */

void to_wide(const char *src, wchar_t *dst, int cap);
int status_refresh(void);
void problem(HWND hwnd, const wchar_t *text);
int modal_box(HWND hwnd, const wchar_t *text, const wchar_t *title, UINT flags);
extern int g_modal;
long_job *job_new(job_work work, job_done done);
/* From the worker: what the job is doing now, for the status line. text
   must be a string literal - it outlives the job. */
void job_stage(long_job *j, const wchar_t *text);
extern int g_switch_note;         /* last switch failed, old profile runs */
void job_free(long_job *j);
int job_start(HWND hwnd, const wchar_t *label, long_job *j);
void after_action(HWND hwnd);

/* ---- ui_zapret.c ---- */

void strategies_reload(void);
const wchar_t *selected_strategy(void);
void act_stop(HWND hwnd);
void act_start(HWND hwnd, const wchar_t *name);
void act_restart(HWND hwnd);
void exc_check_start(HWND hwnd);
void act_zapret_fix(HWND hwnd);
void act_zap_game(HWND hwnd);
void act_zap_ipset(HWND hwnd);
void act_zap_ipset_update(HWND hwnd);
void act_zap_hosts(HWND hwnd);
void on_pick_path(HWND hwnd);

/* ---- ui_vpn.c ---- */

void profiles_reload(void);
int profile_selected(void);
void ping_start(HWND hwnd);
void subscription_apply(HWND hwnd, const wchar_t *url, const char *body, size_t len, int silent);
void act_subscription(HWND hwnd);
void sub_auto_check(HWND hwnd);
int  offer_install(HWND hwnd);
void offer_awg_install(HWND hwnd, int resume);
int vpn_refresh(void);
void act_vpn(HWND hwnd);
void vpn_restart(HWND hwnd, int target);
void vpn_reap_orphan(HWND hwnd);
extern int g_switch_pending;     /* profile a switch waits to run, -1 none */
void act_profile_add(HWND hwnd);
void act_profile_delete(HWND hwnd);
void act_profile_activate(HWND hwnd);

/* ---- ui_lists.c ---- */

int ed_path_top(void);
int root_file(const wchar_t *tail, wchar_t *out, size_t cap);
void lists_refresh_counts(void);
void hosts_open(HWND hwnd, int mode);
void hosts_save_zapret(HWND hwnd);
void hosts_save_start(HWND hwnd, int leave_after);
void hosts_tidy(HWND hwnd);
void hosts_back(HWND hwnd);
void act_edit_hosts(HWND hwnd);
void act_edit_apps(HWND hwnd);
void apps_reload(void);
LRESULT CALLBACK alist_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
int pk_is_checked(const wchar_t *path);
void pk_toggle(const wchar_t *path);
void pk_refresh(void);
void pk_open(HWND hwnd);
void pk_close(HWND hwnd);
void pk_save(HWND hwnd);
void ed_open_new(HWND hwnd);
void ed_remove(HWND *rows, int *count, int at);
void ed_add(HWND hwnd, HWND *rows, int *count);
void ed_save(HWND hwnd);

/* ---- ui_settings.c ---- */

void set_open(HWND hwnd);
void set_save(HWND hwnd);
void upd_start(HWND hwnd, int manual);
void upd_prompt(HWND hwnd);
void upd_done(HWND hwnd, upd_job *j);

#endif
