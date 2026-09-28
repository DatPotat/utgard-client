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
#include "pacstatus.h"
#include <stdlib.h>
#include <time.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <windowsx.h>

/* ---- palette -------------------------------------------------------
   Chosen at run time: light or dark, after Windows unless the user picked
   one in the settings. The CLR_* names stay the ones the pages were written
   with; each is a field of the current palette. Contrast of every text and
   border pair is checked against WCAG AA (see the redesign document). */

typedef struct {
    COLORREF bg, side, surface, text, muted;
    COLORREF accent, accent_lo, accent_hi, on_accent, tint;
    COLORREF warn, warn_lo, warn_hi, line, border, hover;
} ui_palette;
extern ui_palette g_pal;
extern int        g_dark;

#define CLR_BG        (g_pal.bg)
#define CLR_SIDE      (g_pal.side)
#define CLR_FOOTER    (g_pal.side)
#define CLR_SURFACE   (g_pal.surface)
#define CLR_LINE      (g_pal.line)
#define CLR_BORDER    (g_pal.border)
#define CLR_TEXT      (g_pal.text)
#define CLR_MUTED     (g_pal.muted)
#define CLR_ACCENT    (g_pal.accent)
#define CLR_ACCENT_LO (g_pal.accent_lo)
#define CLR_ACCENT_HI (g_pal.accent_hi)
#define CLR_ON_ACCENT (g_pal.on_accent)
#define CLR_TINT      (g_pal.tint)
#define CLR_HOVER     (g_pal.hover)
#define CLR_OK        (g_pal.accent)
#define CLR_WARN      (g_pal.warn)
#define CLR_WARN_LO   (g_pal.warn_lo)
#define CLR_WARN_HI   (g_pal.warn_hi)
/* ---- control ids ---------------------------------------------------- */

#define ID_NAV_FIRST  101   /* + NAV_*: the sidebar sections */
#define ID_ROW_SERVER 110   /* connection page: the server row */
#define ID_TAB_SITES  111   /* routing page: its three tabs */
#define ID_TAB_APPS   112
#define ID_TAB_PAC    113
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
#define ID_PAC        412
#define ID_PAC_LIST   1000
#define ID_PAC_BACK   1001
#define ID_PAC_FILE   1002
#define ID_PAC_URL    1003
#define ID_PAC_TOGGLE 1004
#define ID_PAC_REFRESH 1005
#define ID_PAC_DELETE 1006
#define ID_PAC_HELP   1007
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
#define TIMER_NOTICE  4     /* first-run questions: stop waiting for the update check */
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
#define ID_ED_PBROWSE  1300
#define ID_SEL_FIRST   1400   /* + SEL_*: the drop-down fields of the settings */   /* + row: the folder button inside a path field */
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
#define ID_SET_THEME   967
#define ID_SET_ADV     968
#define ID_ZG_FIRST    1200   /* zapret: game filter chips, GAME_* order */
#define ID_ZI_FIRST    1210   /* zapret: IPSet chips: none, loaded, any */
#define ID_ZAP_SEARCH  1220
#define ID_ZAP_AGAIN   1221
#define ID_HL_SEARCH   1100   /* the site list page */
#define ID_HL_LIST     1101
#define ID_HL_ADD      1102
#define ID_HL_MODE     1103
#define ID_HL_DEL      1104
#define ID_HL_CLEAR    1105
#define ID_HL_UNDO     1106
#define ID_HL_PASTE    1107
#define ID_HL_ADD_OK   1108
#define ID_HL_ADD_CANCEL 1109
#define ID_HL_MOVE     1110
#define ID_HL_SCLEAR   1111
#define ID_HL_TBACK    1112
#define ID_ZAP_SCLEAR  1222
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

/* The page area starts right of the sidebar: layout moves controls by g_ox
   and painting shifts its origin by it. TABS_H is where a page's own
   content begins - below the routing tabs on the routing pages. */
extern int g_ox;
int tabs_top(void);
#define TABS_H   tabs_top()
#define FOOTER_H S(56)
#define PAD      S(24)
#define SIDE_W(rail) ((rail) ? S(112) : S(232))
#define RAIL_BELOW   920    /* DIP: narrower windows get the icon rail */
#define WIN_W_DEF    1040
#define WIN_H_DEF    640
#define WIN_W_MIN    760
#define WIN_H_MIN    520
#define APP_ZONE_TOGGLE 96
#define APP_ZONE_DELETE 84

