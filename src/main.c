/*
 * Utgard client - main window skeleton.
 * Layout and painting only: no VPN, no zapret, no config logic yet.
 */

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
#define WM_APP_JOB_DONE    (WM_APP + 10)    /* a background job finished */

#define ID_TRAY_OPEN   1901
#define ID_TRAY_VPN    1902
#define ID_TRAY_EXIT   1903
#define TIMER_STATUS  1

/* button kinds, stored in GWLP_USERDATA */
enum { BK_TAB = 0, BK_PRIMARY, BK_SECONDARY, BK_DANGER, BK_CHECK };

/* pages */
enum { PAGE_UTGARD = 0, PAGE_ZAPRET, PAGE_APPS, PAGE_HOSTS, PAGE_PICK, PAGE_EDIT,
       PAGE_SETTINGS };

/* undocumented in mingw's dwmapi.h; documented by Microsoft for Win11 22000+ */
#define UTG_DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define UTG_DWMWA_CAPTION_COLOR           35

/* ---- state ---------------------------------------------------------- */

static int    g_dpi  = USER_DEFAULT_SCREEN_DPI;
static int    g_page = PAGE_UTGARD;
static HFONT  g_font, g_font_big, g_font_small;
static HBRUSH g_brush_bg, g_brush_footer, g_brush_surface, g_brush_line;

static HWND g_tab_utgard, g_tab_zapret, g_toggle, g_pick_path, g_list;
static HWND g_zap_start, g_zap_stop, g_zap_restart;
static HWND g_plist, g_prof_add, g_prof_del, g_prof_sub;
static int  g_sub_busy;

/* -2 not measured yet, -1 unreachable, otherwise milliseconds. Not stored:
   a latency from last week would be a lie. */
static int g_ping[PROFILES_MAX];
static int g_ping_gen;      /* results from an older list are discarded */
static int g_ping_busy;
static int g_vpn_on;
static int g_installing;
static HWND g_btn_hosts, g_btn_apps, g_zap_fix;
static HWND g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_tip;
static HWND g_alist, g_app_back, g_app_pick, g_app_manual;
static WNDPROC   g_alist_prev;
static app_entry g_appv[APPS_MAX];
static int       g_appv_n;
static int       g_app_hover_item = -1;   /* row under the cursor, -1 if none */
static int       g_app_hover_zone = -1;   /* 0 name, 1 enable/disable, 2 delete */
static HWND      g_hedit, g_h_back, g_h_tidy, g_h_save;
/* The list page serves two files: the VPN site list, compiled for sing-box,
   and zapret's own list-general-user.txt, which zapret reads as plain text. */
enum { HOSTS_VPN = 0, HOSTS_ZAPRET };
static int       g_hosts_mode;
static HWND      g_pk_search, g_pk_list, g_pk_back, g_pk_save;
static pick_proc g_pk_all[PICK_MAX];
static int       g_pk_all_n;
static int       g_pk_view[PICK_MAX];     /* indices into g_pk_all after filtering */
static int       g_pk_view_n;
static wchar_t   g_pk_checked[64][MAX_PATH];  /* ticked, remembered by path */
static int       g_pk_checked_n;

static HWND    g_ed_name[ED_ROWS], g_ed_nplus[ED_ROWS], g_ed_nminus[ED_ROWS];
static HWND    g_ed_path[ED_ROWS], g_ed_pplus[ED_ROWS], g_ed_pminus[ED_ROWS];
static HWND    g_ed_back, g_ed_save;
static int     g_ed_ncount = 1, g_ed_pcount = 1;
static wchar_t g_ed_orig[APPS_NAME_MAX];     /* empty when creating */
static int     g_ed_enabled;                 /* kept across an edit */

static app_settings g_set;
static UINT         g_taskbar_created;   /* Explorer restarted */
static HWND    g_set_open, g_set_mtu, g_set_log, g_set_back, g_set_save;
static HWND    g_set_upd, g_set_upd_now;
static HWND    g_set_stack, g_set_dns, g_set_tray, g_set_auto, g_set_sub, g_ping_now, g_zap_list;
static long long g_sub_retry;   /* after a failed automatic refresh, not before */
static int  g_exc_known, g_exc_present;
static int  g_zap_dirty;   /* Game Filter changed: it lives in winws arguments; lists are reread live */
static int            g_busy;        /* a background job is running */
static const wchar_t *g_busy_text;   /* what it is doing, for the status line */
static int  g_host_count, g_app_count;
static profile_store g_prof;
static HFONT g_font_mono;
static zapret_info   g_zap;
static zapret_status g_status;
static wchar_t g_names[ZAPRET_MAX_STRATEGIES][ZAPRET_NAME_MAX];
static int     g_count;

static const wchar_t *selected_strategy(void);
static int profile_selected(void);
static void to_wide(const char *src, wchar_t *dst, int cap);
static void exc_check_start(HWND hwnd);
static void apps_reload(void);
static int  pk_is_checked(const wchar_t *path);
static int  ed_path_top(void);
static int  ed_open_existing(HWND hwnd, const app_entry *e);

/* logical pixels -> device pixels */
static int S(int v) { return MulDiv(v, g_dpi, USER_DEFAULT_SCREEN_DPI); }

/* ---- resources ------------------------------------------------------ */

