/* A one-field modal prompt, built in code rather than from an .rc template:
   the dialog texts are Russian, and keeping them in C avoids the code-page
   dance that Cyrillic in a resource script would need. */

#include "ask.h"

#include <string.h>
#include <stdlib.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <dwmapi.h>

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
static int                g_ask_dark;
static COLORREF           g_ask_caption, g_ask_border;
static ask_round_fn       g_round;

void ask_configure_frame(int dark, COLORREF caption, COLORREF border, ask_round_fn round)
{
    g_ask_dark = dark;
    g_ask_caption = caption;
    g_ask_border = border;
    g_round = round;
}

/* 20 and 35 are DWMWA_USE_IMMERSIVE_DARK_MODE and DWMWA_CAPTION_COLOR;
   builds that do not know an attribute ignore it. */
static void frame_theme(HWND hwnd)
{
    BOOL     dark = g_ask_dark;
    COLORREF cap  = g_ask_caption;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof cap);
}


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

static int scaled(int v) { return g_scale ? g_scale(v) : v; }

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

static void field_box(HDC dc, const RECT *r)
{
    if (g_round) g_round(dc, r, g_ask_surface_color, g_ask_border, scaled(10));
    else FrameRect(dc, r, g_ask_line);
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
            int   side = scaled(8);

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

        frame_theme(hwnd);
        /* The edit sits inside a rounded 40-pixel field, clear of its corners. */
        st->edit = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   scaled(26), scaled(24) + st->hint_h + scaled(10) + scaled(4), scaled(388), scaled(32), hwnd,
                                   (HMENU)(INT_PTR)ID_EDIT, cs->hInstance, NULL);
        SendMessageW(st->edit, EM_SETLIMITTEXT, (WPARAM)(st->cap - 1), 0);
        SendMessageW(st->edit, WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        ask_edit_center(st->edit);

        {
            HWND b;
            int  by = scaled(24) + st->hint_h + scaled(10) + scaled(40) + scaled(16);
            b = CreateWindowExW(0, L"BUTTON", L"Отмена",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                scaled(238), by, scaled(90), scaled(40), hwnd,
                                (HMENU)(INT_PTR)ID_CANCEL, cs->hInstance, NULL);
            SetWindowLongPtrW(b, GWLP_USERDATA, ASK_BTN_SECONDARY);            ask_hover_attach(b);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_ask_font, TRUE);

            b = CreateWindowExW(0, L"BUTTON", L"Добавить",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                scaled(336), by, scaled(90), scaled(40), hwnd,
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
            RECT t = { scaled(16), scaled(16), c.right - scaled(16),
                       scaled(16) + (st ? st->hint_h : scaled(20)) };
            DrawTextW(dc, st ? st->hint : L"", -1, &t, DT_LEFT | DT_WORDBREAK);
        }
        SelectObject(dc, old);

        box.left = scaled(16); box.top = scaled(24) + (st ? st->hint_h : 0) + scaled(10);
        box.right = c.right - scaled(16); box.bottom = box.top + scaled(40);
        field_box(dc, &box);

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
        RECT    t   = { 0, 0, scaled(408), 0 };
        DrawTextW(dc, hint ? hint : L"", -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
        st.hint_h = t.bottom - t.top;
        if (st.hint_h < scaled(16)) st.hint_h = scaled(16);
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }

    register_class(inst);

    want.left = 0; want.top = 0; want.right = scaled(440);
    want.bottom = scaled(24) + st.hint_h + scaled(10) + scaled(40) + scaled(16) + scaled(40) + scaled(16);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)scaled(96));
    GetWindowRect(owner, &o);

    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"UtgardAsk", title, style,
                           o.left + ((o.right - o.left) - (want.right - want.left)) / 2,
                           o.top + scaled(120),
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

/* ---- ask_steps -------------------------------------------------------- */

#define ID_STEP_YES 1101
#define ID_STEP_NO  1102

typedef struct {
    ask_step *steps;
    int       count, current, text_h, done;
    HWND      yes, no;
} steps_state;

static int steps_text_top(const steps_state *st) { return st->count > 1 ? scaled(56) : scaled(18); }

static int caption_width(HWND b)
{
    wchar_t text[64];
    SIZE    size = { 0, 0 };
    int     n = GetWindowTextW(b, text, 64);
    HDC     dc = GetDC(b);
    HGDIOBJ old;
    if (!dc) return scaled(120);
    old = SelectObject(dc, g_ask_font);
    GetTextExtentPoint32W(dc, text, n, &size);
    SelectObject(dc, old);
    ReleaseDC(b, dc);
    return size.cx + scaled(32);
}

