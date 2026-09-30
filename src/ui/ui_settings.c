/*
 * Utgard client - Settings page and the update check.
 */

#include "ui.h"

/* State owned by this file (declared in ui.h). */
int g_set_adv_open;

/* State owned by this file (declared in ui.h). */
app_settings g_set;
int g_upd_pending;     /* found while hidden in the tray: ask on show */

/* Controls of this page, created by settings_create. */
HWND g_set_adv;
HWND g_sel[SEL_COUNT];
HWND g_set_theme;
HWND g_set_mtu, g_set_log, g_set_back, g_set_save;
HWND g_set_upd, g_set_upd_now;
HWND g_set_v_utgard, g_set_v_singbox, g_set_v_awg;
HWND g_set_stack, g_set_dns, g_set_tray, g_set_auto, g_set_sub;

void set_open(HWND hwnd)
{
    wchar_t buf[16];

    settings_load(&g_set);
    StringCchPrintfW(buf, 16, L"%d", g_set.mtu);
    SetWindowTextW(g_set_mtu, buf);
    SendMessageW(g_set_log, CB_SETCURSEL, (WPARAM)g_set.log_level, 0);
    SendMessageW(g_set_stack, CB_SETCURSEL, (WPARAM)g_set.stack, 0);
    SendMessageW(g_set_dns, CB_SETCURSEL, (WPARAM)g_set.dns, 0);
    SendMessageW(g_set_sub, CB_SETCURSEL, (WPARAM)g_set.sub_interval, 0);
    {
        /* Light, dark, khokhloma; the one Windows itself uses is marked. */
        int dark = theme_system_is_dark();
        SendMessageW(g_set_theme, CB_RESETCONTENT, 0, 0);
        SendMessageW(g_set_theme, CB_ADDSTRING, 0, (LPARAM)(dark ? L"Светлая" : L"Светлая (как в системе)"));
        SendMessageW(g_set_theme, CB_ADDSTRING, 0, (LPARAM)(dark ? L"Тёмная (как в системе)" : L"Тёмная"));
        SendMessageW(g_set_theme, CB_ADDSTRING, 0, (LPARAM)L"Хохлома");
        SendMessageW(g_set_theme, CB_SETCURSEL, (WPARAM)(g_set.theme - SETTINGS_THEME_LIGHT), 0);
    }
    SetPropW(g_set_tray, L"utgard.checked", (HANDLE)(INT_PTR)(g_set.tray_on_close ? 1 : 0));
    InvalidateRect(g_set_tray, NULL, FALSE);
    SetPropW(g_set_auto, L"utgard.checked", (HANDLE)(INT_PTR)autostart_get());
    InvalidateRect(g_set_auto, NULL, FALSE);
    SetPropW(g_set_upd, L"utgard.checked", (HANDLE)(INT_PTR)(g_set.update_check ? 1 : 0));
    InvalidateRect(g_set_upd, NULL, FALSE);
    g_page = PAGE_SETTINGS;
    layout(hwnd);
}

void set_save(HWND hwnd)
{
    wchar_t buf[16], msg[160];
    long    mtu;
    LRESULT lvl;
    int     theme_was;

    GetWindowTextW(g_set_mtu, buf, 16);
    mtu = wcstol(buf, NULL, 10);
    if (mtu < SETTINGS_MTU_MIN || mtu > SETTINGS_MTU_MAX) {
        StringCchPrintfW(msg, 160, L"MTU должно быть от %d до %d.",
                         SETTINGS_MTU_MIN, SETTINGS_MTU_MAX);
        problem(hwnd, msg);
        SetFocus(g_set_mtu);
        return;
    }

    lvl = SendMessageW(g_set_log, CB_GETCURSEL, 0, 0);
    g_set.mtu       = (int)mtu;
    g_set.log_level = (lvl == CB_ERR) ? SETTINGS_LOG_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_stack, CB_GETCURSEL, 0, 0);
    g_set.stack = (lvl == CB_ERR) ? SETTINGS_STACK_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_dns, CB_GETCURSEL, 0, 0);
    g_set.dns = (lvl == CB_ERR) ? SETTINGS_DNS_DEFAULT : (int)lvl;
    lvl = SendMessageW(g_set_sub, CB_GETCURSEL, 0, 0);
    g_set.sub_interval = (lvl == CB_ERR) ? SETTINGS_SUB_DEFAULT : (int)lvl;
    g_set.tray_on_close = GetPropW(g_set_tray, L"utgard.checked") != NULL;
    g_set.update_check  = GetPropW(g_set_upd, L"utgard.checked") != NULL;
    lvl = SendMessageW(g_set_theme, CB_GETCURSEL, 0, 0);
    theme_was = g_set.theme;
    if (lvl != CB_ERR) g_set.theme = SETTINGS_THEME_LIGHT + (int)lvl;

    if (!settings_save(&g_set)) {
        problem(hwnd, L"Не удалось сохранить настройки");
        return;
    }

    /* The scheduler holds this one; touch it only when the box changed. */
    {
        int want = GetPropW(g_set_auto, L"utgard.checked") != NULL;
        if (want != autostart_get() && !autostart_set(want, msg, 160)) {
            problem(hwnd, msg);
            return;
        }
    }
    /* Applied as it changes: the page stays, the theme repaints only when
       it was the theme that changed. */
    if (theme_was != g_set.theme) theme_apply(hwnd);
    layout(hwnd);
}

