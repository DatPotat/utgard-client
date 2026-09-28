/*
 * Utgard client - Placement of every control on every page.
 */

#include "coremanifest.h"
#include "ui.h"

/* Every page is laid out in its own coordinates, from 0 at the sidebar's
   edge; this shifts the lot right by the sidebar. The one place that places
   the sidebar itself (nav_layout) lives in ui_theme.c, outside this macro. */
#define MoveWindow(h, x, y, w, hh, r) MoveWindow((h), (x) + g_ox, (y), (w), (hh), (r))

/* Where a page's own content starts: below the tab strip on the routing
   pages (sites, applications, PAC and their sub-pages), else at the top. */
int tabs_top(void)
{
    int routing = g_page == PAGE_APPS || g_page == PAGE_PAC || g_page == PAGE_PICK ||
                  g_page == PAGE_EDIT || (g_page == PAGE_HOSTS && g_hosts_mode == HOSTS_VPN);
    return routing ? S(64) : S(8);
}

/* Places a control in page coordinates; handed to other modules. */
static void place(HWND h, int x, int y, int w, int hh)
{
    MoveWindow(h, x, y, w, hh, TRUE);
}

/* Back on the left and the main action on the right of the footer bar,
   centred in its height, each as wide as its caption. */
static void footer_buttons(HWND back, HWND save, const RECT *c)
{
    int y = c->bottom - FOOTER_H + (FOOTER_H - S(40)) / 2;
    int wb = caption_width(back) + S(32), ws = caption_width(save) + S(32);
    MoveWindow(back, PAD, y, wb, S(40), TRUE);
    MoveWindow(save, c->right - PAD - ws, y, ws, S(40), TRUE);
}

static void tip_rect(TTTOOLINFOW *ti)
{
    if (!IsRectEmpty(&ti->rect)) OffsetRect(&ti->rect, g_ox, 0);
    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)ti);
}

/* Width of a button's caption in the font buttons are drawn with. */
int caption_width(HWND b)
{
    wchar_t text[64];
    SIZE    size = { 0, 0 };
    int     n = GetWindowTextW(b, text, 64);
    HDC     dc = GetDC(b);
    HGDIOBJ old;
    if (!dc) return S(100);
    old = SelectObject(dc, g_font_bold);
    GetTextExtentPoint32W(dc, text, n, &size);
    SelectObject(dc, old);
    ReleaseDC(b, dc);
    return size.cx;
}

/* Buttons left to right from PAD with the same gap between every two. Each
   is its caption plus the same padding; the padding shrinks (down to S(8))
   when the row would not fit in avail, and never exceeds S(28). */
static void button_row(const HWND *buttons, int count, int y, int avail)
{
    int i, x = PAD, text = 0, pad, widths[8];
    if (count > 8) count = 8;
    for (i = 0; i < count; i++) text += widths[i] = caption_width(buttons[i]);
    pad = (avail - S(8) * (count - 1) - text) / count;
    if (pad > S(28)) pad = S(28);
    if (pad < S(8))  pad = S(8);
    for (i = 0; i < count; i++) {
        MoveWindow(buttons[i], x, y, widths[i] + pad, S(40), TRUE);
        x += widths[i] + pad + S(8);
    }
}

/* Settings page: "Запуск" and "Подключение" on the left, "Внешний вид" and
   the folded "Для опытных" on the right; one column in a narrow window.
   A label sits above its drop-down, so neither is ever cut short. */
void settings_geometry(const RECT *c, set_geo *g)
{
    int w = c->right - PAD * 2, one = w < S(600);
    g->y0     = S(96);
    g->colw   = one ? w : (w - S(24)) / 2;
    g->lx     = PAD;
    g->rx     = one ? PAD : PAD + g->colw + S(24);
    g->launch = g->y0;
    g->conn   = g->launch + S(26) + S(144) + S(12) + S(40) + S(16);
    g->look   = one ? g->conn + S(26) + S(144) + S(16) : g->y0;
    g->adv    = g->look + S(26) + S(72) + S(16);
}

