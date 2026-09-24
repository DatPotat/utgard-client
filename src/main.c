/*
 * Utgard client - Window procedure, tray menu, entry point and the shared state.
 */

#include "ui.h"

/* ---- shared state: every module sees it through the externs in ui.h -- */

int    g_dpi  = USER_DEFAULT_SCREEN_DPI;

int    g_page = PAGE_UTGARD;

HFONT  g_font, g_font_big, g_font_small;

HBRUSH g_brush_bg, g_brush_footer, g_brush_surface, g_brush_line;

HWND g_tab_utgard, g_tab_zapret, g_toggle, g_pick_path, g_list;

HWND g_zap_start, g_zap_stop, g_zap_restart;

HWND g_plist, g_prof_add, g_prof_del, g_prof_sub;

int  g_sub_busy;

/* -2 not measured yet, -1 unreachable, otherwise milliseconds. Not stored:
   a latency from last week would be a lie. */
int g_ping[PROFILES_MAX];

int g_ping_gen;      /* results from an older list are discarded */

int g_ping_busy;

int g_vpn_on;

int g_installing;

HWND g_btn_hosts, g_btn_apps, g_zap_fix;

HWND g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_tip;

HWND g_alist, g_app_back, g_app_pick, g_app_manual;

WNDPROC   g_alist_prev;

app_entry g_appv[APPS_MAX];

int       g_appv_n;

int       g_app_hover_item = -1;   /* row under the cursor, -1 if none */

int       g_app_hover_zone = -1;   /* 0 name, 1 enable/disable, 2 delete */

HWND      g_hedit, g_h_back, g_h_tidy, g_h_save;

int       g_hosts_mode;

HWND      g_pk_search, g_pk_list, g_pk_back, g_pk_save;

pick_proc g_pk_all[PICK_MAX];

int       g_pk_view[PICK_MAX];     /* indices into g_pk_all after filtering */

int       g_pk_view_n;

int       g_pk_checked_n;

HWND    g_ed_name[ED_ROWS], g_ed_nplus[ED_ROWS], g_ed_nminus[ED_ROWS];

HWND    g_ed_path[ED_ROWS], g_ed_pplus[ED_ROWS], g_ed_pminus[ED_ROWS];

HWND    g_ed_back, g_ed_save;

int     g_ed_ncount = 1, g_ed_pcount = 1;

wchar_t g_ed_orig[APPS_NAME_MAX];     /* empty when creating */

app_settings g_set;

static UINT         g_taskbar_created;   /* Explorer restarted */

HWND    g_set_open, g_set_mtu, g_set_log, g_set_back, g_set_save;

HWND    g_set_upd, g_set_upd_now;

HWND    g_set_stack, g_set_dns, g_set_tray, g_set_auto, g_set_sub, g_ping_now, g_zap_list;

long long g_sub_retry;   /* after a failed automatic refresh, not before */

int  g_exc_known, g_exc_present;

int  g_zap_dirty;   /* Game Filter changed: it lives in winws arguments; lists are reread live */

int            g_busy;        /* a background job is running */

const wchar_t *g_busy_text;   /* what it is doing, for the status line */

int  g_host_count, g_app_count;

profile_store g_prof;
/* Set by WM_CREATE, reported once the window exists: a message box inside
   WM_CREATE would run a modal loop before the window is fully built. */
static int     g_prof_unreadable;
static wchar_t g_prof_aside[MAX_PATH * 2];

HFONT g_font_mono;

zapret_info   g_zap;

zapret_status g_status;

int     g_count;

/* Word for word from the zapret project README, so the explanation matches
   what its author actually promises. */
static const wchar_t TIP_GAME[] =
    L"Game Filter — переключение режима обхода для игр (и других сервисов, "
    L"использующих UDP и TCP на портах выше 1023). После переключения "
    L"требуется перезапуск стратегии.";

static const wchar_t TIP_IPSET[] =
    L"IPSet Filter — переключение режима обхода сервисов из ipset-all.txt. "
    L"Полезно при тестировании, если не работает ресурс, который без zapret "
    L"работает.\r\n"
    L"none — никакие айпи не попадают под проверку\r\n"
    L"loaded — айпи проверяется на вхождение в список\r\n"
    L"any — любой айпи попадает под фильтр";

static const wchar_t TIP_IPUPD[] =
    L"Update IPSet List — обновление списка ipset-all.txt актуальным из "
    L"репозитория.";

static const wchar_t TIP_MTU[] =
    L"Максимальный размер пакета внутри туннеля. Если часть сайтов не "
    L"открывается или загрузка зависает на середине — уменьшите значение, "
    L"например до 1380. Стандартно 1430. Применяется при следующем включении VPN.";

