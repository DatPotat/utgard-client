/*
 * Utgard client - Window procedure, tray menu, entry point and the shared state.
 */

#include "coremanifest.h"
#include "shellopen.h"
#include "awgcore.h"
#include "awgsvc.h"
#include "pacstore.h"
#include "ui.h"

/* ---- shared state: every module sees it through the externs in ui.h -- */

int    g_dpi  = USER_DEFAULT_SCREEN_DPI;

int    g_page = PAGE_UTGARD;

HFONT  g_font, g_font_big, g_font_small;
HFONT  g_font_bold, g_font_small_bold, g_font_title, g_font_deco;
HWND   g_set_adv;
HWND   g_sel[SEL_COUNT];
int    g_set_adv_open;
HWND   g_zap_search, g_zap_again, g_zg[4], g_zi[3];
HWND   g_nav[NAV_COUNT], g_row_server, g_tab_sites, g_tab_apps, g_tab_pac, g_set_theme;

HBRUSH g_brush_bg, g_brush_footer, g_brush_surface, g_brush_line;

HWND g_toggle, g_pick_path, g_list;

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
int g_awg_lost;
int g_awg_ready;
int g_switch_pending = -1;
int g_switch_note;

HWND g_btn_hosts, g_btn_apps, g_btn_pac, g_zap_fix;
HWND g_pac_list, g_pac_back, g_pac_file, g_pac_url, g_pac_toggle;
HWND g_pac_refresh, g_pac_delete, g_pac_help;

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

HWND    g_ed_path[ED_ROWS], g_ed_pplus[ED_ROWS], g_ed_pminus[ED_ROWS], g_ed_pbrowse[ED_ROWS];

HWND    g_ed_back, g_ed_save;

int     g_ed_ncount = 1, g_ed_pcount = 1;

wchar_t g_ed_orig[APPS_NAME_MAX];     /* empty when creating */

app_settings g_set;

static UINT         g_taskbar_created;   /* Explorer restarted */

HWND    g_set_open, g_set_mtu, g_set_log, g_set_back, g_set_save;

HWND    g_set_upd, g_set_upd_now;
HWND    g_set_v_utgard, g_set_v_singbox, g_set_v_awg;

HWND    g_set_stack, g_set_dns, g_set_tray, g_set_auto, g_set_sub, g_ping_now, g_zap_list;

long long g_sub_retry;   /* after a failed automatic refresh, not before */

int  g_exc_known, g_exc_present;

int  g_zap_dirty;   /* Game Filter changed: it lives in winws arguments; lists are reread live */

int            g_busy;        /* a background job is running */

const wchar_t *g_busy_text;   /* what it is doing, for the status line */

int  g_host_count, g_app_count, g_pac_count;
pac_status_record g_pac_status;
int g_pac_status_valid;

profile_store g_prof;
/* Set by WM_CREATE, reported once the window exists: a message box inside
   WM_CREATE would run a modal loop before the window is fully built. */
static int     g_prof_unreadable;
static wchar_t g_prof_aside[MAX_PATH * 2];

HFONT g_font_mono;

zapret_info   g_zap;

zapret_status g_status;

static void pac_unreadable_notice(HWND hwnd)
{
    wchar_t aside[MAX_PATH * 2], text[MAX_PATH * 2 + 400];
    const wchar_t *name;
    if (!pacstore_unreadable_notice(aside, MAX_PATH * 2)) return;
    name = wcsrchr(aside, L'\\');
    if (aside[0])
        StringCchPrintfW(text, sizeof text / sizeof text[0],
            L"Не удалось прочитать pac.json. Файл не удалён: он переименован в %s рядом с utgard.exe. Настройки PAC начаты заново.",
            name ? name + 1 : aside);
    else
        StringCchCopyW(text, sizeof text / sizeof text[0],
            L"Не удалось прочитать pac.json и переименовать его. Чтобы не потерять файл, настройки PAC не будут сохраняться до перезапуска Utgard.");
    problem(hwnd, text);
}

int     g_count;

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

