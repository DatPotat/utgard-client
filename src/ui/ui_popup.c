/*
 * Utgard client - A popup list of choices in the client's look: the
 * "Добавить сервер" menu and every drop-down field use it. Rounded corners
 * come from DWM on Windows 11 (the documented corner preference and border
 * colour attributes); Windows 10 does not know them, so there the window
 * gets a rounded region and draws its own border.
 */

#include "ui.h"

#define POP_ITEM_H 36
#define UTG_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define UTG_DWMWA_BORDER_COLOR             34
#define UTG_DWMWCP_ROUND                   2

typedef struct {
    const wchar_t *const *items;
    int n, current, hot, done, dwm_round;
} pop_state;

static LRESULT CALLBACK pop_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    pop_state *st = (pop_state *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC  dc = BeginPaint(h, &ps);
        RECT c;
        int  k;
        GetClientRect(h, &c);
        {
            HBRUSH b = CreateSolidBrush(CLR_SURFACE);
            FillRect(dc, &c, b);
            DeleteObject(b);
        }
        if (!st->dwm_round) rounded_r(dc, &c, CLR_SURFACE, CLR_LINE, S(8));
        for (k = 0; k < st->n; k++) {
            RECT r = { S(4), S(4) + k * S(POP_ITEM_H), c.right - S(4), S(4) + (k + 1) * S(POP_ITEM_H) };
            if (k == st->hot) rounded_r(dc, &r, CLR_TINT, CLR_TINT, S(6));
            text_at(dc, r.left + S(12), r.top, r.right - r.left - S(40), r.bottom - r.top, st->items[k],
                    CLR_TEXT, k == st->current ? g_font_bold : g_font, DT_LEFT | DT_END_ELLIPSIS);
            if (k == st->current)
                gfx_icon(dc, ICON_CHECK, r.right - S(28), (r.top + r.bottom) / 2 - S(8), S(16), CLR_ACCENT);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int k = (GET_Y_LPARAM(l) - S(4)) / S(POP_ITEM_H);
        if (k < 0 || k >= st->n) k = -1;
        if (k != st->hot) { st->hot = k; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        int k = (GET_Y_LPARAM(l) - S(4)) / S(POP_ITEM_H);
        if (k >= 0 && k < st->n) st->done = k + 1;
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) st->done = -1;
        else if (w == VK_RETURN && st->hot >= 0) st->done = st->hot + 1;
        else if (w == VK_DOWN) { st->hot = st->hot + 1 < st->n ? st->hot + 1 : 0; InvalidateRect(h, NULL, FALSE); }
        else if (w == VK_UP)   { st->hot = st->hot > 0 ? st->hot - 1 : st->n - 1; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && st && !st->done) st->done = -1;   /* clicked elsewhere */
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* anchor: the control the list drops from, in screen coordinates. The list
   opens under it (above, when the screen ends first), at least as wide as
   it, right edges lined up when align_right. Returns the chosen index or -1. */
int popup_choose(HWND owner, const RECT *anchor, const wchar_t *const *items, int n,
                 int current, int align_right)
{
    static int registered;
    HINSTANCE  inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    pop_state  st;
    HWND       h;
    MSG        m;
    int        w = anchor->right - anchor->left, ht, k, x, y;
    HMONITOR   mon = MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;

    if (n <= 0) return -1;
    {
        HDC     dc = GetDC(owner);
        HGDIOBJ old = SelectObject(dc, g_font_bold);
        for (k = 0; k < n; k++) {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, items[k], (int)wcslen(items[k]), &sz);
            if (sz.cx + S(64) > w) w = sz.cx + S(64);
        }
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }
    ht = S(8) + n * S(POP_ITEM_H);
    x  = align_right ? anchor->right - w : anchor->left;
    y  = anchor->bottom + S(4);
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(mon, &mi)) {
        if (y + ht > mi.rcWork.bottom) y = anchor->top - S(4) - ht;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
    }
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = pop_proc; wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.lpszClassName = L"UtgardPopup";
        wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc);
        registered = 1;
    }
    st.items = items; st.n = n; st.current = current; st.hot = current; st.done = 0; st.dwm_round = 0;
    h = CreateWindowExW(WS_EX_TOOLWINDOW, L"UtgardPopup", L"", WS_POPUP, x, y, w, ht, owner, NULL, inst, &st);
    if (!h) return -1;
    {
        DWORD    pref = UTG_DWMWCP_ROUND;
        COLORREF border = CLR_LINE;
        st.dwm_round = SUCCEEDED(DwmSetWindowAttribute(h, UTG_DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref));
        if (st.dwm_round)
            DwmSetWindowAttribute(h, UTG_DWMWA_BORDER_COLOR, &border, sizeof border);
        else
            SetWindowRgn(h, CreateRoundRectRgn(0, 0, w + 1, ht + 1, S(16), S(16)), FALSE);
    }
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    while (!st.done && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.hwnd != h) m.hwnd = h;          /* keys go to the list */
        if ((m.message == WM_LBUTTONDOWN || m.message == WM_NCLBUTTONDOWN) && m.hwnd != h) {
            st.done = -1;                                                /* a click elsewhere closes it */
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyWindow(h);
    return st.done > 0 ? st.done - 1 : -1;
}