static void layout_settings(HWND hwnd, const RECT *rc)
{
    set_geo g;
    int     sp = (g_page == PAGE_SETTINGS) ? SW_SHOW : SW_HIDE;
    int     ap = sp == SW_SHOW && g_set_adv_open ? SW_SHOW : SW_HIDE;
    int     cw, x, y, k;
    HWND    checks[3];

    settings_geometry(rc, &g);
    cw = g.colw - S(32);
    checks[0] = g_set_auto; checks[1] = g_set_tray; checks[2] = g_set_upd;
    for (k = 0; k < 3; k++) {
        MoveWindow(checks[k], g.lx + S(16), g.launch + S(26) + k * S(48) + S(1), cw, S(46), TRUE);
        ShowWindow(checks[k], sp);
    }
    MoveWindow(g_set_upd_now, g.lx, g.launch + S(26) + S(144) + S(12),
               caption_width(g_set_upd_now) + S(32), S(40), TRUE);
    ShowWindow(g_set_upd_now, sp);

    /* The combo boxes only hold the choices; the fields on the page are
       the app-drawn drop-downs (ui_popup.c). */
    MoveWindow(g_sel[SEL_DNS],   g.lx + S(16), g.conn + S(26) + S(32), cw, S(32), TRUE);
    MoveWindow(g_sel[SEL_SUB],   g.lx + S(16), g.conn + S(26) + S(104), cw, S(32), TRUE);
    MoveWindow(g_sel[SEL_THEME], g.rx + S(16), g.look + S(26) + S(32), cw, S(32), TRUE);
    ShowWindow(g_sel[SEL_DNS], sp);
    ShowWindow(g_sel[SEL_SUB], sp);
    ShowWindow(g_sel[SEL_THEME], sp);
    ShowWindow(g_set_dns, SW_HIDE);
    ShowWindow(g_set_sub, SW_HIDE);
    ShowWindow(g_set_theme, SW_HIDE);

    SetWindowTextW(g_set_adv, g_set_adv_open ? L"Скрыть" : L"Сеть, журнал, версии ядер");
    SetPropW(g_set_adv, L"utgard.round", (HANDLE)(INT_PTR)(g_set_adv_open ? 1 : 3));
    MoveWindow(g_set_adv, g.rx + S(1), g.adv + S(26) + S(1), g.colw - S(2), S(46), TRUE);
    ShowWindow(g_set_adv, sp);
    y = g.adv + S(26) + S(48);
    MoveWindow(g_set_mtu,   g.rx + S(16), y + S(34), S(100), S(28), TRUE);
    MoveWindow(g_sel[SEL_LOG],   g.rx + S(16), y + S(72) + S(32), cw, S(32), TRUE);
    MoveWindow(g_sel[SEL_STACK], g.rx + S(16), y + S(144) + S(32), cw, S(32), TRUE);
    ShowWindow(g_set_mtu, ap);
    ShowWindow(g_sel[SEL_LOG], ap);
    ShowWindow(g_sel[SEL_STACK], ap);
    ShowWindow(g_set_log, SW_HIDE);
    ShowWindow(g_set_stack, SW_HIDE);
    {
        int k;
        for (k = 0; k < SEL_COUNT; k++) InvalidateRect(g_sel[k], NULL, FALSE);
    }
    {
        wchar_t sb[48], awg[64];
        StringCchPrintfW(sb, 48, L"sing-box %s", CORE_SINGBOX.version);
        StringCchPrintfW(awg, 64, g_awg_ready ? L"AmneziaWG %s" : L"AmneziaWG %s (не загружен)",
                         CORE_AWG.version);
        SetWindowTextW(g_set_v_singbox, sb);
        SetWindowTextW(g_set_v_awg, awg);
        x = g.rx + S(16); y += S(216) + S(12);
        MoveWindow(g_set_v_utgard,  x, y, S(100), S(24), TRUE);  x += S(100) + S(12);
        MoveWindow(g_set_v_singbox, x, y, S(110), S(24), TRUE);  x += S(110) + S(12);
        MoveWindow(g_set_v_awg,     x, y, g.colw - (x - g.rx) - S(16), S(24), TRUE);
        ShowWindow(g_set_v_utgard,  ap);
        ShowWindow(g_set_v_singbox, ap);
        ShowWindow(g_set_v_awg,     ap);
    }
    ShowWindow(g_set_back, SW_HIDE);
    ShowWindow(g_set_save, SW_HIDE);

    if (g_tip) {
        /* "Что это?" beside the MTU, stack and DNS labels. */
        TTTOOLINFOW ti;
        int         ys[3], xs[3], shown[3];
        ZeroMemory(&ti, sizeof ti);
        ti.cbSize = sizeof ti;
        ti.hwnd   = hwnd;
        y = g.adv + S(26) + S(48);
        xs[0] = g.rx; ys[0] = y + S(8);          shown[0] = ap == SW_SHOW;
        xs[1] = g.rx; ys[1] = y + S(144) + S(8); shown[1] = ap == SW_SHOW;
        xs[2] = g.lx; ys[2] = g.conn + S(26) + S(8); shown[2] = sp == SW_SHOW;
        for (k = 0; k < 3; k++) {
            ti.uId = (UINT_PTR)(4 + k);
            SetRectEmpty(&ti.rect);
            if (shown[k]) {
                ti.rect.left = xs[k] + g.colw - S(96); ti.rect.top = ys[k];
                ti.rect.right = ti.rect.left + S(80); ti.rect.bottom = ys[k] + S(22);
            }
            tip_rect(&ti);
        }
    }
}

