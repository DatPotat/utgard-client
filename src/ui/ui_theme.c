/*
 * Utgard client - Palette (light/dark, following Windows unless the user
 * picked one), and the sidebar: section buttons, their icons, the status
 * lines under them.
 */

#include "ui.h"

ui_palette g_pal;
int        g_dark;
int        g_ox;

static const ui_palette PAL_LIGHT = {
    RGB(0xF4, 0xF2, 0xEE), RGB(0xEA, 0xE7, 0xE1), RGB(0xFB, 0xFA, 0xF8),
    RGB(0x26, 0x28, 0x2B), RGB(0x5E, 0x61, 0x66),
    RGB(0x3A, 0x5F, 0x7D), RGB(0x2F, 0x4E, 0x67), RGB(0x46, 0x6E, 0x8F),
    RGB(0xFF, 0xFF, 0xFF), RGB(0xE3, 0xEA, 0xF0),
    RGB(0x94, 0x55, 0x1A), RGB(0x7A, 0x45, 0x15), RGB(0xA8, 0x6A, 0x33),
    RGB(0xDD, 0xD8, 0xCF), RGB(0x8C, 0x8B, 0x86), RGB(0xDF, 0xDB, 0xD3)
};

/* Khokhloma: gold on black with cinnabar, held to the same WCAG AA pairs
   as the other two (text 4.5:1, borders 3:1): the cinnabar is lightened
   to #E85F45 and carries dark text, which a deep red could not. */
static const ui_palette PAL_KHOKHLOMA = {
    RGB(0x12, 0x0D, 0x0A), RGB(0x1C, 0x13, 0x0E), RGB(0x22, 0x17, 0x11),
    RGB(0xF2, 0xC9, 0x4C), RGB(0xD8, 0xB2, 0x5A),
    RGB(0xE8, 0x5F, 0x45), RGB(0xD9, 0x58, 0x40), RGB(0xF0, 0x7A, 0x62),
    RGB(0x1A, 0x0E, 0x08), RGB(0x3A, 0x1A, 0x14),
    RGB(0xE8, 0x83, 0x3A), RGB(0xC9, 0x6E, 0x2E), RGB(0xF0, 0x9A, 0x55),
    RGB(0x4A, 0x33, 0x20), RGB(0xD4, 0xA5, 0x2A), RGB(0x2C, 0x1D, 0x15)
};

static const ui_palette PAL_DARK = {
    RGB(0x1B, 0x1E, 0x21), RGB(0x16, 0x19, 0x1C), RGB(0x24, 0x28, 0x2C),
    RGB(0xE6, 0xE4, 0xDF), RGB(0xA9, 0xAD, 0xB1),
    RGB(0x8E, 0xB3, 0xD1), RGB(0x7A, 0xA0, 0xBE), RGB(0xA6, 0xC4, 0xDD),
    RGB(0x13, 0x20, 0x2B), RGB(0x2C, 0x3A, 0x46),
    RGB(0xE3, 0xA4, 0x62), RGB(0xC9, 0x8E, 0x50), RGB(0xED, 0xBB, 0x85),
    RGB(0x33, 0x38, 0x3D), RGB(0x6E, 0x74, 0x7A), RGB(0x24, 0x28, 0x2D)
};

/* AppsUseLightTheme is what Windows' own apps follow; Microsoft documents
   the WinRT UISettings route, not this value, so a missing value means the
   Windows default, light. */
int theme_system_is_dark(void)
{
    DWORD v = 1, n = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &n) != ERROR_SUCCESS)
        return 0;
    return v == 0;
}

/* The first run has no theme saved: take the one Windows uses for apps and
   keep it, so later runs start the same way whatever Windows does. */
void theme_pick(void)
{
    if (g_set.theme < SETTINGS_THEME_LIGHT || g_set.theme > SETTINGS_THEME_KHOKHLOMA) {
        g_set.theme = theme_system_is_dark() ? SETTINGS_THEME_DARK : SETTINGS_THEME_LIGHT;
        settings_save(&g_set);
    }
    g_dark = g_set.theme != SETTINGS_THEME_LIGHT;
    g_pal  = g_set.theme == SETTINGS_THEME_LIGHT ? PAL_LIGHT
           : g_set.theme == SETTINGS_THEME_DARK  ? PAL_DARK : PAL_KHOKHLOMA;
}

static BOOL CALLBACK retheme_child(HWND h, LPARAM lp)
{
    wchar_t cls[32];
    (void)lp;
    GetClassNameW(h, cls, 32);
    if (!_wcsicmp(cls, L"COMBOBOX"))
        SetWindowTheme(h, g_dark ? L"DarkMode_CFD" : L"Explorer", NULL);
    else if (!_wcsicmp(cls, L"LISTBOX") || !_wcsicmp(cls, L"EDIT") ||
             !_wcsicmp(cls, WC_LISTVIEWW))
        SetWindowTheme(h, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    return TRUE;
}

/* The caption follows the page: dark title bar on the dark palette. Both
   attributes are ignored by builds that do not know them. */
void theme_caption(HWND hwnd)
{
    BOOL     dark    = g_dark;
    COLORREF caption = CLR_SIDE;
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_CAPTION_COLOR, &caption, sizeof caption);
}

