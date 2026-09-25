/*
 * Utgard client - Settings page and the update check.
 */

#include "ui.h"

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
    g_page = PAGE_UTGARD;
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

void upd_prompt(HWND hwnd)
{
    wchar_t text[256], tag[32];

    /* The tag passed update_tag_from_location: ASCII only. */
    MultiByteToWideChar(CP_UTF8, 0, g_upd_tag, -1, tag, 32);
    StringCchPrintfW(text, 256, L"Вышла новая версия Utgard: %s (у вас %s).\r\n\r\n"
                                L"Открыть страницу загрузки?",
                     tag, UTGARD_VERSION_W);
    if (MessageBoxW(hwnd, text, L"Utgard", MB_ICONINFORMATION | MB_YESNO) != IDYES) return;
    if (!shell_open_unelevated(UTGARD_RELEASES_URL))
        MessageBoxW(hwnd, L"Не удалось открыть браузер. Скачайте новую версию здесь "
                          L"(Ctrl+C копирует этот текст):\r\n\r\n" UTGARD_RELEASES_URL,
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
}

void upd_done(HWND hwnd, upd_job *j)
{
    g_upd_busy = 0;
    EnableWindow(g_set_upd_now, TRUE);

    if (!j->ok) {
        if (j->manual) problem(hwnd, j->err[0] ? j->err : L"Не удалось проверить обновления");
    } else if (update_is_newer(j->tag, UTGARD_VERSION)) {
        StringCchCopyA(g_upd_tag, sizeof g_upd_tag, j->tag);
        if (j->manual || (IsWindowVisible(hwnd) && !g_modal)) upd_prompt(hwnd);
        else g_upd_pending = 1;
    } else if (j->manual) {
        MessageBoxW(hwnd, L"У вас последняя версия (" UTGARD_VERSION_W L").",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
    }
    free(j);
}