/* Connection page: the state block with the button in it, and on the right
   (or below, in a narrow window) the server row and the three routing rows.
   The painter draws the cards around the rows from the same numbers. */
void connect_geometry(const RECT *c, conn_geo *g)
{
    int w = c->right - PAD * 2;
    g->one_col = w < S(600);
    g->colw    = g->one_col ? w : (w - S(24)) / 2;
    g->block.left = PAD; g->block.top = S(80);
    g->block.right = PAD + g->colw;
    g->block.bottom = g->block.top + (g->one_col ? S(200) : S(260));
    g->rx = g->one_col ? PAD : PAD + g->colw + S(24);
    g->ry = g->one_col ? g->block.bottom + S(16) : S(80);
}

static void layout_utgard(HWND hwnd, const RECT *rc)
{
    conn_geo g;
    int      on = (g_page == PAGE_UTGARD) ? SW_SHOW : SW_HIDE;
    int      y, rw;
    wchar_t  text[256];
    (void)hwnd;

    connect_geometry(rc, &g);
    rw = g.colw - S(2);
    MoveWindow(g_toggle, g.block.left + S(24), g.block.bottom - S(24) - S(48),
               g.colw - S(48), S(48), TRUE);
    SetWindowTextW(g_toggle, g_vpn_on ? L"Выключить" : L"Включить VPN");
    EnableWindow(g_toggle, g_vpn_on ||
                 (!g_installing && g_prof.count > 0 && g_prof.active >= 0));

    y = g.ry + S(26);
    if (g_prof.active >= 0 && g_prof.active < g_prof.count) {
        wchar_t name[128];
        to_wide(g_prof.items[g_prof.active].link.name, name, 128);
        if (g_ping[g_prof.active] >= 0)
            StringCchPrintfW(text, 256, L"%s\t%d мс", name, g_ping[g_prof.active]);
        else
            StringCchPrintfW(text, 256, L"%s\t%s", name,
                             g_ping[g_prof.active] == -1 ? L"нет ответа" : L"");
    } else {
        StringCchCopyW(text, 256, g_prof.count ? L"Сервер не выбран" : L"Добавить сервер");
    }
    SetWindowTextW(g_row_server, text);
    MoveWindow(g_row_server, g.rx + S(1), y + S(1), rw, S(46), TRUE);

    y += S(48) + S(16) + S(26);
    StringCchPrintfW(text, 256, L"Сайты\t%d", g_host_count);
    SetWindowTextW(g_btn_hosts, text);
    MoveWindow(g_btn_hosts, g.rx + S(1), y + S(1), rw, S(47), TRUE);
    StringCchPrintfW(text, 256, L"Приложения\t%d", g_app_count);
    SetWindowTextW(g_btn_apps, text);
    MoveWindow(g_btn_apps, g.rx + S(1), y + S(49), rw, S(47), TRUE);
    StringCchPrintfW(text, 256, L"Правила PAC\t%d", g_pac_count);
    SetWindowTextW(g_btn_pac, text);
    MoveWindow(g_btn_pac, g.rx + S(1), y + S(97), rw, S(46), TRUE);

    SetPropW(g_row_server, L"utgard.round", (HANDLE)3);
    SetPropW(g_btn_hosts,  L"utgard.round", (HANDLE)1);
    SetPropW(g_btn_pac,    L"utgard.round", (HANDLE)2);
    ShowWindow(g_row_server, on);
    ShowWindow(g_btn_hosts, on);
    ShowWindow(g_btn_apps,  on);
    ShowWindow(g_btn_pac,   on);
    EnableWindow(g_btn_pac, !g_busy && !g_sub_busy && !g_installing);
}