/* Captions of the current step, right-aligned: [no] [yes]. */
static void steps_place(HWND hwnd, steps_state *st)
{
    RECT c;
    int  y = steps_text_top(st) + st->text_h + scaled(18), yw, nw;
    GetClientRect(hwnd, &c);
    SetWindowTextW(st->yes, st->steps[st->current].yes);
    SetWindowTextW(st->no,  st->steps[st->current].no);
    yw = caption_width(st->yes);
    nw = caption_width(st->no);
    MoveWindow(st->yes, c.right - scaled(16) - yw, y, yw, scaled(30), TRUE);
    MoveWindow(st->no,  c.right - scaled(16) - yw - scaled(8) - nw, y, nw, scaled(30), TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void steps_answer(HWND hwnd, steps_state *st, int answer)
{
    st->steps[st->current].answer = answer;
    if (++st->current >= st->count) { st->done = 1; return; }
    steps_place(hwnd, st);
    SetFocus(st->yes);
}

static void steps_paint(HWND hwnd, steps_state *st)
{
    PAINTSTRUCT ps;
    HDC         dc = BeginPaint(hwnd, &ps);
    RECT        c, t;
    HGDIOBJ     old;
    int         i, x = scaled(16);
    GetClientRect(hwnd, &c);
    FillRect(dc, &c, g_ask_bg);
    SetBkMode(dc, TRANSPARENT);
    old = SelectObject(dc, g_ask_font);
    if (st->count > 1) {
        for (i = 0; i < st->count; i++) {
            SIZE size = { 0, 0 };
            const wchar_t *label = st->steps[i].tab;
            COLORREF color = i == st->current ? g_ask_text
                           : (i < st->current && st->steps[i].answer) ? ask_color(ASK_OK) : g_ask_muted;
            GetTextExtentPoint32W(dc, label, (int)wcslen(label), &size);
            SetTextColor(dc, color);
            TextOutW(dc, x, scaled(16), label, (int)wcslen(label));
            if (i == st->current) {
                HBRUSH accent = CreateSolidBrush(ask_color(ASK_ACCENT));
                RECT   under = { x, scaled(16) + size.cy + scaled(6), x + size.cx, scaled(16) + size.cy + scaled(8) };
                if (accent) { FillRect(dc, &under, accent); DeleteObject(accent); }
            }
            x += size.cx + scaled(24);
        }
        t.left = 0; t.right = c.right; t.top = scaled(44); t.bottom = scaled(45);
        FillRect(dc, &t, g_ask_line);
    }
    SetTextColor(dc, g_ask_text);
    t.left = scaled(16); t.right = c.right - scaled(16);
    t.top = steps_text_top(st); t.bottom = t.top + st->text_h;
    DrawTextW(dc, st->steps[st->current].text, -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK steps_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    steps_state *st = (steps_state *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        st = (steps_state *)cs->lpCreateParams;
        frame_theme(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->no = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STEP_NO, cs->hInstance, NULL);
        st->yes = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STEP_YES, cs->hInstance, NULL);
        SetWindowLongPtrW(st->no, GWLP_USERDATA, ASK_BTN_SECONDARY);
        SetWindowLongPtrW(st->yes, GWLP_USERDATA, ASK_BTN_PRIMARY);
        ask_hover_attach(st->no);
        ask_hover_attach(st->yes);
        SendMessageW(st->no, WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        SendMessageW(st->yes, WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        steps_place(hwnd, st);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (st) steps_paint(hwnd, st);
        else DefWindowProcW(hwnd, msg, wp, lp);
        return 0;
    case WM_DRAWITEM:
        if (g_draw) g_draw((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_COMMAND:
        if (st && !st->done) {
            if (LOWORD(wp) == ID_STEP_YES) steps_answer(hwnd, st, 1);
            else if (LOWORD(wp) == ID_STEP_NO) steps_answer(hwnd, st, 0);
        }
        return 0;
    case WM_CLOSE:
        if (st) st->done = 1;       /* the rest stay "no" */
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ask_steps(HWND owner, const wchar_t *title, ask_step *steps, int count)
{
    steps_state st;
    HINSTANCE   inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    WNDCLASSEXW wc;
    HWND        hwnd;
    RECT        o, want;
    MSG         msg;
    DWORD       style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    int         i;
    static int  registered;

    if (!steps || count <= 0) return;
    if (count > ASK_STEPS_MAX) count = ASK_STEPS_MAX;
    for (i = 0; i < count; i++) steps[i].answer = 0;
    ZeroMemory(&st, sizeof st);
    st.steps = steps;
    st.count = count;

    /* The tallest question sets the height, so the window does not jump
       between tabs. */
    {
        HDC     dc  = GetDC(owner);
        HGDIOBJ old = SelectObject(dc, g_ask_font);
        for (i = 0; i < count; i++) {
            RECT t = { 0, 0, scaled(428), 0 };
            DrawTextW(dc, steps[i].text, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
            if (t.bottom - t.top > st.text_h) st.text_h = t.bottom - t.top;
        }
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }

    if (!registered) {
        ZeroMemory(&wc, sizeof wc);
        wc.cbSize        = sizeof wc;
        wc.lpfnWndProc   = steps_proc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = g_ask_bg;
        wc.lpszClassName = L"UtgardSteps";
        RegisterClassExW(&wc);
        registered = 1;
    }

    want.left = 0; want.top = 0; want.right = scaled(460);
    want.bottom = steps_text_top(&st) + st.text_h + scaled(18) + scaled(30) + scaled(16);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)scaled(96));
    GetWindowRect(owner, &o);
    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"UtgardSteps", title, style,
                           o.left + ((o.right - o.left) - (want.right - want.left)) / 2,
                           o.top + scaled(100), want.right - want.left, want.bottom - want.top,
                           owner, NULL, inst, &st);
    if (!hwnd) return;

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(st.yes);
    while (!st.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd))) {
            if (msg.wParam == VK_ESCAPE) { st.done = 1; continue; }
            if (msg.wParam == VK_RETURN) { steps_answer(hwnd, &st, GetFocus() != st.no); continue; }
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(hwnd);
}

/* ---- messages ------------------------------------------------------------ */

typedef struct {
    const wchar_t *text;
    int            text_h, done;
} msg_state;

#define ID_MSG_B1 1101

static LRESULT CALLBACK msg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    msg_state *st = (msg_state *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        frame_theme(hwnd);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        c, t;
        HGDIOBJ     old;
        GetClientRect(hwnd, &c);
        FillRect(dc, &c, g_ask_bg);
        old = SelectObject(dc, g_ask_font);
        SetTextColor(dc, g_ask_text);
        SetBkMode(dc, TRANSPARENT);
        t.left = scaled(24); t.top = scaled(24); t.right = c.right - scaled(24); t.bottom = t.top + (st ? st->text_h : 0);
        DrawTextW(dc, st ? st->text : L"", -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        if (g_draw) g_draw((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_COMMAND:
        if (st && LOWORD(wp) >= ID_MSG_B1 && LOWORD(wp) < ID_MSG_B1 + 3) st->done = LOWORD(wp) - ID_MSG_B1 + 1;
        return 0;
    case WM_CLOSE:
        if (st) st->done = -1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int ask_message(HWND owner, const wchar_t *title, const wchar_t *text,
                const wchar_t *b1, const wchar_t *b2, const wchar_t *b3)
{
    static int registered;
    HINSTANCE  inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    const wchar_t *labels[3];
    msg_state  st;
    HWND       hwnd, buttons[3];
    RECT       o, want;
    MSG        m;
    DWORD      style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    int        width = scaled(460), k, n = 0, x, by, bw[3];

    labels[0] = b1; labels[1] = b2; labels[2] = b3;
    for (k = 0; k < 3 && labels[k]; k++) n++;
    if (!n) { labels[0] = L"OK"; n = 1; }
    st.text = text ? text : L"";
    st.done = 0;
    {
        HDC     dc  = GetDC(owner);
        HGDIOBJ old = SelectObject(dc, g_ask_font);
        RECT    t   = { 0, 0, width - scaled(48), 0 };
        DrawTextW(dc, st.text, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
        st.text_h = t.bottom - t.top;
        SelectObject(dc, g_ask_font);
        for (k = 0; k < n; k++) {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, labels[k], (int)wcslen(labels[k]), &sz);
            bw[k] = sz.cx + scaled(40);
            if (bw[k] < scaled(90)) bw[k] = scaled(90);
        }
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = msg_proc; wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = g_ask_bg;
        wc.lpszClassName = L"UtgardMsg";
        RegisterClassExW(&wc);
        registered = 1;
    }
    by = scaled(24) + st.text_h + scaled(24);
    want.left = 0; want.top = 0; want.right = width; want.bottom = by + scaled(40) + scaled(20);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)scaled(96));
    GetWindowRect(owner, &o);
    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"UtgardMsg", title ? title : L"Utgard", style,
                           o.left + ((o.right - o.left) - (want.right - want.left)) / 2,
                           o.top + ((o.bottom - o.top) - (want.bottom - want.top)) / 3,
                           want.right - want.left, want.bottom - want.top, owner, NULL, inst, &st);
    if (!hwnd) return 0;
    x = width - scaled(20);
    for (k = 0; k < n; k++) {
        x -= bw[k];
        buttons[k] = CreateWindowExW(0, L"BUTTON", labels[k], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     x, by, bw[k], scaled(40), hwnd, (HMENU)(INT_PTR)(ID_MSG_B1 + k), inst, NULL);
        SetWindowLongPtrW(buttons[k], GWLP_USERDATA, k == 0 ? ASK_BTN_PRIMARY : ASK_BTN_SECONDARY);
        ask_hover_attach(buttons[k]);
        SendMessageW(buttons[k], WM_SETFONT, (WPARAM)g_ask_font, TRUE);
        x -= scaled(10);
    }
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(buttons[0]);
    while (!st.done && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { st.done = -1; continue; }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && GetFocus() && GetParent(GetFocus()) == hwnd) {
            st.done = GetDlgCtrlID(GetFocus()) - ID_MSG_B1 + 1;
            continue;
        }
        if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetActiveWindow(owner);
    return st.done > 0 ? st.done : 0;
}
