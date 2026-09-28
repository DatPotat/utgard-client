/*
 * Utgard client - The site lists (VPN and zapret) as a list: search as you
 * type, letter groups and a letter strip, ticks, delete with undo, a bulk
 * add panel with a live preview, and the plain text editor as the second
 * step. The text editor (g_hedit) stays the one store of the list: every
 * operation rewrites its text, so saving, compiling and "save changes?"
 * work exactly as before.
 */

#include "ui.h"
#include "hostlist.h"

HWND g_hl_search, g_hl_list, g_hl_add, g_hl_mode, g_hl_del, g_hl_clear, g_hl_undo;
HWND g_hl_paste, g_hl_add_ok, g_hl_add_cancel, g_hl_move, g_hl_sclear, g_zap_sclear, g_hl_tback;

static hl_entry      g_all[LIST_MAX];
static int           g_all_n;
static int           g_view[LIST_MAX];
static int           g_view_n;
static unsigned char g_chk[LIST_MAX];
static int           g_chk_n;
static int           g_text_mode;     /* the plain editor instead of the list */
static int           g_adding;        /* the bulk add panel is open */
static hl_preview    g_pv;
static wchar_t      *g_undo_text;     /* the text before the last delete */
static int           g_undo_n;
static RECT          g_strip;         /* letter strip, page coordinates */

static char g_u8[LIST_TEXT_MAX], g_u8b[LIST_TEXT_MAX], g_u8p[LIST_TEXT_MAX];

static int zapret(void) { return g_hosts_mode == HOSTS_ZAPRET; }

static int edit_utf8(HWND edit, char *out)
{
    static wchar_t w[LIST_TEXT_MAX];
    GetWindowTextW(edit, w, LIST_TEXT_MAX);
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, LIST_TEXT_MAX, NULL, NULL) != 0;
}

static int set_edit_utf8(const char *text)
{
    static wchar_t w[LIST_TEXT_MAX];
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, w, LIST_TEXT_MAX)) return 0;
    SetWindowTextW(g_hedit, w);
    SendMessageW(g_hedit, EM_SETMODIFY, TRUE, 0);
    return 1;
}

static void filter(void)
{
    char    needle[LIST_ENTRY_MAX];
    wchar_t w[LIST_ENTRY_MAX];
    int     i;

    GetWindowTextW(g_hl_search, w, LIST_ENTRY_MAX);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, needle, sizeof needle, NULL, NULL);
    g_view_n = 0;
    for (i = 0; i < g_all_n; i++)
        if (hostlist_match(&g_all[i], needle)) g_view[g_view_n++] = i;
    SendMessageW(g_hl_list, LB_SETCOUNT, (WPARAM)g_view_n, 0);
    InvalidateRect(g_hl_list, NULL, FALSE);
}

/* After any change of the text: parse again, forget the ticks (line
   numbers moved), filter with the search as it stands. */
void hl_reload(void)
{
    g_all_n = 0;
    if (edit_utf8(g_hedit, g_u8)) {
        g_all_n = hostlist_parse(g_u8, zapret(), g_all, LIST_MAX);
        hostlist_sort(g_all, g_all_n);
    }
    memset(g_chk, 0, sizeof g_chk);
    g_chk_n = 0;
    filter();
}

/* Opening a list: back to the list view, search and panels cleared. */
void hl_open(void)
{
    g_text_mode = 0;
    g_adding = 0;
    free(g_undo_text);
    g_undo_text = NULL;
    SetWindowTextW(g_hl_search, L"");
    hl_reload();
}

static wchar_t first_letter(const hl_entry *e)
{
    wchar_t c = (wchar_t)(unsigned char)e->host[0];
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - L'a' + L'A');
    if (c >= L'A' && c <= L'Z') return c;
    return c >= L'0' && c <= L'9' ? L'#' : L'…';
}

