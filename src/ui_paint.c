/*
 * Utgard client - WM_PAINT: page backgrounds, profile/app/process rows, strategy list.
 */

#include "ui.h"

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

void on_paint(HWND hwnd)
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

void draw_strategy(const DRAWITEMSTRUCT *d)
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

static const wchar_t *proto_label(link_proto p)
{
    switch (p) {
    case LINK_VLESS: return L"VLESS";
    case LINK_HY2:   return L"Hysteria2";
    case LINK_SS:    return L"Shadowsocks";
    case LINK_TROJAN: return L"Trojan";
    case LINK_VMESS: return L"VMess";
    case LINK_WG:    return L"WireGuard";
    default:         return L"?";
    }
}

void draw_profile(const DRAWITEMSTRUCT *d)
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

/* Each row carries its own actions, so nothing has to be selected first. */
int app_zone_at(int x, int width)
{
    int del = width - S(12) - S(APP_ZONE_DELETE);
    int tog = del - S(10) - S(APP_ZONE_TOGGLE);

    if (x >= del) return 2;
    if (x >= tog) return 1;
    return 0;
}

void draw_app_row(const DRAWITEMSTRUCT *d)
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

void draw_pick_row(const DRAWITEMSTRUCT *d)
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
