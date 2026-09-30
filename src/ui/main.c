/*
 * Utgard client - Window procedure, tray menu, entry point and the shared state.
 */

#include "coremanifest.h"
#include "coredir.h"
#include "shellopen.h"
#include "awgcore.h"
#include "awgsvc.h"
#include "pacstore.h"
#include "ui.h"

/* ---- shared state: every module sees it through the externs in ui.h -- */

int    g_page = PAGE_UTGARD;

HWND g_nav[NAV_COUNT];

HWND g_tip;

static UINT         g_taskbar_created;   /* Explorer restarted */

/* Set by WM_CREATE, reported once the window exists: a message box inside
   WM_CREATE would run a modal loop before the window is fully built. */
static int     g_prof_unreadable;
static wchar_t g_prof_aside[MAX_PATH * 2];

static void pac_unreadable_notice(HWND hwnd)
{
    wchar_t aside[MAX_PATH * 2], text[MAX_PATH * 2 + 400];
    if (!pacstore_unreadable_notice(aside, MAX_PATH * 2)) return;
    if (aside[0])
        StringCchPrintfW(text, sizeof text / sizeof text[0],
            L"Не удалось прочитать файл PAC. Он не удалён, а переименован в %s. Этот PAC нужно добавить заново, остальные работают как прежде.",
            aside);                              /* full path: the file may be in list\\pac */
    else
        StringCchCopyW(text, sizeof text / sizeof text[0],
            L"Не удалось прочитать файл PAC и переименовать его. Чтобы не потерять его, настройки PAC не будут сохраняться до перезапуска Utgard.");
    problem(hwnd, text);
}

/* Shown on hover over "Как работает PAC"; describes what the code does
   now: any PROXY wins, errors go DIRECT, lists and apps come first. */
static const wchar_t TIP_PAC[] =
    L"Активные PAC проверяются вместе: если хотя бы один отвечает PROXY, HTTP, "
    L"SOCKS или SOCKS5, соединение идёт через выбранный сервер Utgard. "
    L"Напрямую — только если все ответили DIRECT. Адреса прокси из самих "
    L"PAC-файлов не используются.\n\n"
    L"Списки сайтов и приложений, локальная сеть и правила рабочего VPN "
    L"проверяются раньше PAC.\n\n"
    L"Имена, которые PAC отправляет в VPN, разрешаются DNS-сервером Utgard "
    L"с первого запроса, остальные — системным DNS.\n\n"
    L"Если PAC не удалось вычислить, соединение идёт напрямую, а число "
    L"ошибок показывается на этой странице.\n\n"
    L"При включённом VPN изменения PAC применяются на лету. Переподключение "
    L"нужно, только когда включается первый PAC или выключается последний, — "
    L"Utgard сделает его сам.";

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
    L"Запросы шифруются — провайдер их не видит.\n\n"
    L"HTTP/3 и HTTP/2 — два способа отправить запрос. HTTP/3 обычно "
    L"соединяется быстрее, но в некоторых сетях его блокируют. HTTP/2 работает "
    L"почти везде. Если сайты через VPN не открываются — выберите HTTP/2.";

static const wchar_t TIP_HOSTS[] =
    L"Update Hosts File — обновление файла hosts для починки веб-версии "
    L"телеграма и подключения к голосовому чату Discord.";

static void window_show(HWND hwnd)
{
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    startup_notice_shown(hwnd);
}