/* Servers page: the title row carries the actions, the list fills the rest. */
static void layout_servers(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    int  sp = (g_page == PAGE_SERVERS) ? SW_SHOW : SW_HIDE;
    HWND acts[4];
    int  i, x;
    (void)hwnd;

    acts[0] = g_prof_add; acts[1] = g_prof_sub; acts[2] = g_ping_now; acts[3] = g_prof_del;
    x = c.right - PAD;
    for (i = 0; i < 4; i++) {
        int w = caption_width(acts[i]) + S(32);
        x -= w;
        MoveWindow(acts[i], x, S(24), w, S(40), TRUE);
        x -= S(12);
        ShowWindow(acts[i], sp);
    }
    MoveWindow(g_plist, PAD + S(1), S(141), c.right - PAD * 2 - S(2),
               c.bottom - S(24) - S(142) - S(5), TRUE);
    ShowWindow(g_plist, sp);
    EnableWindow(g_prof_del, !g_sub_busy && profile_selected() >= 0);
    EnableWindow(g_prof_add, !g_sub_busy);
    EnableWindow(g_prof_sub, !g_sub_busy);
    EnableWindow(g_ping_now, !g_ping_busy && g_prof.count > 0);
    {
        BOOL idle = !g_busy && !g_sub_busy;
        if ((EnableWindow(g_plist, idle) != 0) == (idle != 0))
            InvalidateRect(g_plist, NULL, TRUE);
    }
}

/* Routing pages: the three tabs across the top. */
static void layout_routing_tabs(const RECT *rc)
{
    HWND tabs[3];
    int  i, x = PAD, show = tabs_top() > S(8) ? SW_SHOW : SW_HIDE;
    (void)rc;
    tabs[0] = g_tab_sites; tabs[1] = g_tab_apps; tabs[2] = g_tab_pac;
    for (i = 0; i < 3; i++) {
        int w = caption_width(tabs[i]) + S(24);
        MoveWindow(tabs[i], x, S(16), w, S(40), TRUE);
        x += w + S(8);
        ShowWindow(tabs[i], show);
        InvalidateRect(tabs[i], NULL, FALSE);
    }
}

/* zapret page: the state block and the strategies on the left, VPN
   compatibility, the two filters and the lists on the right. */
void zapret_geometry(const RECT *c, zap_geo *g)
{
    int w = c->right - PAD * 2;
    g->banner = S(96);
    g->y0  = g_zap_dirty ? S(160) : S(96);
    g->lw  = (w - S(24)) * 45 / 100;
    g->rx  = PAD + g->lw + S(24);
    g->rw  = c->right - PAD - g->rx;
    g->status.left = PAD; g->status.top = g->y0;
    g->status.right = PAD + g->lw; g->status.bottom = g->y0 + S(128);
    g->strat  = g->status.bottom + S(16);
    g->search = g->strat + S(26);
    g->list_top = g->search + S(48);
    g->start  = c->bottom - S(24) - S(40);
    g->list_bottom = g->start - S(12);
    g->compat  = g->y0;
    g->filters = g->compat + S(26) + S(48) + S(16);
    g->lists   = g->filters + S(26) + S(152) + S(16);
}

static void chip_row(HWND *chips, int n, int x, int y, int selected)
{
    int k;
    for (k = 0; k < n; k++) {
        int w = caption_width(chips[k]) + S(24);
        if (k == selected) SetPropW(chips[k], L"utgard.checked", (HANDLE)1);
        else RemovePropW(chips[k], L"utgard.checked");
        MoveWindow(chips[k], x, y, w, S(32), TRUE);
        InvalidateRect(chips[k], NULL, FALSE);
        x += w + S(6);
    }
}