static HFONT make_font(int size_pt10, int weight)
{
    return CreateFontW(-MulDiv(size_pt10, g_dpi, 720), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

static void fonts_create(void)
{
    g_font       = make_font(105, FW_NORMAL);     /* 10.5 pt */
    g_font_big   = make_font(120, FW_SEMIBOLD);   /* 12 pt   */
    g_font_small = make_font(90,  FW_NORMAL);     /* 9 pt    */
    g_font_mono  = CreateFontW(-MulDiv(95, g_dpi, 720), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void fonts_destroy(void)
{
    if (g_font)       DeleteObject(g_font);
    if (g_font_big)   DeleteObject(g_font_big);
    if (g_font_small) DeleteObject(g_font_small);
    if (g_font_mono)  DeleteObject(g_font_mono);
    g_font = g_font_big = g_font_small = g_font_mono = NULL;
}

static void brushes_create(void)
{
    g_brush_bg      = CreateSolidBrush(CLR_BG);
    g_brush_footer  = CreateSolidBrush(CLR_FOOTER);
    g_brush_surface = CreateSolidBrush(CLR_SURFACE);
    g_brush_line    = CreateSolidBrush(CLR_LINE);
}

static void brushes_destroy(void)
{
    DeleteObject(g_brush_bg);
    DeleteObject(g_brush_footer);
    DeleteObject(g_brush_surface);
    DeleteObject(g_brush_line);
}

/* ---- helpers -------------------------------------------------------- */

/* Row hover for the plain lists. The row under the pointer is kept as a
   window property (row + 1, since 0 means "none"), so each painter can ask
   for its own list without a global per list. */
static const wchar_t HOT_ROW[] = L"utgard.hotrow";

static int list_hot_row(HWND list)
{
    return (int)(INT_PTR)GetPropW(list, HOT_ROW) - 1;
}

static void list_row_repaint(HWND list, int row)
{
    RECT r;
    if (row < 0) return;
    if (SendMessageW(list, LB_GETITEMRECT, (WPARAM)row, (LPARAM)&r) != LB_ERR)
        InvalidateRect(list, &r, FALSE);
}

static LRESULT CALLBACK list_hover_proc(HWND h, UINT m, WPARAM w, LPARAM l,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (m) {
    case WM_MOUSEMOVE: {
        LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
        int     row = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        int     old = list_hot_row(h);

        if (row >= (int)SendMessageW(h, LB_GETCOUNT, 0, 0)) row = -1;
        if (row != old) {
            SetPropW(h, HOT_ROW, (HANDLE)(INT_PTR)(row + 1));
            list_row_repaint(h, old);
            list_row_repaint(h, row);
        }
        {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof t; t.dwFlags = TME_LEAVE;
            t.hwndTrack = h; t.dwHoverTime = 0;
            TrackMouseEvent(&t);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        int old = list_hot_row(h);
        RemovePropW(h, HOT_ROW);
        list_row_repaint(h, old);
        break;
    }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && list_hot_row(h) >= 0) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case LB_RESETCONTENT:
        /* The rows are about to be rebuilt; the stored index would point at
           something else afterwards. */
        RemovePropW(h, HOT_ROW);
        break;
    case WM_NCDESTROY:
        RemovePropW(h, HOT_ROW);
        RemoveWindowSubclass(h, list_hover_proc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

static void list_hover_attach(HWND list)
{
    SetWindowSubclass(list, list_hover_proc, 2, 0);
}

/* A list rebuilt on a timer loses its hover on every rebuild while the
   pointer sits still; this reads the pointer again and puts it back. */
static void list_hover_resync(HWND list)
{
    POINT pt;
    RECT  rc;

    if (!GetCursorPos(&pt) || !ScreenToClient(list, &pt)) return;
    GetClientRect(list, &rc);
    if (PtInRect(&rc, pt)) {
        LRESULT hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        int     row = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        if (row >= 0) SetPropW(list, HOT_ROW, (HANDLE)(INT_PTR)(row + 1));
    }
}

/* kind in the low byte, the colour behind the button in the rest: a rounded
   shape leaves its corners unpainted, and they have to be filled with
   whatever the parent draws there. 0 means the ordinary page background. */
static HWND make_button_on(HWND parent, const wchar_t *text, int id, int kind,
                           COLORREF backdrop)
{
    HWND b = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                             (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                             NULL);
    SetWindowLongPtrW(b, GWLP_USERDATA,
                      (LONG_PTR)kind | ((LONG_PTR)(backdrop & 0xFFFFFF) << 8));
    ask_hover_attach(b);
    return b;
}

static HWND make_button(HWND parent, const wchar_t *text, int id, int kind)
{
    return make_button_on(parent, text, id, kind, 0);
}

static void fill(HDC dc, int x, int y, int w, int h, HBRUSH br)
{
    RECT r = { x, y, x + w, y + h };
    FillRect(dc, &r, br);
}

static void text_at(HDC dc, int x, int y, int w, int h,
                    const wchar_t *s, COLORREF color, HFONT font, UINT flags)
{
    RECT r = { x, y, x + w, y + h };
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    /* DT_SINGLELINE cancels DT_WORDBREAK, so a caller asking for wrapping gets
       a top-aligned multi-line block instead of a clipped single line. */
    DrawTextW(dc, s, -1, &r,
              (flags & DT_WORDBREAK) ? flags : (DT_SINGLELINE | DT_VCENTER | flags));
    SelectObject(dc, old);
}

static void dot(HDC dc, int cx, int cy, int r, COLORREF color)
{
    HBRUSH br  = CreateSolidBrush(color);
    HPEN   pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void rounded(HDC dc, const RECT *r, COLORREF fillc, COLORREF border)
{
    HBRUSH br  = fillc  == CLR_BG ? g_brush_bg : CreateSolidBrush(fillc);
    HPEN   pen = CreatePen(PS_SOLID, S(1), border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, S(6), S(6));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    if (fillc != CLR_BG) DeleteObject(br);
    DeleteObject(pen);
}

/* ---- layout --------------------------------------------------------- */

#define TABS_H   S(40)
#define FOOTER_H S(38)
#define PAD      S(18)

static void layout(HWND hwnd)
{
    HWND c_hwnd = hwnd;
    RECT c;
    GetClientRect(hwnd, &c);

    MoveWindow(g_tab_utgard, S(14),  S(6), S(96), S(28), TRUE);
    MoveWindow(g_tab_zapret, S(112), S(6), S(96), S(28), TRUE);
    MoveWindow(g_set_open, c.right - PAD - S(110), S(6), S(110), S(28), TRUE);
    MoveWindow(g_ping_now, c.right - PAD - S(110) - S(8) - S(140), S(6), S(140), S(28), TRUE);
    ShowWindow(g_ping_now, g_page == PAGE_UTGARD ? SW_SHOW : SW_HIDE);
    EnableWindow(g_ping_now, !g_ping_busy && g_prof.count > 0);

    {
        int sp = (g_page == PAGE_SETTINGS) ? SW_SHOW : SW_HIDE;

        int cw = c.right - PAD * 2 - S(130) - S(84);   /* room left of "Что это?" */

        MoveWindow(g_set_mtu,   PAD + S(130), TABS_H + S(94),  S(100), S(28), TRUE);
        MoveWindow(g_set_log,   PAD + S(130), TABS_H + S(136), c.right - PAD * 2 - S(130),
                   S(300), TRUE);
        MoveWindow(g_set_stack, PAD + S(130), TABS_H + S(178), cw, S(300), TRUE);
        MoveWindow(g_set_dns,   PAD + S(130), TABS_H + S(220), cw, S(300), TRUE);
        MoveWindow(g_set_sub,   PAD + S(130), TABS_H + S(262), cw, S(300), TRUE);
        MoveWindow(g_set_tray,  PAD, TABS_H + S(318), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_auto,  PAD, TABS_H + S(352), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_upd,   PAD, TABS_H + S(386), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_upd_now, PAD, TABS_H + S(424), S(200), S(30), TRUE);
        ShowWindow(g_set_sub,   sp);
        ShowWindow(g_set_stack, sp);
        ShowWindow(g_set_dns,   sp);
        ShowWindow(g_set_tray,  sp);
        ShowWindow(g_set_auto,  sp);
        ShowWindow(g_set_upd,   sp);
        ShowWindow(g_set_upd_now, sp);
        MoveWindow(g_set_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_set_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_set_mtu,  sp);
        ShowWindow(g_set_log,  sp);
        ShowWindow(g_set_back, sp);
        ShowWindow(g_set_save, sp);

        if (g_tip) {
            TTTOOLINFOW ti;
            ZeroMemory(&ti, sizeof ti);
            ti.cbSize = sizeof ti;
            ti.hwnd   = hwnd;
            ti.uId    = 4;
            if (g_page == PAGE_SETTINGS) {
                ti.rect.left   = PAD + S(238);
                ti.rect.top    = TABS_H + S(98);
                ti.rect.right  = ti.rect.left + S(80);
                ti.rect.bottom = ti.rect.top + S(20);
            }                                    /* else an empty rect: no tip */
            SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);

            {
                static const int ys[2] = { 178, 220 };
                int k;
                for (k = 0; k < 2; k++) {
                    ti.uId = (UINT_PTR)(5 + k);
                    SetRectEmpty(&ti.rect);
                    if (g_page == PAGE_SETTINGS) {
                        ti.rect.left   = c.right - PAD - S(76);
                        ti.rect.top    = TABS_H + S(ys[k]) + S(4);
                        ti.rect.right  = ti.rect.left + S(76);
                        ti.rect.bottom = ti.rect.top + S(20);
                    }
                    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                }
            }
        }
    }

    MoveWindow(g_toggle, c.right - PAD - S(124), TABS_H + S(26), S(124), S(32), TRUE);
    SetWindowTextW(g_toggle, g_vpn_on ? L"Выключить" : L"Включить");
    EnableWindow(g_toggle, g_vpn_on ||
                 (!g_installing && g_prof.count > 0 && g_prof.active >= 0));

    {
        int list_top = TABS_H + S(190);
        int list_h   = c.bottom - FOOTER_H - S(12) - list_top;
        int on       = (g_page == PAGE_UTGARD) ? SW_SHOW : SW_HIDE;

        if (list_h < S(60)) list_h = S(60);
        MoveWindow(g_plist, PAD, list_top, c.right - PAD * 2, list_h, TRUE);
        MoveWindow(g_btn_hosts, PAD, TABS_H + S(112), S(150), S(30), TRUE);
        MoveWindow(g_btn_apps,  PAD + S(158), TABS_H + S(112), S(170), S(30), TRUE);
        ShowWindow(g_btn_hosts, on);
        ShowWindow(g_btn_apps,  on);
        MoveWindow(g_prof_add, PAD, c.bottom - FOOTER_H + S(4), S(176), S(30), TRUE);
        MoveWindow(g_prof_del, PAD + S(184), c.bottom - FOOTER_H + S(4), S(110), S(30), TRUE);
        MoveWindow(g_prof_sub, PAD + S(302), c.bottom - FOOTER_H + S(4), S(116), S(30), TRUE);

        ShowWindow(g_plist,    on);
        ShowWindow(g_prof_add, on);
        ShowWindow(g_prof_del, on);
        ShowWindow(g_prof_sub, on);
        EnableWindow(g_prof_del, !g_sub_busy && profile_selected() >= 0);
        EnableWindow(g_prof_add, !g_sub_busy);
        EnableWindow(g_prof_sub, !g_sub_busy);
    }

    if (g_zap.valid) {
        int list_top = TABS_H + S(448);
        int list_h   = c.bottom - FOOTER_H - S(12) - list_top;

        SetWindowTextW(g_pick_path, L"Изменить путь");
        MoveWindow(g_pick_path, c.right - PAD - S(130), TABS_H + S(24), S(130), S(30), TRUE);
        if (list_h < S(60)) list_h = S(60);
        MoveWindow(g_list, PAD, list_top, c.right - PAD * 2, list_h, TRUE);

        MoveWindow(g_zap_stop, c.right - PAD - S(104), TABS_H + S(84), S(104), S(30), TRUE);
        MoveWindow(g_zap_restart, c.right - PAD - S(104) - S(8) - S(124),
                   TABS_H + S(84), S(124), S(30), TRUE);
        MoveWindow(g_zap_start, PAD, c.bottom - FOOTER_H + S(4), S(140), S(30), TRUE);
        MoveWindow(g_zap_list, PAD, TABS_H + S(354), S(250), S(30), TRUE);
        ShowWindow(g_zap_list, (g_page == PAGE_ZAPRET && g_zap.valid) ? SW_SHOW : SW_HIDE);
        {
            static const int rows[4] = { 210, 246, 282, 318 };
            HWND ctl[4];
            int  i;
            TTTOOLINFOW ti;

            ctl[0] = g_zap_game; ctl[1] = g_zap_ipset;
            ctl[2] = g_zap_ipupd; ctl[3] = g_zap_hosts;

            ZeroMemory(&ti, sizeof ti);
            ti.cbSize = sizeof ti;
            ti.hwnd   = c_hwnd;

            for (i = 0; i < 4; i++) {
                MoveWindow(ctl[i], PAD, TABS_H + S(rows[i]), S(250), S(30), TRUE);
                ShowWindow(ctl[i], (g_page == PAGE_ZAPRET && g_zap.valid)
                                       ? SW_SHOW : SW_HIDE);
                if (g_tip) {
                    ti.uId = (UINT_PTR)i;
                    /* Only on the zapret tab; elsewhere the same spot would
                       pop up a zapret tip over unrelated controls. */
                    if (g_page != PAGE_ZAPRET) {
                        SetRectEmpty(&ti.rect);
                        SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                        continue;
                    }
                    ti.rect.left   = PAD + S(258);
                    ti.rect.top    = TABS_H + S(rows[i]) + S(6);
                    ti.rect.right  = ti.rect.left + S(78);
                    ti.rect.bottom = ti.rect.top + S(18);
                    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                }
            }
        }

        {
            int done = g_exc_known > 0 && g_exc_present >= g_exc_known;
            SetWindowTextW(g_zap_fix, done ? L"Адреса VPN добавлены в исключения"
                                           : L"Исправить конфликт VPN и zapret");
            MoveWindow(g_zap_fix, c.right - PAD - S(252), TABS_H + S(154),
                       S(252), S(30), TRUE);
            EnableWindow(g_zap_fix, !done && g_prof.count > 0);
        }

        {
            const wchar_t *sel = selected_strategy();
            /* Starting the strategy that already runs would be a no-op, so the
               button only wakes up on a different choice. */
            int same = sel && g_status.mode == ZAPRET_SERVICE &&
                       _wcsicmp(sel, g_status.strategy) == 0;

            EnableWindow(g_zap_stop,    g_status.mode != ZAPRET_OFF);
            EnableWindow(g_zap_restart, g_status.mode != ZAPRET_OFF);
            EnableWindow(g_zap_start,   sel != NULL && !same);
        }
    } else {
        SetWindowTextW(g_pick_path, L"Указать папку…");
        MoveWindow(g_pick_path, PAD, TABS_H + S(100), S(168), S(32), TRUE);
    }

    {
        int  ep = (g_page == PAGE_EDIT);
        int  field_w = c.right - PAD * 2 - S(84);
        int  i, y;

        for (i = 0; i < ED_ROWS; i++) {
            int showN = ep && i < g_ed_ncount;
            int showP = ep && i < g_ed_pcount;

            y = TABS_H + S(92) + i * S(36);
            MoveWindow(g_ed_name[i],   PAD, y, field_w, S(28), TRUE);
            MoveWindow(g_ed_nminus[i], PAD + field_w + S(6),  y, S(36), S(28), TRUE);
            MoveWindow(g_ed_nplus[i],  PAD + field_w + S(46), y, S(36), S(28), TRUE);
            ShowWindow(g_ed_name[i],   showN ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_nplus[i],  showN ? SW_SHOW : SW_HIDE);
            /* The first row of a section has no minus: a section is never empty. */
            ShowWindow(g_ed_nminus[i], showN && i > 0 ? SW_SHOW : SW_HIDE);

            y = ed_path_top() + i * S(36);
            MoveWindow(g_ed_path[i],   PAD, y, field_w, S(28), TRUE);
            MoveWindow(g_ed_pminus[i], PAD + field_w + S(6),  y, S(36), S(28), TRUE);
            MoveWindow(g_ed_pplus[i],  PAD + field_w + S(46), y, S(36), S(28), TRUE);
            ShowWindow(g_ed_path[i],   showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pplus[i],  showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pminus[i], showP && i > 0 ? SW_SHOW : SW_HIDE);
        }

        MoveWindow(g_ed_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_ed_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_ed_back, ep ? SW_SHOW : SW_HIDE);
        ShowWindow(g_ed_save, ep ? SW_SHOW : SW_HIDE);
    }

    {
        int pp = (g_page == PAGE_PICK) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_pk_search, PAD + S(62), TABS_H + S(66),
                   c.right - PAD * 2 - S(62), S(24), TRUE);
        MoveWindow(g_pk_list, PAD, TABS_H + S(100), c.right - PAD * 2,
                   c.bottom - FOOTER_H - S(12) - (TABS_H + S(100)), TRUE);
        MoveWindow(g_pk_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_pk_save, c.right - PAD - S(210), c.bottom - FOOTER_H + S(4),
                   S(210), S(30), TRUE);
        ShowWindow(g_pk_search, pp);
        ShowWindow(g_pk_list,   pp);
        ShowWindow(g_pk_back,   pp);
        ShowWindow(g_pk_save,   pp);
        EnableWindow(g_pk_save, g_pk_checked_n > 0);
    }

    {
        int hp = (g_page == PAGE_HOSTS) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_hedit, PAD + S(2), TABS_H + S(98), c.right - PAD * 2 - S(4),
                   c.bottom - FOOTER_H - S(14) - (TABS_H + S(98)), TRUE);
        MoveWindow(g_h_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_h_tidy, PAD + S(108), c.bottom - FOOTER_H + S(4),
                   S(150), S(30), TRUE);
        MoveWindow(g_h_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_hedit,  hp);
        ShowWindow(g_h_back, hp);
        ShowWindow(g_h_tidy, hp);
        ShowWindow(g_h_save, hp);
    }

    {
        int ap = (g_page == PAGE_APPS) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_alist, PAD, TABS_H + S(80), c.right - PAD * 2,
                   c.bottom - FOOTER_H - S(12) - (TABS_H + S(80)), TRUE);
        MoveWindow(g_app_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_app_pick, PAD + S(108), c.bottom - FOOTER_H + S(4),
                   S(210), S(30), TRUE);
        MoveWindow(g_app_manual, PAD + S(326), c.bottom - FOOTER_H + S(4),
                   S(160), S(30), TRUE);
        ShowWindow(g_alist,      ap);
        ShowWindow(g_app_back,   ap);
        ShowWindow(g_app_pick,   ap);
        ShowWindow(g_app_manual, ap);
    }

    {
        int on = (g_page == PAGE_ZAPRET && g_zap.valid) ? SW_SHOW : SW_HIDE;
        ShowWindow(g_list,        on);
        ShowWindow(g_zap_fix,     on);
        ShowWindow(g_zap_stop,    on);
        ShowWindow(g_zap_restart, on);
        ShowWindow(g_zap_start,   on);
    }

    ShowWindow(g_toggle,    g_page == PAGE_UTGARD ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pick_path, g_page == PAGE_ZAPRET ? SW_SHOW : SW_HIDE);

    /* While a job runs, every button that could start another is greyed.
       Done last so it overrides the ordinary rules above. */
    if (g_busy) {
        HWND busy_off[] = {
            g_toggle, g_zap_start, g_zap_stop, g_zap_restart, g_zap_fix,
            g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_pick_path, g_zap_list,
            g_h_save, g_h_tidy, g_h_back
        };
        size_t k;
        for (k = 0; k < sizeof busy_off / sizeof busy_off[0]; k++)
            if (busy_off[k]) EnableWindow(busy_off[k], FALSE);
    } else {
        EnableWindow(g_h_save, TRUE);
        EnableWindow(g_h_tidy, TRUE);
        EnableWindow(g_h_back, TRUE);
        EnableWindow(g_zap_game, TRUE);
        EnableWindow(g_zap_ipset, TRUE);
        EnableWindow(g_zap_ipupd, TRUE);
        EnableWindow(g_zap_hosts, TRUE);
        EnableWindow(g_pick_path, TRUE);
        EnableWindow(g_zap_list, TRUE);
    }

    /* children are not repainted by invalidating the parent */
    InvalidateRect(g_tab_utgard, NULL, TRUE);
    InvalidateRect(g_tab_zapret, NULL, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ---- painting ------------------------------------------------------- */

static void paint_utgard(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    dot(dc, PAD + S(6), top + S(42), S(5), g_vpn_on ? CLR_OK : CLR_MUTED);
    text_at(dc, PAD + S(22), top + S(28), w, S(22),
            g_vpn_on ? L"VPN включён" : L"VPN выключен",
            g_vpn_on ? CLR_OK : CLR_MUTED, g_font_big, DT_LEFT);
    {
        wchar_t sub[320];
        if (g_vpn_on && g_prof.active >= 0 && g_prof.active < g_prof.count) {
            char    tag[96];
            wchar_t wtag[128];
            genconf_tag(&g_prof, g_prof.active, tag, sizeof tag);
            to_wide(tag, wtag, 128);
            StringCchPrintfW(sub, 320,
                             L"через %s",
                             wtag);
        } else if (g_busy && g_busy_text) {
            StringCchCopyW(sub, 320, g_busy_text);
        } else if (g_installing) {
            StringCchCopyW(sub, 320, L"скачиваю sing-box, это займёт минуту…");
        } else if (!singbox_present()) {
            StringCchCopyW(sub, 320, L"нет sing-box — положите его в папку sing-box");
        } else if (g_prof.count == 0) {
            StringCchCopyW(sub, 320, L"добавьте профиль, чтобы включить");
        } else {
            StringCchCopyW(sub, 320, L"выберите профиль и нажмите «Включить»");
        }
        text_at(dc, PAD + S(22), top + S(48), w, S(18), sub,
                CLR_MUTED, g_font_small, DT_LEFT);
    }

    fill(dc, 0, top + S(74), c->right, S(1), g_brush_line);

    {
        wchar_t line[200];
        StringCchPrintfW(line, 200,
                         L"Через туннель идут %d сайтов и %d приложений",
                         g_host_count, g_app_count);
        text_at(dc, PAD, top + S(82), w, S(24), line,
                CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    }

    fill(dc, 0, top + S(152), c->right, S(1), g_brush_line);

    text_at(dc, PAD, top + S(162), w, S(20), L"Профили", CLR_MUTED, g_font, DT_LEFT);
    if (g_sub_busy) {
        text_at(dc, PAD, top + S(162), w, S(20),
                L"загружаю подписку…", CLR_ACCENT, g_font_small, DT_RIGHT);
    } else if (g_ping_busy) {
        text_at(dc, PAD, top + S(162), w, S(20),
                L"проверяю задержки…", CLR_ACCENT, g_font_small, DT_RIGHT);
    } else if (g_prof.count) {
        wchar_t hint[96];
        StringCchPrintfW(hint, 96, L"всего: %d · двойной клик — выбрать",
                         g_prof.count);
        text_at(dc, PAD, top + S(162), w, S(20), hint, CLR_MUTED, g_font_small, DT_RIGHT);
    } else {
        text_at(dc, PAD, top + S(162), w, S(20),
                L"пока пусто — добавьте ссылку", CLR_MUTED, g_font_small, DT_RIGHT);
    }
}

static void paint_apps(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    text_at(dc, PAD, top + S(16), w, S(24),
            L"Приложения через туннель", CLR_TEXT, g_font_big, DT_LEFT);
    text_at(dc, PAD, top + S(42), w, S(20),
            g_appv_n ? L"Сохранённые списки. Включённые применяются при сборке "
                       L"конфигурации."
                     : L"Пока ни одного списка не сохранено.",
            CLR_MUTED, g_font_small, DT_LEFT);

    fill(dc, 0, top + S(68), c->right, S(1), g_brush_line);
}

static void paint_hosts(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    if (g_busy && g_busy_text)
        text_at(dc, PAD + S(260), top + S(14), w - S(260), S(24), g_busy_text,
                CLR_ACCENT, g_font_small, DT_RIGHT | DT_END_ELLIPSIS);

    if (g_hosts_mode == HOSTS_ZAPRET) {
        text_at(dc, PAD, top + S(14), S(260), S(24),
                L"Список хостов zapret", CLR_TEXT, g_font_big, DT_LEFT);
        text_at(dc, PAD, top + S(40), w, S(50),
                L"По одному домену в строке. Поддомены подхватываются сами, ^ в начале — "
                L"только сам домен. zapret подхватывает изменения сам, без перезапуска.",
                CLR_MUTED, g_font_small, DT_LEFT | DT_WORDBREAK);
    } else {
        text_at(dc, PAD, top + S(14), S(260), S(24),
                L"Сайты через туннель", CLR_TEXT, g_font_big, DT_LEFT);
        text_at(dc, PAD, top + S(40), w, S(50),
                L"По одному адресу в строке. Поддомены подхватываются сами: "
                L"spotify.com покроет и audio.spotify.com. Подсети — 198.51.100.0/24. "
                L"Строки с # — комментарии.",
                CLR_MUTED, g_font_small, DT_LEFT | DT_WORDBREAK);
    }
}

static void paint_pick(HDC dc, const RECT *c)
{
    int     top = TABS_H;
    int     w   = c->right - PAD * 2;
    wchar_t line[160];

    text_at(dc, PAD, top + S(14), w, S(24),
            L"Выбрать из запущенных", CLR_TEXT, g_font_big, DT_LEFT);
    StringCchPrintfW(line, 160,
                     L"Приложений: %d, отмечено: %d · список обновляется сам",
                     g_pk_view_n, g_pk_checked_n);
    text_at(dc, PAD, top + S(40), w, S(18), line, CLR_MUTED, g_font_small, DT_LEFT);
    text_at(dc, PAD, top + S(64), S(60), S(28), L"Поиск", CLR_MUTED, g_font_small, DT_LEFT);
}

/* Where the second section starts depends on how many rows the first has. */
static int ed_path_top(void) { return TABS_H + S(92) + g_ed_ncount * S(36) + S(14); }

static void paint_edit(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    text_at(dc, PAD, top + S(14), w, S(24),
            g_ed_orig[0] ? g_ed_orig : L"Новый список приложений",
            CLR_TEXT, g_font_big, DT_LEFT | DT_END_ELLIPSIS);
    text_at(dc, PAD, top + S(40), w, S(18),
            L"Заполните любой из разделов или оба. Пустые строки не сохраняются.",
            CLR_MUTED, g_font_small, DT_LEFT);

    text_at(dc, PAD, top + S(70), w, S(20), L"Имя процесса",
            CLR_MUTED, g_font, DT_LEFT);
    text_at(dc, PAD, ed_path_top() - S(22), w, S(20), L"Расположение exe-файла",
            CLR_MUTED, g_font, DT_LEFT);
}

static void paint_settings(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    text_at(dc, PAD, top + S(14), w, S(24), L"Настройки", CLR_TEXT, g_font_big, DT_LEFT);
    text_at(dc, PAD, top + S(40), w, S(34),
            L"Туннель собирается заново при включении VPN — изменения "
            L"вступят в силу со следующего включения.",
            CLR_MUTED, g_font_small, DT_LEFT | DT_WORDBREAK);

    fill(dc, 0, top + S(80), c->right, S(1), g_brush_line);

    text_at(dc, PAD, top + S(94),  S(120), S(28), L"MTU",     CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, PAD, top + S(136), S(120), S(28), L"Журнал",  CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, PAD, top + S(178), S(120), S(28), L"Стек",    CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, PAD, top + S(220), S(120), S(28), L"DNS",     CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, PAD, top + S(262), S(120), S(28), L"Подписка", CLR_TEXT, g_font, DT_LEFT);

    text_at(dc, PAD + S(238), top + S(94), S(80), S(28), L"Что это?",
            CLR_ACCENT, g_font_small, DT_LEFT);
    text_at(dc, c->right - PAD - S(76), top + S(178), S(76), S(28), L"Что это?",
            CLR_ACCENT, g_font_small, DT_LEFT);
    text_at(dc, c->right - PAD - S(76), top + S(220), S(76), S(28), L"Что это?",
            CLR_ACCENT, g_font_small, DT_LEFT);

    fill(dc, 0, top + S(304), c->right, S(1), g_brush_line);
    text_at(dc, PAD + S(216), top + S(430), w - S(216), S(20),
            L"Версия " UTGARD_VERSION_W, CLR_MUTED, g_font_small, DT_LEFT);
}

static void paint_zapret(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    if (!g_zap.valid) {
        text_at(dc, PAD, top + S(28), w, S(22),
                L"Папка zapret не указана", CLR_TEXT, g_font_big, DT_LEFT);
        text_at(dc, PAD, top + S(54), w, S(20),
                L"Укажите папку, в которую распакован zapret —",
                CLR_MUTED, g_font_small, DT_LEFT);
        text_at(dc, PAD, top + S(72), w, S(20),
                L"ту, где лежат service.bat и файлы стратегий",
                CLR_MUTED, g_font_small, DT_LEFT);

        if (g_zap.problem[0])
            text_at(dc, PAD, top + S(162), w, S(20),
                    g_zap.problem, CLR_WARN, g_font_small, DT_LEFT);
        return;
    }

    text_at(dc, PAD, top + S(24), w - S(160), S(20),
            g_zap.path, CLR_TEXT, g_font_mono, DT_LEFT | DT_PATH_ELLIPSIS);

    {
        wchar_t line[128];
        if (g_zap.version[0])
            StringCchPrintfW(line, 128, L"версия %s · стратегий: %d",
                             g_zap.version, g_zap.strategy_count);
        else
            StringCchPrintfW(line, 128, L"версия не определена · стратегий: %d",
                             g_zap.strategy_count);
        text_at(dc, PAD, top + S(44), w, S(18), line, CLR_MUTED, g_font_small, DT_LEFT);
    }

    fill(dc, 0, top + S(70), c->right, S(1), g_brush_line);

    {
        const wchar_t *sub;
        COLORREF       color;
        wchar_t        head[ZAPRET_NAME_MAX + 32];

        if (g_status.mode == ZAPRET_OFF) {
            color = CLR_MUTED;
            StringCchCopyW(head, ZAPRET_NAME_MAX + 32, L"zapret выключен");
            sub   = L"winws.exe не запущен, служба zapret не работает";
        } else {
            color = CLR_OK;
            if (g_status.strategy[0])
                StringCchPrintfW(head, ZAPRET_NAME_MAX + 32, L"Работает: %s",
                                 g_status.strategy);
            else
                StringCchCopyW(head, ZAPRET_NAME_MAX + 32,
                               L"Работает, стратегия неизвестна");
            sub = g_status.mode == ZAPRET_SERVICE
                      ? L"запускается при загрузке"
                      : L"запущен отдельным bat — в автозапуске не будет";
        }

        dot(dc, PAD + S(6), top + S(96), S(5), color);
        text_at(dc, PAD + S(22), top + S(86), w, S(20), head, color, g_font, DT_LEFT);
        if (g_busy && g_busy_text) sub = g_busy_text;
        text_at(dc, PAD + S(22), top + S(104), w, S(18), sub,
                CLR_MUTED, g_font_small, DT_LEFT);
    }

    /* Instead of a message box after every change: one line that stays until
       the strategy is actually restarted. */
    if (g_zap_dirty)
        text_at(dc, PAD + S(22), top + S(122), w - S(22), S(20),
                L"Есть изменения, требующие перезапуска стратегии",
                CLR_WARN, g_font_small, DT_LEFT);

    fill(dc, 0, top + S(146), c->right, S(1), g_brush_line);

    fill(dc, 0, top + S(147), c->right, S(46), g_brush_surface);
    {
        const wchar_t *text;
        COLORREF       color;
        wchar_t        line[200];

        if (g_exc_known == 0) {
            text  = g_prof.count ? L"Проверяю адреса VPN в исключениях zapret…"
                                 : L"Нет профилей — исключать нечего";
            color = CLR_MUTED;
        } else if (g_exc_present >= g_exc_known) {
            StringCchPrintfW(line, 200, L"Адреса VPN в исключениях zapret: %d из %d",
                             g_exc_present, g_exc_known);
            text  = line;
            color = CLR_OK;
        } else {
            StringCchPrintfW(line, 200, L"В исключениях осталось %d из %d адресов VPN",
                             g_exc_present, g_exc_known);
            text  = line;
            color = CLR_WARN;
        }
        text_at(dc, PAD, top + S(147), w - S(264), S(46), text,
                color, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    }

    fill(dc, 0, top + S(202), c->right, S(1), g_brush_line);

    {
        static const int rows[4] = { 210, 246, 282, 318 };
        wchar_t          state[120];
        int              i;

        for (i = 0; i < 4; i++)
            text_at(dc, PAD + S(278), top + S(rows[i]), S(78), S(30),
                    L"Что это?", CLR_ACCENT, g_font_small, DT_LEFT);

        switch (zapret_game_get(g_zap.path)) {
        case GAME_ALL: StringCchCopyW(state, 120, L"TCP и UDP"); break;
        case GAME_TCP: StringCchCopyW(state, 120, L"только TCP"); break;
        case GAME_UDP: StringCchCopyW(state, 120, L"только UDP"); break;
        default:       StringCchCopyW(state, 120, L"выключен");   break;
        }
        text_at(dc, PAD + S(364), top + S(210), w - S(364), S(30), state,
                CLR_TEXT, g_font_small, DT_LEFT);

        switch (zapret_ipset_get(g_zap.path)) {
        case IPSET_NONE: StringCchCopyW(state, 120, L"none"); break;
        case IPSET_ANY:  StringCchCopyW(state, 120, L"any");  break;
        default:         StringCchCopyW(state, 120, L"loaded"); break;
        }
        text_at(dc, PAD + S(364), top + S(246), w - S(364), S(30), state,
                CLR_TEXT, g_font_small, DT_LEFT);
    }

    fill(dc, 0, top + S(392), c->right, S(1), g_brush_line);

    text_at(dc, PAD, top + S(402), w, S(20), L"Стратегии", CLR_MUTED, g_font, DT_LEFT);
    {
        wchar_t hint[64];
        StringCchPrintfW(hint, 64, L"всего: %d", g_count);
        text_at(dc, PAD, top + S(402), w, S(20), hint, CLR_MUTED, g_font_small, DT_RIGHT);
    }
}

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT c;
    GetClientRect(hwnd, &c);

    fill(dc, 0, 0, c.right, c.bottom - FOOTER_H, g_brush_bg);
    fill(dc, 0, TABS_H - S(1), c.right, S(1), g_brush_line);

    if (g_page == PAGE_UTGARD)      paint_utgard(dc, &c);
    else if (g_page == PAGE_APPS)   paint_apps(dc, &c);
    else if (g_page == PAGE_HOSTS)  paint_hosts(dc, &c);
    else if (g_page == PAGE_PICK)   paint_pick(dc, &c);
    else if (g_page == PAGE_EDIT)   paint_edit(dc, &c);
    else if (g_page == PAGE_SETTINGS) paint_settings(dc, &c);
    else                            paint_zapret(dc, &c);

    fill(dc, 0, c.bottom - FOOTER_H, c.right, FOOTER_H, g_brush_footer);

    EndPaint(hwnd, &ps);
}

static void draw_strategy(const DRAWITEMSTRUCT *d)
{
    wchar_t  name[ZAPRET_NAME_MAX] = { 0 };
    RECT     r        = d->rcItem;
    BOOL     selected = (d->itemState & ODS_SELECTED) != 0;
    int      running;

    if (d->itemID == (UINT)-1) return;
    if (SendMessageW(d->hwndItem, LB_GETTEXTLEN, d->itemID, 0) >= ZAPRET_NAME_MAX)
        return;
    SendMessageW(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)name);

    running = g_status.mode != ZAPRET_OFF && g_status.strategy[0] &&
              _wcsicmp(name, g_status.strategy) == 0;

    FillRect(d->hDC, &r, (selected || (int)d->itemID == list_hot_row(d->hwndItem))
                             ? g_brush_line : g_brush_surface);
    if (running) {
        HBRUSH br = CreateSolidBrush(CLR_OK);
        fill(d->hDC, r.left, r.top, S(3), r.bottom - r.top, br);
        DeleteObject(br);
    }

    text_at(d->hDC, r.left + S(12), r.top, r.right - r.left - S(90), r.bottom - r.top,
            name, CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    if (running)
        text_at(d->hDC, r.left, r.top, r.right - r.left - S(12), r.bottom - r.top,
                L"работает", CLR_OK, g_font_small, DT_RIGHT);
}

/* UTF-8 to UTF-16 that always leaves a usable string: on overflow the
   conversion fails and would otherwise leave the buffer untouched. */
static void to_wide(const char *src, wchar_t *dst, int cap)
{
    if (cap <= 0) return;
    dst[0] = L'\0';
    if (!src || !src[0]) return;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap) == 0) {
        dst[0] = L'\0';
    }
}

static const wchar_t *proto_label(link_proto p)
{
    switch (p) {
    case LINK_VLESS: return L"VLESS";
    case LINK_HY2:   return L"Hysteria2";
    case LINK_SS:    return L"Shadowsocks";
    case LINK_TROJAN: return L"Trojan";
    default:         return L"?";
    }
}

static void draw_profile(const DRAWITEMSTRUCT *d)
{
    const profile_entry *e;
    RECT     r        = d->rcItem;
    BOOL     selected = (d->itemState & ODS_SELECTED) != 0;
    int      active;
    wchar_t  right[128];

    if (d->itemID == (UINT)-1 || (int)d->itemID >= g_prof.count) return;
    e = &g_prof.items[d->itemID];
    active = ((int)d->itemID == g_prof.active);

    FillRect(d->hDC, &r, (selected || (int)d->itemID == list_hot_row(d->hwndItem))
                             ? g_brush_line : g_brush_surface);
    if (active) {
        HBRUSH br = CreateSolidBrush(CLR_OK);
        fill(d->hDC, r.left, r.top, S(3), r.bottom - r.top, br);
        DeleteObject(br);
    }

    {
        wchar_t title[288];
        to_wide(e->link.name[0] ? e->link.name : e->link.server, title, 288);
        text_at(d->hDC, r.left + S(12), r.top, r.right - r.left - S(150),
                r.bottom - r.top, title, active ? CLR_OK : CLR_TEXT,
                g_font, DT_LEFT | DT_END_ELLIPSIS);
    }

    {
        wchar_t server[288];
        to_wide(e->link.server, server, 288);
        StringCchPrintfW(right, 128, active ? L"%s · %s · выбран" : L"%s · %s",
                         proto_label(e->link.proto), server);
    }
    text_at(d->hDC, r.left, r.top, r.right - r.left - S(84), r.bottom - r.top,
            right, active ? CLR_OK : CLR_MUTED, g_font_small, DT_RIGHT | DT_END_ELLIPSIS);

    {
        int      ms = g_ping[d->itemID];
        wchar_t  lat[24];
        COLORREF color;

        if (ms == -2)      { StringCchCopyW(lat, 24, L"…");   color = CLR_MUTED; }
        else if (ms < 0)   { StringCchCopyW(lat, 24, L"нет"); color = CLR_WARN;  }
        else {
            StringCchPrintfW(lat, 24, L"%d мс", ms);
            color = ms < 150 ? CLR_OK : (ms < 400 ? CLR_ACCENT : CLR_WARN);
        }
        text_at(d->hDC, r.left, r.top, r.right - r.left - S(12), r.bottom - r.top,
                lat, color, g_font_small, DT_RIGHT);
    }
}

#define APP_ZONE_TOGGLE 96
#define APP_ZONE_DELETE 84

/* Each row carries its own actions, so nothing has to be selected first. */
static int app_zone_at(int x, int width)
{
    int del = width - S(12) - S(APP_ZONE_DELETE);
    int tog = del - S(10) - S(APP_ZONE_TOGGLE);

    if (x >= del) return 2;
    if (x >= tog) return 1;
    return 0;
}

static void draw_app_row(const DRAWITEMSTRUCT *d)
{
    const app_entry *e;
    RECT r = d->rcItem;
    int  width = r.right - r.left;
    int  del = r.left + width - S(12) - S(APP_ZONE_DELETE);
    int  tog = del - S(10) - S(APP_ZONE_TOGGLE);

    if (d->itemID == (UINT)-1 || (int)d->itemID >= g_appv_n) return;
    e = &g_appv[d->itemID];

    {
        int      hot_row = ((int)d->itemID == g_app_hover_item);
        COLORREF row_bg  = hot_row ? CLR_LINE : CLR_SURFACE;
        HBRUSH   rb      = hot_row ? g_brush_line : g_brush_surface;
        int      ph      = S(24);
        int      py      = r.top + (r.bottom - r.top - ph) / 2;

        FillRect(d->hDC, &r, rb);
        if (e->enabled) {
            HBRUSH br = CreateSolidBrush(CLR_OK);
            fill(d->hDC, r.left, r.top, S(3), r.bottom - r.top, br);
            DeleteObject(br);
        }

        /* The name opens the editor, so it reacts too: underlined when the
           cursor is over it. */
        {
            COLORREF nc = e->enabled ? CLR_OK : CLR_TEXT;
            text_at(d->hDC, r.left + S(14), r.top, tog - r.left - S(22),
                    r.bottom - r.top, e->name, nc, g_font,
                    DT_LEFT | DT_END_ELLIPSIS);
            if (hot_row && g_app_hover_zone == 0) {
                SIZE    sz;
                HGDIOBJ old = SelectObject(d->hDC, g_font);
                int     tw;
                GetTextExtentPoint32W(d->hDC, e->name, (int)wcslen(e->name), &sz);
                SelectObject(d->hDC, old);
                tw = sz.cx;
                if (tw > tog - r.left - S(22)) tw = tog - r.left - S(22);
                {
                    HBRUSH ub = CreateSolidBrush(nc);
                    fill(d->hDC, r.left + S(14), r.top + (r.bottom - r.top) / 2 + S(9),
                         tw, S(1), ub);
                    DeleteObject(ub);
                }
            }
        }

        /* Outlined buttons at rest, filled under the cursor: they have to
           read as buttons before anyone hovers them, and answer when they do. */
        {
            int  hot_t = hot_row && g_app_hover_zone == 1;
            int  hot_d = hot_row && g_app_hover_zone == 2;
            RECT bt = { tog + S(4), py, tog + S(APP_ZONE_TOGGLE) - S(4), py + ph };
            RECT bd = { del + S(4), py, del + S(APP_ZONE_DELETE) - S(4), py + ph };

            rounded(d->hDC, &bt, hot_t ? CLR_ACCENT : row_bg, CLR_ACCENT);
            text_at(d->hDC, bt.left, bt.top, bt.right - bt.left, ph,
                    e->enabled ? L"Выключить" : L"Включить",
                    hot_t ? CLR_BG : CLR_ACCENT, g_font_small, DT_CENTER);

            rounded(d->hDC, &bd, hot_d ? CLR_WARN : row_bg, CLR_WARN);
            text_at(d->hDC, bd.left, bd.top, bd.right - bd.left, ph,
                    L"Удалить", hot_d ? CLR_BG : CLR_WARN, g_font_small, DT_CENTER);
        }
    }
}

static void draw_pick_row(const DRAWITEMSTRUCT *d)
{
    const pick_proc *p;
    RECT  r = d->rcItem;
    int   ticked, box, readable;
    HICON icon;

    if (d->itemID == (UINT)-1 || (int)d->itemID >= g_pk_view_n) return;
    p = &g_pk_all[g_pk_view[d->itemID]];
    readable = p->path[0] != L'\0';
    ticked   = pk_is_checked(p->path);

    FillRect(d->hDC, &r, (ticked || (int)d->itemID == list_hot_row(d->hwndItem))
                             ? g_brush_line : g_brush_surface);

    icon = readable ? pick_icon(p->path) : NULL;
    if (icon)
        DrawIconEx(d->hDC, r.left + S(10), r.top + (r.bottom - r.top - S(16)) / 2,
                   icon, S(16), S(16), 0, NULL, DI_NORMAL);

    {
        wchar_t title[MAX_PATH + 16];
        if (p->count > 1) StringCchPrintfW(title, MAX_PATH + 16, L"%s  ×%d",
                                           p->name, p->count);
        else              StringCchCopyW(title, MAX_PATH + 16, p->name);
        text_at(d->hDC, r.left + S(34), r.top + S(2), r.right - r.left - S(80), S(18),
                title, readable ? CLR_TEXT : CLR_MUTED, g_font, DT_LEFT | DT_END_ELLIPSIS);
    }
    text_at(d->hDC, r.left + S(34), r.top + S(20), r.right - r.left - S(80), S(16),
            readable ? p->path : L"расположение недоступно", CLR_MUTED,
            g_font_small, DT_LEFT | DT_PATH_ELLIPSIS);

    if (readable) {
        RECT b;
        box = S(16);
        b.left   = r.right - S(14) - box;
        b.top    = r.top + (r.bottom - r.top - box) / 2;
        b.right  = b.left + box;
        b.bottom = b.top + box;
        rounded(d->hDC, &b, ticked ? CLR_OK : CLR_SURFACE, ticked ? CLR_OK : CLR_MUTED);
        if (ticked)
            text_at(d->hDC, b.left, b.top, box, box, L"✓", CLR_BG, g_font_small, DT_CENTER);
    }
}

static void draw_button(const DRAWITEMSTRUCT *d)
{
    LONG_PTR ud       = GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    int      kind     = (int)(ud & 0xFF);
    COLORREF backdrop = (COLORREF)((ud >> 8) & 0xFFFFFF);
    BOOL     pressed  = (d->itemState & ODS_SELECTED) != 0;
    BOOL     disabled = (d->itemState & ODS_DISABLED) != 0;
    BOOL     hot      = !disabled && ask_is_hot(d->hwndItem);
    RECT     r = d->rcItem;
    wchar_t  caption[64];

    if (((ud >> 8) & 0xFFFFFF) == 0) backdrop = CLR_BG;

    GetWindowTextW(d->hwndItem, caption, (int)(sizeof caption / sizeof caption[0]));

    if (kind == BK_CHECK) {
        /* A checkbox drawn in the palette: the themed one ignores our colours. */
        int  on  = GetPropW(d->hwndItem, L"utgard.checked") != NULL;
        int  box = S(18);
        RECT b;

        FillRect(d->hDC, &r, g_brush_bg);
        b.left = r.left; b.top = r.top + (r.bottom - r.top - box) / 2;
        b.right = b.left + box; b.bottom = b.top + box;
        rounded(d->hDC, &b, on ? CLR_OK : (hot ? CLR_SURFACE : CLR_BG),
                on ? CLR_OK : (hot ? CLR_TEXT : CLR_MUTED));
        if (on) text_at(d->hDC, b.left, b.top, box, box, L"✓", CLR_BG, g_font_small, DT_CENTER);
        text_at(d->hDC, r.left + box + S(10), r.top, r.right - r.left - box - S(10),
                r.bottom - r.top, caption, CLR_TEXT, g_font, DT_LEFT);
        return;
    }

    if (kind == BK_TAB) {
        /* The applications page belongs to the Utgard tab. */
        BOOL active = (d->CtlID == ID_TAB_UTGARD &&
                       (g_page == PAGE_UTGARD || g_page == PAGE_APPS ||
                        (g_page == PAGE_HOSTS && g_hosts_mode == HOSTS_VPN) ||
                        g_page == PAGE_PICK || g_page == PAGE_EDIT)) ||
                      (d->CtlID == ID_TAB_ZAPRET &&
                       (g_page == PAGE_ZAPRET ||
                        (g_page == PAGE_HOSTS && g_hosts_mode == HOSTS_ZAPRET)));
        FillRect(d->hDC, &r, g_brush_bg);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                caption, active ? CLR_ACCENT : (hot ? CLR_TEXT : CLR_MUTED),
                g_font, DT_CENTER);
        if (active) {
            HBRUSH br = CreateSolidBrush(CLR_ACCENT);
            fill(d->hDC, r.left, r.bottom - S(2), r.right - r.left, S(2), br);
            DeleteObject(br);
        }
    } else if (kind == BK_PRIMARY) {
        COLORREF bg = disabled ? CLR_LINE
                    : pressed  ? CLR_ACCENT_LO
                    : hot      ? CLR_ACCENT_HI : CLR_ACCENT;
        HBRUSH   back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        rounded(d->hDC, &r, bg, bg);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                caption, disabled ? CLR_MUTED : CLR_BG, g_font, DT_CENTER);
    } else {
        COLORREF border = kind == BK_DANGER ? CLR_WARN : CLR_MUTED;
        HBRUSH   back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        /* Filled with its own border colour under the pointer, like the row
           buttons in the application manager. */
        rounded(d->hDC, &r, pressed ? CLR_SURFACE : (hot ? border : backdrop), border);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                caption, disabled ? CLR_MUTED : (hot ? CLR_BG : CLR_TEXT),
                g_font, DT_CENTER);
    }

    /* The focus frame only when the keyboard is in use. Windows marks a
       mouse-driven focus with ODS_NOFOCUSRECT; pressing Tab clears that and
       the frame comes back, so keyboard users still see where they are. */
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(d->hDC, &f);
    }
}

/* ---- window --------------------------------------------------------- */

/* Reload the strategy list from the current folder. */
static void strategies_reload(void)
{
    int i;

    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_count = 0;
    if (!g_zap.valid) return;

    g_count = zapret_list(g_zap.path, g_names, ZAPRET_MAX_STRATEGIES);
    if (g_count > ZAPRET_MAX_STRATEGIES) g_count = ZAPRET_MAX_STRATEGIES;

    for (i = 0; i < g_count; i++)
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_names[i]);

    if (g_count) SendMessageW(g_list, LB_SETCURSEL, 0, 0);
}

