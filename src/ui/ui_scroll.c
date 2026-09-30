/*
 * Utgard client - Scroll bars in the theme, and rounded corners for the
 * scrolling controls that sit inside cards.
 *
 * The native scroll bar stays - it is what the wheel and the keyboard move -
 * but a thin bar of our own is laid over it, drawn from the control's own
 * scroll info and driving the control back through its scroll messages. The
 * control gets a rounded region so its square corners do not show through
 * the card's curve; the overlay is rounded on its right-hand corners.
 */

#include "ui.h"

static const wchar_t SB_PROP[] = L"utgard.sb";

typedef struct { HWND target; int drag, grab, hot; } sb_state;

static int is_listview(HWND h)
{
    wchar_t cls[32];
    GetClassNameW(h, cls, 32);
    return !_wcsicmp(cls, WC_LISTVIEWW);
}

/* The thumb within a track of the given height: 0 when nothing scrolls. */
static int thumb(HWND target, int track, int *top, int *height)
{
    SCROLLINFO si;
    int range;
    ZeroMemory(&si, sizeof si);
    si.cbSize = sizeof si;
    si.fMask  = SIF_ALL;
    if (!GetScrollInfo(target, SB_VERT, &si)) return 0;
    range = si.nMax - si.nMin + 1;
    if (si.nPage == 0 || (int)si.nPage >= range) return 0;
    *height = (int)((long long)track * (int)si.nPage / range);
    if (*height < scaled(24)) *height = scaled(24);
    *top = (int)((long long)(track - *height) * (si.nPos - si.nMin) / (range - (int)si.nPage));
    return 1;
}

static void scroll_to(HWND target, int pos)
{
    SCROLLINFO si;
    ZeroMemory(&si, sizeof si);
    si.cbSize = sizeof si;
    si.fMask  = SIF_ALL;
    GetScrollInfo(target, SB_VERT, &si);
    if (pos < si.nMin) pos = si.nMin;
    if (pos > si.nMax - (int)si.nPage + 1) pos = si.nMax - (int)si.nPage + 1;
    if (is_listview(target)) {
        /* A report list view scrolls by pixels: rows times the row height. */
        RECT r;
        if (ListView_GetItemRect(target, 0, &r, LVIR_BOUNDS))
            ListView_Scroll(target, 0, (pos - si.nPos) * (r.bottom - r.top));
    } else {
        SendMessageW(target, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, pos), 0);
        SendMessageW(target, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    }
}