static void layout_zapret(HWND hwnd, const RECT *rc)
{
    zap_geo g;
    int     on = g_page == PAGE_ZAPRET && g_zap.valid ? SW_SHOW : SW_HIDE;
    int     running = g_status.mode != ZAPRET_OFF, k, w;
    (void)hwnd;

    /* Tips: hovering either filter's row (its label and its choices) or the
       IPSet and hosts rows explains what the setting does. */
    if (g_tip) {
        TTTOOLINFOW ti;
        zap_geo     t;
        int         tops[4], hs[4];
        zapret_geometry(rc, &t);
        tops[0] = t.filters + S(26);        hs[0] = S(76);
        tops[1] = t.filters + S(26) + S(76); hs[1] = S(76);
        tops[2] = t.lists + S(26) + S(48);  hs[2] = S(48);
        tops[3] = t.lists + S(26) + S(96);  hs[3] = S(48);
        ZeroMemory(&ti, sizeof ti);
        ti.cbSize = sizeof ti; ti.hwnd = hwnd;
        for (k = 0; k < 4; k++) {
            ti.uId = (UINT_PTR)k;
            SetRectEmpty(&ti.rect);
            if (g_page == PAGE_ZAPRET && g_zap.valid) {
                ti.rect.left = t.rx; ti.rect.top = tops[k];
                ti.rect.right = t.rx + (k < 2 ? t.rw : t.rw / 2); ti.rect.bottom = tops[k] + hs[k];
            }
            tip_rect(&ti);
        }
    }
    ShowWindow(g_zap_game, SW_HIDE);
    ShowWindow(g_zap_ipset, SW_HIDE);

    if (!g_zap.valid) {
        SetWindowTextW(g_pick_path, L"Указать папку…");
        w = caption_width(g_pick_path) + S(32);
        MoveWindow(g_pick_path, (rc->right - w) / 2, S(300), w, S(40), TRUE);
        for (k = 0; k < 4; k++) ShowWindow(g_zg[k], SW_HIDE);
        for (k = 0; k < 3; k++) ShowWindow(g_zi[k], SW_HIDE);
        ShowWindow(g_zap_search, SW_HIDE);
        ShowWindow(g_zap_again, SW_HIDE);
        ShowWindow(g_zap_list, SW_HIDE);
        return;
    }
    zapret_geometry(rc, &g);
    SetWindowTextW(g_pick_path, L"Изменить папку");
    w = caption_width(g_pick_path) + S(32);
    MoveWindow(g_pick_path, rc->right - PAD - w, S(24), w, S(40), TRUE);

    w = caption_width(g_zap_again) + S(24);
    MoveWindow(g_zap_again, rc->right - PAD - S(8) - w, g.banner + S(8), w, S(32), TRUE);
    ShowWindow(g_zap_again, on && g_zap_dirty ? SW_SHOW : SW_HIDE);

    /* Inside the state block, side by side. */
    w = (g.lw - S(48) - S(12)) / 2;
    MoveWindow(g_zap_restart, g.status.left + S(24), g.status.bottom - S(16) - S(40), w, S(40), TRUE);
    MoveWindow(g_zap_stop, g.status.left + S(24) + w + S(12), g.status.bottom - S(16) - S(40), w, S(40), TRUE);
    ShowWindow(g_zap_restart, on && running ? SW_SHOW : SW_HIDE);
    ShowWindow(g_zap_stop,    on && running ? SW_SHOW : SW_HIDE);

    search_clear_place(g_zap_search, g_zap_sclear, PAD, g.search, g.lw, place);
    ShowWindow(g_zap_sclear, on == SW_SHOW && GetWindowTextLengthW(g_zap_search) ? SW_SHOW : SW_HIDE);
    MoveWindow(g_list, PAD + S(1), g.list_top + S(1), g.lw - S(2), g.list_bottom - g.list_top - S(2) - S(5), TRUE);
    MoveWindow(g_zap_start, PAD, g.start, caption_width(g_zap_start) + S(32), S(40), TRUE);
    ShowWindow(g_zap_search, on);

    {
        int done = g_exc_known > 0 && g_exc_present >= g_exc_known;
        w = caption_width(g_zap_fix) + S(32);
        MoveWindow(g_zap_fix, g.rx + g.rw - S(8) - w, g.compat + S(26) + S(4), w, S(40), TRUE);
        ShowWindow(g_zap_fix, on && !done && g_prof.count > 0 ? SW_SHOW : SW_HIDE);
    }
    {
        static const int ipset_chip[3] = { 1, 0, 2 };   /* IPSET_LOADED, NONE, ANY -> chip */
        int im = (int)zapret_ipset_get(g_zap.path);
        chip_row(g_zg, 4, g.rx + S(16), g.filters + S(26) + S(34), (int)zapret_game_get(g_zap.path));
        chip_row(g_zi, 3, g.rx + S(16), g.filters + S(26) + S(110),
                 im >= 0 && im < 3 ? ipset_chip[im] : -1);
        for (k = 0; k < 4; k++) ShowWindow(g_zg[k], on);
        for (k = 0; k < 3; k++) ShowWindow(g_zi[k], on);
    }
    MoveWindow(g_zap_list, g.rx + S(1), g.lists + S(26) + S(1), g.rw - S(2), S(47), TRUE);
    SetPropW(g_zap_list, L"utgard.round", (HANDLE)1);
    ShowWindow(g_zap_list, on);
    w = caption_width(g_zap_ipupd) + S(24);
    MoveWindow(g_zap_ipupd, g.rx + g.rw - S(12) - w, g.lists + S(26) + S(48) + S(8), w, S(32), TRUE);
    w = caption_width(g_zap_hosts) + S(24);
    MoveWindow(g_zap_hosts, g.rx + g.rw - S(12) - w, g.lists + S(26) + S(96) + S(8), w, S(32), TRUE);
    ShowWindow(g_zap_ipupd, on);
    ShowWindow(g_zap_hosts, on);

    {
        const wchar_t *sel = selected_strategy();
        int same = sel && g_status.mode == ZAPRET_SERVICE &&
                   _wcsicmp(sel, g_status.strategy) == 0;
        EnableWindow(g_zap_stop,    running);
        EnableWindow(g_zap_restart, running);
        EnableWindow(g_zap_start,   sel != NULL && !same);
    }
}