/* Re-read live state; returns 1 when something changed. */
static int status_refresh(void)
{
    zapret_status now;

    zapret_status_read(&now);
    if (now.mode == g_status.mode && wcscmp(now.strategy, g_status.strategy) == 0)
        return 0;

    g_status = now;
    return 1;
}

/* Failures used to go to a line in the footer; that line is gone, so they
   have to be shown outright rather than disappear. */
static void problem(HWND hwnd, const wchar_t *text)
{
    MessageBoxW(hwnd, text, L"Utgard", MB_ICONWARNING | MB_OK);
}

/* Name of the strategy highlighted in the list, or NULL when none is. */
static const wchar_t *selected_strategy(void)
{
    LRESULT i = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (i == LB_ERR || i < 0 || i >= g_count) return NULL;
    return g_names[i];
}

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


static long_job *job_new(job_work work, job_done done)
{
    long_job *j = (long_job *)calloc(1, sizeof *j);
    if (j) { j->work = work; j->done = done; }
    return j;
}

static void job_free(long_job *j)
{
    if (!j) return;
    if (j->extra) {
        SecureZeroMemory(j->extra, j->extra_size);
        free(j->extra);
    }
    free(j->text);
    free(j);
}

static DWORD WINAPI job_thread(LPVOID param)
{
    long_job *j = (long_job *)param;
    j->work(j);
    PostMessageW(j->hwnd, WM_APP_JOB_DONE, 0, (LPARAM)j);
    return 0;
}

