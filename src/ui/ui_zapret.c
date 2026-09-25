/*
 * Utgard client - zapret page: strategies, service, VPN exclusions, Game Filter, IPSet, hosts.
 */

#include "ui.h"

static wchar_t g_names[ZAPRET_MAX_STRATEGIES][ZAPRET_NAME_MAX];

/* Reload the strategy list from the current folder. */
void strategies_reload(void)
{
    int i;

    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_count = 0;
    if (!g_zap.valid) return;

    g_count = zapret_list(g_zap.path, g_names, ZAPRET_MAX_STRATEGIES);
    if (g_count > ZAPRET_MAX_STRATEGIES) g_count = ZAPRET_MAX_STRATEGIES;

    for (i = 0; i < g_count; i++)
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_names[i]);

    if (g_count) SendMessageW(g_list, LB_SETCURSEL, 0, 0);
}

/* Name of the strategy highlighted in the list, or NULL when none is. */
const wchar_t *selected_strategy(void)
{
    LRESULT i = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (i == LB_ERR || i < 0 || i >= g_count) return NULL;
    return g_names[i];
}

static void zap_done(HWND hwnd, long_job *j)
{
    if (!j->ok) problem(hwnd, j->msg[0] ? j->msg : L"Операция с zapret не удалась");
    after_action(hwnd);
}

static void work_zap_stop(long_job *j)    { j->ok = zapret_service_remove(j->msg, SB_MSG_MAX); }

static void work_zap_restart(long_job *j) { j->ok = zapret_service_restart(j->msg, SB_MSG_MAX); }

static void work_zap_start(long_job *j)
{
    j->ok = zapret_service_install(j->dir, j->name, j->msg, SB_MSG_MAX);
}

void act_stop(HWND hwnd)
{
    if (g_busy) return;
    g_zap_dirty = 0;
    job_start(hwnd, L"Выключение zapret…", job_new(work_zap_stop, zap_done));
}

/* name == NULL means "whatever is highlighted in the list". */
void act_start(HWND hwnd, const wchar_t *name)
{
    if (g_busy) return;
    if (!name) name = selected_strategy();
    if (!name) {
        problem(hwnd, L"Сначала выберите стратегию в списке");
        return;
    }

    /* Same guard as the greyed-out button, for the double-click path. */
    if (g_status.mode == ZAPRET_SERVICE && _wcsicmp(name, g_status.strategy) == 0)
        return;

    g_zap_dirty = 0;
    {
        long_job *j = job_new(work_zap_start, zap_done);
        if (j) {
            StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
            StringCchCopyW(j->name, ZAPRET_NAME_MAX, name);
        }
        job_start(hwnd, L"Установка службы zapret…", j);
    }
}

void act_restart(HWND hwnd)
{
    if (g_busy) return;
    if (g_status.mode != ZAPRET_SERVICE) {
        /* Nothing installed: restarting means installing what is selected. */
        act_start(hwnd, NULL);
        return;
    }

    g_zap_dirty = 0;
    job_start(hwnd, L"Перезапуск zapret…", job_new(work_zap_restart, zap_done));
}

/* Every IPv4 address behind the profiles. Hostnames go through the system
   resolver - the same path the generated config gives them, since it routes
   their names to "local". IPv6 is skipped: zapret's exclude list is handled
   as v4 here and mixing families would only add cases. */
/* A copy of the server names, taken on the UI thread. The worker resolves
   from this copy only: reading g_prof itself from another thread while the
   window adds, removes or refreshes profiles is a data race. */

static void snapshot_hosts(host_snapshot *snap)
{
    int i;
    snap->count = 0;
    for (i = 0; i < g_prof.count && i < PROFILES_MAX; i++)
        StringCchCopyA(snap->host[snap->count++], 256, g_prof.items[i].link.server);
}

static int collect_server_ips(const host_snapshot *snap, char ips[][16], int max)
{
    int i, n = 0;

    for (i = 0; i < snap->count && n < max; i++) {
        const char *host = snap->host[i];
        if (!host[0] || strchr(host, ':')) continue;

        if (host[0] >= '0' && host[0] <= '9') {
            int j, dup = 0;
            for (j = 0; j < n; j++) if (strcmp(ips[j], host) == 0) dup = 1;
            if (!dup && strlen(host) < 16) { StringCchCopyA(ips[n], 16, host); n++; }
            continue;
        }
        {
            /* Two profiles often sit on one server, and two names can resolve
               to the same address. Each address is written once. */
            char found[16][16];
            int  got = net_resolve4(host, found, 16);
            int  k;

            for (k = 0; k < got && n < max; k++) {
                int j, dup = 0;
                for (j = 0; j < n; j++) if (strcmp(ips[j], found[k]) == 0) dup = 1;
                if (dup) continue;
                StringCchCopyA(ips[n], 16, found[k]);
                n++;
            }
        }
    }
    return n;
}