/* button kinds, stored in GWLP_USERDATA */
enum { BK_TAB = 0, BK_PRIMARY, BK_SECONDARY, BK_DANGER, BK_CHECK, BK_LINK, BK_NAV, BK_ROW, BK_CHIP, BK_ICON, BK_SELECT };
enum { BACK_PAGE = 0, BACK_CARD, BACK_ACCENT, BACK_FOOTER, BACK_TINT };
enum { NAV_CONNECT = 0, NAV_SERVERS, NAV_ROUTING, NAV_ZAPRET, NAV_SETTINGS, NAV_COUNT };
enum { STATE_OFF = 0, STATE_WAIT, STATE_ON, STATE_ERROR };
/* pages */
enum { PAGE_UTGARD = 0, PAGE_ZAPRET, PAGE_APPS, PAGE_HOSTS, PAGE_PICK, PAGE_EDIT,
       PAGE_SETTINGS, PAGE_PAC, PAGE_SERVERS };
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
extern HFONT g_font_bold, g_font_small_bold, g_font_title, g_font_deco, g_font_meta;
void  fonts_load_embedded(void);
HFONT title_font(void);
extern HWND g_nav[NAV_COUNT], g_row_server, g_tab_sites, g_tab_apps, g_tab_pac, g_set_theme;
extern HBRUSH g_brush_bg, g_brush_footer, g_brush_surface, g_brush_line;
extern HWND g_toggle, g_pick_path, g_list;
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
extern HWND g_btn_hosts, g_btn_apps, g_btn_pac, g_zap_fix;
extern HWND g_pac_list, g_pac_back, g_pac_file, g_pac_url, g_pac_toggle;
extern HWND g_pac_refresh, g_pac_delete, g_pac_help;
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
extern HWND g_ed_pbrowse[ED_ROWS];
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
extern int g_host_count, g_app_count, g_pac_count;
extern pac_status_record g_pac_status;
extern int g_pac_status_valid;
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
void rounded_r(HDC dc, const RECT *r, COLORREF fillc, COLORREF border, int radius);

/* ---- ui_scroll.c ---- */
void scroll_attach(HWND target);
void scroll_place(HWND target);
void scroll_sync(HWND target);

/* ---- ui_popup.c ---- */
enum { SEL_DNS = 0, SEL_SUB, SEL_THEME, SEL_LOG, SEL_STACK, SEL_COUNT };
extern HWND g_sel[SEL_COUNT];
int  popup_choose(HWND owner, const RECT *anchor, const wchar_t *const *items, int n,
                  int current, int align_right);
void select_open(HWND owner, HWND field, HWND combo);
LRESULT tip_draw(NMTTCUSTOMDRAW *cd);
void    tip_shape(HWND tip);
void    pac_columns(int width, int *x_type, int *x_state);
LRESULT pac_row_draw(NMLVCUSTOMDRAW *cd);

/* ---- ui_menu.c ---- */
HMENU menu_create(void);
void  menu_add(HMENU m, UINT id, const wchar_t *text, int grayed);
void  menu_separator(HMENU m);
int   menu_measure(MEASUREITEMSTRUCT *mi);
int   menu_draw(const DRAWITEMSTRUCT *d);
int   pick_file(HWND hwnd, const wchar_t *title, const wchar_t *filter_name,
                const wchar_t *filter_spec, wchar_t *path, size_t cap);

/* ---- ui_gfx.c ---- */
#include "icons_lucide.h"
void gfx_startup(void);
void gfx_shutdown(void);
void gfx_icon(HDC dc, int icon, int x, int y, int size, COLORREF color);
void draw_button(const DRAWITEMSTRUCT *d);

/* ---- ui_theme.c ---- */

void theme_pick(void);
int  theme_system_is_dark(void);
void theme_apply(HWND hwnd);
void theme_caption(HWND hwnd);
int  nav_section(void);
int  nav_rail(HWND hwnd);
void nav_layout(HWND hwnd);
void draw_nav(const DRAWITEMSTRUCT *d);
void state_icon(HDC dc, int kind, int cx, int cy, int r, int inv);
int  vpn_state(void);
void paint_sidebar(HDC dc, const RECT *client);
void theme_ask(void);

/* ---- ui_hostlist.c ---- */