/* Takes ownership of the job in every case. */
static int job_start(HWND hwnd, const wchar_t *label, long_job *j)
{
    HANDLE th;

    if (!j) { problem(hwnd, L"Не хватило памяти"); return 0; }
    if (g_busy) { job_free(j); return 0; }

    j->hwnd = hwnd;
    th = CreateThread(NULL, 0, job_thread, j, 0, NULL);
    if (!th) {
        job_free(j);
        problem(hwnd, L"Не удалось запустить фоновую задачу");
        return 0;
    }
    CloseHandle(th);
    g_busy      = 1;
    g_busy_text = label;
    layout(hwnd);
    return 1;
}

static void after_action(HWND hwnd)
{
    status_refresh();
    layout(hwnd);
}

static void zap_done(HWND hwnd, long_job *j)
{
    if (!j->ok) problem(hwnd, j->msg[0] ? j->msg : L"Операция с zapret не удалась");
    after_action(hwnd);
}

static void work_zap_stop(long_job *j)    { j->ok = zapret_service_remove(j->msg, SB_MSG_MAX); }
static void work_zap_restart(long_job *j) { j->ok = zapret_service_restart(j->msg, SB_MSG_MAX); }
static void work_zap_start(long_job *j)
{
    j->ok = zapret_service_install(j->dir, j->name, j->msg, SB_MSG_MAX);
}

static void act_stop(HWND hwnd)
{
    if (g_busy) return;
    g_zap_dirty = 0;
    job_start(hwnd, L"выключаю zapret…", job_new(work_zap_stop, zap_done));
}

/* name == NULL means "whatever is highlighted in the list". */
static void act_start(HWND hwnd, const wchar_t *name)
{
    if (g_busy) return;
    if (!name) name = selected_strategy();
    if (!name) {
        problem(hwnd, L"Сначала выберите стратегию в списке");
        return;
    }

    /* Same guard as the greyed-out button, for the double-click path. */
    if (g_status.mode == ZAPRET_SERVICE && _wcsicmp(name, g_status.strategy) == 0)
        return;

    g_zap_dirty = 0;
    {
        long_job *j = job_new(work_zap_start, zap_done);
        if (j) {
            StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
            StringCchCopyW(j->name, ZAPRET_NAME_MAX, name);
        }
        job_start(hwnd, L"устанавливаю службу zapret…", j);
    }
}

static void act_restart(HWND hwnd)
{
    if (g_busy) return;
    if (g_status.mode != ZAPRET_SERVICE) {
        /* Nothing installed: restarting means installing what is selected. */
        act_start(hwnd, NULL);
        return;
    }

    g_zap_dirty = 0;
    job_start(hwnd, L"перезапускаю zapret…", job_new(work_zap_restart, zap_done));
}

static void profiles_reload(void)
{
    int i;

    SendMessageW(g_plist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_prof.count; i++)
        SendMessageW(g_plist, LB_ADDSTRING, 0, (LPARAM)L"");
    if (g_prof.count)
        SendMessageW(g_plist, LB_SETCURSEL,
                     (WPARAM)(g_prof.active >= 0 ? g_prof.active : 0), 0);
}

static int profile_selected(void)
{
    LRESULT i = SendMessageW(g_plist, LB_GETCURSEL, 0, 0);
    if (i == LB_ERR || i < 0 || i >= g_prof.count) return -1;
    return (int)i;
}

/* Same server reached the same way is the same profile. Several profiles of
   one protocol are fine now, so only a full match counts as a duplicate. */
static int same_profile(const link_profile *a, const link_profile *b)
{
    return a->proto == b->proto && a->port == b->port &&
           strcmp(a->server, b->server) == 0 &&
           strcmp(a->uuid, b->uuid) == 0 &&
           strcmp(a->password, b->password) == 0;
}

static int profile_duplicate(const link_profile *l)
{
    int i;
    for (i = 0; i < g_prof.count; i++)
        if (same_profile(&g_prof.items[i].link, l)) return i;
    return -1;
}

typedef struct {
    HWND hwnd;
    int  gen;
    int  count;
    struct { char server[256]; int port; int icmp; } target[PROFILES_MAX];
} ping_job;

/* One worker walks the list so the rows fill in as answers arrive, instead of
   the window sitting still until the slowest server times out. */
static DWORD WINAPI ping_thread(LPVOID param)
{
    ping_job *job = (ping_job *)param;
    int       i;

    for (i = 0; i < job->count; i++) {
        int ms = net_probe(job->target[i].server, job->target[i].port,
                           job->target[i].icmp, 1500);
        PostMessageW(job->hwnd, WM_APP_PING_ONE,
                     (WPARAM)((job->gen << 8) | (i & 0xFF)), (LPARAM)ms);
    }

    PostMessageW(job->hwnd, WM_APP_PING_DONE, (WPARAM)job->gen, 0);
    free(job);
    return 0;
}

static void ping_start(HWND hwnd)
{
    ping_job *job;
    HANDLE    th;
    int       i;

    g_ping_gen++;
    for (i = 0; i < PROFILES_MAX; i++) g_ping[i] = -2;
    if (g_prof.count == 0) { g_ping_busy = 0; return; }

    job = (ping_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd  = hwnd;
    job->gen   = g_ping_gen & 0xFF;
    job->count = g_prof.count;
    for (i = 0; i < g_prof.count; i++) {
        StringCchCopyA(job->target[i].server, 256, g_prof.items[i].link.server);
        job->target[i].port = g_prof.items[i].link.port;
        job->target[i].icmp = (g_prof.items[i].link.proto == LINK_HY2);
    }

    th = CreateThread(NULL, 0, ping_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);
    g_ping_busy = 1;
    layout(hwnd);                   /* greys the refresh button while it runs */
}

typedef struct {
    HWND    hwnd;
    wchar_t url[2048];
    char   *body;
    size_t  len;
    wchar_t err[256];
    int     ok;
    int     silent;     /* automatic refresh: no windows either way */
} sub_job;

/* Runs off the UI thread and does nothing but fetch: everything that parses
   the answer happens back on the UI thread, in already-tested code. */
static DWORD WINAPI sub_thread(LPVOID param)
{
    sub_job *job = (sub_job *)param;

    job->ok = net_fetch(job->url, &job->body, &job->len, job->err, 256);
    PostMessageW(job->hwnd, WM_APP_SUB_DONE, 0, (LPARAM)job);
    return 0;
}

/* Replace the profiles that came from this subscription, leave every other
   profile alone, and try to keep the same one active. */
static void subscription_apply(HWND hwnd, const wchar_t *url,
                               const char *body, size_t len, int silent)
{
    static link_profile  fetched[PROFILES_MAX];
    static profile_entry keep[PROFILES_MAX];
    char         url8[PROFILE_SRC];
    char         err[256];
    wchar_t      msg[320];
    link_profile was_active;
    int          had_active, n, skipped = 0, i, kept = 0, added = 0, dropped = 0;

    if (WideCharToMultiByte(CP_UTF8, 0, url, -1, url8, (int)sizeof url8,
                            NULL, NULL) == 0) {
        problem(hwnd, L"Слишком длинный адрес подписки");
        return;
    }

    n = link_parse_subscription(body, len, fetched, PROFILES_MAX, &skipped,
                                err, sizeof err);
    if (n <= 0) {
        /* The profiles are left untouched either way; an automatic refresh
           that met an error page just tries again later, without a window. */
        if (!silent) {
            to_wide(err, msg, 320);
            problem(hwnd, msg);
        }
        return;
    }

    had_active = (g_prof.active >= 0 && g_prof.active < g_prof.count);
    if (had_active) was_active = g_prof.items[g_prof.active].link;

    /* One subscription at a time: everything that came from a subscription is
       replaced, everything added by hand survives. Keeping profiles from a
       previous subscription would leave orphans nothing can ever refresh. */
    for (i = 0; i < g_prof.count; i++)
        if (g_prof.items[i].source[0] == '\0')
            keep[kept++] = g_prof.items[i];

    memset(&g_prof.items, 0, sizeof g_prof.items);
    memcpy(g_prof.items, keep, (size_t)kept * sizeof keep[0]);
    g_prof.count = kept;

    for (i = 0; i < n; i++) {
        if (g_prof.count >= PROFILES_MAX) { dropped = n - i; break; }
        /* The kept manual profiles may already describe one of these servers,
           and a subscription can repeat itself. Either way, no second copy. */
        if (profile_duplicate(&fetched[i]) >= 0) continue;
        memset(&g_prof.items[g_prof.count], 0, sizeof g_prof.items[0]);
        g_prof.items[g_prof.count].link = fetched[i];
        StringCchCopyA(g_prof.items[g_prof.count].source, PROFILE_SRC, url8);
        g_prof.count++;
        added++;
    }

    StringCchCopyA(g_prof.subscription, PROFILE_SRC, url8);

    g_prof.active = g_prof.count ? 0 : -1;
    if (had_active) {
        for (i = 0; i < g_prof.count; i++) {
            const link_profile *o = &g_prof.items[i].link;
            if (o->proto == was_active.proto && o->port == was_active.port &&
                strcmp(o->server, was_active.server) == 0 &&
                strcmp(o->uuid, was_active.uuid) == 0 &&
                strcmp(o->password, was_active.password) == 0) {
                g_prof.active = i;
                break;
            }
        }
    }

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Подписка загружена, но сохранить её не удалось");

    /* Skipped lines are routine - panels carry protocols we do not support -
       so the count in the header is enough. A full list is worth a warning. */
    if (dropped && !silent) {
        StringCchPrintfW(msg, 320,
                         L"Загружено профилей: %d, ещё %d не поместилось — "
                         L"список заполнен.", added, dropped);
        problem(hwnd, msg);
    }
    (void)skipped;

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

static void act_subscription(HWND hwnd)
{
    wchar_t  url[2048];
    wchar_t  current[PROFILE_SRC];
    sub_job *job;
    HANDLE   th;

    if (g_sub_busy) return;

    to_wide(g_prof.subscription, current, PROFILE_SRC);
    if (!ask_string(hwnd, L"Подписка",
                    L"Адрес подписки — профили из неё будут обновляться целиком, "
                    L"добавленные вручную останутся",
                    current[0] ? current : NULL, url, 2048))
        return;

    job = (sub_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd = hwnd;
    StringCchCopyW(job->url, 2048, url);

    th = CreateThread(NULL, 0, sub_thread, job, 0, NULL);
    if (!th) {
        free(job);
        problem(hwnd, L"Не удалось запустить загрузку");
        return;
    }
    CloseHandle(th);

    g_sub_busy = 1;
    layout(hwnd);
}

/* Refresh the subscription on its own once the chosen interval has passed.
   Called from a one-minute timer and once at startup. */
static void sub_auto_check(HWND hwnd)
{
    sub_job  *job;
    HANDLE    th;
    long long now = _time64(NULL);
    long long due;

    if (g_sub_busy || !g_prof.subscription[0]) return;
    if (g_sub_retry && now < g_sub_retry) return;

    settings_load(&g_set);
    due = g_set.sub_last + (long long)settings_sub_hours[g_set.sub_interval] * 3600;
    if (g_set.sub_last && now < due) return;

    job = (sub_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd   = hwnd;
    job->silent = 1;
    to_wide(g_prof.subscription, job->url, 2048);

    th = CreateThread(NULL, 0, sub_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);

    g_sub_busy = 1;
    layout(hwnd);
}

/* ---- list files ------------------------------------------------------
   All file access for the lists goes through Win32, not stdio: the product
   may well live under C:\Users\<кириллица>, where fopen with a UTF-8 path
   fails. The list module itself only ever sees text. */

static int root_file(const wchar_t *tail, wchar_t *out, size_t cap)
{
    wchar_t root[MAX_PATH * 2];
    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%s", root, tail));
}



static int count_entries(const char *text)
{
    static char e[LIST_MAX][LIST_ENTRY_MAX];
    return lists_read_text(text, e, LIST_MAX);
}

static void lists_refresh_counts(void)
{
    static char buf[LIST_TEXT_MAX];
    wchar_t path[MAX_PATH * 2];

    g_host_count = 0;
    g_app_count  = 0;

    if (root_file(L"list\\hosts", path, MAX_PATH * 2) &&
        file_read(path, buf, LIST_TEXT_MAX, NULL))
        g_host_count = count_entries(buf);

    /* Applications are one config file each, the way the original client kept
       them; the count is simply how many files are there. */
    {
        wchar_t          mask[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        HANDLE           h;

        if (root_file(L"list\\applications\\active\\*.json", mask, MAX_PATH * 2)) {
            h = FindFirstFileW(mask, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) g_app_count++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
}

/* Bridge between the editor, which works in UTF-16, and the list module,
   which works in UTF-8. */
/* The same bridge for zapret's list, tidied by zapret's rules. */
static int zap_tidy_bridge(const wchar_t *in, wchar_t *out, size_t cap, int *removed)
{
    static char a[LIST_TEXT_MAX], b[LIST_TEXT_MAX];

    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, a, LIST_TEXT_MAX, NULL, NULL) == 0)
        return 0;
    if (!lists_tidy_zapret(a, b, LIST_TEXT_MAX, removed)) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, b, -1, out, (int)cap) != 0;
}

static int tidy_bridge(const wchar_t *in, wchar_t *out, size_t cap, int *removed)
{
    static char a[LIST_TEXT_MAX], b[LIST_TEXT_MAX];

    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, a, LIST_TEXT_MAX, NULL, NULL) == 0)
        return 0;
    if (!lists_tidy_text(a, b, LIST_TEXT_MAX, removed)) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, b, -1, out, (int)cap) != 0;
}


/* zapret's user host list, inside the zapret folder. */
static int zapret_user_list(wchar_t *out, size_t cap)
{
    size_t len;
    if (!g_zap.valid || !g_zap.path[0]) return 0;
    len = wcslen(g_zap.path);
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%slists\\list-general-user.txt",
                                      g_zap.path,
                                      (len && g_zap.path[len - 1] == L'\\') ? L"" : L"\\"));
}

static void hosts_open(HWND hwnd, int mode)
{
    static char bytes[LIST_TEXT_MAX], crlf[LIST_TEXT_MAX];
    wchar_t    *text;
    wchar_t     path[MAX_PATH * 2];
    size_t      i, n = 0;
    int         have_path;

    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    if (!text) { problem(hwnd, L"Не хватило памяти"); return; }

    g_hosts_mode = mode;
    have_path = (mode == HOSTS_ZAPRET) ? zapret_user_list(path, MAX_PATH * 2)
                                       : root_file(L"list\\hosts", path, MAX_PATH * 2);
    if (have_path && file_read(path, bytes, LIST_TEXT_MAX, NULL)) {
        /* The edit control breaks lines on CR+LF only. */
        for (i = 0; bytes[i] && n + 2 < LIST_TEXT_MAX; i++) {
            if (bytes[i] == '\r') continue;
            if (bytes[i] == '\n') crlf[n++] = '\r';
            crlf[n++] = bytes[i];
        }
        crlf[n] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, crlf, -1, text, LIST_TEXT_MAX);
    }

    SetWindowTextW(g_hedit, text);
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    free(text);

    g_page = PAGE_HOSTS;
    layout(hwnd);
    SetFocus(g_hedit);
}

/* Writing the file is instant; compiling it runs sing-box, so the whole
   save happens on a worker. The text is copied out of the edit first. */
static void work_hosts_save(long_job *j)
{
    wchar_t     path[MAX_PATH * 2], jpath[MAX_PATH * 2], dir[MAX_PATH * 2];
    char       *json = (char *)malloc(LIST_TEXT_MAX);
    lists_stats st;

    if (!json) { StringCchCopyW(j->msg, SB_MSG_MAX, L"Не хватило памяти"); return; }

    if (root_file(L"list", dir, MAX_PATH * 2)) CreateDirectoryW(dir, NULL);
    if (!root_file(L"list\\hosts", path, MAX_PATH * 2) || !file_write(path, j->text, strlen(j->text))) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось сохранить список");
        goto out;
    }
    if (!lists_build_text(j->text, json, LIST_TEXT_MAX, &st)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Список слишком велик");
        goto out;
    }
    if (!root_file(L"list\\general.json", jpath, MAX_PATH * 2) ||
        !file_write(jpath, json, strlen(json))) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось записать general.json");
        goto out;
    }
    if (!singbox_compile_list(j->msg, SB_MSG_MAX)) {
        /* general.json stays: it is the artefact that broke. */
        if (!j->msg[0]) StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось собрать список");
        goto out;
    }
    DeleteFileW(jpath);
    j->ok = 1;

out:
    free(json);
}

static void done_hosts_save(HWND hwnd, long_job *j)
{
    if (!j->ok) { problem(hwnd, j->msg); return; }
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    lists_refresh_counts();
    /* Leave only once it really saved, and only if the user is still here:
       they may have switched tabs while it compiled. */
    if (j->flag && g_page == PAGE_HOSTS) g_page = PAGE_UTGARD;
}