/* Manual list editor. */
static void layout_edit(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    (void)hwnd; (void)c;
    {
        int  ep = (g_page == PAGE_EDIT);
        int  field_w = c.right - PAD * 2 - S(96);
        int  i, y;

        for (i = 0; i < ED_ROWS; i++) {
            int showN = ep && i < g_ed_ncount;
            int showP = ep && i < g_ed_pcount;

            y = TABS_H + S(96) + i * S(48);
            MoveWindow(g_ed_name[i],   PAD + S(10), y + S(4), field_w - S(20), S(32), TRUE);
            MoveWindow(g_ed_nminus[i], PAD + field_w + S(8),  y, S(40), S(40), TRUE);
            MoveWindow(g_ed_nplus[i],  PAD + field_w + S(56), y, S(40), S(40), TRUE);
            ShowWindow(g_ed_name[i],   showN ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_nplus[i],  showN ? SW_SHOW : SW_HIDE);
            /* The first row of a section has no minus: a section is never empty. */
            ShowWindow(g_ed_nminus[i], showN && i > 0 ? SW_SHOW : SW_HIDE);

            /* The path field keeps its right end for the folder button. */
            y = ed_path_top() + i * S(48);
            MoveWindow(g_ed_path[i],    PAD + S(10), y + S(4), field_w - S(40) - S(20), S(32), TRUE);
            MoveWindow(g_ed_pbrowse[i], PAD + field_w - S(36), y + S(4), S(32), S(32), TRUE);
            MoveWindow(g_ed_pminus[i],  PAD + field_w + S(8),  y, S(40), S(40), TRUE);
            MoveWindow(g_ed_pplus[i],   PAD + field_w + S(56), y, S(40), S(40), TRUE);
            ShowWindow(g_ed_path[i],    showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pbrowse[i], showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pplus[i],   showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pminus[i],  showP && i > 0 ? SW_SHOW : SW_HIDE);
        }

        footer_buttons(g_ed_back, g_ed_save, &c);
        ShowWindow(g_ed_back, ep ? SW_SHOW : SW_HIDE);
        ShowWindow(g_ed_save, ep ? SW_SHOW : SW_HIDE);
    }
}