static const wchar_t TIP_STACK[] =
    L"Каким способом sing-box разбирает трафик, пришедший в туннель.\r\n"
    L"system — сетевой стек самой Windows: быстрый, стандартно.\r\n"
    L"gvisor — собственный стек sing-box: медленнее, но не зависит от "
    L"особенностей Windows. Пробовать, если с system что-то не работает.\r\n"
    L"mixed — TCP через system, UDP через gvisor.\r\n"
    L"В следующих версиях sing-box выбор уберут и оставят один встроенный стек.";

static const wchar_t TIP_DNS[] =
    L"Через какой сервер узнавать адреса сайтов из списка. Запросы идут "
    L"зашифрованными по HTTP/3, провайдер интернета их не видит. Все три "
    L"сервера работают одинаково; другой стоит выбрать, если текущий "
    L"недоступен или медленный.";

static const wchar_t TIP_HOSTS[] =
    L"Update Hosts File — обновление файла hosts для починки веб-версии "
    L"телеграма и подключения к голосовому чату Discord.";

int  g_upd_pending;     /* found while hidden in the tray: ask on show */

static void window_show(HWND hwnd)
{
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    if (g_upd_pending) { g_upd_pending = 0; upd_prompt(hwnd); }
}

static void tray_menu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    UINT  cmd;

    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Открыть Utgard");
    AppendMenuW(menu, MF_STRING | (g_prof.count && !g_busy ? 0 : MF_GRAYED), ID_TRAY_VPN,
                g_vpn_on ? L"Выключить VPN" : L"Включить VPN");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");
    SetMenuDefaultItem(menu, ID_TRAY_OPEN, FALSE);

    /* Without becoming foreground first, the menu would not close when the
       user clicks elsewhere - a documented quirk of notification menus. */
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                               pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == ID_TRAY_OPEN) window_show(hwnd);
    else if (cmd == ID_TRAY_VPN) act_vpn(hwnd);       /* the result updates the icon */
    else if (cmd == ID_TRAY_EXIT) {
        if (g_busy) {
            window_show(hwnd);
            problem(hwnd, L"Дождитесь окончания операции — она займёт несколько секунд.");
        } else {
            DestroyWindow(hwnd);
        }
    }
}

static void apply_dark_caption(HWND hwnd)
{
    BOOL     dark    = TRUE;
    COLORREF caption = CLR_BG;
    /* Documented for Windows 11 build 22000+; older builds return an error
       and keep the system caption. Both failures are harmless. */
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    DwmSetWindowAttribute(hwnd, UTG_DWMWA_CAPTION_COLOR, &caption, sizeof caption);
}