/* zapret's list is written as it stands: no compiling, zapret reads the text
   itself when the strategy starts. service.bat seeds it with a placeholder
   and a note never to leave it empty; if every entry was removed, the same
   placeholder goes back. */
static void hosts_save_zapret(HWND hwnd)
{
    static char   bytes[LIST_TEXT_MAX + 64];
    static char   probe[64][LIST_ENTRY_MAX];
    wchar_t      *text;
    wchar_t       path[MAX_PATH * 2];
    size_t        len;

    if (!zapret_user_list(path, MAX_PATH * 2)) {
        problem(hwnd, L"Сначала укажите папку zapret");
        return;
    }
    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    if (!text) { problem(hwnd, L"Не хватило памяти"); return; }
    GetWindowTextW(g_hedit, text, LIST_TEXT_MAX);
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes, LIST_TEXT_MAX, NULL, NULL) == 0) {
        free(text);
        problem(hwnd, L"Список слишком велик");
        return;
    }
    free(text);

    if (lists_read_text(bytes, probe, 1) == 0) {
        len = strlen(bytes);
        if (len && bytes[len - 1] != '\n')
            StringCchCatA(bytes, sizeof bytes, "\r\n");
        StringCchCatA(bytes, sizeof bytes, "domain.example.abc\r\n");
    }

    if (!file_write(path, bytes, strlen(bytes))) {
        problem(hwnd, L"Не удалось сохранить список zapret");
        return;
    }
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    g_page = PAGE_ZAPRET;
    layout(hwnd);
}

static void hosts_save_start(HWND hwnd, int leave_after)
{
    wchar_t  *text;
    long_job *j;

    if (g_busy) return;
    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    j    = job_new(work_hosts_save, done_hosts_save);
    if (j) j->text = (char *)malloc(LIST_TEXT_MAX);
    if (!text || !j || !j->text) {
        free(text);
        job_free(j);
        problem(hwnd, L"Не хватило памяти");
        return;
    }

    GetWindowTextW(g_hedit, text, LIST_TEXT_MAX);
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, j->text, LIST_TEXT_MAX, NULL, NULL) == 0) {
        free(text);
        job_free(j);
        problem(hwnd, L"Список слишком велик");
        return;
    }
    free(text);

    j->flag = leave_after;
    job_start(hwnd, L"собираю список сайтов…", j);
}

static void hosts_tidy(HWND hwnd)
{
    wchar_t *cur  = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    wchar_t *done = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    int      removed = 0;

    if (cur && done) {
        GetWindowTextW(g_hedit, cur, LIST_TEXT_MAX);
        if ((g_hosts_mode == HOSTS_ZAPRET ? zap_tidy_bridge(cur, done, LIST_TEXT_MAX, &removed)
                                          : tidy_bridge(cur, done, LIST_TEXT_MAX, &removed))
            && removed) {
            SetWindowTextW(g_hedit, done);
            SendMessageW(g_hedit, EM_SETMODIFY, TRUE, 0);
        }
    }
    free(cur);
    free(done);
    (void)hwnd;
}

/* Leaving with unsaved edits asks first; losing a pasted list silently
   would be the worst outcome of this page. */
static void hosts_back(HWND hwnd)
{
    if (SendMessageW(g_hedit, EM_GETMODIFY, 0, 0)) {
        int answer = MessageBoxW(hwnd, L"Сохранить изменения в списке?",
                                 L"Utgard", MB_ICONQUESTION | MB_YESNOCANCEL);
        if (answer == IDCANCEL) return;
        if (answer == IDYES) {
            if (g_hosts_mode == HOSTS_ZAPRET) hosts_save_zapret(hwnd);
            else hosts_save_start(hwnd, 1); /* leaves the page when it succeeds */
            return;
        }
    }
    g_page = (g_hosts_mode == HOSTS_ZAPRET) ? PAGE_ZAPRET : PAGE_UTGARD;
    layout(hwnd);
}

static void act_edit_hosts(HWND hwnd)
{
    hosts_open(hwnd, HOSTS_VPN);
}

/* One config file per application, as in the original client. There is no
   editor for them yet, so the button opens the folder they live in. */
static void act_edit_apps(HWND hwnd)
{
    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}

/* ---- first run: fetch sing-box -------------------------------------- */

typedef struct { HWND hwnd; wchar_t msg[SB_MSG_MAX]; int ok; } install_job;

static DWORD WINAPI install_thread(LPVOID param)
{
    install_job *job = (install_job *)param;

    job->ok = singbox_install(job->msg, SB_MSG_MAX);
    PostMessageW(job->hwnd, WM_APP_INSTALL, 0, (LPARAM)job);
    return 0;
}

/* Asks first. The download is 21 MB from GitHub, and a client that reaches out
   on its own the first time it starts is not something to do silently. */