/* Process picker. */
static void layout_pick(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    (void)hwnd; (void)c;
    {
        int pp = (g_page == PAGE_PICK) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_pk_search, PAD + S(10), TABS_H + S(72) + S(4), c.right - PAD * 2 - S(20), S(32), TRUE);
        MoveWindow(g_pk_list, PAD + S(1), TABS_H + S(128) + S(1), c.right - PAD * 2 - S(2),
                   c.bottom - FOOTER_H - S(12) - (TABS_H + S(128)) - S(2) - S(5), TRUE);
        footer_buttons(g_pk_back, g_pk_save, &c);
        ShowWindow(g_pk_search, pp);
        ShowWindow(g_pk_list,   pp);
        ShowWindow(g_pk_back,   pp);
        ShowWindow(g_pk_save,   pp);
        EnableWindow(g_pk_save, g_pk_checked_n > 0);
    }
}

static void layout_hosts(HWND hwnd, const RECT *rc)
{
    (void)hwnd;
    hl_layout(rc, place);
}

/* Applications tab: the add buttons on top, the sets below in a card. */
static void layout_apps(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    int  ap = (g_page == PAGE_APPS) ? SW_SHOW : SW_HIDE;
    HWND add[2];
    (void)hwnd;
    add[0] = g_app_pick; add[1] = g_app_manual;
    button_row(add, 2, TABS_H + S(16), c.right - PAD * 2);
    MoveWindow(g_alist, PAD + S(1), TABS_H + S(92) + S(1), c.right - PAD * 2 - S(2),
               c.bottom - S(24) - (TABS_H + S(92)) - S(2) - S(5), TRUE);
    ShowWindow(g_alist,      ap);
    ShowWindow(g_app_back,   SW_HIDE);
    ShowWindow(g_app_pick,   ap);
    ShowWindow(g_app_manual, ap);
}

/* PAC page. */
static void layout_pac(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    int  pp = (g_page == PAGE_PAC) ? SW_SHOW : SW_HIDE;
    int  selected = pp ? pac_selected() : -1;
    int  table_top = TABS_H + S(112);
    int  actions_y = c.bottom - S(24) - S(40);
    /* Source takes what the type and state leave, so the table has no dead
       strip on either side. */
    int  source_w = c.right - PAD * 2 - S(2) - S(70 + 160) - GetSystemMetrics(SM_CXVSCROLL);
    HWND controls[] = { g_pac_list, g_pac_file, g_pac_url,
                        g_pac_toggle, g_pac_refresh, g_pac_delete, g_pac_help };
    HWND add[2], actions[3];
    int  help_w = caption_width(g_pac_help) + S(4);    /* a link: no padding */
    size_t i;
    (void)hwnd;

    if (source_w < S(140)) source_w = S(140);
    /* Rows are drawn whole; the columns only need to stay inside the width
       so no horizontal bar appears. */
    ListView_SetColumnWidth(g_pac_list, 0, source_w > S(60) ? source_w - S(60) : S(60));
    ListView_SetColumnWidth(g_pac_list, 1, S(30));
    ListView_SetColumnWidth(g_pac_list, 2, S(30));
    MoveWindow(g_pac_list, PAD + S(1), table_top + S(37), c.right - PAD * 2 - S(2),
               actions_y - S(12) - table_top - S(38) - S(5), TRUE);
    add[0] = g_pac_url; add[1] = g_pac_file;
    button_row(add, 2, TABS_H + S(16), c.right - PAD * 2 - help_w - S(16));
    actions[0] = g_pac_toggle; actions[1] = g_pac_refresh; actions[2] = g_pac_delete;
    button_row(actions, 3, actions_y, c.right - PAD * 2);
    MoveWindow(g_pac_help, c.right - PAD - help_w, TABS_H + S(24), help_w, S(24), TRUE);
    for (i = 0; i < sizeof controls / sizeof controls[0]; i++) ShowWindow(controls[i], pp);
    ShowWindow(g_pac_back, SW_HIDE);
    EnableWindow(g_pac_toggle, selected >= 0 && !g_busy);
    EnableWindow(g_pac_refresh, selected >= 0 && !g_busy);
    EnableWindow(g_pac_delete, selected >= 0 && !g_busy);
    EnableWindow(g_pac_file, !g_busy);
    EnableWindow(g_pac_url, !g_busy);
}