void theme_apply(HWND hwnd)
{
    theme_pick();
    brushes_destroy();
    brushes_create();
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_brush_bg);
    theme_caption(hwnd);
    EnumChildWindows(hwnd, retheme_child, 0);
    theme_ask();
    if (g_tip) {
        /* Honoured because the tooltip is unthemed: its colours stay right
           even where the custom drawing is not used. */
        SendMessageW(g_tip, TTM_SETTIPBKCOLOR, (WPARAM)CLR_SURFACE, 0);
        SendMessageW(g_tip, TTM_SETTIPTEXTCOLOR, (WPARAM)CLR_TEXT, 0);
    }
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

/* ---- sidebar -------------------------------------------------------- */

int nav_section(void)
{
    switch (g_page) {
    case PAGE_UTGARD:   return NAV_CONNECT;
    case PAGE_SERVERS:  return NAV_SERVERS;
    case PAGE_ZAPRET:   return NAV_ZAPRET;
    case PAGE_SETTINGS: return NAV_SETTINGS;
    case PAGE_HOSTS:    return g_hosts_mode == HOSTS_ZAPRET ? NAV_ZAPRET : NAV_ROUTING;
    default:            return NAV_ROUTING;   /* apps, PAC, picker, editor */
    }
}

int nav_rail(HWND hwnd)
{
    RECT c;
    GetClientRect(hwnd, &c);
    return c.right < S(RAIL_BELOW);
}

void nav_layout(HWND hwnd)
{
    HWND items[NAV_COUNT] = { g_nav[0], g_nav[1], g_nav[2], g_nav[3], g_nav[4] };
    int  rail = nav_rail(hwnd);
    int  x = S(16), w = SIDE_W(rail) - S(32), h = rail ? S(60) : S(44);
    int  y = rail ? S(24) : S(64);
    int  i;
    RECT c;

    GetClientRect(hwnd, &c);
    if (rail) { x = S(8); w = SIDE_W(1) - S(16); }
    for (i = 0; i < NAV_SETTINGS; i++) {
        MoveWindow(items[i], x, y, w, h, TRUE);
        y += h + S(4);
    }
    /* Settings sit at the foot, above the two status lines. */
    MoveWindow(items[NAV_SETTINGS], x,
               c.bottom - (rail ? S(24) : S(84)) - h, w, h, TRUE);
    for (i = 0; i < NAV_COUNT; i++) InvalidateRect(items[i], NULL, FALSE);
}

/* The four states, Lucide shapes: an empty ring (off), a dashed ring
   (connecting), a filled disc with a tick (on), a ring with "!" (error).
   Shape carries the meaning; colour only repeats it. r is the ring radius;
   the Lucide circle has r = 10 on its 24 grid, hence the 2.4 box.
   inv: drawn on the accent fill. */
void state_icon(HDC dc, int kind, int cx, int cy, int r, int inv)
{
    int size = r * 12 / 5;
    if (kind == STATE_ON) {
        int cs = r * 6 / 5;
        dot(dc, cx, cy, r, inv ? CLR_ON_ACCENT : CLR_ACCENT);
        gfx_icon(dc, ICON_CHECK, cx - cs / 2, cy - cs / 2, cs, inv ? CLR_ACCENT : CLR_ON_ACCENT);
    } else {
        int icon = kind == STATE_ERROR ? ICON_CIRCLE_ALERT
                 : kind == STATE_WAIT  ? ICON_CIRCLE_DASHED : ICON_CIRCLE;
        COLORREF c = inv ? CLR_ON_ACCENT : kind == STATE_ERROR ? CLR_WARN : CLR_MUTED;
        gfx_icon(dc, icon, cx - size / 2, cy - size / 2, size, c);
    }
}

static void nav_icon(HDC dc, int which, int cx, int cy, COLORREF c)
{
    static const int icons[NAV_COUNT] = { ICON_POWER, ICON_SERVER, ICON_ROUTE, ICON_SHIELD, ICON_SETTINGS };
    gfx_icon(dc, icons[which], cx - S(10), cy - S(10), S(20), c);
}

void draw_nav(const DRAWITEMSTRUCT *d)
{
    int      which  = (int)d->CtlID - ID_NAV_FIRST;
    int      cur    = which == nav_section();
    BOOL     hot    = ask_is_hot(d->hwndItem);
    int      rail   = nav_rail(GetParent(d->hwndItem));
    RECT     r      = d->rcItem;
    HBRUSH   side   = CreateSolidBrush(CLR_SIDE);
    wchar_t  caption[32];

    FillRect(d->hDC, &r, side);
    DeleteObject(side);
    if (cur || hot || (d->itemState & ODS_SELECTED))
        rounded_r(d->hDC, &r, cur ? CLR_TINT : CLR_HOVER, cur ? CLR_TINT : CLR_HOVER, S(10));
    GetWindowTextW(d->hwndItem, caption, 32);
    if (rail) {
        nav_icon(d->hDC, which, (r.left + r.right) / 2, r.top + S(22), CLR_TEXT);
        text_at(d->hDC, r.left, r.top + S(34), r.right - r.left, S(18), caption,
                CLR_TEXT, cur ? g_font_small_bold : g_font_small, DT_CENTER);
    } else {
        nav_icon(d->hDC, which, r.left + S(22), (r.top + r.bottom) / 2, CLR_TEXT);
        text_at(d->hDC, r.left + S(44), r.top, r.right - r.left - S(44), r.bottom - r.top,
                caption, CLR_TEXT, cur ? g_font_bold : g_font, DT_LEFT);
    }
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&r, -S(3), -S(3));
        DrawFocusRect(d->hDC, &r);
    }
}