/* ---- drop-down fields -------------------------------------------------- */

/* The COMBOBOX stays, hidden, as the store of the items and the choice -
   the settings code reads and writes it as before. The field on the page is
   an owner-drawn button showing the choice; picking sends the combo's own
   CBN_SELCHANGE, so everything downstream is unchanged. */
void select_open(HWND owner, HWND field, HWND combo)
{
    static wchar_t texts[16][96];
    const wchar_t *items[16];
    RECT r;
    int  n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0), k, pick;
    if (n > 16) n = 16;
    for (k = 0; k < n; k++) {
        if (SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)k, 0) >= 96) StringCchCopyW(texts[k], 96, L"…");
        else SendMessageW(combo, CB_GETLBTEXT, (WPARAM)k, (LPARAM)texts[k]);
        items[k] = texts[k];
    }
    GetWindowRect(field, &r);
    pick = popup_choose(owner, &r, items, n, (int)SendMessageW(combo, CB_GETCURSEL, 0, 0), 0);
    if (pick >= 0 && pick != (int)SendMessageW(combo, CB_GETCURSEL, 0, 0)) {
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)pick, 0);
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
    }
    InvalidateRect(field, NULL, FALSE);
}

/* ---- tooltips ------------------------------------------------------------ */

/* A tooltip in the theme: a rounded card with the text, drawn whole by the
   app (the control is unthemed and its own painting skipped). */