static LRESULT CALLBACK sb_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    sb_state *st = (sb_state *)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT      c;
    int       top, height, track;

    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return TRUE;
    }
    if (!st) return DefWindowProcW(h, m, w, l);
    GetClientRect(h, &c);
    track = c.bottom - scaled(8);
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        /* Drawn off-screen and copied in one go: no half-drawn frame. */
        PAINTSTRUCT ps;
        HDC     dc  = BeginPaint(h, &ps);
        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH  b = CreateSolidBrush(CLR_SURFACE);
        FillRect(mem, &c, b);
        DeleteObject(b);
        if (thumb(st->target, track, &top, &height)) {
            RECT t;
            int  wdt = (st->hot || st->drag) ? scaled(8) : scaled(6);
            t.left = (c.right - wdt) / 2; t.right = t.left + wdt;
            t.top = scaled(4) + top; t.bottom = t.top + height;
            rounded_r(mem, &t, (st->hot || st->drag) ? CLR_MUTED : CLR_BORDER,
                      (st->hot || st->drag) ? CLR_MUTED : CLR_BORDER, wdt / 2);
        }
        BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!st->hot) {
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            st->hot = 1;
            InvalidateRect(h, NULL, FALSE);
        }
        if (st->drag && thumb(st->target, track, &top, &height)) {
            SCROLLINFO si;
            int y = GET_Y_LPARAM(l) - scaled(4) - st->grab;
            ZeroMemory(&si, sizeof si);
            si.cbSize = sizeof si; si.fMask = SIF_ALL;
            GetScrollInfo(st->target, SB_VERT, &si);
            if (track > height)
                scroll_to(st->target, si.nMin + (int)((long long)y * (si.nMax - si.nMin + 1 - (int)si.nPage) /
                                                      (track - height)));
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        st->hot = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (thumb(st->target, track, &top, &height)) {
            int y = GET_Y_LPARAM(l) - scaled(4);
            if (y >= top && y < top + height) {
                st->drag = 1;
                st->grab = y - top;
                SetCapture(h);
            } else {
                SendMessageW(st->target, WM_VSCROLL, y < top ? SB_PAGEUP : SB_PAGEDOWN, 0);
            }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (st->drag) { st->drag = 0; ReleaseCapture(); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_MOUSEWHEEL:
        return SendMessageW(st->target, m, w, l);
    case WM_NCDESTROY:
        free(st);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* Messages after which the target may have scrolled or changed length. */
static int moves(UINT m, WPARAM w)
{
    return m == WM_VSCROLL || m == WM_MOUSEWHEEL || m == WM_KEYDOWN || m == WM_CHAR ||
           m == WM_SIZE || m == WM_SETTEXT || m == WM_TIMER || m == WM_LBUTTONUP ||
           (m == WM_MOUSEMOVE && (w & MK_LBUTTON)) ||
           (m >= 0x0180 && m <= 0x01AF) ||      /* LB_* */
           (m >= 0x00B0 && m <= 0x00DF) ||      /* EM_* */
           (m >= LVM_FIRST && m <= LVM_FIRST + 0xFF);
}

static LRESULT CALLBACK target_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    LRESULT r = DefSubclassProc(h, m, w, l);
    (void)ref;
    if (moves(m, w)) scroll_sync(h);
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, target_proc, id);
    return r;
}

void scroll_attach(HWND target)
{
    static int registered;
    HINSTANCE  inst = (HINSTANCE)GetWindowLongPtrW(target, GWLP_HINSTANCE);
    sb_state  *st;
    HWND       sb;

    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = sb_proc; wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.lpszClassName = L"UtgardScroll";
        RegisterClassExW(&wc);
        registered = 1;
    }
    st = (sb_state *)calloc(1, sizeof *st);
    if (!st) return;
    st->target = target;
    /* The control must not paint over its sibling bar. */
    SetWindowLongPtrW(target, GWL_STYLE, GetWindowLongPtrW(target, GWL_STYLE) | WS_CLIPSIBLINGS);
    sb = CreateWindowExW(0, L"UtgardScroll", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                         GetParent(target), NULL, inst, st);
    if (!sb) { free(st); return; }
    SetPropW(target, SB_PROP, sb);
    SetWindowSubclass(target, target_proc, 9, 0);
}

static const wchar_t SBV_PROP[] = L"utgard.sbv";   /* 1 + whether the bar shows */

/* Placement: the control's region is rounded and, while it scrolls, stops
   short of its own scroll bar - so the native bar is never drawn at all and
   ours, in that strip, has nothing to fight with. Done when the control is
   laid out and when its bar appears or goes, not on every scroll. */
void scroll_place(HWND target)
{
    HWND sb = (HWND)GetPropW(target, SB_PROP);
    RECT r;
    int  w, h, bw, show;
    HRGN rg;

    if (!sb) return;
    GetWindowRect(target, &r);
    MapWindowPoints(NULL, GetParent(target), (POINT *)&r, 2);
    w = r.right - r.left; h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    bw   = GetSystemMetricsForDpi(SM_CXVSCROLL, (UINT)g_dpi);
    show = (GetWindowLongPtrW(target, GWL_STYLE) & WS_VSCROLL) != 0 && IsWindowVisible(target);
    SetPropW(target, SBV_PROP, (HANDLE)(INT_PTR)(1 + show));

    rg = CreateRoundRectRgn(0, 0, w + 1, h + 1, scaled(22), scaled(22));
    if (show) {
        HRGN cut = CreateRectRgn(0, 0, w - bw, h);
        CombineRgn(rg, rg, cut, RGN_AND);
        DeleteObject(cut);
    }
    SetWindowRgn(target, rg, TRUE);

    if (!show) { ShowWindow(sb, SW_HIDE); return; }
    SetWindowPos(sb, HWND_TOP, r.right - bw, r.top, bw, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    /* Only the right-hand corners are rounded: the region starts left of it. */
    SetWindowRgn(sb, CreateRoundRectRgn(-scaled(24), 0, bw + 1, h + 1, scaled(22), scaled(22)), TRUE);
}

/* After anything that may have scrolled the control: redraw our bar, and
   re-place only if the native bar came or went. */
void scroll_sync(HWND target)
{
    HWND sb = (HWND)GetPropW(target, SB_PROP);
    int  was, show;

    if (!sb) return;
    was  = (int)(INT_PTR)GetPropW(target, SBV_PROP) - 1;
    show = (GetWindowLongPtrW(target, GWL_STYLE) & WS_VSCROLL) != 0 && IsWindowVisible(target);
    if (was != show) { scroll_place(target); return; }
    if (show) InvalidateRect(sb, NULL, FALSE);
}