void hl_create(HWND hwnd)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    g_hl_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HL_SEARCH, inst, NULL);
    ask_edit_center(g_hl_search);
    g_hl_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                WS_CHILD | WS_VSCROLL | WS_TABSTOP | LBS_NODATA |
                                LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HL_LIST, inst, NULL);
    SendMessageW(g_hl_list, LB_SETITEMHEIGHT, 0, (LPARAM)S(40));
    list_hover_attach(g_hl_list);
    SetWindowSubclass(g_hl_list, hl_list_proc, 7, 0);
    g_hl_paste = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                                 ES_AUTOVSCROLL | ES_WANTRETURN,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HL_PASTE, inst, NULL);
    SendMessageW(g_hl_paste, EM_SETLIMITTEXT, (WPARAM)(LIST_TEXT_MAX / 4), 0);
    g_hl_add        = make_button(hwnd, L"Добавить…", ID_HL_ADD, BK_PRIMARY);
    g_hl_mode       = make_button(hwnd, L"Редактировать как текст", ID_HL_MODE, BK_SECONDARY);
    g_hl_del        = make_button_on(hwnd, L"Удалить", ID_HL_DEL, BK_DANGER, BACK_TINT);
    g_hl_clear      = make_button_on(hwnd, L"Снять выбор", ID_HL_CLEAR, BK_SECONDARY, BACK_TINT);
    g_hl_undo       = make_button_on(hwnd, L"Вернуть", ID_HL_UNDO, BK_SECONDARY, BACK_TINT);
    g_hl_move       = make_button_on(hwnd, L"", ID_HL_MOVE, BK_SECONDARY, BACK_TINT);
    g_hl_sclear     = make_button(hwnd, L"×", ID_HL_SCLEAR, BK_SECONDARY);
    g_hl_tback      = make_button(hwnd, L"Назад", ID_HL_TBACK, BK_SECONDARY);
    g_zap_sclear    = make_button(hwnd, L"×", ID_ZAP_SCLEAR, BK_SECONDARY);
    g_hl_add_ok     = make_button(hwnd, L"Добавить", ID_HL_ADD_OK, BK_PRIMARY);
    g_hl_add_cancel = make_button(hwnd, L"Отмена", ID_HL_ADD_CANCEL, BK_SECONDARY);
}