LRESULT tip_draw(NMTTCUSTOMDRAW *cd)
{
    TTTOOLINFOW ti;
    wchar_t     text[1024];
    RECT        c, m, t;

    if (cd->nmcd.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    ZeroMemory(&ti, sizeof ti);
    ti.cbSize = sizeof ti;
    if (!SendMessageW(cd->nmcd.hdr.hwndFrom, TTM_GETCURRENTTOOLW, 0, (LPARAM)&ti)) return CDRF_DODEFAULT;
    text[0] = 0;
    ti.lpszText = text;
    SendMessageW(cd->nmcd.hdr.hwndFrom, TTM_GETTEXTW, 1024, (LPARAM)&ti);
    GetClientRect(cd->nmcd.hdr.hwndFrom, &c);
    {
        HBRUSH b = CreateSolidBrush(CLR_SURFACE);
        FillRect(cd->nmcd.hdc, &c, b);
        DeleteObject(b);
    }
    rounded_r(cd->nmcd.hdc, &c, CLR_SURFACE, CLR_BORDER, S(8));
    SendMessageW(cd->nmcd.hdr.hwndFrom, TTM_GETMARGIN, 0, (LPARAM)&m);
    t.left = c.left + m.left; t.top = c.top + m.top;
    t.right = c.right - m.right; t.bottom = c.bottom - m.bottom;
    {
        HGDIOBJ old = SelectObject(cd->nmcd.hdc, g_font_small);
        SetBkMode(cd->nmcd.hdc, TRANSPARENT);
        SetTextColor(cd->nmcd.hdc, CLR_TEXT);
        DrawTextW(cd->nmcd.hdc, text, -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(cd->nmcd.hdc, old);
    }
    return CDRF_SKIPDEFAULT;
}

/* Rounded like the popups: DWM on Windows 11, a region on Windows 10. */
void tip_shape(HWND tip)
{
    DWORD    pref = UTG_DWMWCP_ROUND;
    COLORREF border = CLR_BORDER;
    RECT     r;
    if (SUCCEEDED(DwmSetWindowAttribute(tip, UTG_DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref))) {
        DwmSetWindowAttribute(tip, UTG_DWMWA_BORDER_COLOR, &border, sizeof border);
        return;
    }
    GetWindowRect(tip, &r);
    SetWindowRgn(tip, CreateRoundRectRgn(0, 0, r.right - r.left + 1, r.bottom - r.top + 1, S(16), S(16)), TRUE);
}

/* ---- the PAC table, drawn like the servers table ------------------------- */

void pac_columns(int width, int *x_type, int *x_state)
{
    *x_state = width - S(16) - S(180);
    *x_type  = *x_state - S(16) - S(90);
}

LRESULT pac_row_draw(NMLVCUSTOMDRAW *cd)
{
    HWND    lv = cd->nmcd.hdr.hwndFrom;
    int     i = (int)cd->nmcd.dwItemSpec, xt, xs, w;
    RECT    r, c;
    wchar_t src[512], type[32], state[96];
    HBRUSH  b;
    int     sel;

    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    if (cd->nmcd.dwDrawStage == CDDS_POSTPAINT) {
        /* Below the last row the list view draws its own column lines;
           that empty area is the card's, so it is filled over. */
        int n = ListView_GetItemCount(lv);
        GetClientRect(lv, &c);
        if (n > 0 && ListView_GetItemRect(lv, n - 1, &r, LVIR_BOUNDS)) c.top = r.bottom;
        if (c.top < c.bottom) {
            b = CreateSolidBrush(CLR_SURFACE);
            FillRect(cd->nmcd.hdc, &c, b);
            DeleteObject(b);
        }
        return CDRF_DODEFAULT;
    }
    if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    GetClientRect(lv, &c);
    ListView_GetItemRect(lv, i, &r, LVIR_BOUNDS);
    r.left = 0; r.right = c.right;
    w = r.right - r.left;
    sel = (ListView_GetItemState(lv, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    b = CreateSolidBrush(sel ? CLR_TINT : CLR_SURFACE);
    FillRect(cd->nmcd.hdc, &r, b);
    DeleteObject(b);
    if (i) fill(cd->nmcd.hdc, r.left + S(16), r.top, w - S(32), S(1), g_brush_line);
    ListView_GetItemText(lv, i, 0, src, 512);
    ListView_GetItemText(lv, i, 1, type, 32);
    ListView_GetItemText(lv, i, 2, state, 96);
    pac_columns(w, &xt, &xs);
    text_at(cd->nmcd.hdc, r.left + S(16), r.top, xt - S(32), r.bottom - r.top, src,
            CLR_TEXT, g_font, DT_LEFT | DT_PATH_ELLIPSIS);
    text_at(cd->nmcd.hdc, r.left + xt, r.top, xs - xt - S(16), r.bottom - r.top, type,
            CLR_MUTED, g_font_meta, DT_LEFT);
    text_at(cd->nmcd.hdc, r.left + xs, r.top, S(180), r.bottom - r.top, state,
            CLR_MUTED, g_font_meta, DT_LEFT | DT_END_ELLIPSIS);
    return CDRF_SKIPDEFAULT;
}
