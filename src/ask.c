/* A one-field modal prompt, built in code rather than from an .rc template:
   the dialog texts are Russian, and keeping them in C avoids the code-page
   dance that Cyrillic in a resource script would need. */

#include "ask.h"

#include <string.h>
#include <stdlib.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <commctrl.h>

#define ID_EDIT   1001
#define ID_OK     1002
#define ID_CANCEL 1003

typedef struct {
    const wchar_t *hint;
    int            hint_h;      /* measured, not guessed */
    wchar_t       *out;
    size_t         cap;
    int            done;     /* 0 running, 1 accepted, -1 cancelled */
    HWND           edit;
} ask_state;

static ask_draw_button_fn g_draw;
static ask_metric_fn      g_scale;
static HFONT              g_ask_font, g_ask_small;
static HBRUSH             g_ask_bg, g_ask_surface, g_ask_line;
static COLORREF           g_ask_text, g_ask_muted, g_ask_surface_color;

void ask_configure(ask_draw_button_fn draw, ask_metric_fn scale,
                   HFONT font, HFONT small_font,
                   HBRUSH bg, HBRUSH surface, HBRUSH line,
                   COLORREF text, COLORREF muted, COLORREF surface_color)
{
    g_draw       = draw;
    g_scale      = scale;
    g_ask_font   = font;
    g_ask_small  = small_font;
    g_ask_bg     = bg;
    g_ask_surface = surface;
    g_ask_line   = line;
    g_ask_text   = text;
    g_ask_muted  = muted;
    g_ask_surface_color = surface_color;
}

static int S(int v) { return g_scale ? g_scale(v) : v; }

static const wchar_t HOT_PROP[] = L"utgard.hot";