static void set_fonts(void)
{
    SendMessageW(g_tab_utgard, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_tab_zapret, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_open,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_ping_now,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_mtu,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_log,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_stack,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_dns,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_tray,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_auto,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_upd,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_sub,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_back,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_set_save,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_toggle,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pick_path,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_list,       WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_start,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_stop,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_restart,WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_fix,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_game,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_ipset,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_ipupd,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_hosts,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_list,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_plist,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_add,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_del,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_prof_sub,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_btn_hosts,  WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_alist,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hedit,      WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_search,  WM_SETFONT, (WPARAM)g_font, TRUE);
    ask_edit_center(g_pk_search);
    ask_edit_center(g_set_mtu);
    {
        int i;
        for (i = 0; i < ED_ROWS; i++) {
            SendMessageW(g_ed_name[i],   WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_path[i],   WM_SETFONT, (WPARAM)g_font, TRUE);
            ask_edit_center(g_ed_name[i]);
            ask_edit_center(g_ed_path[i]);
            SendMessageW(g_ed_nplus[i],  WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_nminus[i], WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_pplus[i],  WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_ed_pminus[i], WM_SETFONT, (WPARAM)g_font, TRUE);
        }
    }
    SendMessageW(g_ed_back,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_ed_save,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_list,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_back,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pk_save,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_back,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_tidy,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_h_save,     WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_back,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_pick,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_app_manual, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_btn_apps,   WM_SETFONT, (WPARAM)g_font, TRUE);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_dpi = (int)GetDpiForWindow(hwnd);
        fonts_create();
        apply_dark_caption(hwnd);
        g_tab_utgard = make_button(hwnd, L"Utgard", ID_TAB_UTGARD, BK_TAB);
        g_tab_zapret = make_button(hwnd, L"zapret", ID_TAB_ZAPRET, BK_TAB);
        g_set_open   = make_button(hwnd, L"Настройки", ID_SET_OPEN, BK_SECONDARY);
        g_ping_now   = make_button(hwnd, L"ping до сервера", ID_PING_NOW, BK_SECONDARY);
        {
            HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            int i;

            g_set_mtu = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_CENTER, 0, 0, 0, 0, hwnd,
                (HMENU)(INT_PTR)ID_SET_MTU, inst, NULL);
            SendMessageW(g_set_mtu, EM_SETLIMITTEXT, 4, 0);

            /* A drop-down list, not an editable combo: only real levels go in.
               DarkMode_CFD is the theme that darkens combo boxes. */
            g_set_log = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_LOG, inst, NULL);
            SetWindowTheme(g_set_log, L"DarkMode_CFD", NULL);
            {
                static const wchar_t *labels[] = {
                    L"trace — всё подряд",
                    L"debug — подробно, для разбора проблем",
                    L"info — обычная работа",
                    L"warn — предупреждения и ошибки",
                    L"error — только ошибки",
                    L"fatal — только критические ошибки",
                    L"panic — почти ничего"
                };
                for (i = 0; i < 7; i++)
                    SendMessageW(g_set_log, CB_ADDSTRING, 0, (LPARAM)labels[i]);
            }
            g_set_stack = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_STACK, inst, NULL);
            SetWindowTheme(g_set_stack, L"DarkMode_CFD", NULL);
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"system — стек Windows");
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"gvisor — стек sing-box");
            SendMessageW(g_set_stack, CB_ADDSTRING, 0, (LPARAM)L"mixed — TCP system, UDP gvisor");

            g_set_dns = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_DNS, inst, NULL);
            SetWindowTheme(g_set_dns, L"DarkMode_CFD", NULL);
            for (i = 0; i < settings_dns_count; i++) {
                wchar_t line[96];
                StringCchPrintfW(line, 96, L"%s — HTTP/3", settings_dns[i].label);
                SendMessageW(g_set_dns, CB_ADDSTRING, 0, (LPARAM)line);
            }

            g_set_sub = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)ID_SET_SUB, inst, NULL);
            SetWindowTheme(g_set_sub, L"DarkMode_CFD", NULL);
            for (i = 0; i < settings_sub_count; i++) {
                wchar_t line[64];
                int     h = settings_sub_hours[i];
                /* Russian plural: 3 часа, 6 и 12 часов. */
                StringCchPrintfW(line, 64, L"обновлять каждые %d %s", h,
                                 (h % 10 >= 2 && h % 10 <= 4 && (h % 100 < 12 || h % 100 > 14))
                                     ? L"часа" : L"часов");
                SendMessageW(g_set_sub, CB_ADDSTRING, 0, (LPARAM)line);
            }

            g_set_tray = make_button(hwnd, L"Сворачивать в трей при закрытии",
                                     ID_SET_TRAY, BK_CHECK);
            g_set_auto = make_button(hwnd, L"Запускать вместе с Windows (свёрнутым в трей)",
                                     ID_SET_AUTO, BK_CHECK);
            g_set_upd  = make_button(hwnd, L"Проверять обновления при запуске",
                                     ID_SET_UPD, BK_CHECK);
            g_set_upd_now = make_button(hwnd, L"Проверить обновления", ID_SET_UPD_NOW,
                                        BK_SECONDARY);

            g_set_back = make_button_on(hwnd, L"Назад", ID_SET_BACK, BK_SECONDARY, CLR_FOOTER);
            g_set_save = make_button_on(hwnd, L"Сохранить", ID_SET_SAVE, BK_PRIMARY, CLR_FOOTER);
        }
        g_toggle     = make_button(hwnd, L"Включить", ID_TOGGLE, BK_PRIMARY);
        g_pick_path  = make_button(hwnd, L"Указать папку…", ID_PICK_PATH, BK_SECONDARY);
        g_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                 WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                 LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STRATEGIES,
                                 (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                 NULL);
        SendMessageW(g_list, LB_SETITEMHEIGHT, 0, (LPARAM)S(30));
        /* Dark scrollbar: undocumented since Windows 10 1809, and a no-op
           where the theme is absent. */
        SetWindowTheme(g_list, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_list);
        g_plist = CreateWindowExW(0, L"LISTBOX", NULL,
                                  WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                  LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PROFILES,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        SetWindowTheme(g_plist, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_plist);
        g_prof_add = make_button_on(hwnd, L"Добавить профиль…", ID_PROF_ADD,
                                    BK_SECONDARY, CLR_FOOTER);
        g_prof_del = make_button_on(hwnd, L"Удалить", ID_PROF_DEL,
                                    BK_DANGER, CLR_FOOTER);
        g_prof_sub = make_button_on(hwnd, L"Подписка…", ID_PROF_SUB,
                                    BK_SECONDARY, CLR_FOOTER);
        g_alist = CreateWindowExW(0, L"LISTBOX", NULL,
                                  WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                  LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_APPS_LIST,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_alist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        SetWindowTheme(g_alist, L"DarkMode_Explorer", NULL);
        g_alist_prev = (WNDPROC)SetWindowLongPtrW(g_alist, GWLP_WNDPROC,
                                                  (LONG_PTR)alist_proc);
        g_app_back   = make_button_on(hwnd, L"Назад", ID_APPS_BACK,
                                      BK_SECONDARY, CLR_FOOTER);
        g_app_pick   = make_button_on(hwnd, L"Выбрать из запущенных…",
                                      ID_APPS_PICK, BK_PRIMARY, CLR_FOOTER);
        g_app_manual = make_button_on(hwnd, L"Добавить вручную…",
                                      ID_APPS_MANUAL, BK_SECONDARY, CLR_FOOTER);
        g_hedit = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_TABSTOP | WS_VSCROLL |
                                  ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HOSTS_EDIT,
                                  (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                  NULL);
        SendMessageW(g_hedit, EM_SETLIMITTEXT, (WPARAM)(LIST_TEXT_MAX - 1), 0);
        SetWindowTheme(g_hedit, L"DarkMode_Explorer", NULL);
        g_pk_search = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PICK_SEARCH,
                                      (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                      NULL);
        g_pk_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                    WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                                    0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PICK_LIST,
                                    (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                    NULL);
        SendMessageW(g_pk_list, LB_SETITEMHEIGHT, 0, (LPARAM)S(40));
        SetWindowTheme(g_pk_list, L"DarkMode_Explorer", NULL);
        list_hover_attach(g_pk_list);
        {
            HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            int i;
            for (i = 0; i < ED_ROWS; i++) {
                g_ed_name[i] = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                    (HMENU)(INT_PTR)(ID_ED_NAME + i), inst, NULL);
                g_ed_path[i] = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                    (HMENU)(INT_PTR)(ID_ED_PATH + i), inst, NULL);
                SendMessageW(g_ed_name[i], EM_SETLIMITTEXT, MAX_PATH - 1, 0);
                SendMessageW(g_ed_path[i], EM_SETLIMITTEXT, MAX_PATH - 1, 0);
                g_ed_nplus[i]  = make_button(hwnd, L"+", ID_ED_NPLUS + i, BK_SECONDARY);
                g_ed_nminus[i] = make_button(hwnd, L"−", ID_ED_NMINUS + i, BK_DANGER);
                g_ed_pplus[i]  = make_button(hwnd, L"+", ID_ED_PPLUS + i, BK_SECONDARY);
                g_ed_pminus[i] = make_button(hwnd, L"−", ID_ED_PMINUS + i, BK_DANGER);
            }
            g_ed_back = make_button_on(hwnd, L"Назад", ID_ED_BACK, BK_SECONDARY, CLR_FOOTER);
            g_ed_save = make_button_on(hwnd, L"Сохранить", ID_ED_SAVE, BK_PRIMARY, CLR_FOOTER);
        }
        g_pk_back = make_button_on(hwnd, L"Назад", ID_PICK_BACK, BK_SECONDARY, CLR_FOOTER);
        g_pk_save = make_button_on(hwnd, L"Сохранить приложение", ID_PICK_SAVE,
                                   BK_PRIMARY, CLR_FOOTER);
        g_h_back = make_button_on(hwnd, L"Назад", ID_HOSTS_BACK, BK_SECONDARY, CLR_FOOTER);
        g_h_tidy = make_button_on(hwnd, L"Убрать дубли", ID_HOSTS_TIDY,
                                  BK_SECONDARY, CLR_FOOTER);
        g_h_save = make_button_on(hwnd, L"Сохранить", ID_HOSTS_SAVE, BK_PRIMARY, CLR_FOOTER);
        g_btn_hosts = make_button(hwnd, L"Список сайтов…", ID_EDIT_HOSTS, BK_SECONDARY);
        g_btn_apps  = make_button(hwnd, L"Приложения…", ID_EDIT_APPS, BK_SECONDARY);
        g_zap_start   = make_button_on(hwnd, L"Запустить", ID_ZAP_START,
                                       BK_PRIMARY, CLR_FOOTER);
        g_zap_stop    = make_button(hwnd, L"Выключить", ID_ZAP_STOP, BK_DANGER);
        g_zap_restart = make_button(hwnd, L"Перезапустить", ID_ZAP_RESTART, BK_SECONDARY);
        g_zap_fix = make_button_on(hwnd, L"Исправить конфликт VPN и zapret",
                                   ID_ZAP_FIX, BK_SECONDARY, CLR_SURFACE);
        g_zap_game  = make_button(hwnd, L"Game filter", ID_ZAP_GAME, BK_SECONDARY);
        g_zap_ipset = make_button(hwnd, L"IPSet filter", ID_ZAP_IPSET, BK_SECONDARY);
        g_zap_ipupd = make_button(hwnd, L"Обновить список IPSet", ID_ZAP_IPUPD, BK_SECONDARY);
        g_zap_hosts = make_button(hwnd, L"Обновить файл hosts", ID_ZAP_HOSTS, BK_SECONDARY);
        g_zap_list  = make_button(hwnd, L"Список хостов zapret…", ID_ZAP_LIST, BK_SECONDARY);

        {
            /* Rect-based tooltips on the parent: the "что это?" labels are
               painted, not controls, so there is no window to attach to. */
            INITCOMMONCONTROLSEX icc;
            TTTOOLINFOW ti;
            int i;
            static const wchar_t *tips[7];

            icc.dwSize = sizeof icc;
            icc.dwICC  = ICC_TAB_CLASSES;
            InitCommonControlsEx(&icc);

            tips[0] = TIP_GAME; tips[1] = TIP_IPSET;
            tips[2] = TIP_IPUPD; tips[3] = TIP_HOSTS; tips[4] = TIP_MTU;
            tips[5] = TIP_STACK; tips[6] = TIP_DNS;

            g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                    0, 0, 0, 0, hwnd, NULL,
                                    (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                                    NULL);
            if (g_tip) {
                SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)S(420));
                ZeroMemory(&ti, sizeof ti);
                ti.cbSize   = sizeof ti;
                ti.uFlags   = TTF_SUBCLASS;
                ti.hwnd     = hwnd;
                for (i = 0; i < 7; i++) {
                    ti.uId     = (UINT_PTR)i;
                    ti.lpszText = (LPWSTR)tips[i];
                    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                }
            }
        }
        set_fonts();
        {
            wchar_t remembered[ZAPRET_PATH_MAX];
            if (zapret_path_load(remembered, ZAPRET_PATH_MAX))
                zapret_scan(remembered, &g_zap);
        }
        if (!profiles_load(&g_prof, g_prof_aside, MAX_PATH * 2))
            g_prof_unreadable = 1;
        profiles_reload();
        {
            static const wchar_t *dirs[] = { L"sing-box", L"list",
                                             L"list\\applications", L"logs" };
            static const COLORREF theme[ASK_COLOR_COUNT] = {
                CLR_BG, CLR_SURFACE, CLR_LINE, CLR_TEXT,
                CLR_MUTED, CLR_OK, CLR_WARN, CLR_ACCENT
            };
            wchar_t d[MAX_PATH * 2];
            size_t  k;

            ask_configure_colors(theme);
            for (k = 0; k < sizeof dirs / sizeof dirs[0]; k++)
                if (root_file(dirs[k], d, MAX_PATH * 2)) CreateDirectoryW(d, NULL);
            /* Checked on every start, not only the first: a folder deleted
               between runs comes back. */
            apps_prepare();
        }
        singbox_seed_config();
        g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        /* We run elevated, Explorer does not: UIPI drops its messages above
           WM_USER. Without these, an autostart that beats the taskbar leaves
           no icon at all, and the icon may not answer clicks. */
        ChangeWindowMessageFilterEx(hwnd, g_taskbar_created, MSGFLT_ALLOW, NULL);
        ChangeWindowMessageFilterEx(hwnd, WM_APP_TRAY, MSGFLT_ALLOW, NULL);
        tray_init(hwnd, WM_APP_TRAY);
        PostMessageW(hwnd, WM_APP_EXC_START, 0, 0);
        SetTimer(hwnd, TIMER_SUB, 60 * 1000, NULL);
        vpn_refresh();
        lists_refresh_counts();
        ping_start(hwnd);
        {
            app_settings st;
            settings_load(&st);
            if (st.update_check) upd_start(hwnd, 0);
        }
    exc_check_start(hwnd);
        /* Ask after the window is up, not before: a message box over nothing
           is a poor first impression. */
        PostMessageW(hwnd, WM_APP_INSTALL_ASK, 0, 0);
        ask_configure(draw_button, S, g_font, g_font_small,
                      g_brush_bg, g_brush_surface, g_brush_line,
                      CLR_TEXT, CLR_MUTED, CLR_SURFACE);
        strategies_reload();
        status_refresh();
        SetTimer(hwnd, TIMER_STATUS, 2000, NULL);
        layout(hwnd);
        return 0;

    case WM_COMMAND:
        {
            int id = LOWORD(wp);
            if (id >= ID_ED_NPLUS && id < ID_ED_NPLUS + ED_ROWS) {
                ed_add(hwnd, g_ed_name, &g_ed_ncount); return 0;
            }
            if (id >= ID_ED_PPLUS && id < ID_ED_PPLUS + ED_ROWS) {
                ed_add(hwnd, g_ed_path, &g_ed_pcount); return 0;
            }
            if (id >= ID_ED_NMINUS && id < ID_ED_NMINUS + ED_ROWS) {
                ed_remove(g_ed_name, &g_ed_ncount, id - ID_ED_NMINUS);
                layout(hwnd); return 0;
            }
            if (id >= ID_ED_PMINUS && id < ID_ED_PMINUS + ED_ROWS) {
                ed_remove(g_ed_path, &g_ed_pcount, id - ID_ED_PMINUS);
                layout(hwnd); return 0;
            }
        }
        switch (LOWORD(wp)) {
        case ID_SET_OPEN: set_open(hwnd); return 0;
        case ID_PING_NOW:
            if (!g_ping_busy) ping_start(hwnd);
            return 0;
        case ID_SET_UPD_NOW: upd_start(hwnd, 1); return 0;
        case ID_SET_TRAY:
        case ID_SET_AUTO:
        case ID_SET_UPD: {
            HWND box = (LOWORD(wp) == ID_SET_TRAY) ? g_set_tray
                     : (LOWORD(wp) == ID_SET_AUTO) ? g_set_auto : g_set_upd;
            if (GetPropW(box, L"utgard.checked")) RemovePropW(box, L"utgard.checked");
            else SetPropW(box, L"utgard.checked", (HANDLE)1);
            InvalidateRect(box, NULL, FALSE);
            return 0;
        }
        case ID_SET_BACK: g_page = PAGE_UTGARD; layout(hwnd); return 0;
        case ID_SET_SAVE: set_save(hwnd); return 0;

        case ID_TAB_UTGARD:
            KillTimer(hwnd, TIMER_PICK);
            g_page = PAGE_UTGARD;
            lists_refresh_counts();
            layout(hwnd);
            return 0;
        case ID_TAB_ZAPRET:
            KillTimer(hwnd, TIMER_PICK);
            g_page = PAGE_ZAPRET;
            status_refresh();
            exc_check_start(hwnd);
            layout(hwnd);
            return 0;
        case ID_TOGGLE:     act_vpn(hwnd); return 0;
        case ID_PICK_PATH:  on_pick_path(hwnd); return 0;
        case ID_PROF_ADD:   act_profile_add(hwnd); return 0;
        case ID_PROF_DEL:   act_profile_delete(hwnd); return 0;
        case ID_PROF_SUB:   act_subscription(hwnd); return 0;
        case ID_EDIT_HOSTS: act_edit_hosts(hwnd); return 0;
        case ID_EDIT_APPS:  act_edit_apps(hwnd); return 0;

        case ID_HOSTS_BACK: hosts_back(hwnd); return 0;
        case ID_HOSTS_TIDY: hosts_tidy(hwnd); return 0;
        case ID_HOSTS_SAVE:
            if (g_hosts_mode == HOSTS_ZAPRET) hosts_save_zapret(hwnd);
            else                              hosts_save_start(hwnd, 1);
            return 0;

        case ID_APPS_BACK:
            g_page = PAGE_UTGARD;
            lists_refresh_counts();
            layout(hwnd);
            return 0;

        case ID_APPS_PICK:   pk_open(hwnd);  return 0;
        case ID_PICK_BACK:   pk_close(hwnd); return 0;
        case ID_PICK_SAVE:   pk_save(hwnd);  return 0;

        case ID_PICK_SEARCH:
            if (HIWORD(wp) == EN_CHANGE) pk_refresh();
            return 0;

        case ID_PICK_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                LRESULT i = SendMessageW(g_pk_list, LB_GETCURSEL, 0, 0);
                if (i != LB_ERR && i >= 0 && i < g_pk_view_n) {
                    pk_toggle(g_pk_all[g_pk_view[i]].path);
                    SendMessageW(g_pk_list, LB_SETCURSEL, (WPARAM)-1, 0);
                    InvalidateRect(g_pk_list, NULL, TRUE);
                    EnableWindow(g_pk_save, g_pk_checked_n > 0);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            return 0;

        case ID_ED_BACK:
            g_page = PAGE_APPS;
            apps_reload();
            layout(hwnd);
            return 0;
        case ID_ED_SAVE: ed_save(hwnd); return 0;

        case ID_APPS_MANUAL: ed_open_new(hwnd); return 0;

        case ID_PROFILES:
            if (HIWORD(wp) == LBN_DBLCLK)         act_profile_activate(hwnd);
            else if (HIWORD(wp) == LBN_SELCHANGE) layout(hwnd);
            return 0;
        case ID_ZAP_START:  act_start(hwnd, NULL); return 0;
        case ID_ZAP_STOP:   act_stop(hwnd);       return 0;

        case ID_ZAP_RESTART: act_restart(hwnd); return 0;
        case ID_ZAP_FIX:     act_zapret_fix(hwnd); return 0;
        case ID_ZAP_GAME:    act_zap_game(hwnd); return 0;
        case ID_ZAP_IPSET:   act_zap_ipset(hwnd); return 0;
        case ID_ZAP_IPUPD:   act_zap_ipset_update(hwnd); return 0;
        case ID_ZAP_HOSTS:   act_zap_hosts(hwnd); return 0;
        case ID_ZAP_LIST:    hosts_open(hwnd, HOSTS_ZAPRET); return 0;

        case ID_STRATEGIES:
            if (HIWORD(wp) == LBN_DBLCLK)        act_start(hwnd, NULL);
            else if (HIWORD(wp) == LBN_SELCHANGE) layout(hwnd);
            return 0;
        default: return 0;
        }

    case WM_APP_SUB_DONE: {
        sub_job *job = (sub_job *)lp;

        g_sub_busy = 0;
        if (job->ok) {
            subscription_apply(hwnd, job->url, job->body, job->len, job->silent);
            /* Any successful load, manual or automatic, restarts the interval. */
            settings_load(&g_set);
            g_set.sub_last = _time64(NULL);
            settings_save(&g_set);
            g_sub_retry = 0;
        } else if (job->silent) {
            g_sub_retry = _time64(NULL) + 15 * 60;
        } else {
            problem(hwnd, job->err);
        }

        free(job->body);
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_JOB_DONE: {
        long_job *j = (long_job *)lp;
        g_busy      = 0;
        g_busy_text = NULL;
        if (j->done) j->done(hwnd, j);
        job_free(j);
        layout(hwnd);
        return 0;
    }

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: window_show(hwnd); break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:   tray_menu(hwnd);   break;
        }
        return 0;

    case WM_APP_SHOW:
        window_show(hwnd);
        return 0;

    case WM_CLOSE:
        /* With the setting on, the cross hides the window and the program
           stays in the tray; with it off, the cross exits. The tunnel keeps
           running either way, as it always has. */
        settings_load(&g_set);
        if (g_set.tray_on_close) { ShowWindow(hwnd, SW_HIDE); return 0; }
        /* Exiting mid-job would leave sing-box half-started or the zapret
           service half-replaced; the job takes seconds, so ask to wait. */
        if (g_busy) {
            problem(hwnd, L"Дождитесь окончания операции — она займёт несколько секунд.");
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_APP_INSTALL_ASK:
        offer_install(hwnd);
        return 0;

    case WM_APP_INSTALL: {
        install_job *job = (install_job *)lp;

        g_installing = 0;
        if (!job->ok)
            problem(hwnd, job->msg[0] ? job->msg
                                      : L"Не удалось установить sing-box");
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_EXC_START:
        exc_check_start(hwnd);
        sub_auto_check(hwnd);        /* due already if the PC was off a while */
        return 0;

    case WM_APP_UPD_DONE: upd_done(hwnd, (upd_job *)lp); return 0;

    case WM_APP_EXC_DONE: {
        exc_job *job = (exc_job *)lp;
        g_exc_known   = job->total;
        g_exc_present = job->present;
        free(job);
        layout(hwnd);
        return 0;
    }

    case WM_APP_PING_ONE: {
        int gen = (int)(wp >> 8);
        int idx = (int)(wp & 0xFF);
        if (gen == (g_ping_gen & 0xFF) && idx >= 0 && idx < PROFILES_MAX) {
            g_ping[idx] = (int)lp;
            InvalidateRect(g_plist, NULL, TRUE);
        }
        return 0;
    }

    case WM_APP_PING_DONE:
        if ((int)wp == (g_ping_gen & 0xFF)) g_ping_busy = 0;
        layout(hwnd);               /* re-enables the refresh button too */
        return 0;

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_LISTBOX) {
            if (d->CtlID == ID_PROFILES)      draw_profile(d);
            else if (d->CtlID == ID_APPS_LIST) draw_app_row(d);
            else if (d->CtlID == ID_PICK_LIST) draw_pick_row(d);
            else                               draw_strategy(d);
        } else {
            draw_button(d);
        }
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, CLR_TEXT);
        SetBkColor((HDC)wp, CLR_SURFACE);
        return (LRESULT)g_brush_surface;

    case WM_CTLCOLORLISTBOX:
        if ((HWND)lp != g_list && (HWND)lp != g_plist && (HWND)lp != g_alist &&
            (HWND)lp != g_pk_list) {
            /* The drop-down part of the log level combo. */
            SetTextColor((HDC)wp, CLR_TEXT);
            SetBkColor((HDC)wp, CLR_SURFACE);
            return (LRESULT)g_brush_surface;
        }
        SetBkColor((HDC)wp, CLR_SURFACE);
        return (LRESULT)g_brush_surface;

    case WM_TIMER:
        if (wp == TIMER_SUB) { sub_auto_check(hwnd); return 0; }
        if (wp == TIMER_PICK) {
            if (g_page == PAGE_PICK) pk_refresh();
            else                     KillTimer(hwnd, TIMER_PICK);
            return 0;
        }
        if (wp != TIMER_STATUS) return 0;
        if (g_page == PAGE_ZAPRET && g_zap.valid) {
            if (status_refresh()) {
                InvalidateRect(hwnd, NULL, TRUE);
                InvalidateRect(g_list, NULL, TRUE);
            }
        }
        /* Checked on every page and while hidden: the tray icon must not keep
           showing "on" after sing-box has died. */
        if (vpn_refresh()) {
            tray_set_state(g_vpn_on);
            if (g_page == PAGE_UTGARD) layout(hwnd);
        }
        return 0;

    case WM_DPICHANGED: {
        const RECT *sug = (const RECT *)lp;
        g_dpi = (int)HIWORD(wp);
        fonts_destroy();
        fonts_create();
        set_fonts();
        SendMessageW(g_list,  LB_SETITEMHEIGHT, 0, (LPARAM)S(30));
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(34));
        ask_configure(draw_button, S, g_font, g_font_small,
                      g_brush_bg, g_brush_surface, g_brush_line,
                      CLR_TEXT, CLR_MUTED, CLR_SURFACE);
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        return 0;
    }

    case WM_SIZE:
        layout(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        on_paint(hwnd);
        return 0;

    case WM_DESTROY:
        tray_remove();
        KillTimer(hwnd, TIMER_STATUS);
        PostQuitMessage(0);
        return 0;
    }
    /* Explorer restarted: every notification icon is gone and must be re-added. */
    if (g_taskbar_created && msg == g_taskbar_created) {
        tray_readd();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc = { 0 };
    HWND hwnd;
    MSG msg;
    RECT want;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    (void)prev;

    /* One instance. With a tray icon a second launch would otherwise mean a
       second icon and a second client fighting over the same sing-box; instead
       it asks the running one to show itself and leaves. */
    {
        HANDLE one = CreateMutexW(NULL, TRUE, L"Local\\UtgardClient");
        if (one && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = FindWindowW(L"UtgardMain", NULL);
            if (other) PostMessageW(other, WM_APP_SHOW, 0, 0);
            CloseHandle(one);
            return 0;
        }
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 1;

    brushes_create();

    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));   /* res/utgard.rc */
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_brush_bg;
    wc.lpszClassName = L"UtgardMain";
    if (!RegisterClassExW(&wc)) return 1;

    g_dpi = (int)GetDpiForSystem();
    want.left = 0; want.top = 0; want.right = S(560); want.bottom = S(700);
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, (UINT)g_dpi);

    hwnd = CreateWindowExW(0, wc.lpszClassName, L"Utgard", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           want.right - want.left, want.bottom - want.top,
                           NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    /* Start with focus cues hidden: they appear once the keyboard is used. */
    SendMessageW(hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

    /* Started by autostart with --minimized: straight to the tray, no window.
       Started by hand: the window shows, because a double-click that seems to
       do nothing is worse than an icon that appears. */
    if (cmdline && wcsstr(cmdline, L"--minimized")) show = SW_HIDE;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    if (g_prof_unreadable) {
        wchar_t        msg[MAX_PATH * 2 + 512];
        const wchar_t *name = wcsrchr(g_prof_aside, L'\\');

        if (g_prof_aside[0])
            StringCchPrintfW(msg, sizeof msg / sizeof msg[0],
                L"Не удалось прочитать сохранённые профили: файл создан другой "
                L"учётной записью Windows, на другом компьютере или более новой "
                L"версией Utgard, либо повреждён.\n\n"
                L"Файл не удалён: он переименован в %s рядом с utgard.exe. "
                L"Список профилей начат заново.",
                name ? name + 1 : g_prof_aside);
        else
            StringCchCopyW(msg, sizeof msg / sizeof msg[0],
                L"Не удалось прочитать сохранённые профили (profiles.dat), и файл "
                L"не получилось переименовать — возможно, он занят другой программой.\n\n"
                L"Чтобы не перезаписать его, изменения профилей до перезапуска "
                L"Utgard сохраняться не будут.");
        problem(hwnd, msg);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    fonts_destroy();
    brushes_destroy();
    CoUninitialize();
    return (int)msg.wParam;
}
