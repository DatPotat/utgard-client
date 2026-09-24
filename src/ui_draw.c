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
    g_font_small = make_font(90,  FW_NORMAL);     /* 9 pt    */
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
    g_font = g_font_big = g_font_small = g_font_mono = NULL;
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

/* kind in the low byte, the colour behind the button in the rest: a rounded
   shape leaves its corners unpainted, and they have to be filled with
   whatever the parent draws there. 0 means the ordinary page background. */
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

void dot(HDC dc, int cx, int cy, int r, COLORREF color)
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

void rounded(HDC dc, const RECT *r, COLORREF fillc, COLORREF border)
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
        /* The VPN toggle turns red while the tunnel is up: it now stops it. */
        BOOL     stop = (d->hwndItem == g_toggle && g_vpn_on);
        COLORREF bg = disabled ? CLR_LINE
                    : pressed  ? (stop ? CLR_WARN_LO : CLR_ACCENT_LO)
                    : hot      ? (stop ? CLR_WARN_HI : CLR_ACCENT_HI)
                    : stop     ? CLR_WARN : CLR_ACCENT;
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