extern HWND g_hl_search, g_hl_list, g_hl_add, g_hl_mode, g_hl_del, g_hl_clear, g_hl_undo;
extern HWND g_hl_paste, g_hl_add_ok, g_hl_add_cancel, g_hl_move, g_hl_sclear, g_zap_sclear, g_hl_tback;
void search_frame(HDC dc, HWND edit);
void field_frame(HDC dc, HWND edit, int extra_right);
void search_clear_place(HWND edit, HWND clear, int x, int y, int w, void (*place)(HWND, int, int, int, int));
void hl_create(HWND hwnd);
void hl_fonts(void);
void hl_open(void);
void hl_reload(void);
void hl_layout(const RECT *c, void (*place)(HWND, int, int, int, int));
void hl_paint(HDC dc, const RECT *c);
void draw_host_row(const DRAWITEMSTRUCT *d);
int  hl_command(HWND hwnd, int id, int code);
LRESULT CALLBACK hl_list_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref);
void hl_click(int x, int y);
int  hl_over_strip(int x, int y);
int  page_leave_ok(HWND hwnd);

/* ---- ui_layout.c ---- */

/* The connection page's geometry, shared by layout and paint. */
typedef struct { RECT block; int colw, rx, ry, one_col; } conn_geo;
void connect_geometry(const RECT *c, conn_geo *g);
/* The zapret page, likewise. */
typedef struct {
    int  banner, y0, lw, rx, rw;
    RECT status;
    int  strat, search, list_top, list_bottom, start;
    int  compat, filters, lists;
} zap_geo;
void zapret_geometry(const RECT *c, zap_geo *g);
/* The settings page. */
typedef struct { int y0, colw, lx, rx, launch, conn, look, adv; } set_geo;
void settings_geometry(const RECT *c, set_geo *g);
void layout(HWND hwnd);
int  caption_width(HWND b);

/* ---- ui_paint.c ---- */

void on_paint(HWND hwnd);
void draw_strategy(const DRAWITEMSTRUCT *d);
void draw_profile(const DRAWITEMSTRUCT *d);
void server_columns(int width, int *x_proto, int *x_host, int *x_ping);
int app_zone_at(int x, int width);
void draw_app_row(const DRAWITEMSTRUCT *d);
void draw_pick_row(const DRAWITEMSTRUCT *d);

/* ---- ui_common.c ---- */

void to_wide(const char *src, wchar_t *dst, int cap);
const wchar_t *plural_ru(long n, const wchar_t *one, const wchar_t *few, const wchar_t *many);
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
void strategies_filter(void);
int  strategies_shown(void);
void act_zap_game_to(HWND hwnd, int mode);
void act_zap_ipset_to(HWND hwnd, int mode);
extern HWND g_set_adv;
extern int  g_set_adv_open;
extern HWND g_zap_search, g_zap_again, g_zg[4], g_zi[3];
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
int  subscription_apply(HWND hwnd, const wchar_t *url, const char *body, size_t len, int silent);
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
void act_profile_pick(HWND hwnd);

/* ---- ui_lists.c ---- */

int ed_path_top(void);
int root_file(const wchar_t *tail, wchar_t *out, size_t cap);
void lists_refresh_counts(void);
void hosts_open(HWND hwnd, int mode);
void hosts_save_zapret(HWND hwnd);
void hosts_save_start(HWND hwnd, int leave_after);
void hosts_save_vpn_text(HWND hwnd, const char *utf8);
int  zapret_user_list(wchar_t *out, size_t cap);
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

/* ---- ui_pac.c ---- */
enum { PAC_UI_REFRESH = 2, PAC_UI_TOGGLE, PAC_UI_DELETE };
int pac_selected(void);
void pac_reload(void);
void pac_open_page(HWND hwnd);
void pac_auto_check(HWND hwnd);
/* ui_vpn.c: the first-run questions in one window */
extern int g_awg_after_singbox;
int  startup_notice_waiting(void);
void startup_notice_begin(HWND hwnd);
void startup_notice_timer(HWND hwnd);
void startup_notice_shown(HWND hwnd);
void startup_notice(HWND hwnd);
void offer_awg_download(HWND hwnd);
LRESULT CALLBACK pac_list_proc(HWND list, UINT msg, WPARAM wp, LPARAM lp,
                               UINT_PTR id, DWORD_PTR ref);
void pac_back(HWND hwnd);
void pac_add_file(HWND hwnd);
void pac_add_url(HWND hwnd);
void pac_action(HWND hwnd, int op);

/* ---- ui_settings.c ---- */

void set_open(HWND hwnd);
void set_save(HWND hwnd);
void upd_start(HWND hwnd, int manual);
void upd_prompt(HWND hwnd);
int  upd_running(void);
int  upd_question(wchar_t *text, size_t cap);
void upd_open_page(HWND hwnd);
void upd_done(HWND hwnd, upd_job *j);

#endif