static void offer_install(HWND hwnd)
{
    wchar_t      question[900];
    install_job *job;
    HANDLE       th;

    if (g_installing) return;

    /* Present but not the pinned release - an older version left over, or a
       modified file. Launching is refused either way, so offer the fix here
       instead of leaving only an error at the moment of switching on. */
    if (singbox_present()) {
        if (singbox_verified()) return;
        if (singbox_running()) {
            problem(hwnd, L"Установленный sing-box не совпадает с нужной версией. "
                          L"Выключите VPN, и программа предложит скачать правильную.");
            return;
        }
        StringCchPrintfW(question, 900,
            L"Установленный sing-box не совпадает с версией %s — это старая "
            L"версия или изменённый файл. Запускать его программа не будет.\n\n"
            L"Скачать правильную версию сейчас?", singbox_version());
        if (MessageBoxW(hwnd, question, L"sing-box",
                        MB_ICONWARNING | MB_YESNO) != IDYES)
            return;
        goto start_install;
    }

    StringCchPrintfW(question, 900,
        L"Не найден sing-box — без него VPN работать не может.\n\n"
        L"Скачать его сейчас?\n\n"
        L"Если не хотите скачивать автоматически — загрузите sing-box %s "
        L"для Windows x64 и положите его файлы в папку sing-box рядом с "
        L"программой. На других версиях работоспособность не гарантируется.",
        singbox_version());

    if (MessageBoxW(hwnd, question, L"Первый запуск",
                    MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;

start_install:
    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd = hwnd;

    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return; }
    CloseHandle(th);

    g_installing = 1;
    layout(hwnd);
}

/* Word for word from the zapret project README, so the explanation matches
   what its author actually promises. */
static const wchar_t TIP_GAME[] =
    L"Game Filter — переключение режима обхода для игр (и других сервисов, "
    L"использующих UDP и TCP на портах выше 1023). После переключения "
    L"требуется перезапуск стратегии.";
static const wchar_t TIP_IPSET[] =
    L"IPSet Filter — переключение режима обхода сервисов из ipset-all.txt. "
    L"Полезно при тестировании, если не работает ресурс, который без zapret "
    L"работает.\r\n"
    L"none — никакие айпи не попадают под проверку\r\n"
    L"loaded — айпи проверяется на вхождение в список\r\n"
    L"any — любой айпи попадает под фильтр";
static const wchar_t TIP_IPUPD[] =
    L"Update IPSet List — обновление списка ipset-all.txt актуальным из "
    L"репозитория.";
static const wchar_t TIP_MTU[] =
    L"Максимальный размер пакета внутри туннеля. Если часть сайтов не "
    L"открывается или загрузка зависает на середине — уменьшите значение, "
    L"например до 1380. Стандартно 1430. Применяется при следующем включении VPN.";

static const wchar_t TIP_STACK[] =
    L"Каким способом sing-box разбирает трафик, пришедший в туннель.\r\n"
    L"system — сетевой стек самой Windows: быстрый, стандартно.\r\n"
    L"gvisor — собственный стек sing-box: медленнее, но не зависит от "
    L"особенностей Windows. Пробовать, если с system что-то не работает.\r\n"
    L"mixed — TCP через system, UDP через gvisor.\r\n"
    L"В следующих версиях sing-box выбор уберут и оставят один встроенный стек.";
static const wchar_t TIP_DNS[] =
    L"Через какой сервер узнавать адреса сайтов из списка. Запросы идут "
    L"зашифрованными по HTTP/3, провайдер интернета их не видит. Все три "
    L"сервера работают одинаково; другой стоит выбрать, если текущий "
    L"недоступен или медленный.";

static const wchar_t TIP_HOSTS[] =
    L"Update Hosts File — обновление файла hosts для починки веб-версии "
    L"телеграма и подключения к голосовому чату Discord.";

static int vpn_refresh(void)
{
    int now = singbox_running();
    if (now == g_vpn_on) return 0;
    g_vpn_on = now;
    return 1;
}

/* Every IPv4 address behind the profiles. Hostnames go through the system
   resolver - the same path the generated config gives them, since it routes
   their names to "local". IPv6 is skipped: zapret's exclude list is handled
   as v4 here and mixing families would only add cases. */
/* A copy of the server names, taken on the UI thread. The worker resolves
   from this copy only: reading g_prof itself from another thread while the
   window adds, removes or refreshes profiles is a data race. */

static void snapshot_hosts(host_snapshot *snap)
{
    int i;
    snap->count = 0;
    for (i = 0; i < g_prof.count && i < PROFILES_MAX; i++)
        StringCchCopyA(snap->host[snap->count++], 256, g_prof.items[i].link.server);
}

static int collect_server_ips(const host_snapshot *snap, char ips[][16], int max)
{
    int i, n = 0;

    for (i = 0; i < snap->count && n < max; i++) {
        const char *host = snap->host[i];
        if (!host[0] || strchr(host, ':')) continue;

        if (host[0] >= '0' && host[0] <= '9') {
            int j, dup = 0;
            for (j = 0; j < n; j++) if (strcmp(ips[j], host) == 0) dup = 1;
            if (!dup && strlen(host) < 16) { StringCchCopyA(ips[n], 16, host); n++; }
            continue;
        }
        {
            /* Two profiles often sit on one server, and two names can resolve
               to the same address. Each address is written once. */
            char found[16][16];
            int  got = net_resolve4(host, found, 16);
            int  k;

            for (k = 0; k < got && n < max; k++) {
                int j, dup = 0;
                for (j = 0; j < n; j++) if (strcmp(ips[j], found[k]) == 0) dup = 1;
                if (dup) continue;
                StringCchCopyA(ips[n], 16, found[k]);
                n++;
            }
        }
    }
    return n;
}

typedef struct {
    HWND          hwnd;
    wchar_t       dir[ZAPRET_PATH_MAX];
    host_snapshot hosts;          /* the worker's own copy */
    int           total;
    int           present;
} exc_job;

/* Resolving several hostnames can take seconds, so the check that runs at
   startup does not sit on the UI thread. */
static DWORD WINAPI exc_thread(LPVOID param)
{
    exc_job *job = (exc_job *)param;
    char     ips[64][16];         /* on this thread's stack: two checks at once
                                     must not share one buffer */

    job->total   = collect_server_ips(&job->hosts, ips, 64);
    job->present = zapret_exclude_present(job->dir, ips, job->total);

    PostMessageW(job->hwnd, WM_APP_EXC_DONE, 0, (LPARAM)job);
    return 0;
}

static void exc_check_start(HWND hwnd)
{
    exc_job *job;
    HANDLE   th;

    if (!g_zap.valid || g_prof.count == 0) {
        g_exc_known = g_exc_present = 0;
        return;
    }

    job = (exc_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd = hwnd;
    StringCchCopyW(job->dir, ZAPRET_PATH_MAX, g_zap.path);
    snapshot_hosts(&job->hosts);

    th = CreateThread(NULL, 0, exc_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);
}

/* One button for the whole conflict: resolve every profile server through the
   system resolver - the same path the generated config gives them, since it
   routes their names to "local" - and write the addresses into zapret's user
   exclude list. */
static void work_zap_fix(long_job *j)
{
    j->n1 = collect_server_ips(&j->hosts, j->ips, 64);
    if (j->n1 == 0) {
        StringCchCopyW(j->msg, SB_MSG_MAX,
                       L"Не удалось определить адреса серверов. Проверьте подключение к сети.");
        return;
    }
    if (!zapret_exclude_patch(j->dir, (const char (*)[16])j->ips, j->n1,
                              NULL, NULL, j->msg, SB_MSG_MAX))
        return;
    j->n2 = zapret_exclude_present(j->dir, (const char (*)[16])j->ips, j->n1);
    j->ok = 1;
}

static void done_zap_fix(HWND hwnd, long_job *j)
{
    if (!j->ok) {
        problem(hwnd, j->msg[0] ? j->msg : L"Не удалось изменить список исключений");
        return;
    }
    g_exc_known   = j->n1;
    g_exc_present = j->n2;
    /* winws rereads the list on its own, but keeps the verdict of a connection
       it has already seen: the tunnel must redial to be left alone. */
    if (g_vpn_on)
        MessageBoxW(hwnd, L"Адреса серверов добавлены в исключения zapret. "
                          L"Выключите и снова включите VPN, чтобы уже открытое "
                          L"соединение пошло мимо zapret.",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
}

static void act_zapret_fix(HWND hwnd)
{
    long_job *j;

    if (g_busy) return;
    if (!g_zap.valid) { problem(hwnd, L"Сначала укажите папку zapret"); return; }
    if (g_prof.count == 0) { problem(hwnd, L"Нет профилей — нечего исключать"); return; }

    j = job_new(work_zap_fix, done_zap_fix);
    if (j) {
        snapshot_hosts(&j->hosts);
        StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
    }
    job_start(hwnd, L"определяю адреса серверов…", j);
}

static void act_zap_game(HWND hwnd)
{
    /* Disabled -> TCP and UDP -> TCP only -> UDP only -> disabled, the four
       modes service.bat offers. */
    zapret_game_mode m = zapret_game_get(g_zap.path);
    zapret_game_mode next = (m == GAME_OFF) ? GAME_ALL
                          : (m == GAME_ALL) ? GAME_TCP
                          : (m == GAME_TCP) ? GAME_UDP : GAME_OFF;

    if (!zapret_game_set(g_zap.path, next)) {
        problem(hwnd, L"Не удалось изменить режим игрового фильтра");
        return;
    }
    g_zap_dirty = 1;
    layout(hwnd);
}

static void act_zap_ipset(HWND hwnd)
{
    wchar_t err[320] = { 0 };

    if (!zapret_ipset_cycle(g_zap.path, err, 320)) {
        problem(hwnd, err[0] ? err : L"Не удалось переключить режим IPSet");
        return;
    }
    layout(hwnd);
}

static void work_ipset_update(long_job *j)
{
    j->ok = zapret_ipset_update(j->dir, j->msg, SB_MSG_MAX);
}

static void done_ipset_update(HWND hwnd, long_job *j)
{
    if (!j->ok) { problem(hwnd, j->msg[0] ? j->msg : L"Не удалось обновить список"); return; }
}

static void act_zap_ipset_update(HWND hwnd)
{
    long_job *j;
    if (g_busy) return;
    j = job_new(work_ipset_update, done_ipset_update);
    if (j) StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
    job_start(hwnd, L"скачиваю список IPSet…", j);
}

static void work_hosts_check(long_job *j)
{
    j->ok = zapret_hosts_check(j->temp, MAX_PATH * 2, &j->n1, j->msg, SB_MSG_MAX);
}

static void done_hosts_check(HWND hwnd, long_job *j)
{
    const wchar_t *temp = j->temp;

    if (!j->ok) {
        problem(hwnd, j->msg[0] ? j->msg : L"Не удалось проверить файл hosts");
        return;
    }

    if (!j->n1) {
        MessageBoxW(hwnd, L"Файл hosts уже соответствует репозиторию.",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
        DeleteFileW(temp);
        return;
    }

    /* The system hosts file is never written by us, exactly as service.bat
       does it: the downloaded text is opened and the real file is revealed,
       and the copying is left to the user. */
    MessageBoxW(hwnd,
        L"Файл hosts отличается от репозитория.\n\n"
        L"Сейчас откроется скачанный файл и папка с системным hosts — "
        L"скопируйте содержимое вручную. Сам системный файл клиент не меняет.",
        L"Utgard", MB_ICONINFORMATION | MB_OK);

    ShellExecuteW(hwnd, L"open", L"notepad.exe", temp, NULL, SW_SHOWNORMAL);
    {
        wchar_t sysdir[MAX_PATH], arg[MAX_PATH * 2];
        if (GetSystemDirectoryW(sysdir, MAX_PATH) &&
            SUCCEEDED(StringCchPrintfW(arg, MAX_PATH * 2,
                                       L"/select,\"%s\\drivers\\etc\\hosts\"", sysdir)))
            ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    }
}

static void act_zap_hosts(HWND hwnd)
{
    if (g_busy) return;
    job_start(hwnd, L"скачиваю файл hosts…", job_new(work_hosts_check, done_hosts_check));
}

static void apps_reload(void)
{
    int i;

    g_appv_n = apps_scan(g_appv, APPS_MAX);
    /* Rows are rebuilt; the remembered hover would point at a different one. */
    g_app_hover_item = -1;
    g_app_hover_zone = -1;
    SendMessageW(g_alist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_appv_n; i++)
        SendMessageW(g_alist, LB_ADDSTRING, 0, (LPARAM)L"");
}

static void app_row_invalidate(HWND hwnd, int item)
{
    RECT r;
    if (item < 0 || item >= g_appv_n) return;
    if (SendMessageW(hwnd, LB_GETITEMRECT, (WPARAM)item, (LPARAM)&r) != LB_ERR)
        InvalidateRect(hwnd, &r, FALSE);
}

static LRESULT CALLBACK alist_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEMOVE) {
        int     x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
        int     item = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        int     zone = -1;
        RECT    rc;

        GetClientRect(hwnd, &rc);
        if (item >= 0 && item < g_appv_n) zone = app_zone_at(x, rc.right);
        else                              item = -1;

        /* Repaint only the rows that changed, not the whole list. */
        if (item != g_app_hover_item || zone != g_app_hover_zone) {
            int old = g_app_hover_item;
            g_app_hover_item = item;
            g_app_hover_zone = zone;
            app_row_invalidate(hwnd, old);
            if (item != old) app_row_invalidate(hwnd, item);
        }

        {
            TRACKMOUSEEVENT tme;
            tme.cbSize      = sizeof tme;
            tme.dwFlags     = TME_LEAVE;
            tme.hwndTrack   = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
    } else if (msg == WM_MOUSELEAVE) {
        int old = g_app_hover_item;
        g_app_hover_item = -1;
        g_app_hover_zone = -1;
        app_row_invalidate(hwnd, old);
    } else if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT &&
               g_app_hover_item >= 0) {
        /* Everything in a row is clickable: the name opens the editor. */
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }

    if (msg == WM_LBUTTONDOWN) {
        LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0,
                                   MAKELPARAM(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        int  i = (int)LOWORD(hit);
        RECT rc;

        GetClientRect(hwnd, &rc);
        if (!HIWORD(hit) && i >= 0 && i < g_appv_n) {
            int zone = app_zone_at(GET_X_LPARAM(lp), rc.right);

            if (zone == 0) {
                app_entry e = g_appv[i];
                ed_open_existing(GetParent(hwnd), &e);
                return 0;
            }
            if (zone == 1) {
                if (!apps_set_enabled(&g_appv[i], !g_appv[i].enabled))
                    MessageBoxW(hwnd, L"Не удалось переместить файл списка",
                                L"Utgard", MB_ICONWARNING | MB_OK);
                apps_reload();
                InvalidateRect(hwnd, NULL, TRUE);
                InvalidateRect(GetParent(hwnd), NULL, TRUE);
                return 0;
            }
            if (zone == 2) {
                wchar_t q[320];
                StringCchPrintfW(q, 320,
                    L"Удалить список «%s»?\n\n"
                    L"Это сотрёт сохранённые настройки обхода для этих "
                    L"приложений. Отменить будет нельзя.", g_appv[i].name);
                if (MessageBoxW(hwnd, q, L"Удаление списка",
                                MB_ICONWARNING | MB_YESNO) == IDYES) {
                    if (!apps_delete(&g_appv[i]))
                        MessageBoxW(hwnd, L"Не удалось удалить файл",
                                    L"Utgard", MB_ICONWARNING | MB_OK);
                    apps_reload();
                    InvalidateRect(hwnd, NULL, TRUE);
                    InvalidateRect(GetParent(hwnd), NULL, TRUE);
                }
                return 0;
            }
        }
    }
    return CallWindowProcW(g_alist_prev, hwnd, msg, wp, lp);
}

static int pk_is_checked(const wchar_t *path)
{
    int i;
    if (!path[0]) return 0;
    for (i = 0; i < g_pk_checked_n; i++)
        if (_wcsicmp(g_pk_checked[i], path) == 0) return 1;
    return 0;
}

static void pk_toggle(const wchar_t *path)
{
    int i;

    if (!path[0]) return;                       /* unreadable: cannot be picked */
    for (i = 0; i < g_pk_checked_n; i++) {
        if (_wcsicmp(g_pk_checked[i], path) == 0) {
            memmove(&g_pk_checked[i], &g_pk_checked[i + 1],
                    (size_t)(g_pk_checked_n - i - 1) * sizeof g_pk_checked[0]);
            g_pk_checked_n--;
            return;
        }
    }
    if (g_pk_checked_n < 64)
        StringCchCopyW(g_pk_checked[g_pk_checked_n++], MAX_PATH, path);
}

/* Re-read the processes and re-apply the filter. The ticks survive because
   they are keyed by path, and the scroll position is put back so a refresh
   every two seconds does not yank the list under the user. */
static void pk_refresh(void)
{
    wchar_t query[128];
    int     i, top;

    GetWindowTextW(g_pk_search, query, 128);
    top = (int)SendMessageW(g_pk_list, LB_GETTOPINDEX, 0, 0);

    g_pk_all_n  = pick_snapshot(g_pk_all, PICK_MAX);
    g_pk_view_n = 0;
    for (i = 0; i < g_pk_all_n; i++)
        if (pick_match(&g_pk_all[i], query)) g_pk_view[g_pk_view_n++] = i;

    SendMessageW(g_pk_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_pk_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_pk_view_n; i++)
        SendMessageW(g_pk_list, LB_ADDSTRING, 0, (LPARAM)L"");
    if (top > 0 && top < g_pk_view_n) SendMessageW(g_pk_list, LB_SETTOPINDEX, (WPARAM)top, 0);
    list_hover_resync(g_pk_list);
    SendMessageW(g_pk_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_pk_list, NULL, TRUE);

    EnableWindow(g_pk_save, g_pk_checked_n > 0);
}

static void pk_open(HWND hwnd)
{
    g_pk_checked_n = 0;
    SetWindowTextW(g_pk_search, L"");
    g_page = PAGE_PICK;
    layout(hwnd);
    pk_refresh();
    SetTimer(hwnd, TIMER_PICK, 2000, NULL);
    SetFocus(g_pk_search);
}

static void pk_close(HWND hwnd)
{
    KillTimer(hwnd, TIMER_PICK);
    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}

static void pk_save(HWND hwnd)
{
    static wchar_t names[64][MAX_PATH], paths[64][MAX_PATH];
    wchar_t        list_name[APPS_NAME_MAX * 2], err[256];
    int            i, n = 0;

    if (g_pk_checked_n == 0) return;

    for (;;) {
        if (!ask_string(hwnd, L"Сохранить приложение",
                        L"Имя списка: латиница, цифры, дефис и подчёркивание. "
                        L"Под этим именем набор появится в менеджере.",
                        NULL, list_name, APPS_NAME_MAX * 2))
            return;
        if (!apps_name_ok(list_name)) {
            problem(hwnd, L"Имя — только латиница, цифры, дефис и подчёркивание.");
            continue;
        }
        if (apps_exists(list_name)) {
            problem(hwnd, L"Список с таким именем уже есть. Выберите другое имя.");
            continue;
        }
        break;
    }

    /* Both the executable name and its full path, as in the reference config:
       the path pins it on this machine, the name keeps it working elsewhere. */
    for (i = 0; i < g_pk_checked_n && n < 64; i++) {
        const wchar_t *slash = wcsrchr(g_pk_checked[i], L'\\');
        StringCchCopyW(names[n], MAX_PATH, slash ? slash + 1 : g_pk_checked[i]);
        StringCchCopyW(paths[n], MAX_PATH, g_pk_checked[i]);
        n++;
    }

    if (!apps_write(list_name, names, n, paths, n, 0, err, 256)) {
        problem(hwnd, err[0] ? err : L"Не удалось сохранить список");
        return;
    }
    pk_close(hwnd);
}

/* ---- manual editor ---------------------------------------------------- */

static void ed_clear(void)
{
    int i;
    for (i = 0; i < ED_ROWS; i++) {
        SetWindowTextW(g_ed_name[i], L"");
        SetWindowTextW(g_ed_path[i], L"");
    }
    g_ed_ncount = 1;
    g_ed_pcount = 1;
}

static void ed_open_new(HWND hwnd)
{
    ed_clear();
    g_ed_orig[0]  = L'\0';
    g_ed_enabled  = 0;
    g_page = PAGE_EDIT;
    layout(hwnd);
    SetFocus(g_ed_name[0]);
}

static int ed_open_existing(HWND hwnd, const app_entry *e)
{
    static wchar_t names[ED_ROWS][MAX_PATH], paths[ED_ROWS][MAX_PATH];
    int nn = 0, np = 0, i;

    if (!apps_read(e, names, &nn, paths, &np, ED_ROWS)) {
        problem(hwnd, L"Этот список содержит правила, которые редактор не умеет "
                      L"показывать, — например, регулярные выражения, исключения "
                      L"или имя и путь в одном правиле. Сохранение из формы стёрло "
                      L"бы их, поэтому его можно только включать, выключать и удалять.");
        return 0;
    }

    ed_clear();
    for (i = 0; i < nn; i++) SetWindowTextW(g_ed_name[i], names[i]);
    for (i = 0; i < np; i++) SetWindowTextW(g_ed_path[i], paths[i]);
    g_ed_ncount = nn > 0 ? nn : 1;
    g_ed_pcount = np > 0 ? np : 1;

    StringCchCopyW(g_ed_orig, APPS_NAME_MAX, e->name);
    g_ed_enabled = e->enabled;
    g_page = PAGE_EDIT;
    layout(hwnd);
    return 1;
}

/* Rows below the removed one move up; its text is gone, the others stay. */
static void ed_remove(HWND *rows, int *count, int at)
{
    wchar_t buf[MAX_PATH];
    int     i;

    if (at <= 0 || at >= *count) return;
    for (i = at; i < *count - 1; i++) {
        GetWindowTextW(rows[i + 1], buf, MAX_PATH);
        SetWindowTextW(rows[i], buf);
    }
    SetWindowTextW(rows[*count - 1], L"");
    (*count)--;
}

static void ed_add(HWND hwnd, HWND *rows, int *count)
{
    if (*count >= ED_ROWS) {
        problem(hwnd, L"В одном разделе помещается не больше шести строк. "
                      L"Если нужно больше — сохраните второй список.");
        return;
    }
    SetWindowTextW(rows[*count], L"");
    (*count)++;
    layout(hwnd);
    SetFocus(rows[*count - 1]);
}

static void ed_save(HWND hwnd)
{
    static wchar_t names[ED_ROWS][MAX_PATH], paths[ED_ROWS][MAX_PATH];
    wchar_t        list_name[APPS_NAME_MAX * 2], err[256];
    int            nn = 0, np = 0, i;

    for (i = 0; i < g_ed_ncount; i++) {
        GetWindowTextW(g_ed_name[i], names[nn], MAX_PATH);
        if (names[nn][0]) nn++;
    }
    for (i = 0; i < g_ed_pcount; i++) {
        GetWindowTextW(g_ed_path[i], paths[np], MAX_PATH);
        if (paths[np][0]) np++;
    }
    if (nn == 0 && np == 0) {
        problem(hwnd, L"Заполните хотя бы одно поле");
        return;
    }

    for (;;) {
        if (!ask_string(hwnd, L"Сохранить список",
                        L"Имя списка: латиница, цифры, дефис и подчёркивание.",
                        g_ed_orig[0] ? g_ed_orig : NULL,
                        list_name, APPS_NAME_MAX * 2))
            return;
        if (!apps_name_ok(list_name)) {
            problem(hwnd, L"Имя — только латиница, цифры, дефис и подчёркивание.");
            continue;
        }
        /* Keeping its own name is fine; taking another list's name is not. */
        if (_wcsicmp(list_name, g_ed_orig) != 0 && apps_exists(list_name)) {
            problem(hwnd, L"Список с таким именем уже есть. Выберите другое имя.");
            continue;
        }
        break;
    }

    if (!apps_write(list_name, names, nn, paths, np, g_ed_enabled, err, 256)) {
        problem(hwnd, err[0] ? err : L"Не удалось сохранить список");
        return;
    }

    /* Renamed: the new file is written, so the old one can go. Written first
       and deleted second, a failure in between leaves a copy, not nothing. */
    if (g_ed_orig[0] && _wcsicmp(list_name, g_ed_orig) != 0) {
        app_entry old;
        StringCchCopyW(old.name, APPS_NAME_MAX, g_ed_orig);
        old.enabled = g_ed_enabled;
        apps_delete(&old);
    }

    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}

static void set_open(HWND hwnd)
{
    wchar_t buf[16];

    settings_load(&g_set);
    StringCchPrintfW(buf, 16, L"%d", g_set.mtu);
    SetWindowTextW(g_set_mtu, buf);
    SendMessageW(g_set_log, CB_SETCURSEL, (WPARAM)g_set.log_level, 0);
    SendMessageW(g_set_stack, CB_SETCURSEL, (WPARAM)g_set.stack, 0);
    SendMessageW(g_set_dns, CB_SETCURSEL, (WPARAM)g_set.dns, 0);
    SendMessageW(g_set_sub, CB_SETCURSEL, (WPARAM)g_set.sub_interval, 0);
    SetPropW(g_set_tray, L"utgard.checked", (HANDLE)(INT_PTR)(g_set.tray_on_close ? 1 : 0));
    InvalidateRect(g_set_tray, NULL, FALSE);
    SetPropW(g_set_auto, L"utgard.checked", (HANDLE)(INT_PTR)autostart_get());
    InvalidateRect(g_set_auto, NULL, FALSE);
    SetPropW(g_set_upd, L"utgard.checked", (HANDLE)(INT_PTR)(g_set.update_check ? 1 : 0));
    InvalidateRect(g_set_upd, NULL, FALSE);
    g_page = PAGE_SETTINGS;
    layout(hwnd);
}

static void set_save(HWND hwnd)
{
    wchar_t buf[16], msg[160];
    long    mtu;
    LRESULT lvl;

    GetWindowTextW(g_set_mtu, buf, 16);
    mtu = wcstol(buf, NULL, 10);
    if (mtu < SETTINGS_MTU_MIN || mtu > SETTINGS_MTU_MAX) {
        StringCchPrintfW(msg, 160, L"MTU должно быть от %d до %d.",
                         SETTINGS_MTU_MIN, SETTINGS_MTU_MAX);
        problem(hwnd, msg);
        SetFocus(g_set_mtu);
        return;
    }

    lvl = SendMessageW(g_set_log, CB_GETCURSEL, 0, 0);
    g_set.mtu       = (int)mtu;
    g_set.log_level = (lvl == CB_ERR) ? SETTINGS_LOG_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_stack, CB_GETCURSEL, 0, 0);
    g_set.stack = (lvl == CB_ERR) ? SETTINGS_STACK_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_dns, CB_GETCURSEL, 0, 0);
    g_set.dns = (lvl == CB_ERR) ? SETTINGS_DNS_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_sub, CB_GETCURSEL, 0, 0);
    g_set.sub_interval = (lvl == CB_ERR) ? SETTINGS_SUB_DEFAULT : (int)lvl;
    g_set.tray_on_close = GetPropW(g_set_tray, L"utgard.checked") != NULL;
    g_set.update_check  = GetPropW(g_set_upd, L"utgard.checked") != NULL;

    if (!settings_save(&g_set)) {
        problem(hwnd, L"Не удалось сохранить настройки");
        return;
    }

    /* The scheduler holds this one; touch it only when the box changed. */
    {
        int want = GetPropW(g_set_auto, L"utgard.checked") != NULL;
        if (want != autostart_get() && !autostart_set(want, msg, 160)) {
            problem(hwnd, msg);
            return;
        }
    }
    g_page = PAGE_UTGARD;
    layout(hwnd);
}

/* ---- update check ------------------------------------------------------ */

typedef struct {
    HWND    hwnd;
    int     manual;     /* the button, not the start-up check: report everything */
    int     ok;
    char    tag[32];
    wchar_t err[256];
} upd_job;

static int  g_upd_busy;
static int  g_upd_pending;     /* found while hidden in the tray: ask on show */
static char g_upd_tag[32];

static DWORD WINAPI upd_thread(LPVOID param)
{
    upd_job *j = (upd_job *)param;
    wchar_t  loc[1024];
    char     loc8[1024];

    if (net_redirect(UTGARD_RELEASES_URL, loc, 1024, j->err, 256)) {
        if (WideCharToMultiByte(CP_UTF8, 0, loc, -1, loc8, sizeof loc8, NULL, NULL) &&
            update_tag_from_location(loc8, j->tag, sizeof j->tag))
            j->ok = 1;
        else
            StringCchCopyW(j->err, 256, L"GitHub ответил неожиданным адресом");
    }
    PostMessageW(j->hwnd, WM_APP_UPD_DONE, 0, (LPARAM)j);
    return 0;
}

static void upd_start(HWND hwnd, int manual)
{
    upd_job *j;
    HANDLE   th;

    if (g_upd_busy) return;
    j = (upd_job *)calloc(1, sizeof *j);
    if (!j) return;
    j->hwnd   = hwnd;
    j->manual = manual;
    th = CreateThread(NULL, 0, upd_thread, j, 0, NULL);
    if (!th) { free(j); return; }
    CloseHandle(th);
    g_upd_busy = 1;
    EnableWindow(g_set_upd_now, FALSE);
}

static void upd_prompt(HWND hwnd)
{
    wchar_t text[256], tag[32];

    /* The tag passed update_tag_from_location: ASCII only. */
    MultiByteToWideChar(CP_UTF8, 0, g_upd_tag, -1, tag, 32);
    StringCchPrintfW(text, 256, L"Вышла новая версия Utgard: %s (у вас %s).\r\n\r\n"
                                L"Открыть страницу загрузки?",
                     tag, UTGARD_VERSION_W);
    if (MessageBoxW(hwnd, text, L"Utgard", MB_ICONINFORMATION | MB_YESNO) != IDYES) return;
    if (!shell_open_unelevated(UTGARD_RELEASES_URL))
        MessageBoxW(hwnd, L"Не удалось открыть браузер. Скачайте новую версию здесь "
                          L"(Ctrl+C копирует этот текст):\r\n\r\n" UTGARD_RELEASES_URL,
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
}

static void upd_done(HWND hwnd, upd_job *j)
{
    g_upd_busy = 0;
    EnableWindow(g_set_upd_now, TRUE);

    if (!j->ok) {
        if (j->manual) problem(hwnd, j->err[0] ? j->err : L"Не удалось проверить обновления");
    } else if (update_is_newer(j->tag, UTGARD_VERSION)) {
        StringCchCopyA(g_upd_tag, sizeof g_upd_tag, j->tag);
        if (j->manual || IsWindowVisible(hwnd)) upd_prompt(hwnd);
        else g_upd_pending = 1;
    } else if (j->manual) {
        MessageBoxW(hwnd, L"У вас последняя версия (" UTGARD_VERSION_W L").",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
    }
    free(j);
}

static void window_show(HWND hwnd)
{
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    if (g_upd_pending) { g_upd_pending = 0; upd_prompt(hwnd); }
}

static void act_vpn(HWND hwnd);

static void tray_menu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    UINT  cmd;

    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Открыть Utgard");
    AppendMenuW(menu, MF_STRING | (g_prof.count && !g_busy ? 0 : MF_GRAYED), ID_TRAY_VPN,
                g_vpn_on ? L"Выключить VPN" : L"Включить VPN");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");
    SetMenuDefaultItem(menu, ID_TRAY_OPEN, FALSE);

    /* Without becoming foreground first, the menu would not close when the
       user clicks elsewhere - a documented quirk of notification menus. */
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                               pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == ID_TRAY_OPEN) window_show(hwnd);
    else if (cmd == ID_TRAY_VPN) act_vpn(hwnd);       /* the result updates the icon */
    else if (cmd == ID_TRAY_EXIT) {
        if (g_busy) {
            window_show(hwnd);
            problem(hwnd, L"Дождитесь окончания операции — она займёт несколько секунд.");
        } else {
            DestroyWindow(hwnd);
        }
    }
}