/* Resolving several hostnames can take seconds, so the check that runs at
   startup does not sit on the UI thread. */
static DWORD WINAPI exc_thread(LPVOID param)
{
    exc_job *job = (exc_job *)param;
    char     ips[64][16];         /* on this thread's stack: two checks at once
                                     must not share one buffer */

    job->total   = collect_server_ips(&job->hosts, ips, 64);
    job->present = zapret_exclude_present(job->dir, ips, job->total);

    PostMessageW(job->hwnd, WM_APP_EXC_DONE, 0, (LPARAM)job);
    return 0;
}

void exc_check_start(HWND hwnd)
{
    exc_job *job;
    HANDLE   th;

    if (!g_zap.valid || g_prof.count == 0) {
        g_exc_known = g_exc_present = 0;
        return;
    }

    job = (exc_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd = hwnd;
    StringCchCopyW(job->dir, ZAPRET_PATH_MAX, g_zap.path);
    snapshot_hosts(&job->hosts);

    th = CreateThread(NULL, 0, exc_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);
}

/* One button for the whole conflict: resolve every profile server through the
   system resolver - the same path the generated config gives them, since it
   routes their names to "local" - and write the addresses into zapret's user
   exclude list. */
static void work_zap_fix(long_job *j)
{
    j->n1 = collect_server_ips(&j->hosts, j->ips, 64);
    if (j->n1 == 0) {
        StringCchCopyW(j->msg, SB_MSG_MAX,
                       L"Не удалось определить адреса серверов. Проверьте подключение к сети.");
        return;
    }
    if (!zapret_exclude_patch(j->dir, (const char (*)[16])j->ips, j->n1,
                              NULL, NULL, j->msg, SB_MSG_MAX))
        return;
    j->n2 = zapret_exclude_present(j->dir, (const char (*)[16])j->ips, j->n1);
    j->ok = 1;
}

static void done_zap_fix(HWND hwnd, long_job *j)
{
    if (!j->ok) {
        problem(hwnd, j->msg[0] ? j->msg : L"Не удалось изменить список исключений");
        return;
    }
    g_exc_known   = j->n1;
    g_exc_present = j->n2;
    /* winws rereads the list on its own, but keeps the verdict of a connection
       it has already seen: the tunnel must redial to be left alone. */
    if (g_vpn_on)
        MessageBoxW(hwnd, L"Адреса серверов добавлены в исключения zapret. "
                          L"Выключите и снова включите VPN, чтобы уже открытое "
                          L"соединение пошло мимо zapret.",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
}

void act_zapret_fix(HWND hwnd)
{
    long_job *j;

    if (g_busy) return;
    if (!g_zap.valid) { problem(hwnd, L"Сначала укажите папку zapret"); return; }
    if (g_prof.count == 0) { problem(hwnd, L"Нет профилей — нечего исключать"); return; }

    j = job_new(work_zap_fix, done_zap_fix);
    if (j) {
        snapshot_hosts(&j->hosts);
        StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
    }
    job_start(hwnd, L"Определение адресов серверов…", j);
}

void act_zap_game(HWND hwnd)
{
    /* Disabled -> TCP and UDP -> TCP only -> UDP only -> disabled, the four
       modes service.bat offers. */
    zapret_game_mode m = zapret_game_get(g_zap.path);
    zapret_game_mode next = (m == GAME_OFF) ? GAME_ALL
                          : (m == GAME_ALL) ? GAME_TCP
                          : (m == GAME_TCP) ? GAME_UDP : GAME_OFF;

    if (!zapret_game_set(g_zap.path, next)) {
        problem(hwnd, L"Не удалось изменить режим игрового фильтра");
        return;
    }
    g_zap_dirty = 1;
    layout(hwnd);
}

void act_zap_ipset(HWND hwnd)
{
    wchar_t err[320] = { 0 };

    if (!zapret_ipset_cycle(g_zap.path, err, 320)) {
        problem(hwnd, err[0] ? err : L"Не удалось переключить режим IPSet");
        return;
    }
    layout(hwnd);
}

static void work_ipset_update(long_job *j)
{
    j->ok = zapret_ipset_update(j->dir, j->msg, SB_MSG_MAX);
}

static void done_ipset_update(HWND hwnd, long_job *j)
{
    if (!j->ok) { problem(hwnd, j->msg[0] ? j->msg : L"Не удалось обновить список"); return; }
}

void act_zap_ipset_update(HWND hwnd)
{
    long_job *j;
    if (g_busy) return;
    j = job_new(work_ipset_update, done_ipset_update);
    if (j) StringCchCopyW(j->dir, ZAPRET_PATH_MAX, g_zap.path);
    job_start(hwnd, L"Загрузка списка IPSet…", j);
}

static void work_hosts_check(long_job *j)
{
    j->ok = zapret_hosts_check(j->temp, MAX_PATH * 2, &j->n1, j->msg, SB_MSG_MAX);
}

static void done_hosts_check(HWND hwnd, long_job *j)
{
    const wchar_t *temp = j->temp;

    if (!j->ok) {
        problem(hwnd, j->msg[0] ? j->msg : L"Не удалось проверить файл hosts");
        return;
    }

    if (!j->n1) {
        MessageBoxW(hwnd, L"Файл hosts уже соответствует репозиторию.",
                    L"Utgard", MB_ICONINFORMATION | MB_OK);
        DeleteFileW(temp);
        return;
    }

    /* The system hosts file is never written by us, exactly as service.bat
       does it: the downloaded text is opened and the real file is revealed,
       and the copying is left to the user. */
    MessageBoxW(hwnd,
        L"Файл hosts отличается от репозитория.\n\n"
        L"Сейчас откроется скачанный файл и папка с системным hosts — "
        L"скопируйте содержимое вручную. Сам системный файл клиент не меняет.",
        L"Utgard", MB_ICONINFORMATION | MB_OK);

    ShellExecuteW(hwnd, L"open", L"notepad.exe", temp, NULL, SW_SHOWNORMAL);
    {
        wchar_t sysdir[MAX_PATH], arg[MAX_PATH * 2];
        if (GetSystemDirectoryW(sysdir, MAX_PATH) &&
            SUCCEEDED(StringCchPrintfW(arg, MAX_PATH * 2,
                                       L"/select,\"%s\\drivers\\etc\\hosts\"", sysdir)))
            ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    }
}

void act_zap_hosts(HWND hwnd)
{
    if (g_busy) return;
    job_start(hwnd, L"Загрузка файла hosts…", job_new(work_hosts_check, done_hosts_check));
}

/* Modern folder picker (IFileDialog with FOS_PICKFOLDERS). */
static int pick_folder(HWND owner, wchar_t *out, size_t cap)
{
    IFileDialog *fd   = NULL;
    IShellItem  *item = NULL;
    PWSTR        wide = NULL;
    DWORD        opts = 0;
    int          ok   = 0;

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&fd)))
        return 0;

    if (SUCCEEDED(IFileDialog_GetOptions(fd, &opts)))
        IFileDialog_SetOptions(fd, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                          FOS_PATHMUSTEXIST);
    IFileDialog_SetTitle(fd, L"Папка zapret");

    if (SUCCEEDED(IFileDialog_Show(fd, owner)) &&
        SUCCEEDED(IFileDialog_GetResult(fd, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide))) {
            ok = SUCCEEDED(StringCchCopyW(out, cap, wide));
            CoTaskMemFree(wide);
        }
        IShellItem_Release(item);
    }

    IFileDialog_Release(fd);
    return ok;
}

void on_pick_path(HWND hwnd)
{
    wchar_t     chosen[ZAPRET_PATH_MAX];
    zapret_info scanned;

    if (!pick_folder(hwnd, chosen, ZAPRET_PATH_MAX)) return;

    if (!zapret_scan(chosen, &scanned)) {
        /* A bad pick must not destroy a folder that already works. */
        if (g_zap.valid) {
            MessageBoxW(hwnd, scanned.problem, L"Папка не подходит",
                        MB_ICONWARNING | MB_OK);
            return;
        }
        g_zap = scanned;
    } else {
        g_zap = scanned;
        zapret_path_save(g_zap.path);
    }

    strategies_reload();
    status_refresh();
    exc_check_start(hwnd);
    layout(hwnd);
}
