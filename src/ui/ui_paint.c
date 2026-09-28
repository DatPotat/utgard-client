/*
 * Utgard client - WM_PAINT: page backgrounds, profile/app/process rows, strategy list.
 */

#include "coremanifest.h"
#include "ui.h"

/* ---- painting ------------------------------------------------------- */

static void card(HDC dc, int x, int y, int w, int h)
{
    RECT r = { x, y, x + w, y + h };
    rounded_r(dc, &r, CLR_SURFACE, CLR_LINE, S(12));
}

static void group_label(HDC dc, int x, int y, int w, const wchar_t *s)
{
    text_at(dc, x + S(4), y, w, S(18), s, CLR_MUTED, g_font_small_bold, DT_LEFT);
}

static void paint_connect(HDC dc, const RECT *c)
{
    conn_geo g;
    int      k = vpn_state(), inv;
    RECT     b, t;
    wchar_t  body[400];
    const wchar_t *title;
    COLORREF tc, bc;

    text_at(dc, PAD, S(24), c->right - PAD * 2, S(32), L"Подключение",
            CLR_TEXT, title_font(), DT_LEFT);
    connect_geometry(c, &g);
    b = g.block;

    if (k == STATE_ON) {
        rounded_r(dc, &b, CLR_ACCENT, CLR_ACCENT, S(16));
        title = L"Включено";
        StringCchPrintfW(body, 400, L"Через VPN: %d %s, %d %s%s. Остальное — напрямую.",
                         g_host_count, plural_ru(g_host_count, L"сайт", L"сайта", L"сайтов"),
                         g_app_count, plural_ru(g_app_count, L"приложение", L"приложения", L"приложений"),
                         g_pac_count ? L", правила PAC" : L"");
    } else if (k == STATE_ERROR) {
        rounded_r(dc, &b, CLR_SURFACE, CLR_WARN, S(16));
        InflateRect(&b, -S(1), -S(1));
        rounded_r(dc, &b, CLR_SURFACE, CLR_WARN, S(15));
        InflateRect(&b, S(1), S(1));
        title = L"Нет соединения";
        StringCchCopyW(body, 400, g_awg_lost
            ? L"Туннель AmneziaWG пропал. Выключите и снова включите VPN."
            : L"Переключение не удалось — работает прежний сервер.");
    } else if (k == STATE_WAIT) {
        rounded_r(dc, &b, CLR_SURFACE, CLR_BORDER, S(16));
        title = L"Подключение…";
        StringCchCopyW(body, 400, g_busy_text ? g_busy_text : L"");
    } else {
        rounded_r(dc, &b, CLR_SURFACE, CLR_LINE, S(16));
        title = L"Выключено";
        if (g_installing)
            StringCchCopyW(body, 400, g_installing == 2 ? L"Загрузка AmneziaWG…" : L"Загрузка sing-box…");
        else if (!singbox_present())
            StringCchCopyW(body, 400, L"Нет sing-box: без него VPN не включится.");
        else if (!g_prof.count)
            StringCchCopyW(body, 400, L"Добавьте сервер, чтобы включить VPN.");
        else
            StringCchCopyW(body, 400, L"Весь трафик идёт напрямую.");
    }
    inv = k == STATE_ON;
    tc  = inv ? CLR_ON_ACCENT : (k == STATE_ERROR ? CLR_WARN : CLR_TEXT);
    bc  = inv ? CLR_ON_ACCENT : (k == STATE_ERROR ? CLR_TEXT : CLR_MUTED);
    state_icon(dc, k, b.left + S(48), b.top + S(48), S(22), inv);
    text_at(dc, b.left + S(88), b.top + S(28), g.colw - S(112), S(40), title, tc,
            title_font(), DT_LEFT | DT_END_ELLIPSIS);
    t.left = b.left + S(24); t.top = b.top + S(88);
    t.right = b.right - S(24); t.bottom = b.bottom - S(24) - S(48) - S(8);
    text_at(dc, t.left, t.top, t.right - t.left, t.bottom - t.top, body, bc,
            g_font, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    group_label(dc, g.rx, g.ry, g.colw, L"Сервер");
    card(dc, g.rx, g.ry + S(26), g.colw, S(48));
    group_label(dc, g.rx, g.ry + S(90), g.colw, L"Что идёт через VPN");
    card(dc, g.rx, g.ry + S(116), g.colw, S(144));
    fill(dc, g.rx + S(1), g.ry + S(116) + S(48), g.colw - S(2), S(1), g_brush_line);
    fill(dc, g.rx + S(1), g.ry + S(116) + S(96), g.colw - S(2), S(1), g_brush_line);
}

static void paint_servers(HDC dc, const RECT *c)
{
    int     w = c->right - PAD * 2;
    wchar_t hint[160];

    text_at(dc, PAD, S(28), w, S(32), L"Серверы", CLR_TEXT, title_font(), DT_LEFT);
    if (g_sub_busy) {
        StringCchCopyW(hint, 160, L"Загрузка подписки…");
    } else if (g_ping_busy) {
        StringCchCopyW(hint, 160, L"Проверка задержек…");
    } else if (g_prof.count) {
        int k, icmp_silent = 0;
        for (k = 0; k < g_prof.count && k < PROFILES_MAX; k++)
            if (g_prof.items[k].link.proto == LINK_WG && g_ping[k] == -1) icmp_silent = 1;
        if (icmp_silent)
            StringCchCopyW(hint, 160, L"«Нет ответа» не значит, что сервер не работает: он может не отвечать на проверку задержки.");
        else
            StringCchPrintfW(hint, 160, L"%d %s · двойной щелчок — подключить",
                             g_prof.count, plural_ru(g_prof.count, L"сервер", L"сервера", L"серверов"));
    } else {
        StringCchCopyW(hint, 160, L"Пока пусто: добавьте ссылку на сервер или подписку.");
    }
    text_at(dc, PAD + S(4), S(76), w, S(20), hint, CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    card(dc, PAD, S(104), w, c->bottom - S(24) - S(104));
    {
        int xp, xh, xg, lw = w - S(2);
        server_columns(lw, &xp, &xh, &xg);
        text_at(dc, PAD + S(1) + S(16), S(104), xp, S(36), L"Сервер", CLR_MUTED, g_font_small_bold, DT_LEFT);
        text_at(dc, PAD + S(1) + xp, S(104), xh - xp, S(36), L"Протокол", CLR_MUTED, g_font_small_bold, DT_LEFT);
        text_at(dc, PAD + S(1) + xh, S(104), xg - xh, S(36), L"Адрес", CLR_MUTED, g_font_small_bold, DT_LEFT);
        text_at(dc, PAD + S(1) + xg, S(104), S(90), S(36), L"Задержка", CLR_MUTED, g_font_small_bold, DT_RIGHT);
        fill(dc, PAD + S(1), S(104) + S(36), lw, S(1), g_brush_line);
    }
}

static void paint_apps(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w   = c->right - PAD * 2;

    text_at(dc, PAD + S(4), top + S(64), w, S(20),
            g_appv_n ? L"Наборы правил: каждый — файл. Включённые применяются при следующем включении VPN."
                     : L"Пока ни одного набора. Выберите приложение из запущенных или добавьте вручную.",
            CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    card(dc, PAD, top + S(92), w, c->bottom - S(24) - (top + S(92)));
}

static void paint_pac(HDC dc, const RECT *c)
{
    int top = TABS_H;
    int w = c->right - PAD * 2;
    int problems = g_vpn_on && g_pac_status_valid &&
                   (g_pac_status.evaluation_errors || g_pac_status.worker_cap_hits ||
                    g_pac_status.dns_cap_hits || g_pac_status.udp_evictions);
    wchar_t status[160], detail[320];
    /* First line: the state, short enough to fit beside the title. */
    if (g_vpn_on && g_pac_status_valid)
        StringCchPrintfW(status, 160, L"Используется PAC: %u", g_pac_status.active_count);
    else if (g_vpn_on)
        StringCchPrintfW(status, 160, L"Включено PAC: %d · не используются", g_pac_count);
    else
        StringCchPrintfW(status, 160, L"Включено PAC: %d · VPN выключен", g_pac_count);
    {
        /* From after the title to just before the help link, wherever
           layout() put it. */
        RECT link;
        int right = PAD + w;
        GetWindowRect(g_pac_help, &link);
        MapWindowPoints(NULL, GetParent(g_pac_help), (POINT *)&link, 2);
        (void)right; (void)link;
        text_at(dc, PAD + S(4), top + S(64), w, S(20), status,
                (g_vpn_on && g_pac_status_valid) ? CLR_TEXT : CLR_MUTED, g_font_small_bold,
                DT_LEFT | DT_END_ELLIPSIS);
    }
    /* Second line, full width: the counters when something went wrong,
       otherwise the one-line summary of how PAC works. */
    if (problems) {
        wchar_t last[48] = L"";
        if (g_pac_status.evaluation_errors) {
            if (g_pac_status.last_error == ERROR_TIMEOUT)
                StringCchCopyW(last, 48, L" (последняя — тайм-аут 3 с)");
            else
                StringCchPrintfW(last, 48, L" (последняя — код %lu)", (unsigned long)g_pac_status.last_error);
        }
        StringCchPrintfW(detail, 320, L"Ошибок вычисления PAC: %llu%s",
                         (unsigned long long)g_pac_status.evaluation_errors, last);
        if (g_pac_status.worker_cap_hits || g_pac_status.dns_cap_hits || g_pac_status.udp_evictions) {
            wchar_t caps[96];
            StringCchPrintfW(caps, 96, L" · лимиты: TCP %llu, DNS %llu, UDP %llu",
                             (unsigned long long)g_pac_status.worker_cap_hits,
                             (unsigned long long)g_pac_status.dns_cap_hits,
                             (unsigned long long)g_pac_status.udp_evictions);
            StringCchCatW(detail, 320, caps);
        }
    } else {
        StringCchCopyW(detail, 320, L"Если хоть один PAC отвечает прокси, соединение идёт через VPN.");
    }
    text_at(dc, PAD + S(4), top + S(84), w, S(20), detail, problems ? CLR_WARN : CLR_MUTED,
            g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    card(dc, PAD, top + S(112), w, c->bottom - S(24) - S(52) - (top + S(112)));
    {
        int xt, xs, lw = w - S(2), y = top + S(112);
        pac_columns(lw, &xt, &xs);
        text_at(dc, PAD + S(1) + S(16), y, xt, S(36), L"Источник", CLR_MUTED, g_font_small_bold, DT_LEFT);
        text_at(dc, PAD + S(1) + xt, y, xs - xt, S(36), L"Тип", CLR_MUTED, g_font_small_bold, DT_LEFT);
        text_at(dc, PAD + S(1) + xs, y, S(180), S(36), L"Состояние", CLR_MUTED, g_font_small_bold, DT_LEFT);
        fill(dc, PAD + S(1), y + S(36), lw, S(1), g_brush_line);
    }
}

static void paint_hosts(HDC dc, const RECT *c)
{
    hl_paint(dc, c);
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
    field_frame(dc, g_pk_search, 0);
    card(dc, PAD, top + S(128), w, c->bottom - FOOTER_H - S(12) - (top + S(128)));
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

    text_at(dc, PAD, top + S(72), w, S(20), L"Имя процесса",
            CLR_MUTED, g_font_small_bold, DT_LEFT);
    text_at(dc, PAD, ed_path_top() - S(24), w, S(20), L"Расположение exe-файла",
            CLR_MUTED, g_font_small_bold, DT_LEFT);
    {
        int i;
        for (i = 0; i < ED_ROWS; i++) {
            field_frame(dc, g_ed_name[i], 0);
            field_frame(dc, g_ed_path[i], S(40));
        }
    }
}

static void paint_settings(HDC dc, const RECT *c)
{
    set_geo g;
    int     y;

    settings_geometry(c, &g);
    text_at(dc, PAD, S(24), c->right - PAD * 2, S(32), L"Настройки", CLR_TEXT, title_font(), DT_LEFT);
    text_at(dc, PAD, S(60), c->right - PAD * 2, S(20),
            L"Изменения сохраняются сразу; туннель возьмёт их при следующем включении.",
            CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);

    group_label(dc, g.lx, g.launch, g.colw, L"Запуск");
    card(dc, g.lx, g.launch + S(26), g.colw, S(144));
    fill(dc, g.lx + S(1), g.launch + S(26) + S(48), g.colw - S(2), S(1), g_brush_line);
    fill(dc, g.lx + S(1), g.launch + S(26) + S(96), g.colw - S(2), S(1), g_brush_line);

    group_label(dc, g.lx, g.conn, g.colw, L"Подключение");
    card(dc, g.lx, g.conn + S(26), g.colw, S(144));
    text_at(dc, g.lx + S(16), g.conn + S(26) + S(8), g.colw, S(22), L"DNS для сайтов через VPN", CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, g.lx + g.colw - S(96), g.conn + S(26) + S(8), S(80), S(22), L"Что это?", CLR_ACCENT, g_font_small, DT_RIGHT);
    fill(dc, g.lx + S(1), g.conn + S(26) + S(72), g.colw - S(2), S(1), g_brush_line);
    text_at(dc, g.lx + S(16), g.conn + S(26) + S(80), g.colw, S(22), L"Обновлять подписку и PAC", CLR_TEXT, g_font, DT_LEFT);

    group_label(dc, g.rx, g.look, g.colw, L"Внешний вид");
    card(dc, g.rx, g.look + S(26), g.colw, S(72));
    text_at(dc, g.rx + S(16), g.look + S(26) + S(8), g.colw, S(22), L"Тема", CLR_TEXT, g_font, DT_LEFT);

    group_label(dc, g.rx, g.adv, g.colw, L"Для опытных");
    card(dc, g.rx, g.adv + S(26), g.colw, g_set_adv_open ? S(48) + S(216) + S(48) : S(48));
    if (!g_set_adv_open) return;
    y = g.adv + S(26) + S(48);
    fill(dc, g.rx + S(1), y, g.colw - S(2), S(1), g_brush_line);
    text_at(dc, g.rx + S(16), y + S(8), g.colw, S(22), L"MTU", CLR_TEXT, g_font, DT_LEFT);
    {
        /* The field itself has no frame; on the card it would vanish. */
        RECT f = { g.rx + S(16) - S(2), y + S(34) - S(2), g.rx + S(16) + S(100) + S(2), y + S(34) + S(28) + S(2) };
        rounded_r(dc, &f, CLR_SURFACE, CLR_BORDER, S(6));
    }
    text_at(dc, g.rx + g.colw - S(96), y + S(8), S(80), S(22), L"Что это?", CLR_ACCENT, g_font_small, DT_RIGHT);
    fill(dc, g.rx + S(1), y + S(72), g.colw - S(2), S(1), g_brush_line);
    text_at(dc, g.rx + S(16), y + S(80), g.colw, S(22), L"Уровень журнала", CLR_TEXT, g_font, DT_LEFT);
    fill(dc, g.rx + S(1), y + S(144), g.colw - S(2), S(1), g_brush_line);
    text_at(dc, g.rx + S(16), y + S(152), g.colw, S(22), L"Сетевой стек", CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, g.rx + g.colw - S(96), y + S(152), S(80), S(22), L"Что это?", CLR_ACCENT, g_font_small, DT_RIGHT);
    fill(dc, g.rx + S(1), y + S(216), g.colw - S(2), S(1), g_brush_line);
}

static void paint_zapret(HDC dc, const RECT *c)
{
    int      w = c->right - PAD * 2, running = g_status.mode != ZAPRET_OFF;
    zap_geo  g;
    wchar_t  line[400];
    RECT     r;

    text_at(dc, PAD, S(24), w, S(32), L"zapret", CLR_TEXT, title_font(), DT_LEFT);
    if (!g_zap.valid) {
        text_at(dc, PAD, S(60), w, S(20),
                L"Отдельный инструмент для сайтов из своего списка. Работает без VPN.",
                CLR_MUTED, g_font_small, DT_LEFT);
        card(dc, PAD, S(96), w, c->bottom - S(24) - S(96));
        text_at(dc, PAD, S(200), w, S(32), L"Папка zapret не указана", CLR_TEXT, g_font_big, DT_CENTER);
        text_at(dc, PAD, S(236), w, S(20), L"Укажите папку, в которую распакован zapret:",
                CLR_MUTED, g_font_small, DT_CENTER);
        text_at(dc, PAD, S(256), w, S(20), L"ту, где лежат service.bat и файлы стратегий.",
                CLR_MUTED, g_font_small, DT_CENTER);
        if (g_zap.problem[0])
            text_at(dc, PAD, S(356), w, S(20), g_zap.problem, CLR_WARN, g_font_small, DT_CENTER);
        return;
    }

    zapret_geometry(c, &g);
    StringCchPrintfW(line, 400, L"%s · %s%s · %d %s", g_zap.path,
                     g_zap.version[0] ? L"версия " : L"версия не определена",
                     g_zap.version, g_zap.strategy_count,
                     plural_ru(g_zap.strategy_count, L"стратегия", L"стратегии", L"стратегий"));
    text_at(dc, PAD, S(60), w - S(200), S(20), line, CLR_MUTED, g_font_small, DT_LEFT | DT_PATH_ELLIPSIS);

    if (g_zap_dirty) {
        r.left = PAD; r.top = g.banner; r.right = c->right - PAD; r.bottom = g.banner + S(48);
        rounded_r(dc, &r, CLR_TINT, CLR_WARN, S(12));
        state_icon(dc, STATE_ERROR, PAD + S(26), g.banner + S(24), S(9), 0);
        text_at(dc, PAD + S(46), g.banner, w - S(200), S(48),
                L"Игровой фильтр изменён. Он начнёт действовать после перезапуска.",
                CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    }

    r = g.status;
    rounded_r(dc, &r, running ? CLR_ACCENT : CLR_SURFACE, running ? CLR_ACCENT : CLR_LINE, S(16));
    state_icon(dc, running ? STATE_ON : STATE_OFF, r.left + S(40), r.top + S(38), S(16), running);
    text_at(dc, r.left + S(68), r.top + S(14), g.lw - S(92), S(30),
            running ? L"Работает" : L"zapret выключен",
            running ? CLR_ON_ACCENT : CLR_TEXT, title_font(), DT_LEFT | DT_END_ELLIPSIS);
    if (g_busy && g_busy_text)
        StringCchCopyW(line, 400, g_busy_text);
    else if (!running)
        StringCchCopyW(line, 400, L"Выберите стратегию и запустите её.");
    else if (g_status.strategy[0])
        StringCchPrintfW(line, 400, L"стратегия %s, %s", g_status.strategy,
                         g_status.mode == ZAPRET_SERVICE ? L"служба Windows" : L"отдельный bat");
    else
        StringCchCopyW(line, 400, L"стратегия неизвестна");
    text_at(dc, r.left + S(68), r.top + S(44), g.lw - S(92), S(20), line,
            running ? CLR_ON_ACCENT : CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);

    if (GetWindowTextLengthW(g_zap_search) > 0)
        StringCchPrintfW(line, 400, L"Найдено %d из %d", strategies_shown(), g_count);
    else
        StringCchPrintfW(line, 400, L"Стратегии · %d", g_count);
    group_label(dc, PAD, g.strat, g.lw, line);
    search_frame(dc, g_zap_search);
    card(dc, PAD, g.list_top, g.lw, g.list_bottom - g.list_top);
    {
        const wchar_t *sel = selected_strategy();
        if (sel && g_status.mode == ZAPRET_SERVICE && _wcsicmp(sel, g_status.strategy) == 0)
            text_at(dc, PAD + caption_width(g_zap_start) + S(44), g.start, g.lw, S(40),
                    L"выбрана уже работающая", CLR_MUTED, g_font_small, DT_LEFT);
    }

    group_label(dc, g.rx, g.compat, g.rw, L"Совместимость с VPN");
    card(dc, g.rx, g.compat + S(26), g.rw, S(48));
    {
        int ok = g_exc_known > 0 && g_exc_present >= g_exc_known;
        if (g_exc_known == 0)
            StringCchCopyW(line, 400, g_prof.count ? L"Проверяю адреса серверов…" : L"Нет серверов — исключать нечего");
        else
            StringCchPrintfW(line, 400, ok ? L"Адреса серверов VPN в исключениях: %d из %d"
                                           : L"В исключениях %d из %d адресов серверов VPN",
                             g_exc_present, g_exc_known);
        state_icon(dc, ok ? STATE_ON : (g_exc_known ? STATE_ERROR : STATE_OFF),
                   g.rx + S(24), g.compat + S(50), S(8), 0);
        text_at(dc, g.rx + S(42), g.compat + S(26), g.rw - S(42) - S(120), S(48), line,
                CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    }

    group_label(dc, g.rx, g.filters, g.rw, L"Фильтры");
    card(dc, g.rx, g.filters + S(26), g.rw, S(152));
    text_at(dc, g.rx + S(16), g.filters + S(26) + S(8), g.rw, S(22), L"Игровой фильтр", CLR_TEXT, g_font, DT_LEFT);
    fill(dc, g.rx + S(1), g.filters + S(26) + S(76), g.rw - S(2), S(1), g_brush_line);
    text_at(dc, g.rx + S(16), g.filters + S(26) + S(84), g.rw, S(22), L"Фильтр IPSet", CLR_TEXT, g_font, DT_LEFT);

    group_label(dc, g.rx, g.lists, g.rw, L"Списки");
    card(dc, g.rx, g.lists + S(26), g.rw, S(144));
    fill(dc, g.rx + S(1), g.lists + S(26) + S(48), g.rw - S(2), S(1), g_brush_line);
    fill(dc, g.rx + S(1), g.lists + S(26) + S(96), g.rw - S(2), S(1), g_brush_line);
    text_at(dc, g.rx + S(16), g.lists + S(26) + S(48), g.rw, S(48), L"Список IPSet", CLR_TEXT, g_font, DT_LEFT);
    text_at(dc, g.rx + S(16), g.lists + S(26) + S(96), g.rw, S(48), L"Файл hosts", CLR_TEXT, g_font, DT_LEFT);
}

void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC   dc = BeginPaint(hwnd, &ps);
    RECT  client, c;
    POINT old;
    int   footer = g_page == PAGE_PICK || g_page == PAGE_EDIT;

    GetClientRect(hwnd, &client);
    fill(dc, 0, 0, client.right, client.bottom, g_brush_bg);
    paint_sidebar(dc, &client);

    c = client;
    c.right = client.right - g_ox;
    SetViewportOrgEx(dc, g_ox, 0, &old);
    if (g_page == PAGE_UTGARD)       paint_connect(dc, &c);
    else if (g_page == PAGE_SERVERS) paint_servers(dc, &c);
    else if (g_page == PAGE_APPS)    paint_apps(dc, &c);
    else if (g_page == PAGE_PAC)     paint_pac(dc, &c);
    else if (g_page == PAGE_HOSTS)   paint_hosts(dc, &c);
    else if (g_page == PAGE_PICK)    paint_pick(dc, &c);
    else if (g_page == PAGE_EDIT)    paint_edit(dc, &c);
    else if (g_page == PAGE_SETTINGS) paint_settings(dc, &c);
    else                             paint_zapret(dc, &c);
    if (tabs_top() > S(8))
        fill(dc, PAD, S(56), c.right - PAD * 2, S(1), g_brush_line);
    if (footer) {
        fill(dc, 0, c.bottom - FOOTER_H, c.right, FOOTER_H, g_brush_footer);
        fill(dc, 0, c.bottom - FOOTER_H, c.right, S(1), g_brush_line);
    }
    SetViewportOrgEx(dc, old.x, old.y, NULL);

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

static const wchar_t *proto_label(const link_profile *l)
{
    if (link_is_awg(l)) return L"AmneziaWG";
    switch (l->proto) {
    case LINK_VLESS: return L"VLESS";
    case LINK_HY2:   return L"Hysteria2";
    case LINK_SS:    return L"Shadowsocks";
    case LINK_TROJAN: return L"Trojan";
    case LINK_VMESS: return L"VMess";
    case LINK_WG:    return L"WireGuard";
    default:         return L"?";
    }
}

/* The servers table: name, protocol, address, delay - each in its own
   column, so the rows line up. Shared with the column headings. */
void server_columns(int width, int *x_proto, int *x_host, int *x_ping)
{
    *x_ping  = width - S(16) - S(90);
    *x_host  = *x_ping - S(16) - S(220);
    *x_proto = *x_host - S(16) - S(120);
    if (*x_proto < width / 3) {                 /* narrow window: the name keeps a third */
        *x_proto = width / 3;
        *x_host  = *x_proto + S(120) + S(16);
    }
}

void draw_profile(const DRAWITEMSTRUCT *d)
{
    const profile_entry *e;
    RECT     r        = d->rcItem;
    BOOL     locked   = !IsWindowEnabled(d->hwndItem);   /* a job is running */
    BOOL     selected = (d->itemState & ODS_SELECTED) != 0 && !locked;
    BOOL     hot;
    int      active, xp, xh, xg, w = r.right - r.left;
    wchar_t  text[288];
    HBRUSH   br;

    if (d->itemID == (UINT)-1 || (int)d->itemID >= g_prof.count) return;
    e = &g_prof.items[d->itemID];
    active = ((int)d->itemID == g_prof.active);
    hot = !locked && (int)d->itemID == list_hot_row(d->hwndItem);

    br = CreateSolidBrush(selected ? CLR_TINT : hot ? CLR_HOVER : CLR_SURFACE);
    FillRect(d->hDC, &r, br);
    DeleteObject(br);
    if (d->itemID) fill(d->hDC, r.left + S(16), r.top, w - S(32), S(1), g_brush_line);
    if (active) {
        /* The server in use: an accent mark and a bold name - not colour alone. */
        br = CreateSolidBrush(CLR_ACCENT);
        fill(d->hDC, r.left, r.top + S(8), S(3), r.bottom - r.top - S(16), br);
        DeleteObject(br);
    }
    server_columns(w, &xp, &xh, &xg);

    to_wide(e->link.name[0] ? e->link.name : e->link.server, text, 288);
    text_at(d->hDC, r.left + S(16), r.top, xp - S(32), r.bottom - r.top, text,
            locked ? CLR_MUTED : CLR_TEXT, active ? g_font_bold : g_font, DT_LEFT | DT_END_ELLIPSIS);
    text_at(d->hDC, r.left + xp, r.top, xh - xp - S(16), r.bottom - r.top,
            proto_label(&e->link), CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    to_wide(e->link.server, text, 288);
    text_at(d->hDC, r.left + xh, r.top, xg - xh - S(16), r.bottom - r.top,
            text, CLR_MUTED, g_font_small, DT_LEFT | DT_END_ELLIPSIS);
    {
        int      ms = g_ping[d->itemID];
        wchar_t  lat[24];
        COLORREF color = CLR_TEXT;
        if (ms == -2)    { StringCchCopyW(lat, 24, L"…"); color = CLR_MUTED; }
        else if (ms < 0) { StringCchCopyW(lat, 24, L"Нет ответа"); color = CLR_WARN; }
        else             StringCchPrintfW(lat, 24, L"%d мс", ms);
        text_at(d->hDC, r.left + xg, r.top, S(90), r.bottom - r.top, lat, color, g_font_small, DT_RIGHT);
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