/* Everything the VPN worker needs, copied on the UI thread: paths, the
   enabled application files, the settings, and the profiles themselves. The
   profiles carry credentials, so the job wipes this block before freeing it. */
typedef struct {
    char          base[1024], out[1024];
    char          overlay[32][MAX_PATH * 2];
    const char   *overlay_ptr[32];
    profile_store store;
    genconf_input in;
} vpn_inputs;

static void work_vpn_on(long_job *j)
{
    vpn_inputs *v = (vpn_inputs *)j->extra;
    char        err[256] = { 0 };

    if (!genconf_build(&v->in, err, sizeof err)) {
        to_wide(err, j->msg, SB_MSG_MAX);
        return;
    }
    if (!singbox_check(j->msg, SB_MSG_MAX)) return;
    j->ok = singbox_start(j->msg, SB_MSG_MAX);
}

static void work_vpn_off(long_job *j)
{
    j->ok = singbox_stop(j->msg, SB_MSG_MAX);
}

static void done_vpn(HWND hwnd, long_job *j)
{
    if (!j->ok)          problem(hwnd, j->msg[0] ? j->msg : L"Не удалось переключить VPN");
    else if (j->msg[0])  problem(hwnd, j->msg);     /* stopped, but had to be killed */
    vpn_refresh();
    tray_set_state(g_vpn_on);
}

static void act_vpn(HWND hwnd)
{
    long_job   *j;
    vpn_inputs *v;

    if (g_busy) return;

    if (g_vpn_on) {
        job_start(hwnd, L"выключаю VPN…", job_new(work_vpn_off, done_vpn));
        return;
    }

    if (g_prof.count == 0 || g_prof.active < 0) {
        problem(hwnd, L"Сначала добавьте профиль");
        return;
    }

    /* sing-box first: without it even the site list cannot be built, so
       pointing at the list would only lead to a second error. */
    if (!singbox_present()) {
        offer_install(hwnd);
        return;
    }

    {
        wchar_t root[MAX_PATH * 2], srs[MAX_PATH * 2];
        if (singbox_root(root, MAX_PATH * 2) &&
            SUCCEEDED(StringCchPrintfW(srs, MAX_PATH * 2, L"%slist\\general.srs", root)) &&
            GetFileAttributesW(srs) == INVALID_FILE_ATTRIBUTES) {
            problem(hwnd, L"Список сайтов ещё не собран. Откройте «Список сайтов…», "
                          L"впишите адреса и нажмите «Сохранить».");
            return;
        }
    }

    j = job_new(work_vpn_on, done_vpn);
    v = (vpn_inputs *)calloc(1, sizeof *v);
    if (!j || !v) {
        free(v);
        job_free(j);
        problem(hwnd, L"Не хватило памяти");
        return;
    }
    j->extra      = v;
    j->extra_size = sizeof *v;

    if (!singbox_base_utf8(v->base, sizeof v->base) ||
        !singbox_generated_utf8(v->out, sizeof v->out)) {
        job_free(j);
        problem(hwnd, L"Не удалось определить пути к конфигурации");
        return;
    }

    /* Enabled application lists, read now: the user may have toggled one. */
    {
        wchar_t          mask[MAX_PATH * 2], full[MAX_PATH * 2], dir[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        HANDLE           h;
        int              n = 0;

        if (root_file(L"list\\applications\\active", dir, MAX_PATH * 2) &&
            root_file(L"list\\applications\\active\\*.json", mask, MAX_PATH * 2)) {
            h = FindFirstFileW(mask, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (n >= 32) break;
                    if (FAILED(StringCchPrintfW(full, MAX_PATH * 2, L"%s\\%s",
                                                dir, fd.cFileName))) continue;
                    if (WideCharToMultiByte(CP_UTF8, 0, full, -1, v->overlay[n],
                                            MAX_PATH * 2, NULL, NULL) == 0) continue;
                    v->overlay_ptr[n] = v->overlay[n];
                    n++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        v->in.overlays      = n ? v->overlay_ptr : NULL;
        v->in.overlay_count = n;
    }

    v->store = g_prof;
    settings_load(&g_set);

    v->in.base_path     = v->base;
    v->in.out_path      = v->out;
    v->in.rule_set_path = "list/general.srs";
    v->in.mtu           = g_set.mtu;
    v->in.log_level     = settings_log_levels[g_set.log_level];
    v->in.stack         = settings_stacks[g_set.stack];
    v->in.dns_host      = settings_dns[g_set.dns].host;
    v->in.store         = &v->store;

    job_start(hwnd, L"проверяю конфигурацию и запускаю sing-box…", j);
}

static void act_profile_add(HWND hwnd)
{
    wchar_t      wide[2048];
    char         utf8[2048];
    char         err[256];
    link_profile parsed;
    wchar_t      msg[320];

    if (!ask_string(hwnd, L"Добавить профиль",
                    L"Вставьте ссылку vless://, hysteria2://, ss:// или trojan://",
                    NULL, wide, 2048))
        return;

    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof utf8,
                            NULL, NULL) == 0) {
        problem(hwnd, L"Ссылка слишком длинная");
        return;
    }

    if (!link_parse(utf8, &parsed, err, sizeof err)) {
        to_wide(err, msg, 320);
        problem(hwnd, msg);
        return;
    }

    if (g_prof.count >= PROFILES_MAX) {
        problem(hwnd, L"Больше профилей не помещается");
        return;
    }
    if (profile_duplicate(&parsed) >= 0) {
        problem(hwnd, L"Такой профиль уже есть в списке");
        return;
    }

    memset(&g_prof.items[g_prof.count], 0, sizeof g_prof.items[0]);
    g_prof.items[g_prof.count].link = parsed;
    if (g_prof.active < 0) g_prof.active = g_prof.count;
    g_prof.count++;

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль добавлен, но сохранить его не удалось");

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

static void act_profile_delete(HWND hwnd)
{
    int i = profile_selected();

    if (i < 0) return;

    memmove(&g_prof.items[i], &g_prof.items[i + 1],
            (size_t)(g_prof.count - i - 1) * sizeof g_prof.items[0]);
    g_prof.count--;

    if (g_prof.active == i)      g_prof.active = g_prof.count ? 0 : -1;
    else if (g_prof.active > i)  g_prof.active--;

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль удалён, но сохранить изменение не удалось");

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

static void act_profile_activate(HWND hwnd)
{
    int i = profile_selected();

    if (i < 0 || i == g_prof.active) return;
    g_prof.active = i;
    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль выбран, но сохранить выбор не удалось");
    InvalidateRect(g_plist, NULL, TRUE);
    layout(hwnd);
}

/* Modern folder picker (IFileDialog with FOS_PICKFOLDERS). */
static int pick_folder(HWND owner, wchar_t *out, size_t cap)
{
    IFileDialog *fd   = NULL;
    IShellItem  *item = NULL;
    PWSTR        wide = NULL;
    DWORD        opts = 0;
    int          ok   = 0;

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&fd)))
        return 0;

    if (SUCCEEDED(IFileDialog_GetOptions(fd, &opts)))
        IFileDialog_SetOptions(fd, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                          FOS_PATHMUSTEXIST);
    IFileDialog_SetTitle(fd, L"Папка zapret");

    if (SUCCEEDED(IFileDialog_Show(fd, owner)) &&
        SUCCEEDED(IFileDialog_GetResult(fd, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide))) {
            ok = SUCCEEDED(StringCchCopyW(out, cap, wide));
            CoTaskMemFree(wide);
        }
        IShellItem_Release(item);
    }

    IFileDialog_Release(fd);
    return ok;
}

static void on_pick_path(HWND hwnd)
{
    wchar_t     chosen[ZAPRET_PATH_MAX];
    zapret_info scanned;

    if (!pick_folder(hwnd, chosen, ZAPRET_PATH_MAX)) return;

    if (!zapret_scan(chosen, &scanned)) {
        /* A bad pick must not destroy a folder that already works. */
        if (g_zap.valid) {
            MessageBoxW(hwnd, scanned.problem, L"Папка не подходит",
                        MB_ICONWARNING | MB_OK);
            return;
        }
        g_zap = scanned;
    } else {
        g_zap = scanned;
        zapret_path_save(g_zap.path);
    }

    strategies_reload();
    status_refresh();
    exc_check_start(hwnd);
    layout(hwnd);
}

static void apply_dark_caption(HWND hwnd)
{
    BOOL     dark    = TRUE;
    COLORREF caption = CLR_BG;
    /* Documented for Windows 11 build 22000+; older builds return an error
       and keep the system caption. Both failures are harmless. */
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_CAPTION_COLOR, &caption, sizeof caption);
}