int vpn_state(void)
{
    if (g_vpn_on && (g_awg_lost || g_switch_note)) return STATE_ERROR;
    if (g_vpn_on) return STATE_ON;
    if (g_busy && g_busy_text) return STATE_WAIT;
    return STATE_OFF;
}

void paint_sidebar(HDC dc, const RECT *client)
{
    int    rail = nav_rail(WindowFromDC(dc));
    int    sw = SIDE_W(rail);
    HBRUSH side = CreateSolidBrush(CLR_SIDE);
    RECT   r = { 0, 0, sw, client->bottom };

    FillRect(dc, &r, side);
    DeleteObject(side);
    fill(dc, sw - S(1), 0, S(1), client->bottom, g_brush_line);
    if (rail) return;

    if (g_set.theme == SETTINGS_THEME_KHOKHLOMA) {
        /* A strip of "травка": berries and leaves under the name. */
        int x;
        text_at(dc, S(28), S(20), sw - S(40), S(28), L"Утгардъ", CLR_TEXT, title_font(), DT_LEFT);
        for (x = S(24); x + S(28) < sw; x += S(36)) {
            HBRUSH  leaf = CreateSolidBrush(RGB(0xD4, 0xA5, 0x2A));
            HBRUSH  green = CreateSolidBrush(RGB(0x3F, 0x6B, 0x2A));
            HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN)), ob = SelectObject(dc, leaf);
            Ellipse(dc, x, S(52), x + S(14), S(58));
            SelectObject(dc, green);
            Ellipse(dc, x + S(18), S(56), x + S(30), S(61));
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(leaf); DeleteObject(green);
            dot(dc, x + S(15), S(55), S(3), RGB(0xC6, 0x28, 0x28));
        }
    } else {
        /* The name, and the version beside it in small type, on one line;
           the smaller type moved down two units onto the name's baseline. */
        SIZE    sz = { 0, 0 };
        HGDIOBJ old = SelectObject(dc, g_font_bold);
        GetTextExtentPoint32W(dc, L"Utgard", 6, &sz);
        SelectObject(dc, old);
        text_at(dc, S(28), S(24), sw - S(40), S(24), L"Utgard", CLR_TEXT, g_font_bold, DT_LEFT);
        text_at(dc, S(28) + sz.cx + S(6), S(24) + S(2), sw - S(40) - sz.cx, S(24),
                UTGARD_VERSION_W, CLR_MUTED, g_font_meta, DT_LEFT);
    }
    fill(dc, S(16), client->bottom - S(72), sw - S(32), S(1), g_brush_line);
    {
        static const wchar_t *vpn_text[4] = {
            L"VPN выключен", L"Подключение…", L"VPN включён", L"Нет соединения" };
        int k = vpn_state(), z = g_status.mode != ZAPRET_OFF;
        int y1 = client->bottom - S(52), y2 = client->bottom - S(26);
        state_icon(dc, k, S(37), y1, S(8), 0);
        text_at(dc, S(54), y1 - S(10), sw - S(66), S(20), vpn_text[k],
                CLR_TEXT, g_font_small_bold, DT_LEFT | DT_END_ELLIPSIS);
        state_icon(dc, z ? STATE_ON : STATE_OFF, S(37), y2, S(8), 0);
        text_at(dc, S(54), y2 - S(10), sw - S(66), S(20),
                !g_zap.valid ? L"zapret не настроен"
                : z ? L"zapret работает" : L"zapret выключен",
                CLR_TEXT, g_font_small_bold, DT_LEFT | DT_END_ELLIPSIS);
    }
}

/* The dialogs in ask.c keep their own copy of fonts, brushes and colours. */
void theme_ask(void)
{
    COLORREF c[ASK_COLOR_COUNT];
    c[0] = CLR_BG; c[1] = CLR_SURFACE; c[2] = CLR_LINE; c[3] = CLR_TEXT;
    c[4] = CLR_MUTED; c[5] = CLR_OK; c[6] = CLR_WARN; c[7] = CLR_ACCENT;
    ask_configure_colors(c);
    ask_configure_frame(g_dark, CLR_SIDE, CLR_BORDER, rounded_r);
    ask_configure(draw_button, S, g_font, g_font_small,
                  g_brush_bg, g_brush_surface, g_brush_line,
                  CLR_TEXT, CLR_MUTED, CLR_SURFACE);
}