static int  g_upd_busy;

static char g_upd_tag[32];

static DWORD WINAPI upd_thread(LPVOID param)
{
    upd_job *j = (upd_job *)param;
    wchar_t  loc[1024];
    char     loc8[1024];

    if (net_redirect(UTGARD_RELEASES_URL, loc, 1024, j->err, 256)) {
        if (WideCharToMultiByte(CP_UTF8, 0, loc, -1, loc8, sizeof loc8, NULL, NULL) &&
            update_tag_from_location(loc8, j->tag, sizeof j->tag))
            j->ok = 1;
        else
            StringCchCopyW(j->err, 256, L"GitHub ответил неожиданным адресом");
    }
    PostMessageW(j->hwnd, WM_APP_UPD_DONE, 0, (LPARAM)j);
    return 0;
}

void upd_start(HWND hwnd, int manual)
{
    upd_job *j;
    HANDLE   th;

    if (g_upd_busy) return;
    j = (upd_job *)calloc(1, sizeof *j);
    if (!j) return;
    j->hwnd   = hwnd;
    j->manual = manual;
    th = CreateThread(NULL, 0, upd_thread, j, 0, NULL);
    if (!th) { free(j); return; }
    CloseHandle(th);
    g_upd_busy = 1;
    EnableWindow(g_set_upd_now, FALSE);
}

int upd_running(void) { return g_upd_busy; }

/* The question for a found newer release; 0 when there is none to ask. */
int upd_question(wchar_t *text, size_t cap)
{
    wchar_t tag[32];
    if (!g_upd_tag[0] || !update_is_newer(g_upd_tag, UTGARD_VERSION)) return 0;
    /* The tag passed update_tag_from_location: ASCII only. */
    MultiByteToWideChar(CP_UTF8, 0, g_upd_tag, -1, tag, 32);
    return SUCCEEDED(StringCchPrintfW(text, cap, L"Вышла новая версия Utgard: %s (у вас %s).\n\n"
                                                 L"Открыть страницу загрузки?",
                                      tag, UTGARD_VERSION_W));
}

void upd_open_page(HWND hwnd)
{
    if (!shell_open_unelevated(UTGARD_RELEASES_URL))
        modal_box(hwnd, L"Не удалось открыть браузер. Скачайте новую версию здесь "
                          L"(Ctrl+C копирует этот текст):\r\n\r\n" UTGARD_RELEASES_URL,
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
}

void upd_prompt(HWND hwnd)
{
    wchar_t  text[256];
    ask_step step = { L"Обновление", text, L"Открыть страницу", L"Позже", 0 };
    if (!upd_question(text, 256)) return;
    ask_steps(hwnd, L"Utgard", &step, 1);
    if (step.answer) upd_open_page(hwnd);
}

void upd_done(HWND hwnd, upd_job *j)
{
    g_upd_busy = 0;
    EnableWindow(g_set_upd_now, TRUE);

    if (!j->manual && startup_notice_waiting() &&
        (!j->ok || !update_is_newer(j->tag, UTGARD_VERSION))) {
        free(j);
        startup_notice(hwnd);           /* nothing newer: the other questions */
        return;
    }
    if (!j->ok) {
        if (j->manual) problem(hwnd, j->err[0] ? j->err : L"Не удалось проверить обновления");
    } else if (update_is_newer(j->tag, UTGARD_VERSION)) {
        StringCchCopyA(g_upd_tag, sizeof g_upd_tag, j->tag);
        /* At start the answer joins the other first-run questions. */
        if (!j->manual && startup_notice_waiting()) { free(j); startup_notice(hwnd); return; }
        if (j->manual || (IsWindowVisible(hwnd) && !g_modal)) upd_prompt(hwnd);
        else g_upd_pending = 1;
    } else if (j->manual) {
        modal_box(hwnd, L"У вас последняя версия (" UTGARD_VERSION_W L").",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
    }
    free(j);
}

/* The page's controls, in on_create's order: creation order is the
   z-order and the tab order. */
void settings_create(HWND hwnd)
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