static void tray_menu(HWND hwnd)
{
    HMENU menu = menu_create();
    POINT pt;
    UINT  cmd;

    menu_add(menu, ID_TRAY_OPEN, L"Открыть Utgard", 0);
    menu_add(menu, ID_TRAY_VPN, g_vpn_on ? L"Выключить VPN" : L"Включить VPN",
             !(g_prof.count && !g_busy));
    menu_separator(menu);
    menu_add(menu, ID_TRAY_EXIT, L"Выход", 0);

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

static void set_fonts(void)
{
    SendMessageW(g_set_theme,  WM_SETFONT, (WPARAM)g_font, TRUE);
    hl_fonts();
    if (g_tip) {
        RECT m = { scaled(12), scaled(8), scaled(12), scaled(8) };
        SendMessageW(g_tip, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_tip, TTM_SETMARGIN, 0, (LPARAM)&m);
    }
    SendMessageW(g_zap_search, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_zap_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Найти стратегию");
    SendMessageW(g_pk_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Найти приложение");
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
    SendMessageW(g_btn_pac,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_list,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_back,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_file,   WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_url,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_toggle, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_refresh, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_delete, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_pac_help,   WM_SETFONT, (WPARAM)g_font, TRUE);
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

/* A release page in the user's browser - started without our elevation,
   as for the update check. If that fails, the address is shown instead. */
static void open_release(HWND hwnd, const wchar_t *url)
{
    wchar_t msg[400];
    if (shell_open_unelevated(url)) return;
    StringCchPrintfW(msg, 400, L"Не удалось открыть браузер. Адрес страницы:\n\n%s", url);
    problem(hwnd, msg);
}

/* WM_CREATE: every control, the tooltips, the state loaded from disk. */
static LRESULT on_create(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)msg; (void)wp; (void)lp;
    g_dpi = (int)GetDpiForWindow(hwnd);
    fonts_create();
    theme_caption(hwnd);
    {
        static const wchar_t *names[NAV_COUNT] = {
            L"Подключение", L"Серверы", L"Маршрутизация", L"zapret", L"Настройки" };
        int k;
        for (k = 0; k < NAV_COUNT; k++)
            g_nav[k] = make_button(hwnd, names[k], ID_NAV_FIRST + k, BK_NAV);
    }
    settings_create(hwnd);
    zapret_create_list(hwnd);
    servers_create(hwnd);
    lists_create(hwnd);
    hl_create(hwnd);
    connect_create(hwnd);
    pac_create(hwnd);
    zapret_create(hwnd);

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
            SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)scaled(360));
            /* Unthemed, so the app draws it whole (NM_CUSTOMDRAW below). */
            SetWindowTheme(g_tip, L"", L"");
            ZeroMemory(&ti, sizeof ti);
            ti.cbSize   = sizeof ti;
            ti.uFlags   = TTF_SUBCLASS;
            ti.hwnd     = hwnd;
            for (i = 0; i < 7; i++) {
                ti.uId     = (UINT_PTR)i;
                ti.lpszText = (LPWSTR)tips[i];
                SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
            /* The PAC help is long: it stays until the pointer leaves
               (32767 ms is the longest delay the control accepts). */
            ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
            ti.uId      = (UINT_PTR)g_pac_help;
            ti.lpszText = (LPWSTR)TIP_PAC;
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            /* The filter choices and the list buttons are windows of their
               own: the pointer over them never reaches the rects above. */
            for (i = 0; i < 4; i++) {
                ti.uId = (UINT_PTR)g_zg[i]; ti.lpszText = (LPWSTR)TIP_GAME;
                SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
            for (i = 0; i < 3; i++) {
                ti.uId = (UINT_PTR)g_zi[i]; ti.lpszText = (LPWSTR)TIP_IPSET;
                SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
            ti.uId = (UINT_PTR)g_zap_ipupd; ti.lpszText = (LPWSTR)TIP_IPUPD;
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            ti.uId = (UINT_PTR)g_zap_hosts; ti.lpszText = (LPWSTR)TIP_HOSTS;
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELPARAM(32767, 0));
        }
    }
    {
        /* Theme-drawn scroll bars and rounded corners for every scrolling
           control that sits in a card. */
        HWND lists[] = { g_list, g_plist, g_alist, g_pk_list, g_hedit, g_pac_list, g_hl_list };
        size_t k;
        for (k = 0; k < sizeof lists / sizeof lists[0]; k++) scroll_attach(lists[k]);
    }
    set_fonts();
    theme_apply(hwnd);
    {
        wchar_t remembered[ZAPRET_PATH_MAX];
        if (g_set.zapret_path[0] &&
            MultiByteToWideChar(CP_UTF8, 0, g_set.zapret_path, -1, remembered, ZAPRET_PATH_MAX) > 0)
            zapret_scan(remembered, &g_zap);
    }
    if (!profiles_load(&g_prof, g_prof_aside, MAX_PATH * 2))
        g_prof_unreadable = 1;
    profiles_reload();
    {
        static const wchar_t *dirs[] = { L"core", L"list", L"list\\pac",
                                         L"list\\applications", L"logs" };
        wchar_t d[MAX_PATH * 2];
        size_t  k;

        for (k = 0; k < sizeof dirs / sizeof dirs[0]; k++)
            if (root_file(dirs[k], d, MAX_PATH * 2)) CreateDirectoryW(d, NULL);
        /* Checked on every start, not only the first: a folder deleted
           between runs comes back. */
        apps_prepare();
    }
    singbox_seed_config();
    /* sing-box\ was moved by seed_config; amneziawg\ moves here, before the
       service cleanup compares service paths. */
    coredir_migrate(&CORE_AWG);
    /* A tunnel service left by a crash is removed; one still running
       belongs to a VPN that is still on. */
    awgsvc_cleanup();
    g_awg_ready = awgcore_present();
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
    theme_ask();
    strategies_reload();
    status_refresh();
    SetTimer(hwnd, TIMER_STATUS, 2000, NULL);
    layout(hwnd);
    return 0;
}

/* WM_COMMAND: buttons, lists and menu items. */
static LRESULT on_command(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)msg; (void)wp; (void)lp;
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
    if (hl_command(hwnd, LOWORD(wp), HIWORD(wp))) return 0;
    if (LOWORD(wp) >= ID_SEL_FIRST && LOWORD(wp) < ID_SEL_FIRST + SEL_COUNT) {
        HWND field = g_sel[LOWORD(wp) - ID_SEL_FIRST];
        select_open(hwnd, field, (HWND)GetPropW(field, L"utgard.combo"));
        return 0;
    }
    if (LOWORD(wp) >= ID_ED_PBROWSE && LOWORD(wp) < ID_ED_PBROWSE + ED_ROWS) {
        wchar_t path[MAX_PATH];
        if (pick_file(hwnd, L"Выбрать программу", L"Программы (*.exe)", L"*.exe", path, MAX_PATH))
            SetWindowTextW(g_ed_path[LOWORD(wp) - ID_ED_PBROWSE], path);
        return 0;
    }
    if (LOWORD(wp) >= ID_ZG_FIRST && LOWORD(wp) < ID_ZG_FIRST + 4) {
        act_zap_game_to(hwnd, LOWORD(wp) - ID_ZG_FIRST);
        return 0;
    }
    if (LOWORD(wp) >= ID_ZI_FIRST && LOWORD(wp) < ID_ZI_FIRST + 3) {
        static const int modes[3] = { IPSET_NONE, IPSET_LOADED, IPSET_ANY };
        act_zap_ipset_to(hwnd, modes[LOWORD(wp) - ID_ZI_FIRST]);
        return 0;
    }
    if (LOWORD(wp) == ID_ZAP_SEARCH) {
        if (HIWORD(wp) == EN_CHANGE) { strategies_filter(); layout(hwnd); }
        return 0;
    }
    if (LOWORD(wp) == ID_ZAP_AGAIN) { act_restart(hwnd); return 0; }
    if ((LOWORD(wp) == ID_SET_LOG || LOWORD(wp) == ID_SET_STACK || LOWORD(wp) == ID_SET_DNS ||
         LOWORD(wp) == ID_SET_SUB || LOWORD(wp) == ID_SET_THEME) && HIWORD(wp) == CBN_SELCHANGE) {
        set_save(hwnd);
        return 0;
    }
    if (LOWORD(wp) == ID_SET_MTU && HIWORD(wp) == EN_KILLFOCUS && g_page == PAGE_SETTINGS) {
        set_save(hwnd);
        return 0;
    }
    if (((LOWORD(wp) >= ID_NAV_FIRST && LOWORD(wp) < ID_NAV_FIRST + NAV_COUNT) ||
         LOWORD(wp) == ID_ROW_SERVER || LOWORD(wp) == ID_TAB_SITES ||
         LOWORD(wp) == ID_TAB_APPS || LOWORD(wp) == ID_TAB_PAC) && !page_leave_ok(hwnd))
        return 0;
    switch (LOWORD(wp)) {
    case ID_NAV_FIRST + NAV_CONNECT:
    case ID_NAV_FIRST + NAV_SERVERS:
        KillTimer(hwnd, TIMER_PICK);
        g_page = LOWORD(wp) == ID_NAV_FIRST + NAV_CONNECT ? PAGE_UTGARD : PAGE_SERVERS;
        lists_refresh_counts();
        layout(hwnd);
        return 0;
    case ID_ROW_SERVER:
        g_page = PAGE_SERVERS;
        layout(hwnd);
        return 0;
    case ID_NAV_FIRST + NAV_ROUTING:
    case ID_TAB_SITES:  act_edit_hosts(hwnd); return 0;
    case ID_TAB_APPS:   act_edit_apps(hwnd); return 0;
    case ID_TAB_PAC:    pac_open_page(hwnd); return 0;
    case ID_NAV_FIRST + NAV_SETTINGS: set_open(hwnd); return 0;
    case ID_PING_NOW:
        if (!g_ping_busy) ping_start(hwnd);
        return 0;
    case ID_SET_UPD_NOW: upd_start(hwnd, 1); return 0;
    case ID_SET_V_UTGARD:  open_release(hwnd, UTGARD_RELEASE_URL);        return 0;
    case ID_SET_V_SINGBOX: open_release(hwnd, CORE_SINGBOX.release_page); return 0;
    case ID_SET_V_AWG:     open_release(hwnd, CORE_AWG.release_page);     return 0;
    case ID_SET_TRAY:
    case ID_SET_AUTO:
    case ID_SET_UPD: {
        HWND box = (LOWORD(wp) == ID_SET_TRAY) ? g_set_tray
                 : (LOWORD(wp) == ID_SET_AUTO) ? g_set_auto : g_set_upd;
        if (GetPropW(box, L"utgard.checked")) RemovePropW(box, L"utgard.checked");
        else SetPropW(box, L"utgard.checked", (HANDLE)1);
        InvalidateRect(box, NULL, FALSE);
        set_save(hwnd);
        return 0;
    }
    case ID_SET_ADV:
        g_set_adv_open = !g_set_adv_open;
        layout(hwnd);
        return 0;
    case ID_SET_BACK: g_page = PAGE_UTGARD; layout(hwnd); return 0;
    case ID_SET_SAVE: set_save(hwnd); return 0;

    case ID_NAV_FIRST + NAV_ZAPRET:
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
    case ID_PAC:        pac_open_page(hwnd); return 0;
    case ID_PAC_BACK:   pac_back(hwnd); return 0;
    case ID_PAC_FILE:   pac_add_file(hwnd); return 0;
    case ID_PAC_URL:    pac_add_url(hwnd); return 0;
    case ID_PAC_TOGGLE: pac_action(hwnd, PAC_UI_TOGGLE); return 0;
    case ID_PAC_REFRESH: pac_action(hwnd, PAC_UI_REFRESH); return 0;
    case ID_PAC_DELETE: pac_action(hwnd, PAC_UI_DELETE); return 0;

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
        /* One click chooses the server, a double click switches to it. */
        if (HIWORD(wp) == LBN_DBLCLK)         act_profile_activate(hwnd);
        else if (HIWORD(wp) == LBN_SELCHANGE) act_profile_pick(hwnd);
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
    }
    return 0;
}