void hl_fonts(void)
{
    SendMessageW(g_hl_search, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hl_paste,  WM_SETFONT, (WPARAM)g_font_mono, TRUE);
    SendMessageW(g_hl_list,   LB_SETITEMHEIGHT, 0, (LPARAM)S(40));
    SendMessageW(g_hl_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Найти в списке");
}

/* ---- geometry: layout and paint share it ------------------------------ */

typedef struct { int title, bar, count, add, sel, list, end; } hl_geo;

static void geometry(const RECT *c, hl_geo *g)
{
    int y = TABS_H + S(16);
    g->title = y;
    if (zapret()) y += S(48);
    g->bar = y;          y += S(48);
    g->count = y;        y += S(28);
    g->add = y;          if (g_adding && !g_text_mode) y += S(120) + S(12) + S(40) + S(16);
    g->list = y;
    /* The selection and undo bar sits under the list, not above it: rows
       must not jump under the pointer when the first tick appears. */
    g->sel = c->bottom - S(24) - S(48);
    g->end = (!g_text_mode && (g_chk_n || g_undo_text)) ? g->sel - S(8) : c->bottom - S(24);
}

static int btn_w(HWND b)
{
    wchar_t t[64];
    SIZE    sz = { 0, 0 };
    int     n = GetWindowTextW(b, t, 64);
    HDC     dc = GetDC(b);
    HGDIOBJ old = SelectObject(dc, g_font_bold);
    GetTextExtentPoint32W(dc, t, n, &sz);
    SelectObject(dc, old);
    ReleaseDC(b, dc);
    return sz.cx + S(32);
}

void hl_layout(const RECT *c, void (*place)(HWND, int, int, int, int))
{
    hl_geo g;
    int    on = g_page == PAGE_HOSTS, list_mode = on && !g_text_mode;
    int    right = c->right - PAD, x, w;

    geometry(c, &g);
    SetWindowTextW(g_hl_mode, g_text_mode ? L"Показать списком" : L"Редактировать как текст");

    /* Toolbar: search on the left, the buttons from the right edge. */
    x = right;
    w = btn_w(g_h_save);  x -= w; place(g_h_save, x, g.bar, w, S(40)); x -= S(12);
    if (g_text_mode) {
        w = btn_w(g_h_tidy); x -= w; place(g_h_tidy, x, g.bar, w, S(40)); x -= S(12);
        place(g_hl_tback, PAD, g.bar, btn_w(g_hl_tback), S(40));
    } else {
        w = btn_w(g_hl_mode); x -= w; place(g_hl_mode, x, g.bar, w, S(40)); x -= S(12);
        w = btn_w(g_hl_add);  x -= w; place(g_hl_add, x, g.bar, w, S(40)); x -= S(12);
        search_clear_place(g_hl_search, g_hl_sclear, PAD, g.bar, x - PAD, place);
    }
    /* zapret's list is a step inside the zapret page: its own way back. */
    place(g_h_back, PAD, g.title - S(4), btn_w(g_h_back), S(40));

    if (g_adding) {
        wchar_t cap[48];
        StringCchPrintfW(cap, 48, g_pv.added ? L"Добавить %d" : L"Добавить", g_pv.added);
        SetWindowTextW(g_hl_add_ok, cap);
        place(g_hl_paste, PAD, g.add, (c->right - PAD * 2) * 11 / 20, S(120));
        w = btn_w(g_hl_add_ok);
        place(g_hl_add_ok, PAD, g.add + S(132), w, S(40));
        place(g_hl_add_cancel, PAD + w + S(12), g.add + S(132), btn_w(g_hl_add_cancel), S(40));
        EnableWindow(g_hl_add_ok, g_pv.added > 0);
    }
    {
        int bx = right - S(12);
        HWND bar[3];
        int  k, n = 0;
        SetWindowTextW(g_hl_move, zapret() ? L"Перенести в VPN" : L"Перенести в zapret");
        if (g_chk_n) { bar[n++] = g_hl_move; bar[n++] = g_hl_del; bar[n++] = g_hl_clear; }
        else if (g_undo_text) bar[n++] = g_hl_undo;
        for (k = n - 1; k >= 0; k--) {
            w = btn_w(bar[k]); bx -= w;
            place(bar[k], bx, g.sel + S(8), w, S(32));
            bx -= S(8);
        }
    }

    place(g_hl_list, PAD + S(28) + S(1), g.list + S(1), c->right - PAD * 2 - S(28) - S(2),
          g.end - g.list - S(2) - S(5));
    /* Below the line that explains the format, not over it. */
    place(g_hedit, PAD + S(1), g.count + S(28), c->right - PAD * 2 - S(2),
          c->bottom - S(24) - g.count - S(28) - S(2) - S(5));
    g_strip.left = PAD; g_strip.right = PAD + S(18);
    g_strip.top = g.list; g_strip.bottom = g.end;

    ShowWindow(g_hl_search, list_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_sclear, list_mode && GetWindowTextLengthW(g_hl_search) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_list,   list_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_add,    list_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_mode,   list_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_tback,  on && g_text_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_h_save,    on ? SW_SHOW : SW_HIDE);
    ShowWindow(g_h_tidy,    on && g_text_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hedit,     on && g_text_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_h_back,    on && zapret() && !g_text_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_paste,      list_mode && g_adding ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_add_ok,     list_mode && g_adding ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_add_cancel, list_mode && g_adding ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_del,   list_mode && g_chk_n ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_clear, list_mode && g_chk_n ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_move,  list_mode && g_chk_n ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hl_undo,  list_mode && !g_chk_n && g_undo_text ? SW_SHOW : SW_HIDE);
    EnableWindow(g_h_save, !g_busy && SendMessageW(g_hedit, EM_GETMODIFY, 0, 0));
}

void hl_paint(HDC dc, const RECT *c)
{
    hl_geo  g;
    wchar_t line[256];
    int     w = c->right - PAD * 2;

    geometry(c, &g);
    if (zapret()) {
        int bw = g_text_mode ? 0 : btn_w(g_h_back) + S(16);
        text_at(dc, PAD + bw, g.title, w - bw, S(32), L"Сайты для zapret", CLR_TEXT, title_font(), DT_LEFT);
        text_at(dc, PAD, g.title, w, S(32),
                g_busy && g_busy_text ? g_busy_text : L"", CLR_MUTED, g_font_small, DT_RIGHT);
    }

    if (g_text_mode) {
        StringCchCopyW(line, 256, zapret()
            ? L"По одному в строке. «^» в начале — только сам домен, одно слово — вся зона. Строки с # — комментарии."
            : L"По одному в строке, поддомены подхватываются сами. Подсети — 198.51.100.0/24. Строки с # — комментарии.");
        text_at(dc, PAD + S(4), g.count, w, S(20), line, CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
        {
            RECT r = { PAD, g.count + S(28) - S(1), c->right - PAD, c->bottom - S(24) };
            rounded_r(dc, &r, CLR_SURFACE, CLR_LINE, S(12));
        }
        return;
    }

    search_frame(dc, g_hl_search);
    if (GetWindowTextLengthW(g_hl_search) > 0)
        StringCchPrintfW(line, 256, L"Найдено %d из %d", g_view_n, g_all_n);
    else if (zapret())
        StringCchPrintfW(line, 256, L"%d %s. Запись охватывает поддомены; «^» — только сам домен; одно слово — вся зона.",
                         g_all_n, plural_ru(g_all_n, L"запись", L"записи", L"записей"));
    else
        StringCchPrintfW(line, 256, L"%d %s, каждый вместе с поддоменами",
                         g_all_n, plural_ru(g_all_n, L"сайт", L"сайта", L"сайтов"));
    if (SendMessageW(g_hedit, EM_GETMODIFY, 0, 0))
        StringCchCatW(line, 256, L" · есть несохранённые изменения");
    text_at(dc, PAD + S(4), g.count, w, S(20), line, CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);

    if (g_adding) {
        int  px = PAD + (c->right - PAD * 2) * 11 / 20 + S(24), pw = c->right - PAD - px, y = g.add;
        RECT r = { PAD - S(1), g.add - S(1), PAD + (c->right - PAD * 2) * 11 / 20 + S(1), g.add + S(121) };
        rounded_r(dc, &r, CLR_SURFACE, CLR_BORDER, S(8));
        text_at(dc, px, y, pw, S(20), L"Вставьте адреса: по одному в строке, ссылки и целые списки тоже подойдут.",
                CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
        y += S(28);
        StringCchPrintfW(line, 256, L"Будет добавлено: %d", g_pv.added);
        text_at(dc, px, y, pw, S(20), line, CLR_TEXT, g_font_bold, DT_LEFT); y += S(22);
        StringCchPrintfW(line, 256, L"Уже в списке: %d", g_pv.duplicate);
        text_at(dc, px, y, pw, S(20), line, CLR_MUTED, g_font_small, DT_LEFT); y += S(20);
        StringCchPrintfW(line, 256, L"Уже покрыты доменом из списка: %d", g_pv.covered);
        text_at(dc, px, y, pw, S(20), line, CLR_MUTED, g_font_small, DT_LEFT); y += S(20);
        if (g_pv.invalid) {
            wchar_t ex[64];
            MultiByteToWideChar(CP_UTF8, 0, g_pv.bad[0], -1, ex, 64);
            StringCchPrintfW(line, 256, L"Не похожи на адрес: %d (например, «%s»)", g_pv.invalid, ex);
        } else {
            StringCchCopyW(line, 256, L"Не похожи на адрес: 0");
        }
        text_at(dc, px, y, pw, S(20), line, g_pv.invalid ? CLR_WARN : CLR_MUTED,
                g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    }

    if (g_chk_n || g_undo_text) {
        RECT r = { PAD, g.sel, c->right - PAD, g.sel + S(48) };
        rounded_r(dc, &r, CLR_TINT, CLR_TINT, S(12));
        if (g_chk_n)
            StringCchPrintfW(line, 256, L"Выбрано: %d", g_chk_n);
        else
            StringCchPrintfW(line, 256, L"Удалено: %d %s", g_undo_n,
                             plural_ru(g_undo_n, L"запись", L"записи", L"записей"));
        text_at(dc, PAD + S(16), g.sel, w, S(48), line, CLR_TEXT, g_font_bold, DT_LEFT);
    }

    {
        RECT r = { PAD + S(28), g.list, c->right - PAD, g.end };
        rounded_r(dc, &r, CLR_SURFACE, CLR_LINE, S(12));
    }
    /* The letter strip: letters that begin some entry in the view are
       dark, the rest faint; a click jumps to the first of the letter. */
    {
        static const wchar_t L_[] = L"#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        int      k, n = 27, have[27] = { 0 }, i, step;
        for (i = 0; i < g_view_n; i++) {
            wchar_t f = first_letter(&g_all[g_view[i]]);
            if (f == L'#') have[0] = 1;
            else if (f >= L'A' && f <= L'Z') have[f - L'A' + 1] = 1;
        }
        step = (g_strip.bottom - g_strip.top) / n;
        if (step > S(20)) step = S(20);
        if (step < S(13)) return;       /* too short to read: search does the job */
        for (k = 0; k < n; k++) {
            wchar_t s[2] = { L_[k], 0 };
            text_at(dc, g_strip.left, g_strip.top + k * step, g_strip.right - g_strip.left, step,
                    s, have[k] ? CLR_TEXT : CLR_LINE, g_font_small_bold, DT_CENTER);
        }
    }
}

/* ---- rows ---------------------------------------------------------------- */

void draw_host_row(const DRAWITEMSTRUCT *d)
{
    RECT     r = d->rcItem;
    int      vi = (int)d->itemID, hot, ei;
    HBRUSH   br;
    wchar_t  host[LIST_ENTRY_MAX];
    int      box = S(18), bx, by;
    RECT     b;

    if (vi < 0 || vi >= g_view_n) return;
    ei  = g_view[vi];
    hot = vi == list_hot_row(d->hwndItem);
    br  = CreateSolidBrush(g_chk[ei] ? CLR_TINT : hot ? CLR_HOVER : CLR_SURFACE);
    FillRect(d->hDC, &r, br);
    DeleteObject(br);
    if (vi) fill(d->hDC, r.left + S(40), r.top, r.right - r.left - S(40), S(1), g_brush_line);

    if (vi == 0 || first_letter(&g_all[g_view[vi - 1]]) != first_letter(&g_all[ei])) {
        wchar_t s[2] = { first_letter(&g_all[ei]), 0 };
        text_at(d->hDC, r.left, r.top, S(40), r.bottom - r.top, s, CLR_MUTED,
                g_font_bold, DT_CENTER);
    }

    bx = r.left + S(40); by = r.top + (r.bottom - r.top - box) / 2;
    b.left = bx; b.top = by; b.right = bx + box; b.bottom = by + box;
    rounded_r(d->hDC, &b, g_chk[ei] ? CLR_ACCENT : CLR_SURFACE,
              g_chk[ei] ? CLR_ACCENT : CLR_BORDER, S(5));
    if (g_chk[ei]) gfx_icon(d->hDC, ICON_CHECK, bx + S(2), by + S(2), box - S(4), CLR_ON_ACCENT);

    MultiByteToWideChar(CP_UTF8, 0, g_all[ei].host, -1, host, LIST_ENTRY_MAX);
    text_at(d->hDC, bx + box + S(14), r.top, r.right - bx - box - S(14) - S(180),
            r.bottom - r.top, host, CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    if (g_all[ei].flags) {
        const wchar_t *chip = (g_all[ei].flags & HL_EXACT) ? L"только домен" : L"вся зона";
        RECT c;
        c.right = r.right - S(48); c.left = c.right - S(110);
        c.top = r.top + S(9); c.bottom = r.bottom - S(9);
        rounded_r(d->hDC, &c, CLR_TINT, CLR_TINT, S(22));
        text_at(d->hDC, c.left, c.top, c.right - c.left, c.bottom - c.top, chip,
                CLR_TEXT, g_font_small_bold, DT_CENTER);
    }
    /* The bin, the row's own delete. */
    gfx_icon(d->hDC, ICON_TRASH, r.right - S(24) - S(9), (r.top + r.bottom) / 2 - S(9), S(18),
             hot ? CLR_TEXT : CLR_MUTED);
}

/* ---- operations ---------------------------------------------------------- */

static void delete_lines(HWND hwnd, const int *lines, int n)
{
    wchar_t *keep;
    if (n <= 0 || !edit_utf8(g_hedit, g_u8)) return;
    if (!hostlist_remove(g_u8, lines, n, g_u8b, LIST_TEXT_MAX)) {
        problem(hwnd, L"Не удалось изменить список");
        return;
    }
    keep = (wchar_t *)malloc(LIST_TEXT_MAX * sizeof(wchar_t));
    if (keep) GetWindowTextW(g_hedit, keep, LIST_TEXT_MAX);
    if (!set_edit_utf8(g_u8b)) { free(keep); return; }
    free(g_undo_text);
    g_undo_text = keep;
    g_undo_n = n;
    hl_reload();
    layout(hwnd);
}

static int is_ip_like(const char *h)
{
    for (; *h; h++)
        if ((*h >= 'a' && *h <= 'z') || (*h >= 'A' && *h <= 'Z') || (unsigned char)*h >= 0x80)
            return 0;   /* a letter, Latin or not (IDN): a name, not an address */
    return 1;
}

/* Move the ticked entries to the other list and save both at once, so
   neither is left half-changed: zapret reads its file itself, the VPN list
   is compiled by the usual job. What the other side cannot hold stays. */
static void move_checked(HWND hwnd)
{
    static int  lines[LIST_MAX];
    static char target[LIST_TEXT_MAX];
    wchar_t     path[MAX_PATH * 2], msg[256];
    size_t      len = 0;
    int         i, n = 0, kept = 0, widened = 0, to_zapret = !zapret(), rd;
    hl_preview  pv;

    if (g_busy) return;
    if (to_zapret && !zapret_user_list(path, MAX_PATH * 2)) {
        problem(hwnd, L"Сначала укажите папку zapret на странице zapret.");
        return;
    }
    if (!to_zapret && !root_file(L"list\\hosts", path, MAX_PATH * 2)) return;

    g_u8p[0] = 0;
    for (i = 0; i < g_all_n; i++) {
        const hl_entry *e = &g_all[i];
        size_t k;
        if (!g_chk[i]) continue;
        if ((to_zapret && is_ip_like(e->host)) || (!to_zapret && (e->flags & HL_ZONE))) {
            kept++;
            continue;
        }
        if (!to_zapret && (e->flags & HL_EXACT)) widened++;
        k = strlen(e->host);
        if (len + k + 3 >= LIST_TEXT_MAX) break;
        memcpy(g_u8p + len, e->host, k);
        memcpy(g_u8p + len + k, "\r\n", 3);
        len += k + 2;
        lines[n++] = e->line;
    }
    if (!n) {
        problem(hwnd, to_zapret ? L"IP-адреса и подсети в zapret не переносятся: его список — для доменов."
                                : L"Записи «вся зона» в VPN не переносятся: там это отправило бы в туннель всю зону.");
        return;
    }

    rd = file_read(path, target, LIST_TEXT_MAX, NULL);
    if (rd == FILE_READ_PARTIAL) { problem(hwnd, L"Список-получатель слишком велик"); return; }
    if (!rd) target[0] = 0;
    if (!hostlist_add(target, g_u8p, to_zapret, g_u8b, LIST_TEXT_MAX, &pv)) {
        problem(hwnd, L"Список-получатель слишком велик");
        return;
    }
    if (to_zapret && !file_write(path, g_u8b, strlen(g_u8b))) {
        problem(hwnd, L"Не удалось записать список zapret");
        return;
    }

    if (!edit_utf8(g_hedit, g_u8) || !hostlist_remove(g_u8, lines, n, g_u8p, LIST_TEXT_MAX) ||
        !set_edit_utf8(g_u8p)) {
        problem(hwnd, L"Не удалось изменить список");
        return;
    }
    free(g_undo_text);          /* the other file has changed: no undo */
    g_undo_text = NULL;
    if (to_zapret) {
        hosts_save_start(hwnd, 0);
    } else {
        hosts_save_zapret(hwnd);
        hosts_save_vpn_text(hwnd, g_u8b);
    }
    hl_reload();
    layout(hwnd);

    StringCchPrintfW(msg, 256, L"Перенесено: %d.", n);
    if (kept) {
        wchar_t more[96];
        StringCchPrintfW(more, 96, L" Осталось на месте: %d — %s.", kept,
                         to_zapret ? L"IP-адреса и подсети" : L"записи «вся зона»");
        StringCchCatW(msg, 256, more);
    }
    if (widened) {
        wchar_t more[96];
        StringCchPrintfW(more, 96, L" У %d записей «^» снят: в VPN запись всегда включает поддомены.", widened);
        StringCchCatW(msg, 256, more);
    }
    if (kept || widened) problem(hwnd, msg);
}

static void preview(void)
{
    if (!edit_utf8(g_hedit, g_u8) || !edit_utf8(g_hl_paste, g_u8p)) return;
    hostlist_add(g_u8, g_u8p, zapret(), NULL, 0, &g_pv);
}

int hl_command(HWND hwnd, int id, int code)
{
    switch (id) {
    case ID_HL_SEARCH:
        if (code == EN_CHANGE) { filter(); layout(hwnd); }
        return 1;
    case ID_HL_SCLEAR:
        SetWindowTextW(g_hl_search, L"");   /* EN_CHANGE refilters */
        SetFocus(g_hl_search);
        return 1;
    case ID_ZAP_SCLEAR:
        SetWindowTextW(g_zap_search, L"");
        SetFocus(g_zap_search);
        return 1;
    case ID_HL_PASTE:
        if (code == EN_CHANGE) { preview(); layout(hwnd); }
        return 1;
    case ID_HL_ADD:
        g_adding = 1;
        SetWindowTextW(g_hl_paste, L"");
        memset(&g_pv, 0, sizeof g_pv);
        layout(hwnd);
        SetFocus(g_hl_paste);
        return 1;
    case ID_HL_ADD_CANCEL:
        g_adding = 0;
        layout(hwnd);
        return 1;
    case ID_HL_ADD_OK:
        if (!edit_utf8(g_hedit, g_u8) || !edit_utf8(g_hl_paste, g_u8p) ||
            !hostlist_add(g_u8, g_u8p, zapret(), g_u8b, LIST_TEXT_MAX, &g_pv) ||
            !set_edit_utf8(g_u8b)) {
            problem(hwnd, L"Список слишком велик");
            return 1;
        }
        g_adding = 0;
        hl_reload();
        layout(hwnd);
        return 1;
    case ID_HL_TBACK:
    case ID_HL_MODE:
        g_text_mode = !g_text_mode;
        g_adding = 0;
        if (!g_text_mode) hl_reload();
        layout(hwnd);
        SetFocus(g_text_mode ? g_hedit : g_hl_search);
        return 1;
    case ID_HL_DEL: {
        static int lines[LIST_MAX];
        int i, n = 0;
        for (i = 0; i < g_all_n; i++) if (g_chk[i]) lines[n++] = g_all[i].line;
        delete_lines(hwnd, lines, n);
        return 1;
    }
    case ID_HL_MOVE:
        move_checked(hwnd);
        return 1;
    case ID_HL_CLEAR:
        memset(g_chk, 0, sizeof g_chk);
        g_chk_n = 0;
        layout(hwnd);
        return 1;
    case ID_HL_UNDO:
        if (g_undo_text) {
            SetWindowTextW(g_hedit, g_undo_text);
            SendMessageW(g_hedit, EM_SETMODIFY, TRUE, 0);
            free(g_undo_text);
            g_undo_text = NULL;
            hl_reload();
            layout(hwnd);
        }
        return 1;
    }
    return 0;
}

/* A click on a row ticks it; on the bin at its right end it deletes it. */
LRESULT CALLBACK hl_list_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) {
        LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
        int     vi = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        RECT    rc;
        GetClientRect(h, &rc);
        SetFocus(h);
        if (vi >= 0 && vi < g_view_n) {
            int ei = g_view[vi];
            if (GET_X_LPARAM(l) >= rc.right - S(48)) {
                delete_lines(GetParent(h), &g_all[ei].line, 1);
            } else {
                g_chk[ei] = !g_chk[ei];
                g_chk_n += g_chk[ei] ? 1 : -1;
                layout(GetParent(h));
            }
        }
        return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, hl_list_proc, id);
    return DefSubclassProc(h, m, w, l);
}

/* A click on the page: the letter strip scrolls to the letter. */
void hl_click(int x, int y)
{
    int i, k, step;
    static const wchar_t L_[] = L"#ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if (g_page != PAGE_HOSTS || g_text_mode) return;
    x -= g_ox;
    if (x < g_strip.left || x >= g_strip.right || y < g_strip.top) return;
    step = (g_strip.bottom - g_strip.top) / 27;
    if (step > S(20)) step = S(20);
    if (step < S(13)) return;
    k = (y - g_strip.top) / step;
    if (k < 0 || k >= 27) return;
    for (i = 0; i < g_view_n; i++)
        if (first_letter(&g_all[g_view[i]]) == L_[k]) {
            SendMessageW(g_hl_list, LB_SETTOPINDEX, (WPARAM)i, 0);
            return;
        }
}

int hl_over_strip(int x, int y)
{
    int step;
    if (g_page != PAGE_HOSTS || g_text_mode) return 0;
    x -= g_ox;
    step = (g_strip.bottom - g_strip.top) / 27;
    if (step > S(20)) step = S(20);
    return step >= S(13) && x >= g_strip.left && x < g_strip.right &&
           y >= g_strip.top && y < g_strip.top + 27 * step;
}

/* Leaving the list with unsaved edits asks first, as the old Back did. */
int page_leave_ok(HWND hwnd)
{
    int answer;
    if (g_page != PAGE_HOSTS || !SendMessageW(g_hedit, EM_GETMODIFY, 0, 0)) return 1;
    answer = modal_box(hwnd, L"Сохранить изменения в списке?", L"Utgard",
                       MB_ICONQUESTION | MB_YESNOCANCEL);
    if (answer == IDCANCEL) return 0;
    if (answer == IDYES) {
        if (zapret()) hosts_save_zapret(hwnd);
        else hosts_save_start(hwnd, 0);
    } else {
        SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    }
    return 1;
}

/* A search field with its clear button at the right end, inside the
   field's frame: the edit is narrowed so typed text never runs under it. */
void search_clear_place(HWND edit, HWND clear, int x, int y, int w,
                        void (*place)(HWND, int, int, int, int))
{
    int has = GetWindowTextLengthW(edit) > 0;
    /* The edit sits inside the field's frame, clear of its rounded corners. */
    place(edit, x + S(10), y + S(4), (has ? w - S(40) : w) - S(20), S(32));
    place(clear, x + w - S(36), y + S(6), S(28), S(28));
}

/* The frame of a search field, drawn by the page around the edit and its
   clear button so the two read as one field. */
void search_frame(HDC dc, HWND edit)
{
    field_frame(dc, edit, GetWindowTextLengthW(edit) > 0 ? S(40) : 0);
}

/* The frame of any input field; extra_right takes in a button that sits
   inside the field at its right end. */
void field_frame(HDC dc, HWND edit, int extra_right)
{
    RECT r;
    if (!IsWindowVisible(edit)) return;
    GetWindowRect(edit, &r);
    MapWindowPoints(NULL, GetParent(edit), (POINT *)&r, 2);
    OffsetRect(&r, -g_ox, 0);             /* the page paints in its own coordinates */
    InflateRect(&r, S(10), S(4));         /* the edit is inset by this much */
    r.right += extra_right;
    rounded_r(dc, &r, CLR_SURFACE, CLR_BORDER, S(10));
}