static LRESULT CALLBACK hover_proc(HWND h, UINT m, WPARAM w, LPARAM l,
                                   UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, HOT_PROP)) {
            TRACKMOUSEEVENT t;
            t.cbSize      = sizeof t;
            t.dwFlags     = TME_LEAVE;
            t.hwndTrack   = h;
            t.dwHoverTime = 0;
            TrackMouseEvent(&t);
            SetPropW(h, HOT_PROP, (HANDLE)1);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(h, HOT_PROP);
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_SETCURSOR:
        if (IsWindowEnabled(h)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_ENABLE:
    case WM_SHOWWINDOW:
        /* Disabled or hidden while under the pointer: WM_MOUSELEAVE may never
           come, and the button would stay lit. */
        if (!w) { RemovePropW(h, HOT_PROP); InvalidateRect(h, NULL, FALSE); }
        break;
    case WM_NCDESTROY:
        RemovePropW(h, HOT_PROP);
        RemoveWindowSubclass(h, hover_proc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

void ask_hover_attach(HWND button) { SetWindowSubclass(button, hover_proc, 1, 0); }

/* ---- single-line edits: text centred vertically ----------------------- */

static int edit_line_height(HWND h)
{
    HFONT       f  = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HDC         dc = GetDC(h);
    HGDIOBJ     old = SelectObject(dc, f ? (HGDIOBJ)f : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW tm;

    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return tm.tmHeight;
}

static LRESULT CALLBACK center_proc(HWND h, UINT m, WPARAM w, LPARAM l,
                                    UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (m) {
    case WM_NCCALCSIZE:
        if (w) {
            RECT *r    = &((NCCALCSIZE_PARAMS *)l)->rgrc[0];
            int   line = edit_line_height(h);
            int   high = r->bottom - r->top;
            int   top  = (high - line) / 2;
            int   side = S(8);

            if (top < 0) top = 0;
            r->top    += top;
            r->bottom  = r->top + (line < high ? line : high);
            r->left   += side;
            r->right  -= side;
            if (r->right < r->left) r->right = r->left;
            return 0;
        }
        break;

    case WM_NCPAINT: {
        HDC   dc = GetWindowDC(h);
        RECT  wr, cr;
        POINT o = { 0, 0 };

        GetWindowRect(h, &wr);
        GetClientRect(h, &cr);
        ClientToScreen(h, &o);
        OffsetRect(&cr, o.x - wr.left, o.y - wr.top);
        OffsetRect(&wr, -wr.left, -wr.top);
        ExcludeClipRect(dc, cr.left, cr.top, cr.right, cr.bottom);
        FillRect(dc, &wr, g_ask_surface);
        ReleaseDC(h, dc);
        return 0;
    }

    case WM_NCHITTEST:
        /* The margin belongs to the field: a click there focuses it. */
        return HTCLIENT;

    case WM_SETFONT: {
        /* The margin depends on the line height, so it is recomputed. */
        LRESULT res = DefSubclassProc(h, m, w, l);
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return res;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(h, center_proc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

void ask_edit_center(HWND edit)
{
    SetWindowSubclass(edit, center_proc, 3, 0);
    SetWindowPos(edit, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
int  ask_is_hot(HWND button)       { return GetPropW(button, HOT_PROP) != NULL; }

static COLORREF g_colors[ASK_COLOR_COUNT];

void ask_configure_colors(const COLORREF *colors)
{
    int i;
    for (i = 0; i < ASK_COLOR_COUNT; i++) g_colors[i] = colors[i];
}

COLORREF ask_color(int which)
{
    return (which >= 0 && which < ASK_COLOR_COUNT) ? g_colors[which] : 0;
}

HFONT ask_font(int small_one) { return small_one ? g_ask_small : g_ask_font; }

HBRUSH ask_brush(int which)
{
    if (which == ASK_SURFACE) return g_ask_surface;
    if (which == ASK_LINE)    return g_ask_line;
    return g_ask_bg;
}




static LRESULT CALLBACK ask_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ask_state *st = (ask_state *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        st = (ask_state *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        st->edit = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(16), S(24) + st->hint_h + S(10), S(408), S(30), hwnd,
                                   (HMENU)(INT_PTR)ID_EDIT, cs->hInstance, NULL);
        SendMessageW(st->edit, EM_SETLIMITTEXT, (WPARAM)(st->cap - 1), 0);
        SendMessageW(st->edit, WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        ask_edit_center(st->edit);

        {
            HWND b;
            int  by = S(24) + st->hint_h + S(10) + S(30) + S(16);
            b = CreateWindowExW(0, L"BUTTON", L"Отмена",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                S(250), by, S(84), S(30), hwnd,
                                (HMENU)(INT_PTR)ID_CANCEL, cs->hInstance, NULL);
            SetWindowLongPtrW(b, GWLP_USERDATA, ASK_BTN_SECONDARY);            ask_hover_attach(b);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_ask_font, TRUE);

            b = CreateWindowExW(0, L"BUTTON", L"Добавить",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                S(342), by, S(84), S(30), hwnd,
                                (HMENU)(INT_PTR)ID_OK, cs->hInstance, NULL);
            SetWindowLongPtrW(b, GWLP_USERDATA, ASK_BTN_PRIMARY);            ask_hover_attach(b);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        }
        return 0;
    }

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, g_ask_text);
        SetBkColor((HDC)wp, g_ask_surface_color);
        return (LRESULT)g_ask_surface;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        c, box;
        HGDIOBJ     old;

        GetClientRect(hwnd, &c);
        FillRect(dc, &c, g_ask_bg);

        old = SelectObject(dc, g_ask_small);
        SetTextColor(dc, g_ask_muted);
        SetBkMode(dc, TRANSPARENT);
        {
            RECT t = { S(16), S(16), c.right - S(16),
                       S(16) + (st ? st->hint_h : S(20)) };
            DrawTextW(dc, st ? st->hint : L"", -1, &t, DT_LEFT | DT_WORDBREAK);
        }
        SelectObject(dc, old);

        /* Two pixels outside the 30-pixel field on every side. */
        box.left = S(14); box.top = S(22) + (st ? st->hint_h : 0) + S(10);
        box.right = c.right - S(14); box.bottom = box.top + S(34);
        FrameRect(dc, &box, g_ask_line);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        if (g_draw) g_draw((const DRAWITEMSTRUCT *)lp);
        return TRUE;

    case WM_COMMAND:
        if (!st) return 0;
        if (LOWORD(wp) == ID_OK) {
            GetWindowTextW(st->edit, st->out, (int)st->cap);
            st->done = 1;
        } else if (LOWORD(wp) == ID_CANCEL) {
            st->done = -1;
        }
        return 0;

    case WM_CLOSE:
        if (st) st->done = -1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_class(HINSTANCE inst)
{
    static int done;
    WNDCLASSEXW wc;

    if (done) return;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = ask_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_ask_bg;
    wc.lpszClassName = L"UtgardAsk";
    RegisterClassExW(&wc);
    done = 1;
}

int ask_string(HWND owner, const wchar_t *title, const wchar_t *hint,
               const wchar_t *initial, wchar_t *out, size_t cap)
{
    ask_state st;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    HWND      hwnd;
    RECT      o, want;
    MSG       msg;
    DWORD     style = WS_POPUP | WS_CAPTION | WS_SYSMENU;

    if (!out || cap == 0) return 0;
    out[0] = L'\0';

    st.hint = hint;
    st.out  = out;
    st.cap  = cap;
    st.done = 0;
    st.edit = NULL;

    /* Measure the hint instead of assuming two lines fit: the text is Russian,
       the font follows the system DPI, and a guess clips it. */
    {
        HDC     dc  = GetDC(owner);
        HGDIOBJ old = SelectObject(dc, g_ask_small);
        RECT    t   = { 0, 0, S(408), 0 };
        DrawTextW(dc, hint ? hint : L"", -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
        st.hint_h = t.bottom - t.top;
        if (st.hint_h < S(16)) st.hint_h = S(16);
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }

    register_class(inst);

    want.left = 0; want.top = 0; want.right = S(440);
    want.bottom = S(24) + st.hint_h + S(10) + S(30) + S(16) + S(30) + S(16);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)S(96));
    GetWindowRect(owner, &o);

    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"UtgardAsk", title, style,
                           o.left + ((o.right - o.left) - (want.right - want.left)) / 2,
                           o.top + S(120),
                           want.right - want.left, want.bottom - want.top,
                           owner, NULL, inst, &st);
    if (!hwnd) return 0;

    if (initial && initial[0]) {
        SetWindowTextW(st.edit, initial);
        SendMessageW(st.edit, EM_SETSEL, 0, -1);
    }

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(st.edit);

    while (!st.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.hwnd &&
            (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd))) {
            if (msg.wParam == VK_RETURN) {
                GetWindowTextW(st.edit, out, (int)cap);
                st.done = 1;
                continue;
            }
            if (msg.wParam == VK_ESCAPE) { st.done = -1; continue; }
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(hwnd);

    return st.done == 1 && out[0] != L'\0';
}

