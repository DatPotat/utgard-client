/*
 * Utgard client - Fonts, brushes and drawing primitives: text, fills, buttons, list hover.
 */

#include "ui.h"

/* logical pixels -> device pixels */
int S(int v) { return MulDiv(v, g_dpi, USER_DEFAULT_SCREEN_DPI); }

/* ---- resources ------------------------------------------------------ */

static HFONT make_font(int size_pt10, int weight)
{
    return CreateFontW(-MulDiv(size_pt10, g_dpi, 720), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

void fonts_create(void)
{
    g_font       = make_font(105, FW_NORMAL);     /* 10.5 pt */
    g_font_big   = make_font(120, FW_SEMIBOLD);   /* 12 pt   */
    g_font_small = make_font(100, FW_NORMAL);     /* 10 pt   */
    g_font_meta  = make_font(90,  FW_NORMAL);     /* 9 pt: metadata only (versions, paths, addresses) */
    g_font_bold  = make_font(105, FW_SEMIBOLD);
    g_font_small_bold = make_font(100, FW_SEMIBOLD);
    g_font_title = make_font(165, FW_SEMIBOLD);   /* 16.5 pt: page and state titles */
    g_font_deco  = CreateFontW(-MulDiv(165, g_dpi, 720), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Ruslan Display");
    g_font_mono  = CreateFontW(-MulDiv(95, g_dpi, 720), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");
}

void fonts_destroy(void)
{
    if (g_font)       DeleteObject(g_font);
    if (g_font_big)   DeleteObject(g_font_big);
    if (g_font_small) DeleteObject(g_font_small);
    if (g_font_mono)  DeleteObject(g_font_mono);
    if (g_font_bold)  DeleteObject(g_font_bold);
    if (g_font_small_bold) DeleteObject(g_font_small_bold);
    if (g_font_title) DeleteObject(g_font_title);
    if (g_font_deco)  DeleteObject(g_font_deco);
    if (g_font_meta)  DeleteObject(g_font_meta);
    g_font = g_font_big = g_font_small = g_font_mono = NULL;
    g_font_bold = g_font_small_bold = g_font_title = g_font_deco = g_font_meta = NULL;
}

void brushes_create(void)
{
    g_brush_bg      = CreateSolidBrush(CLR_BG);
    g_brush_footer  = CreateSolidBrush(CLR_FOOTER);
    g_brush_surface = CreateSolidBrush(CLR_SURFACE);
    g_brush_line    = CreateSolidBrush(CLR_LINE);
}

void brushes_destroy(void)
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

int list_hot_row(HWND list)
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

void list_hover_attach(HWND list)
{
    SetWindowSubclass(list, list_hover_proc, 2, 0);
}

/* A list rebuilt on a timer loses its hover on every rebuild while the
   pointer sits still; this reads the pointer again and puts it back. */
void list_hover_resync(HWND list)
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

/* kind in the low byte, what lies behind the button in the rest: a rounded
   shape leaves its corners unpainted, and they have to be filled with
   whatever the parent draws there. Palette roles, not colours, since the
   theme can change under a live button: 0 page, 1 card, 2 accent block,
   3 footer (BACK_* in ui.h). */
HWND make_button_on(HWND parent, const wchar_t *text, int id, int kind,
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

HWND make_button(HWND parent, const wchar_t *text, int id, int kind)
{
    return make_button_on(parent, text, id, kind, 0);
}

void fill(HDC dc, int x, int y, int w, int h, HBRUSH br)
{
    RECT r = { x, y, x + w, y + h };
    FillRect(dc, &r, br);
}

void text_at(HDC dc, int x, int y, int w, int h,
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

/* dot() and rounded_r() live in ui_gfx.c: anti-aliased through GDI+. */

void rounded(HDC dc, const RECT *r, COLORREF fillc, COLORREF border)
{
    rounded_r(dc, r, fillc, border, S(10));
}

/* A row of a card: the label on the left, "\t" then a value on the right,
   and a chevron - the whole row is the target, as the design has it. */
static void draw_row(const DRAWITEMSTRUCT *d, COLORREF backdrop, BOOL hot, BOOL pressed)
{
    RECT     r = d->rcItem;
    wchar_t  cap[256], *tab;
    COLORREF fillc = pressed ? CLR_TINT : hot ? CLR_HOVER : backdrop;
    int      round = (int)(INT_PTR)GetPropW(d->hwndItem, L"utgard.round");   /* 1 top, 2 bottom */
    int      cx, cy;

    if (round) {
        /* An end row of a card lies over the card's rounded corners: it
           paints them itself - the page behind, then the card's curve and
           border - so no square corner shows. The straight end is pushed
           out of the button, where it is clipped away. */
        RECT   o = r;
        HBRUSH page = CreateSolidBrush(CLR_BG);
        FillRect(d->hDC, &r, page);
        DeleteObject(page);
        InflateRect(&o, S(1), S(1));
        if (!(round & 1)) o.top -= S(24);
        if (!(round & 2)) o.bottom += S(24);
        rounded_r(d->hDC, &o, fillc, CLR_LINE, S(12));
    } else {
        HBRUSH br = CreateSolidBrush(fillc);
        FillRect(d->hDC, &r, br);
        DeleteObject(br);
    }
    GetWindowTextW(d->hwndItem, cap, 256);
    tab = wcschr(cap, L'\t');
    if (tab) *tab++ = 0;
    text_at(d->hDC, r.left + S(16), r.top, r.right - r.left - S(48), r.bottom - r.top,
            cap, (d->itemState & ODS_DISABLED) ? CLR_MUTED : CLR_TEXT,
            d->hwndItem == g_row_server ? g_font_bold : g_font, DT_LEFT | DT_END_ELLIPSIS);
    if (tab)
        text_at(d->hDC, r.left + S(16), r.top, r.right - r.left - S(48), r.bottom - r.top,
                tab, CLR_MUTED, g_font, DT_RIGHT);
    cx = r.right - S(24); cy = (r.top + r.bottom) / 2;
    gfx_icon(d->hDC, (d->hwndItem == g_set_adv && g_set_adv_open) ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT,
             cx - S(10), cy - S(10), S(20), CLR_MUTED);
}

void draw_button(const DRAWITEMSTRUCT *d)
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
    if (((ud >> 8) & 0xFFFFFF) == 1) backdrop = CLR_SURFACE;   /* inside a card */
    if (((ud >> 8) & 0xFFFFFF) == 2) backdrop = CLR_ACCENT;    /* inside the state block */
    if (((ud >> 8) & 0xFFFFFF) == 3) backdrop = CLR_FOOTER;
    if (((ud >> 8) & 0xFFFFFF) == 4) backdrop = CLR_TINT;

    /* The VPN button lies on the state card, not on the page: accent while
       on, the card's surface otherwise (off, connecting, error). */
    if (d->hwndItem == g_toggle) backdrop = g_vpn_on ? CLR_ACCENT : CLR_SURFACE;

    if (kind == BK_NAV) { draw_nav(d); return; }
    if (kind == BK_SELECT) {
        /* A drop-down field: the choice of the combo it stands for and a
           chevron, in a rounded field like every other input. */
        HWND    combo = (HWND)GetPropW(d->hwndItem, L"utgard.combo");
        wchar_t text[128] = L"";
        HBRUSH  back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        if (combo) GetWindowTextW(combo, text, 128);
        rounded_r(d->hDC, &r, (hot || pressed) ? CLR_HOVER : CLR_SURFACE, CLR_BORDER, S(8));
        text_at(d->hDC, r.left + S(12), r.top, r.right - r.left - S(48), r.bottom - r.top, text,
                CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
        gfx_icon(d->hDC, ICON_CHEVRON_DOWN, r.right - S(30), (r.top + r.bottom) / 2 - S(9), S(18), CLR_MUTED);
        if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
            RECT f = r;
            InflateRect(&f, -S(3), -S(3));
            DrawFocusRect(d->hDC, &f);
        }
        return;
    }
    if (kind == BK_ICON) {
        /* A button inside an input field: only its icon, and a soft
           highlight under the pointer. The caption is for screen readers. */
        HBRUSH back = CreateSolidBrush(backdrop);
        int    s = (r.bottom - r.top) * 5 / 8;
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        if (hot || pressed) rounded_r(d->hDC, &r, pressed ? CLR_TINT : CLR_HOVER, pressed ? CLR_TINT : CLR_HOVER, S(6));
        gfx_icon(d->hDC, ICON_FOLDER_OPEN, (r.left + r.right - s) / 2, (r.top + r.bottom - s) / 2, s,
                 hot ? CLR_TEXT : CLR_MUTED);
        return;
    }
    if (kind == BK_ROW) { draw_row(d, backdrop, hot, pressed); return; }

    GetWindowTextW(d->hwndItem, caption, (int)(sizeof caption / sizeof caption[0]));

    if (kind == BK_CHIP) {
        /* One of a set of choices: the chosen one tinted and bold, with an
           accent border, so it differs by more than colour. */
        int  on = GetPropW(d->hwndItem, L"utgard.checked") != NULL;
        HBRUSH back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        rounded_r(d->hDC, &r, on ? CLR_TINT : (hot || pressed) ? CLR_HOVER : CLR_SURFACE,
                  on ? CLR_ACCENT : CLR_BORDER, S(8));
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top, caption,
                disabled ? CLR_MUTED : CLR_TEXT, on ? g_font_bold : g_font_small, DT_CENTER);
        return;
    }

    if (kind == BK_LINK) {
        /* Text only, the accent colour of a link; lighter under the pointer. */
        HBRUSH back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top, caption,
                hot || pressed ? CLR_ACCENT_HI : CLR_ACCENT, g_font_small, DT_LEFT);
        return;
    }

    if (kind == BK_CHECK) {
        /* A checkbox drawn in the palette: the themed one ignores our colours. */
        int  on  = GetPropW(d->hwndItem, L"utgard.checked") != NULL;
        int  box = S(18);
        RECT b;

        {
            HBRUSH back = CreateSolidBrush(backdrop);
            FillRect(d->hDC, &r, back);
            DeleteObject(back);
        }
        b.left = r.left; b.top = r.top + (r.bottom - r.top - box) / 2;
        b.right = b.left + box; b.bottom = b.top + box;
        rounded_r(d->hDC, &b, on ? CLR_ACCENT : (hot ? CLR_HOVER : CLR_SURFACE),
                  on ? CLR_ACCENT : CLR_BORDER, S(5));
        if (on) gfx_icon(d->hDC, ICON_CHECK, b.left + S(2), b.top + S(2), box - S(4), CLR_ON_ACCENT);
        text_at(d->hDC, r.left + box + S(10), r.top, r.right - r.left - box - S(10),
                r.bottom - r.top, caption, CLR_TEXT, g_font, DT_LEFT);
        return;
    }

    if (kind == BK_TAB) {
        /* The routing page's tabs: which one matches the page shown. */
        BOOL active = (d->CtlID == ID_TAB_SITES && g_page == PAGE_HOSTS) ||
                      (d->CtlID == ID_TAB_APPS  && (g_page == PAGE_APPS || g_page == PAGE_PICK ||
                                                   g_page == PAGE_EDIT)) ||
                      (d->CtlID == ID_TAB_PAC   && g_page == PAGE_PAC);
        FillRect(d->hDC, &r, g_brush_bg);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top - S(3),
                caption, active || hot ? CLR_TEXT : CLR_MUTED, g_font_bold, DT_CENTER);
        if (active) {
            HBRUSH br = CreateSolidBrush(CLR_ACCENT);
            fill(d->hDC, r.left, r.bottom - S(3), r.right - r.left, S(3), br);
            DeleteObject(br);
        }
    } else if (kind == BK_PRIMARY && d->hwndItem == g_toggle && g_vpn_on) {
        /* Inside the accent state block: an outline in the on-accent colour. */
        HBRUSH back = CreateSolidBrush(CLR_ACCENT);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        rounded(d->hDC, &r, pressed ? CLR_ACCENT_LO : hot ? CLR_ACCENT_HI : CLR_ACCENT,
                CLR_ON_ACCENT);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                caption, CLR_ON_ACCENT, g_font_bold, DT_CENTER);
    } else if (kind == BK_PRIMARY) {
        COLORREF bg = disabled ? CLR_LINE
                    : pressed  ? CLR_ACCENT_LO
                    : hot      ? CLR_ACCENT_HI : CLR_ACCENT;
        HBRUSH   back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        rounded(d->hDC, &r, bg, bg);
        text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                caption, disabled ? CLR_MUTED : CLR_ON_ACCENT, g_font_bold, DT_CENTER);
    } else {
        COLORREF border = kind == BK_DANGER ? CLR_WARN : CLR_BORDER;
        HBRUSH   back = CreateSolidBrush(backdrop);
        FillRect(d->hDC, &r, back);
        DeleteObject(back);
        rounded(d->hDC, &r, pressed ? CLR_TINT : (hot ? CLR_HOVER : CLR_SURFACE), border);
        if (caption[0] == L'\x00D7' && !caption[1]) {
            /* The clear button of a search field: an icon, not a glyph. */
            int s = (r.bottom - r.top) * 2 / 3;
            gfx_icon(d->hDC, ICON_X, (r.left + r.right - s) / 2, (r.top + r.bottom - s) / 2, s, CLR_MUTED);
        } else {
            text_at(d->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                    caption, disabled ? CLR_MUTED : (kind == BK_DANGER ? CLR_WARN : CLR_TEXT),
                    g_font_bold, DT_CENTER);
        }
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

/* The embedded Ruslan Display, for this process only: nothing is installed
   and other programs never see it. Called once at start. */
void fonts_load_embedded(void)
{
    HRSRC   res = FindResourceW(NULL, MAKEINTRESOURCEW(200), (LPCWSTR)RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(NULL, res) : NULL;
    void   *data = mem ? LockResource(mem) : NULL;
    DWORD   n = 0;
    if (data) AddFontMemResourceEx(data, SizeofResource(NULL, res), NULL, &n);
}

/* Titles: Ruslan Display in the Khokhloma theme, the ordinary title font
   otherwise (and whenever the embedded font could not be created). */
HFONT title_font(void)
{
    return (g_set.theme == SETTINGS_THEME_KHOKHLOMA && g_font_deco) ? g_font_deco : g_font_title;
}