/* WM_APP_INSTALL: a core download finished. */
static LRESULT on_install_done(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    install_job *job = (install_job *)lp;
    (void)msg; (void)wp;

    g_installing = 0;
    if (!job->ok && !job->awg) g_awg_after_singbox = 0;   /* nothing to follow */
    if (!job->ok)
        problem(hwnd, job->msg[0] ? job->msg
                                  : job->awg ? L"Не удалось загрузить AmneziaWG"
                                             : L"Не удалось установить sing-box");
    else if (job->awg) {
        /* Asked for on the way to switching on, or to another profile:
           carry on with that. Asked for at start: nothing more to do. */
        g_awg_ready = 1;
        if (job->resume == 2 && g_vpn_on && g_switch_pending >= 0)
            vpn_restart(hwnd, g_switch_pending);
        else if (job->resume == 1 && !g_vpn_on) act_vpn(hwnd);
    }
    {
        int after_singbox = job->ok && !job->awg;
        free(job);
        layout(hwnd);
        /* Asked together at start: AmneziaWG follows sing-box. */
        if (after_singbox && g_awg_after_singbox) {
            g_awg_after_singbox = 0;
            offer_awg_download(hwnd);
        }
    }
    return 0;
}

/* WM_TIMER: status refresh, subscription and PAC refresh, first-run questions. */
static LRESULT on_timer(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)msg; (void)wp; (void)lp;
    if (wp == TIMER_SUB) { sub_auto_check(hwnd); pac_auto_check(hwnd); return 0; }
    if (wp == TIMER_NOTICE) { startup_notice_timer(hwnd); return 0; }
    if (wp == TIMER_PICK) {
        if (g_page == PAGE_PICK) pk_refresh();
        else                     KillTimer(hwnd, TIMER_PICK);
        return 0;
    }
    if (wp != TIMER_STATUS) return 0;
    pac_unreadable_notice(hwnd);
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
        if (g_page == PAGE_UTGARD || g_page == PAGE_PAC) layout(hwnd);
    }
    {
        pac_status_record pac_now;
        int valid = g_vpn_on && pacstatus_read(&pac_now) &&
                    pac_now.active_count > 0;
        if (valid != g_pac_status_valid ||
            (valid && memcmp(&pac_now, &g_pac_status, sizeof pac_now))) {
            g_pac_status_valid = valid;
            if (valid) g_pac_status = pac_now;
            if (g_page == PAGE_PAC) InvalidateRect(hwnd, NULL, TRUE);
        }
    }
    /* An AmneziaWG tunnel with the VPN off is left over: stop it. */
    vpn_reap_orphan(hwnd);
    return 0;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: return on_create(hwnd, msg, wp, lp);

    case WM_COMMAND: return on_command(hwnd, msg, wp, lp);

    case WM_APP_SUB_DONE: {
        sub_job *job = (sub_job *)lp;

        g_sub_busy = 0;
        /* Only a load that gave profiles restarts the interval: an error page
           served with HTTP 200 is a failure too, retried like a network one. */
        if (job->ok && subscription_apply(hwnd, job->url, job->body, job->len, job->silent)) {
            settings_load(&g_set);
            g_set.sub_last = _time64(NULL);
            settings_save(&g_set);
            g_sub_retry = 0;
        } else if (job->ok) {
            if (job->silent) g_sub_retry = _time64(NULL) + 15 * 60;
        } else if (job->silent) {
            g_sub_retry = _time64(NULL) + 15 * 60;
        } else {
            problem(hwnd, job->err);
        }

        /* The body is the subscription as served: server addresses, keys. */
        if (job->body) SecureZeroMemory(job->body, job->len);
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

    case WM_APP_JOB_STAGE:
        if (g_busy) {
            g_busy_text = (const wchar_t *)lp;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_APP_INSTALL_ASK:
        startup_notice_begin(hwnd);
        return 0;

    case WM_APP_INSTALL: return on_install_done(hwnd, msg, wp, lp);

    case WM_APP_EXC_START:
        exc_check_start(hwnd);
        sub_auto_check(hwnd);        /* due already if the PC was off a while */
        pac_auto_check(hwnd);
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
        unsigned gen = (unsigned)(wp >> 8);
        int      idx = (int)(wp & 0xFF);
        if (gen == (unsigned)g_ping_gen && idx < PROFILES_MAX) {
            g_ping[idx] = (int)lp;
            InvalidateRect(g_plist, NULL, TRUE);
        }
        return 0;
    }

    case WM_APP_PING_DONE:
        if ((unsigned)wp == (unsigned)g_ping_gen) g_ping_busy = 0;
        layout(hwnd);               /* re-enables the refresh button too */
        return 0;

    case WM_MEASUREITEM:
        if (menu_measure((MEASUREITEMSTRUCT *)lp)) return TRUE;
        break;

    case WM_DRAWITEM: {
        if (menu_draw((const DRAWITEMSTRUCT *)lp)) return TRUE;
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_LISTBOX) {
            if (d->CtlID == ID_PROFILES)      draw_profile(d);
            else if (d->CtlID == ID_HL_LIST)   draw_host_row(d);
            else if (d->CtlID == ID_APPS_LIST) draw_app_row(d);
            else if (d->CtlID == ID_PICK_LIST) draw_pick_row(d);
            else                               draw_strategy(d);
        } else {
            draw_button(d);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR *h = (NMHDR *)lp;
        if (h && h->hwndFrom == g_tip) {
            if (h->code == NM_CUSTOMDRAW) return tip_draw((NMTTCUSTOMDRAW *)lp);
            if (h->code == TTN_SHOW) { tip_shape(g_tip); return FALSE; }
            break;
        }
        if (h && h->idFrom == ID_PAC_LIST && h->code == NM_CUSTOMDRAW)
            return pac_row_draw((NMLVCUSTOMDRAW *)lp);
        if (h && h->idFrom == ID_PAC_LIST) {
            if (h->code == LVN_ITEMCHANGED) layout(hwnd);
            else if (h->code == NM_DBLCLK && pac_selected() >= 0)
                pac_action(hwnd, PAC_UI_TOGGLE);
            return 0;
        }
        break;
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

    case WM_TIMER: return on_timer(hwnd, msg, wp, lp);

    case WM_DPICHANGED: {
        const RECT *sug = (const RECT *)lp;
        g_dpi = (int)HIWORD(wp);
        fonts_destroy();
        fonts_create();
        set_fonts();
        SendMessageW(g_list,  LB_SETITEMHEIGHT, 0, (LPARAM)scaled(30));
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)scaled(44));
        theme_ask();
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        return 0;
    }

    case WM_SIZE:
        layout(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
        hl_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_SETCURSOR:
        /* The letter strip is painted, not a control: show that it clicks. */
        if ((HWND)wp == hwnd && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (hl_over_strip(pt.x, pt.y)) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        RECT        want = { 0, 0, 0, 0 };
        want.right = scaled(WIN_W_MIN); want.bottom = scaled(WIN_H_MIN);
        AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);
        mm->ptMinTrackSize.x = want.right - want.left;
        mm->ptMinTrackSize.y = want.bottom - want.top;
        {
            /* At 250% on a 1080p screen the minimum is bigger than the
               screen itself: never ask for more than the work area. */
            RECT work;
            if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
                if (mm->ptMinTrackSize.x > work.right - work.left)
                    mm->ptMinTrackSize.x = work.right - work.left;
                if (mm->ptMinTrackSize.y > work.bottom - work.top)
                    mm->ptMinTrackSize.y = work.bottom - work.top;
            }
        }
        return 0;
    }

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
    /* WS_CLIPCHILDREN: the window's own painting must never cover its
       controls - without it a repaint of the page (a background job
       finishing, say) left them blank until the window was moved. */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

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

    /* Runtime DLL loads from System32 only (static imports are resolved
       before this runs; the folder's permissions guard those). */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    gfx_startup();
    fonts_load_embedded();   /* before any font is created */
    settings_load(&g_set);
    {
        /* zapret-path.txt of older versions moves into settings.txt; the
           old file goes only once the new one is written. */
        wchar_t old[ZAPRET_PATH_MAX];
        if (!g_set.zapret_path[0] && zapret_path_load_legacy(old, ZAPRET_PATH_MAX)) {
            if (WideCharToMultiByte(CP_UTF8, 0, old, -1, g_set.zapret_path,
                                    (int)sizeof g_set.zapret_path, NULL, NULL) > 0) {
                if (settings_save(&g_set)) zapret_path_forget_legacy();
            } else {
                g_set.zapret_path[0] = '\0';      /* nothing half-converted kept */
            }
        }
    }
    theme_pick();
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
    {
        /* The design size, trimmed to the work area: at 250% on a 1080p
           screen even the minimum does not fit, and the page scrolls. */
        RECT work;
        int  w = scaled(WIN_W_DEF), h = scaled(WIN_H_DEF);
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
            if (w > work.right - work.left) w = work.right - work.left;
            if (h > work.bottom - work.top - scaled(40)) h = work.bottom - work.top - scaled(40);
        }
        want.left = 0; want.top = 0; want.right = w; want.bottom = h;
    }
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
    pac_unreadable_notice(hwnd);

    if (g_prof_unreadable) {
        wchar_t        msg[MAX_PATH * 2 + 512];
        const wchar_t *name = wcsrchr(g_prof_aside, L'\\');

        if (g_prof_aside[0])
            StringCchPrintfW(msg, sizeof msg / sizeof msg[0],
                L"Не удалось прочитать сохранённые серверы: файл создан другой "
                L"учётной записью Windows, на другом компьютере или более новой "
                L"версией Utgard, либо повреждён.\n\n"
                L"Файл не удалён: он переименован в %s рядом с utgard.exe. "
                L"Список серверов начат заново.",
                name ? name + 1 : g_prof_aside);
        else
            StringCchCopyW(msg, sizeof msg / sizeof msg[0],
                L"Не удалось прочитать сохранённые серверы (profiles.dat), и файл "
                L"не получилось переименовать — возможно, он занят другой программой.\n\n"
                L"Чтобы не перезаписать его, изменения серверов до перезапуска "
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
    gfx_shutdown();
    return (int)msg.wParam;
}