/* zapret action buttons, shown only with a valid folder. */
static void layout_zapret_actions(HWND hwnd, const RECT *rc)
{
    int on = (g_page == PAGE_ZAPRET && g_zap.valid) ? SW_SHOW : SW_HIDE;
    (void)hwnd; (void)rc;
    ShowWindow(g_list,      on);
    ShowWindow(g_zap_start, on);
    if (!on) {
        int k;
        HWND off[] = { g_zap_fix, g_zap_stop, g_zap_restart, g_zap_search, g_zap_again, g_zap_sclear,
                       g_zap_list, g_zap_ipupd, g_zap_hosts };
        for (k = 0; k < (int)(sizeof off / sizeof off[0]); k++) ShowWindow(off[k], SW_HIDE);
        for (k = 0; k < 4; k++) ShowWindow(g_zg[k], SW_HIDE);
        for (k = 0; k < 3; k++) ShowWindow(g_zi[k], SW_HIDE);
    }
}

/* Greying while a job runs; last, so it overrides the rules above. */
static void layout_busy(HWND hwnd, const RECT *rc)
{
    RECT c = *rc;
    (void)hwnd; (void)c;
    /* While a job runs, every button that could start another is greyed.
       Done last so it overrides the ordinary rules above. */
    if (g_busy) {
        HWND busy_off[] = {
            g_toggle, g_zap_start, g_zap_stop, g_zap_restart, g_zap_fix,
            g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_pick_path, g_zap_list,
            g_h_save, g_h_tidy, g_h_back,
            g_pac_back, g_pac_file, g_pac_url, g_pac_toggle,
            g_pac_refresh, g_pac_delete,
            /* profiles are switched by index; the list itself is disabled above */
            g_prof_add, g_prof_del, g_prof_sub
        };
        size_t k;
        for (k = 0; k < sizeof busy_off / sizeof busy_off[0]; k++)
            if (busy_off[k]) EnableWindow(busy_off[k], FALSE);
    } else {
        /* g_h_save: enabled by hl_layout only when the list has changes. */
        EnableWindow(g_h_tidy, TRUE);
        EnableWindow(g_h_back, TRUE);
        EnableWindow(g_zap_game, TRUE);
        EnableWindow(g_zap_ipset, TRUE);
        EnableWindow(g_zap_ipupd, TRUE);
        EnableWindow(g_zap_hosts, TRUE);
        EnableWindow(g_pick_path, TRUE);
        EnableWindow(g_zap_list, TRUE);
    }
}

void layout(HWND hwnd)
{
    RECT client, c;
    GetClientRect(hwnd, &client);

    g_ox = SIDE_W(nav_rail(hwnd));
    c = client;
    c.right = client.right - g_ox;
    nav_layout(hwnd);

    layout_settings(hwnd, &c);
    layout_utgard(hwnd, &c);
    layout_servers(hwnd, &c);
    layout_routing_tabs(&c);
    layout_zapret(hwnd, &c);
    layout_edit(hwnd, &c);
    layout_pick(hwnd, &c);
    layout_hosts(hwnd, &c);
    layout_apps(hwnd, &c);
    layout_pac(hwnd, &c);
    layout_zapret_actions(hwnd, &c);

    ShowWindow(g_toggle,    g_page == PAGE_UTGARD ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pick_path, g_page == PAGE_ZAPRET ? SW_SHOW : SW_HIDE);

    layout_busy(hwnd, &c);
    {
        HWND lists[] = { g_list, g_plist, g_alist, g_pk_list, g_hedit, g_pac_list, g_hl_list };
        size_t k;
        for (k = 0; k < sizeof lists / sizeof lists[0]; k++) scroll_place(lists[k]);
    }

    /* children are not repainted by invalidating the parent */
    InvalidateRect(g_toggle, NULL, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}