static void set_fonts(void)
{
    SendMessageW(g_tab_utgard, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_tab_zapret, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_open,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_ping_now,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_mtu,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_log,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_stack,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_dns,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_tray,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_auto,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_upd,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_sub,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_back,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_save,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_toggle,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pick_path,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_list,       WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_start,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_stop,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_restart,WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_fix,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_game,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_ipset,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_ipupd,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_hosts,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_list,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_plist,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_add,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_del,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_sub,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_btn_hosts,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_alist,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hedit,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_search,  WM_SETFONT, (WPARAM)g_font, TRUE);
    ask_edit_center(g_pk_search);
    ask_edit_center(g_set_mtu);
    {
        int i;
        for (i = 0; i < ED_ROWS; i++) {
            SendMessageW(g_ed_name[i],   WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_path[i],   WM_SETFONT, (WPARAM)g_font, TRUE);
            ask_edit_center(g_ed_name[i]);
            ask_edit_center(g_ed_path[i]);
            SendMessageW(g_ed_nplus[i],  WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_nminus[i], WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_pplus[i],  WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_pminus[i], WM_SETFONT, (WPARAM)g_font, TRUE);
        }
    }
    SendMessageW(g_ed_back,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_ed_save,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_list,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_back,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_save,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_back,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_tidy,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_save,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_back,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_pick,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_manual, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_btn_apps,   WM_SETFONT, (WPARAM)g_font, TRUE);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_dpi = (int)GetDpiForWindow(hwnd);
        fonts_create();
        apply_dark_caption(hwnd);
        g_tab_utgard = make_button(hwnd, L"Utgard", ID_TAB_UTGARD, BK_TAB);
        g_tab_zapret = make_button(hwnd, L"zapret", ID_TAB_ZAPRET, BK_TAB);
        g_set_open   = make_button(hwnd, L"Настройки", ID_SET_OPEN, BK_SECONDARY);
        g_ping_now   = make_button(hwnd, L"ping до сервера", ID_PING_NOW, BK_SECONDARY);
        {
            HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            int i;

            g_set_mtu = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_CENTER, 0, 0, 0, 0, hwnd,
                (HMENU)(INT_PTR)ID_SET_MTU, inst, NULL);
            SendMessageW(g_set_mtu, EM_SETLIMITTEXT, 4, 0);

            /* A drop-down list, not an editable combo: only real levels go in.
               DarkMode_CFD is the theme that darkens combo boxes. */
            g_set_log = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_LOG, inst, NULL);
            SetWindowTheme(g_set_log, L"DarkMode_CFD", NULL);
            {
                static const wchar_t *labels[] = {
                    L"trace — всё подряд",
                    L"debug — подробно, для разбора проблем",
                    L"info — обычная работа",
                    L"warn — предупреждения и ошибки",
                    L"error — только ошибки",
                    L"fatal — только критические ошибки",
                    L"panic — почти ничего"
                };
                for (i = 0; i < 7; i++)
                    SendMessageW(g_set_log, CB_ADDSTRING, 0, (LPARAM)labels[i]);
            }
            g_set_stack = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_STACK, inst, NULL);
            SetWindowTheme(g_set_stack, L"DarkMode_CFD", NULL);
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"system — стек Windows");
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"gvisor — стек sing-box");
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"mixed — TCP system, UDP gvisor");

            g_set_dns = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_DNS, inst, NULL);
            SetWindowTheme(g_set_dns, L"DarkMode_CFD", NULL);
            for (i = 0; i < settings_dns_count; i++) {
                wchar_t line[96];
                StringCchPrintfW(line, 96, L"%s — HTTP/3", settings_dns[i].label);
                SendMessageW(g_set_dns, CB_ADDSTRING, 0, (LPARAM)line);
            }

            g_set_sub = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_SUB, inst, NULL);
            SetWindowTheme(g_set_sub, L"DarkMode_CFD", NULL);
            for (i = 0; i < settings_sub_count; i++) {
                wchar_t line[64];
                int     h = settings_sub_hours[i];
                /* Russian plural: 3 часа, 6 и 12 часов. */
                StringCchPrintfW(line, 64, L"обновлять каждые %d %s", h,
                                 (h % 10 >= 2 && h % 10 <= 4 && (h % 100 < 12 || h % 100 > 14))
                                     ? L"часа" : L"часов");
                SendMessageW(g_set_sub, CB_ADDSTRING, 0, (LPARAM)line);
            }

            g_set_tray = make_button(hwnd, L"Сворачивать в трей при закрытии",
                                     ID_SET_TRAY, BK_CHECK);
            g_set_auto = make_button(hwnd, L"Запускать вместе с Windows (свёрнутым в трей)",
                                     ID_SET_AUTO, BK_CHECK);
            g_set_upd  = make_button(hwnd, L"Проверять обновления при запуске",
                                     ID_SET_UPD, BK_CHECK);
            g_set_upd_now = make_button(hwnd, L"Проверить обновления", ID_SET_UPD_NOW,
                                        BK_SECONDARY);

            g_set_back = make_button_on(hwnd, L"Назад", ID_SET_BACK, BK_SECONDARY, CLR_FOOTER);
            g_set_save = make_button_on(hwnd, L"Сохранить", ID_SET_SAVE, BK_PRIMARY, CLR_FOOTER);
        }
        g_toggle     = make_button(hwnd, L"Включить", ID_TOGGLE, BK_PRIMARY);
        g_pick_path  = make_button(hwnd, L"Указать папку…", ID_PICK_PATH, BK_SECONDARY);
        g_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                 WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                 LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STRATEGIES,
                                 (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                 NULL);
        SendMessageW(g_list, LB_SETITEMHEIGHT, 0, (LPARAM)S(30));
        /* Dark scrollbar: undocumented since Windows 10 1809, and a no-op
           where the theme is absent. */
        SetWindowTheme(g_list, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_list);
        g_plist = CreateWindowExW(0, L"LISTBOX", NULL,
                                  WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                  LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PROFILES,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        SetWindowTheme(g_plist, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_plist);
        g_prof_add = make_button_on(hwnd, L"Добавить по ссылке…", ID_PROF_ADD,
                                    BK_SECONDARY, CLR_FOOTER);
        g_prof_del = make_button_on(hwnd, L"Удалить", ID_PROF_DEL,
                                    BK_DANGER, CLR_FOOTER);
        g_prof_sub = make_button_on(hwnd, L"Подписка…", ID_PROF_SUB,
                                    BK_SECONDARY, CLR_FOOTER);
        g_alist = CreateWindowExW(0, L"LISTBOX", NULL,
                                  WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                  LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_APPS_LIST,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_alist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        SetWindowTheme(g_alist, L"DarkMode_Explorer", NULL);
        g_alist_prev = (WNDPROC)SetWindowLongPtrW(g_alist, GWLP_WNDPROC,
                                                  (LONG_PTR)alist_proc);
        g_app_back   = make_button_on(hwnd, L"Назад", ID_APPS_BACK,
                                      BK_SECONDARY, CLR_FOOTER);
        g_app_pick   = make_button_on(hwnd, L"Выбрать из запущенных…",
                                      ID_APPS_PICK, BK_PRIMARY, CLR_FOOTER);
        g_app_manual = make_button_on(hwnd, L"Добавить вручную…",
                                      ID_APPS_MANUAL, BK_SECONDARY, CLR_FOOTER);
        g_hedit = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_TABSTOP | WS_VSCROLL |
                                  ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HOSTS_EDIT,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_hedit, EM_SETLIMITTEXT, (WPARAM)(LIST_TEXT_MAX - 1), 0);
        SetWindowTheme(g_hedit, L"DarkMode_Explorer", NULL);
        g_pk_search = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PICK_SEARCH,
                                      (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                      NULL);
        g_pk_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                    WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                    0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PICK_LIST,
                                    (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                    NULL);
        SendMessageW(g_pk_list, LB_SETITEMHEIGHT, 0, (LPARAM)S(40));
        SetWindowTheme(g_pk_list, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_pk_list);
        {
            HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            int i;
            for (i = 0; i < ED_ROWS; i++) {
                g_ed_name[i] = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                    (HMENU)(INT_PTR)(ID_ED_NAME + i), inst, NULL);
                g_ed_path[i] = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                    (HMENU)(INT_PTR)(ID_ED_PATH + i), inst, NULL);
                SendMessageW(g_ed_name[i], EM_SETLIMITTEXT, MAX_PATH - 1, 0);
                SendMessageW(g_ed_path[i], EM_SETLIMITTEXT, MAX_PATH - 1, 0);
                g_ed_nplus[i]  = make_button(hwnd, L"+", ID_ED_NPLUS + i, BK_SECONDARY);
                g_ed_nminus[i] = make_button(hwnd, L"−", ID_ED_NMINUS + i, BK_DANGER);
                g_ed_pplus[i]  = make_button(hwnd, L"+", ID_ED_PPLUS + i, BK_SECONDARY);
                g_ed_pminus[i] = make_button(hwnd, L"−", ID_ED_PMINUS + i, BK_DANGER);
            }
            g_ed_back = make_button_on(hwnd, L"Назад", ID_ED_BACK, BK_SECONDARY, CLR_FOOTER);
            g_ed_save = make_button_on(hwnd, L"Сохранить", ID_ED_SAVE, BK_PRIMARY, CLR_FOOTER);
        }
        g_pk_back = make_button_on(hwnd, L"Назад", ID_PICK_BACK, BK_SECONDARY, CLR_FOOTER);
        g_pk_save = make_button_on(hwnd, L"Сохранить приложение", ID_PICK_SAVE,
                                   BK_PRIMARY, CLR_FOOTER);
        g_h_back = make_button_on(hwnd, L"Назад", ID_HOSTS_BACK, BK_SECONDARY, CLR_FOOTER);
        g_h_tidy = make_button_on(hwnd, L"Убрать дубли", ID_HOSTS_TIDY,
                                  BK_SECONDARY, CLR_FOOTER);
        g_h_save = make_button_on(hwnd, L"Сохранить", ID_HOSTS_SAVE, BK_PRIMARY, CLR_FOOTER);
        g_btn_hosts = make_button(hwnd, L"Список сайтов…", ID_EDIT_HOSTS, BK_SECONDARY);
        g_btn_apps  = make_button(hwnd, L"Приложения…", ID_EDIT_APPS, BK_SECONDARY);
        g_zap_start   = make_button_on(hwnd, L"Запустить", ID_ZAP_START,
                                       BK_PRIMARY, CLR_FOOTER);
        g_zap_stop    = make_button(hwnd, L"Выключить", ID_ZAP_STOP, BK_DANGER);
        g_zap_restart = make_button(hwnd, L"Перезапустить", ID_ZAP_RESTART, BK_SECONDARY);
        g_zap_fix = make_button_on(hwnd, L"Исправить конфликт VPN и zapret",
                                   ID_ZAP_FIX, BK_SECONDARY, CLR_SURFACE);
        g_zap_game  = make_button(hwnd, L"Game filter", ID_ZAP_GAME, BK_SECONDARY);
        g_zap_ipset = make_button(hwnd, L"IPSet filter", ID_ZAP_IPSET, BK_SECONDARY);
        g_zap_ipupd = make_button(hwnd, L"Обновить список IPSet", ID_ZAP_IPUPD, BK_SECONDARY);
        g_zap_hosts = make_button(hwnd, L"Обновить файл hosts", ID_ZAP_HOSTS, BK_SECONDARY);
        g_zap_list  = make_button(hwnd, L"Список хостов zapret…", ID_ZAP_LIST, BK_SECONDARY);

        {
            /* Rect-based tooltips on the parent: the "что это?" labels are
               painted, not controls, so there is no window to attach to. */
            INITCOMMONCONTROLSEX icc;
            TTTOOLINFOW ti;
            int i;
            static const wchar_t *tips[7];

            icc.dwSize = sizeof icc;
            icc.dwICC  = ICC_TAB_CLASSES;
            InitCommonControlsEx(&icc);

            tips[0] = TIP_GAME; tips[1] = TIP_IPSET;
            tips[2] = TIP_IPUPD; tips[3] = TIP_HOSTS; tips[4] = TIP_MTU;
            tips[5] = TIP_STACK; tips[6] = TIP_DNS;

            g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                    0, 0, 0, 0, hwnd, NULL,
                                    (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                    NULL);
            if (g_tip) {
                SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)S(420));
                ZeroMemory(&ti, sizeof ti);
                ti.cbSize   = sizeof ti;
                ti.uFlags   = TTF_SUBCLASS;
                ti.hwnd     = hwnd;
                for (i = 0; i < 7; i++) {
                    ti.uId     = (UINT_PTR)i;
                    ti.lpszText = (LPWSTR)tips[i];
                    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                }
            }
        }
        set_fonts();
        {
            wchar_t remembered[ZAPRET_PATH_MAX];
            if (zapret_path_load(remembered, ZAPRET_PATH_MAX))
                zapret_scan(remembered, &g_zap);
        }
        profiles_load(&g_prof);
        profiles_reload();
        {
            static const wchar_t *dirs[] = { L"sing-box", L"list",
                                             L"list\\applications", L"logs" };
            static const COLORREF theme[ASK_COLOR_COUNT] = {
                CLR_BG, CLR_SURFACE, CLR_LINE, CLR_TEXT,
                CLR_MUTED, CLR_OK, CLR_WARN, CLR_ACCENT
            };
            wchar_t d[MAX_PATH * 2];
            size_t  k;

            ask_configure_colors(theme);
            for (k = 0; k < sizeof dirs / sizeof dirs[0]; k++)
                if (root_file(dirs[k], d, MAX_PATH * 2)) CreateDirectoryW(d, NULL);
            /* Checked on every start, not only the first: a folder deleted
               between runs comes back. */
            apps_prepare();
        }
        singbox_seed_config();
        g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        /* We run elevated, Explorer does not: UIPI drops its messages above
           WM_USER. Without these, an autostart that beats the taskbar leaves
           no icon at all, and the icon may not answer clicks. */
        ChangeWindowMessageFilterEx(hwnd, g_taskbar_created, MSGFLT_ALLOW, NULL);
        ChangeWindowMessageFilterEx(hwnd, WM_APP_TRAY, MSGFLT_ALLOW, NULL);
        tray_init(hwnd, WM_APP_TRAY, CLR_OK, CLR_MUTED);
        PostMessageW(hwnd, WM_APP_EXC_START, 0, 0);
        SetTimer(hwnd, TIMER_SUB, 60 * 1000, NULL);
        vpn_refresh();
        lists_refresh_counts();
        ping_start(hwnd);
        {
            app_settings st;
            settings_load(&st);
            if (st.update_check) upd_start(hwnd, 0);
        }
    exc_check_start(hwnd);
        /* Ask after the window is up, not before: a message box over nothing
           is a poor first impression. */
        PostMessageW(hwnd, WM_APP_INSTALL_ASK, 0, 0);
        ask_configure(draw_button, S, g_font, g_font_small,
                      g_brush_bg, g_brush_surface, g_brush_line,
                      CLR_TEXT, CLR_MUTED, CLR_SURFACE);
        strategies_reload();
        status_refresh();
        SetTimer(hwnd, TIMER_STATUS, 2000, NULL);
        layout(hwnd);
        return 0;

    case WM_COMMAND:
        {
            int id = LOWORD(wp);
            if (id >= ID_ED_NPLUS && id < ID_ED_NPLUS + ED_ROWS) {
                ed_add(hwnd, g_ed_name, &g_ed_ncount); return 0;
            }
            if (id >= ID_ED_PPLUS && id < ID_ED_PPLUS + ED_ROWS) {
                ed_add(hwnd, g_ed_path, &g_ed_pcount); return 0;
            }
            if (id >= ID_ED_NMINUS && id < ID_ED_NMINUS + ED_ROWS) {
                ed_remove(g_ed_name, &g_ed_ncount, id - ID_ED_NMINUS);
                layout(hwnd); return 0;
            }
            if (id >= ID_ED_PMINUS && id < ID_ED_PMINUS + ED_ROWS) {
                ed_remove(g_ed_path, &g_ed_pcount, id - ID_ED_PMINUS);
                layout(hwnd); return 0;
            }
        }
        switch (LOWORD(wp)) {
        case ID_SET_OPEN: set_open(hwnd); return 0;
        case ID_PING_NOW:
            if (!g_ping_busy) ping_start(hwnd);
            return 0;
        case ID_SET_UPD_NOW: upd_start(hwnd, 1); return 0;
        case ID_SET_TRAY:
        case ID_SET_AUTO:
        case ID_SET_UPD: {
            HWND box = (LOWORD(wp) == ID_SET_TRAY) ? g_set_tray
                     : (LOWORD(wp) == ID_SET_AUTO) ? g_set_auto : g_set_upd;
            if (GetPropW(box, L"utgard.checked")) RemovePropW(box, L"utgard.checked");
            else SetPropW(box, L"utgard.checked", (HANDLE)1);
            InvalidateRect(box, NULL, FALSE);
            return 0;
        }
        case ID_SET_BACK: g_page = PAGE_UTGARD; layout(hwnd); return 0;
        case ID_SET_SAVE: set_save(hwnd); return 0;

        case ID_TAB_UTGARD:
            KillTimer(hwnd, TIMER_PICK);
            g_page = PAGE_UTGARD;
            lists_refresh_counts();
            layout(hwnd);
            return 0;
        case ID_TAB_ZAPRET:
            KillTimer(hwnd, TIMER_PICK);
            g_page = PAGE_ZAPRET;
            status_refresh();
            exc_check_start(hwnd);
            layout(hwnd);
            return 0;
        case ID_TOGGLE:     act_vpn(hwnd); return 0;
        case ID_PICK_PATH:  on_pick_path(hwnd); return 0;
        case ID_PROF_ADD:   act_profile_add(hwnd); return 0;
        case ID_PROF_DEL:   act_profile_delete(hwnd); return 0;
        case ID_PROF_SUB:   act_subscription(hwnd); return 0;
        case ID_EDIT_HOSTS: act_edit_hosts(hwnd); return 0;
        case ID_EDIT_APPS:  act_edit_apps(hwnd); return 0;

        case ID_HOSTS_BACK: hosts_back(hwnd); return 0;
        case ID_HOSTS_TIDY: hosts_tidy(hwnd); return 0;
        case ID_HOSTS_SAVE:
            if (g_hosts_mode == HOSTS_ZAPRET) hosts_save_zapret(hwnd);
            else                              hosts_save_start(hwnd, 1);
            return 0;

        case ID_APPS_BACK:
            g_page = PAGE_UTGARD;
            lists_refresh_counts();
            layout(hwnd);
            return 0;

        case ID_APPS_PICK:   pk_open(hwnd);  return 0;
        case ID_PICK_BACK:   pk_close(hwnd); return 0;
        case ID_PICK_SAVE:   pk_save(hwnd);  return 0;

        case ID_PICK_SEARCH:
            if (HIWORD(wp) == EN_CHANGE) pk_refresh();
            return 0;

        case ID_PICK_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                LRESULT i = SendMessageW(g_pk_list, LB_GETCURSEL, 0, 0);
                if (i != LB_ERR && i >= 0 && i < g_pk_view_n) {
                    pk_toggle(g_pk_all[g_pk_view[i]].path);
                    SendMessageW(g_pk_list, LB_SETCURSEL, (WPARAM)-1, 0);
                    InvalidateRect(g_pk_list, NULL, TRUE);
                    EnableWindow(g_pk_save, g_pk_checked_n > 0);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            return 0;

        case ID_ED_BACK:
            g_page = PAGE_APPS;
            apps_reload();
            layout(hwnd);
            return 0;
        case ID_ED_SAVE: ed_save(hwnd); return 0;

        case ID_APPS_MANUAL: ed_open_new(hwnd); return 0;

        case ID_PROFILES:
            if (HIWORD(wp) == LBN_DBLCLK)         act_profile_activate(hwnd);
            else if (HIWORD(wp) == LBN_SELCHANGE) layout(hwnd);
            return 0;
        case ID_ZAP_START:  act_start(hwnd, NULL); return 0;
        case ID_ZAP_STOP:   act_stop(hwnd);       return 0;

        case ID_ZAP_RESTART: act_restart(hwnd); return 0;
        case ID_ZAP_FIX:     act_zapret_fix(hwnd); return 0;
        case ID_ZAP_GAME:    act_zap_game(hwnd); return 0;
        case ID_ZAP_IPSET:   act_zap_ipset(hwnd); return 0;
        case ID_ZAP_IPUPD:   act_zap_ipset_update(hwnd); return 0;
        case ID_ZAP_HOSTS:   act_zap_hosts(hwnd); return 0;
        case ID_ZAP_LIST:    hosts_open(hwnd, HOSTS_ZAPRET); return 0;

        case ID_STRATEGIES:
            if (HIWORD(wp) == LBN_DBLCLK)        act_start(hwnd, NULL);
            else if (HIWORD(wp) == LBN_SELCHANGE) layout(hwnd);
            return 0;
        default: return 0;
        }

    case WM_APP_SUB_DONE: {
        sub_job *job = (sub_job *)lp;

        g_sub_busy = 0;
        if (job->ok) {
            subscription_apply(hwnd, job->url, job->body, job->len, job->silent);
            /* Any successful load, manual or automatic, restarts the interval. */
            settings_load(&g_set);
            g_set.sub_last = _time64(NULL);
            settings_save(&g_set);
            g_sub_retry = 0;
        } else if (job->silent) {
            g_sub_retry = _time64(NULL) + 15 * 60;
        } else {
            problem(hwnd, job->err);
        }

        free(job->body);
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_JOB_DONE: {
        long_job *j = (long_job *)lp;
        g_busy      = 0;
        g_busy_text = NULL;
        if (j->done) j->done(hwnd, j);
        job_free(j);
        layout(hwnd);
        return 0;
    }

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: window_show(hwnd); break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:   tray_menu(hwnd);   break;
        }
        return 0;

    case WM_APP_SHOW:
        window_show(hwnd);
        return 0;

    case WM_CLOSE:
        /* With the setting on, the cross hides the window and the program
           stays in the tray; with it off, the cross exits. The tunnel keeps
           running either way, as it always has. */
        settings_load(&g_set);
        if (g_set.tray_on_close) { ShowWindow(hwnd, SW_HIDE); return 0; }
        /* Exiting mid-job would leave sing-box half-started or the zapret
           service half-replaced; the job takes seconds, so ask to wait. */
        if (g_busy) {
            problem(hwnd, L"Дождитесь окончания операции — она займёт несколько секунд.");
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_APP_INSTALL_ASK:
        offer_install(hwnd);
        return 0;

    case WM_APP_INSTALL: {
        install_job *job = (install_job *)lp;

        g_installing = 0;
        if (!job->ok)
            problem(hwnd, job->msg[0] ? job->msg
                                      : L"Не удалось установить sing-box");
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_EXC_START:
        exc_check_start(hwnd);
        sub_auto_check(hwnd);        /* due already if the PC was off a while */
        return 0;

    case WM_APP_UPD_DONE: upd_done(hwnd, (upd_job *)lp); return 0;

    case WM_APP_EXC_DONE: {
        exc_job *job = (exc_job *)lp;
        g_exc_known   = job->total;
        g_exc_present = job->present;
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_PING_ONE: {
        int gen = (int)(wp >> 8);
        int idx = (int)(wp & 0xFF);
        if (gen == (g_ping_gen & 0xFF) && idx >= 0 && idx < PROFILES_MAX) {
            g_ping[idx] = (int)lp;
            InvalidateRect(g_plist, NULL, TRUE);
        }
        return 0;
    }

    case WM_APP_PING_DONE:
        if ((int)wp == (g_ping_gen & 0xFF)) g_ping_busy = 0;
        layout(hwnd);               /* re-enables the refresh button too */
        return 0;

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_LISTBOX) {
            if (d->CtlID == ID_PROFILES)      draw_profile(d);
            else if (d->CtlID == ID_APPS_LIST) draw_app_row(d);
            else if (d->CtlID == ID_PICK_LIST) draw_pick_row(d);
            else                               draw_strategy(d);
        } else {
            draw_button(d);
        }
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, CLR_TEXT);
        SetBkColor((HDC)wp, CLR_SURFACE);
        return (LRESULT)g_brush_surface;

    case WM_CTLCOLORLISTBOX:
        if ((HWND)lp != g_list && (HWND)lp != g_plist && (HWND)lp != g_alist &&
            (HWND)lp != g_pk_list) {
            /* The drop-down part of the log level combo. */
            SetTextColor((HDC)wp, CLR_TEXT);
            SetBkColor((HDC)wp, CLR_SURFACE);
            return (LRESULT)g_brush_surface;
        }
        SetBkColor((HDC)wp, CLR_SURFACE);
        return (LRESULT)g_brush_surface;

    case WM_TIMER:
        if (wp == TIMER_SUB) { sub_auto_check(hwnd); return 0; }
        if (wp == TIMER_PICK) {
            if (g_page == PAGE_PICK) pk_refresh();
            else                     KillTimer(hwnd, TIMER_PICK);
            return 0;
        }
        if (wp != TIMER_STATUS) return 0;
        if (g_page == PAGE_ZAPRET && g_zap.valid) {
            if (status_refresh()) {
                InvalidateRect(hwnd, NULL, TRUE);
                InvalidateRect(g_list, NULL, TRUE);
            }
        }
        /* Checked on every page and while hidden: the tray icon must not keep
           showing "on" after sing-box has died. */
        if (vpn_refresh()) {
            tray_set_state(g_vpn_on);
            if (g_page == PAGE_UTGARD) layout(hwnd);
        }
        return 0;

    case WM_DPICHANGED: {
        const RECT *sug = (const RECT *)lp;
        g_dpi = (int)HIWORD(wp);
        fonts_destroy();
        fonts_create();
        set_fonts();
        SendMessageW(g_list,  LB_SETITEMHEIGHT, 0, (LPARAM)S(30));
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        ask_configure(draw_button, S, g_font, g_font_small,
                      g_brush_bg, g_brush_surface, g_brush_line,
                      CLR_TEXT, CLR_MUTED, CLR_SURFACE);
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        return 0;
    }

    case WM_SIZE:
        layout(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        on_paint(hwnd);
        return 0;

    case WM_DESTROY:
        tray_remove();
        KillTimer(hwnd, TIMER_STATUS);
        PostQuitMessage(0);
        return 0;
    }
    /* Explorer restarted: every notification icon is gone and must be re-added. */
    if (g_taskbar_created && msg == g_taskbar_created) {
        tray_readd();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc = { 0 };
    HWND hwnd;
    MSG msg;
    RECT want;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    (void)prev;

    /* One instance. With a tray icon a second launch would otherwise mean a
       second icon and a second client fighting over the same sing-box; instead
       it asks the running one to show itself and leaves. */
    {
        HANDLE one = CreateMutexW(NULL, TRUE, L"Local\\UtgardClient");
        if (one && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = FindWindowW(L"UtgardMain", NULL);
            if (other) PostMessageW(other, WM_APP_SHOW, 0, 0);
            CloseHandle(one);
            return 0;
        }
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 1;

    brushes_create();

    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));   /* res/utgard.rc */
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_brush_bg;
    wc.lpszClassName = L"UtgardMain";
    if (!RegisterClassExW(&wc)) return 1;

    g_dpi = (int)GetDpiForSystem();
    want.left = 0; want.top = 0; want.right = S(560); want.bottom = S(700);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)g_dpi);

    hwnd = CreateWindowExW(0, wc.lpszClassName, L"Utgard", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           want.right - want.left, want.bottom - want.top,
                           NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    /* Start with focus cues hidden: they appear once the keyboard is used. */
    SendMessageW(hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

    /* Started by autostart with --minimized: straight to the tray, no window.
       Started by hand: the window shows, because a double-click that seems to
       do nothing is worse than an icon that appears. */
    if (cmdline && wcsstr(cmdline, L"--minimized")) show = SW_HIDE;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    fonts_destroy();
    brushes_destroy();
    CoUninitialize();
    return (int)msg.wParam;
}