int  g_upd_pending;     /* found while hidden in the tray: ask on show */
int  g_awg_after_singbox; /* AmneziaWG said yes at start while sing-box downloads */

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
        RECT m = { S(12), S(8), S(12), S(8) };
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
    g_row_server = make_button_on(hwnd, L"", ID_ROW_SERVER, BK_ROW, BACK_CARD);
    g_tab_sites  = make_button(hwnd, L"Сайты", ID_TAB_SITES, BK_TAB);
    g_tab_apps   = make_button(hwnd, L"Приложения", ID_TAB_APPS, BK_TAB);
    g_tab_pac    = make_button(hwnd, L"Правила PAC", ID_TAB_PAC, BK_TAB);
    g_ping_now   = make_button(hwnd, L"Проверить задержку", ID_PING_NOW, BK_SECONDARY);
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
            SendMessageW(g_set_dns, CB_ADDSTRING, 0, (LPARAM)settings_dns[i].label);
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

        g_set_theme = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)ID_SET_THEME, inst, NULL);

        g_set_tray = make_button_on(hwnd, L"Сворачивать в трей при закрытии",
                                 ID_SET_TRAY, BK_CHECK, BACK_CARD);
        g_set_auto = make_button_on(hwnd, L"Запускать вместе с Windows",
                                 ID_SET_AUTO, BK_CHECK, BACK_CARD);
        g_set_upd  = make_button_on(hwnd, L"Сообщать о новых версиях",
                                 ID_SET_UPD, BK_CHECK, BACK_CARD);
        g_set_upd_now = make_button(hwnd, L"Проверить обновления", ID_SET_UPD_NOW,
                                    BK_SECONDARY);
        /* The versions in use, each a link to its own release page. */
        g_set_v_utgard  = make_button_on(hwnd, L"Utgard " UTGARD_VERSION_W, ID_SET_V_UTGARD, BK_LINK, BACK_CARD);
        g_set_v_singbox = make_button_on(hwnd, L"", ID_SET_V_SINGBOX, BK_LINK, BACK_CARD);
        g_set_v_awg     = make_button_on(hwnd, L"", ID_SET_V_AWG, BK_LINK, BACK_CARD);

        g_set_adv  = make_button_on(hwnd, L"", ID_SET_ADV, BK_ROW, BACK_CARD);
        {
            /* Each drop-down field stands for a hidden combo box. */
            HWND combos[SEL_COUNT];
            int  k;
            combos[SEL_DNS] = g_set_dns; combos[SEL_SUB] = g_set_sub; combos[SEL_THEME] = g_set_theme;
            combos[SEL_LOG] = g_set_log; combos[SEL_STACK] = g_set_stack;
            for (k = 0; k < SEL_COUNT; k++) {
                g_sel[k] = make_button_on(hwnd, L"", ID_SEL_FIRST + k, BK_SELECT, BACK_CARD);
                SetPropW(g_sel[k], L"utgard.combo", (HANDLE)combos[k]);
            }
        }
        g_set_back = make_button_on(hwnd, L"Назад", ID_SET_BACK, BK_SECONDARY, BACK_FOOTER);
        g_set_save = make_button_on(hwnd, L"Сохранить", ID_SET_SAVE, BK_PRIMARY, BACK_FOOTER);
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
    SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(44));
    SetWindowTheme(g_plist, L"DarkMode_Explorer", NULL);
    list_hover_attach(g_plist);
    g_prof_add = make_button_on(hwnd, L"Добавить сервер…", ID_PROF_ADD,
                                BK_PRIMARY, BACK_PAGE);
    g_prof_del = make_button_on(hwnd, L"Удалить", ID_PROF_DEL,
                                BK_DANGER, BACK_PAGE);
    g_prof_sub = make_button_on(hwnd, L"Подписка…", ID_PROF_SUB,
                                BK_SECONDARY, BACK_PAGE);
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
                                  BK_SECONDARY, BACK_FOOTER);
    g_app_pick   = make_button_on(hwnd, L"Выбрать из запущенных…",
                                  ID_APPS_PICK, BK_PRIMARY, BACK_PAGE);
    g_app_manual = make_button_on(hwnd, L"Добавить вручную…",
                                  ID_APPS_MANUAL, BK_SECONDARY, BACK_PAGE);
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
            g_ed_pbrowse[i] = make_button_on(hwnd, L"Выбрать exe-файл…", ID_ED_PBROWSE + i,
                                             BK_ICON, BACK_CARD);
            g_ed_nplus[i]  = make_button(hwnd, L"+", ID_ED_NPLUS + i, BK_SECONDARY);
            g_ed_nminus[i] = make_button(hwnd, L"−", ID_ED_NMINUS + i, BK_DANGER);
            g_ed_pplus[i]  = make_button(hwnd, L"+", ID_ED_PPLUS + i, BK_SECONDARY);
            g_ed_pminus[i] = make_button(hwnd, L"−", ID_ED_PMINUS + i, BK_DANGER);
        }
        g_ed_back = make_button_on(hwnd, L"Назад", ID_ED_BACK, BK_SECONDARY, BACK_FOOTER);
        g_ed_save = make_button_on(hwnd, L"Сохранить", ID_ED_SAVE, BK_PRIMARY, BACK_FOOTER);
    }
    g_pk_back = make_button_on(hwnd, L"Назад", ID_PICK_BACK, BK_SECONDARY, BACK_FOOTER);
    g_pk_save = make_button_on(hwnd, L"Сохранить приложение", ID_PICK_SAVE,
                               BK_PRIMARY, BACK_FOOTER);
    g_h_back = make_button_on(hwnd, L"Назад", ID_HOSTS_BACK, BK_SECONDARY, BACK_PAGE);
    g_h_tidy = make_button_on(hwnd, L"Убрать дубли", ID_HOSTS_TIDY,
                              BK_SECONDARY, BACK_PAGE);
    g_h_save = make_button_on(hwnd, L"Сохранить", ID_HOSTS_SAVE, BK_PRIMARY, BACK_PAGE);
    hl_create(hwnd);
    g_btn_hosts = make_button_on(hwnd, L"Сайты", ID_EDIT_HOSTS, BK_ROW, BACK_CARD);
    g_btn_apps  = make_button_on(hwnd, L"Приложения", ID_EDIT_APPS, BK_ROW, BACK_CARD);
    g_btn_pac   = make_button_on(hwnd, L"Правила PAC", ID_PAC, BK_ROW, BACK_CARD);
    {
        LVCOLUMNW column;
        static const wchar_t *titles[] = { L"Источник", L"Тип", L"Состояние" };
        static const int widths[] = { 300, 70, 160 };
        int i;
        INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
        g_pac_list = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PAC_LIST,
            (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
        ListView_SetExtendedListViewStyle(g_pac_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        /* Drawn like the servers table: the page paints the headings, each
           row is drawn whole (pac_row_draw); the columns only hold the text.
           A one-pixel-wide image list sets the row height. */
        SetWindowLongPtrW(g_pac_list, GWL_STYLE, GetWindowLongPtrW(g_pac_list, GWL_STYLE) | LVS_NOCOLUMNHEADER);
        ListView_SetImageList(g_pac_list, ImageList_Create(1, S(44), ILC_COLOR32, 1, 0), LVSIL_SMALL);
        SetWindowTheme(g_pac_list, L"DarkMode_Explorer", NULL);
        ListView_SetBkColor(g_pac_list, CLR_SURFACE);
        ListView_SetTextBkColor(g_pac_list, CLR_SURFACE);
        ListView_SetTextColor(g_pac_list, CLR_TEXT);
        SetWindowSubclass(g_pac_list, pac_list_proc, 1, 0);
        ZeroMemory(&column, sizeof column);
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        for (i = 0; i < 3; i++) {
            column.iSubItem = i; column.pszText = (LPWSTR)titles[i]; column.cx = S(widths[i]);
            ListView_InsertColumn(g_pac_list, i, &column);
        }
    }
    g_pac_back = make_button_on(hwnd, L"Назад", ID_PAC_BACK, BK_SECONDARY, BACK_FOOTER);
    g_pac_file = make_button_on(hwnd, L"Добавить файл…", ID_PAC_FILE, BK_SECONDARY, BACK_PAGE);
    g_pac_url = make_button_on(hwnd, L"Добавить по адресу…", ID_PAC_URL, BK_PRIMARY, BACK_PAGE);
    g_pac_toggle = make_button(hwnd, L"Включить / выключить", ID_PAC_TOGGLE, BK_SECONDARY);
    g_pac_refresh = make_button(hwnd, L"Обновить", ID_PAC_REFRESH, BK_SECONDARY);
    g_pac_delete = make_button(hwnd, L"Удалить", ID_PAC_DELETE, BK_DANGER);
    g_pac_help = make_button(hwnd, L"Как работает PAC", ID_PAC_HELP, BK_LINK);
    g_zap_start   = make_button(hwnd, L"Запустить выбранную", ID_ZAP_START, BK_PRIMARY);
    g_zap_stop    = make_button_on(hwnd, L"Выключить", ID_ZAP_STOP, BK_SECONDARY, BACK_ACCENT);
    g_zap_restart = make_button_on(hwnd, L"Перезапустить", ID_ZAP_RESTART, BK_SECONDARY, BACK_ACCENT);
    g_zap_fix = make_button_on(hwnd, L"Исправить", ID_ZAP_FIX, BK_PRIMARY, BACK_CARD);
    g_zap_game  = make_button(hwnd, L"Game filter", ID_ZAP_GAME, BK_SECONDARY);
    g_zap_ipset = make_button(hwnd, L"IPSet filter", ID_ZAP_IPSET, BK_SECONDARY);
    g_zap_ipupd = make_button_on(hwnd, L"Обновить", ID_ZAP_IPUPD, BK_SECONDARY, BACK_CARD);
    g_zap_hosts = make_button_on(hwnd, L"Проверить", ID_ZAP_HOSTS, BK_SECONDARY, BACK_CARD);
    g_zap_list  = make_button_on(hwnd, L"Сайты для zapret", ID_ZAP_LIST, BK_ROW, BACK_CARD);
    {
        static const wchar_t *game[4] = { L"Выкл.", L"TCP и UDP", L"TCP", L"UDP" };
        static const wchar_t *ipset[3] = { L"Выкл.", L"По списку", L"Любой адрес" };
        int k;
        for (k = 0; k < 4; k++) g_zg[k] = make_button_on(hwnd, game[k], ID_ZG_FIRST + k, BK_CHIP, BACK_CARD);
        for (k = 0; k < 3; k++) g_zi[k] = make_button_on(hwnd, ipset[k], ID_ZI_FIRST + k, BK_CHIP, BACK_CARD);
    }
    g_zap_again = make_button_on(hwnd, L"Перезапустить", ID_ZAP_AGAIN, BK_SECONDARY, BACK_TINT);
    g_zap_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                   0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_ZAP_SEARCH,
                                   (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
    ask_edit_center(g_zap_search);

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
            SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)S(360));
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
        if (zapret_path_load(remembered, ZAPRET_PATH_MAX))
            zapret_scan(remembered, &g_zap);
    }
    if (!profiles_load(&g_prof, g_prof_aside, MAX_PATH * 2))
        g_prof_unreadable = 1;
    profiles_reload();
    {
        static const wchar_t *dirs[] = { L"sing-box", L"list",
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
    return 0;
}

/* WM_APP_INSTALL: a core download finished. */
static LRESULT on_install_done(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)msg; (void)wp; (void)lp;
{
    install_job *job = (install_job *)lp;

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
        SendMessageW(g_list,  LB_SETITEMHEIGHT, 0, (LPARAM)S(30));
        SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)S(44));
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
        want.right = S(WIN_W_MIN); want.bottom = S(WIN_H_MIN);
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

    gfx_startup();
    fonts_load_embedded();   /* before any font is created */
    settings_load(&g_set);
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
        int  w = S(WIN_W_DEF), h = S(WIN_H_DEF);
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
            if (w > work.right - work.left) w = work.right - work.left;
            if (h > work.bottom - work.top - S(40)) h = work.bottom - work.top - S(40);
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
