/*
 * Utgard client - Profiles, subscription, ping, sing-box download, VPN on/off.
 */

#include "ui.h"

void profiles_reload(void)
{
    int i;

    SendMessageW(g_plist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_prof.count; i++)
        SendMessageW(g_plist, LB_ADDSTRING, 0, (LPARAM)L"");
    if (g_prof.count)
        SendMessageW(g_plist, LB_SETCURSEL,
                     (WPARAM)(g_prof.active >= 0 ? g_prof.active : 0), 0);
}

int profile_selected(void)
{
    LRESULT i = SendMessageW(g_plist, LB_GETCURSEL, 0, 0);
    if (i == LB_ERR || i < 0 || i >= g_prof.count) return -1;
    return (int)i;
}

/* Same server reached the same way is the same profile. Several profiles of
   one protocol are fine now, so only a full match counts as a duplicate. */
static int same_profile(const link_profile *a, const link_profile *b)
{
    return a->proto == b->proto && a->port == b->port &&
           strcmp(a->server, b->server) == 0 &&
           strcmp(a->uuid, b->uuid) == 0 &&
           strcmp(a->password, b->password) == 0 &&
           strcmp(a->wg_private_key, b->wg_private_key) == 0;
}

static int profile_duplicate(const link_profile *l)
{
    int i;
    for (i = 0; i < g_prof.count; i++)
        if (same_profile(&g_prof.items[i].link, l)) return i;
    return -1;
}

typedef struct {
    HWND hwnd;
    int  gen;
    int  count;
    struct { char server[256]; int port; int icmp; } target[PROFILES_MAX];
} ping_job;

/* One worker walks the list so the rows fill in as answers arrive, instead of
   the window sitting still until the slowest server times out. */
static DWORD WINAPI ping_thread(LPVOID param)
{
    ping_job *job = (ping_job *)param;
    int       i;

    for (i = 0; i < job->count; i++) {
        int ms = net_probe(job->target[i].server, job->target[i].port,
                           job->target[i].icmp, 1500);
        PostMessageW(job->hwnd, WM_APP_PING_ONE,
                     (WPARAM)((job->gen << 8) | (i & 0xFF)), (LPARAM)ms);
    }

    PostMessageW(job->hwnd, WM_APP_PING_DONE, (WPARAM)job->gen, 0);
    free(job);
    return 0;
}

void ping_start(HWND hwnd)
{
    ping_job *job;
    HANDLE    th;
    int       i;

    g_ping_gen++;
    for (i = 0; i < PROFILES_MAX; i++) g_ping[i] = -2;
    if (g_prof.count == 0) { g_ping_busy = 0; return; }

    job = (ping_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd  = hwnd;
    job->gen   = g_ping_gen & 0xFF;
    job->count = g_prof.count;
    for (i = 0; i < g_prof.count; i++) {
        StringCchCopyA(job->target[i].server, 256, g_prof.items[i].link.server);
        job->target[i].port = g_prof.items[i].link.port;
        job->target[i].icmp = (g_prof.items[i].link.proto == LINK_HY2 ||
                               g_prof.items[i].link.proto == LINK_WG);   /* UDP */
    }

    th = CreateThread(NULL, 0, ping_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);
    g_ping_busy = 1;
    layout(hwnd);                   /* greys the refresh button while it runs */
}

/* Runs off the UI thread and does nothing but fetch: everything that parses
   the answer happens back on the UI thread, in already-tested code. */
static DWORD WINAPI sub_thread(LPVOID param)
{
    sub_job *job = (sub_job *)param;

    job->ok = net_fetch(job->url, &job->body, &job->len, job->err, 256);
    PostMessageW(job->hwnd, WM_APP_SUB_DONE, 0, (LPARAM)job);
    return 0;
}

/* Replace the profiles that came from this subscription, leave every other
   profile alone, and try to keep the same one active. */
void subscription_apply(HWND hwnd, const wchar_t *url,
                               const char *body, size_t len, int silent)
{
    static link_profile  fetched[PROFILES_MAX];
    static profile_entry keep[PROFILES_MAX];
    char         url8[PROFILE_SRC];
    char         err[256];
    wchar_t      msg[320];
    link_profile was_active;
    int          had_active, n, skipped = 0, i, kept = 0, added = 0, dropped = 0;

    if (WideCharToMultiByte(CP_UTF8, 0, url, -1, url8, (int)sizeof url8,
                            NULL, NULL) == 0) {
        problem(hwnd, L"Слишком длинный адрес подписки");
        return;
    }

    n = link_parse_subscription(body, len, fetched, PROFILES_MAX, &skipped,
                                err, sizeof err);
    if (n <= 0) {
        /* The profiles are left untouched either way; an automatic refresh
           that met an error page just tries again later, without a window. */
        if (!silent) {
            to_wide(err, msg, 320);
            problem(hwnd, msg);
        }
        return;
    }

    had_active = (g_prof.active >= 0 && g_prof.active < g_prof.count);
    if (had_active) was_active = g_prof.items[g_prof.active].link;

    /* One subscription at a time: everything that came from a subscription is
       replaced, everything added by hand survives. Keeping profiles from a
       previous subscription would leave orphans nothing can ever refresh. */
    for (i = 0; i < g_prof.count; i++)
        if (g_prof.items[i].source[0] == '\0')
            keep[kept++] = g_prof.items[i];

    memset(&g_prof.items, 0, sizeof g_prof.items);
    memcpy(g_prof.items, keep, (size_t)kept * sizeof keep[0]);
    g_prof.count = kept;

    for (i = 0; i < n; i++) {
        if (g_prof.count >= PROFILES_MAX) { dropped = n - i; break; }
        /* The kept manual profiles may already describe one of these servers,
           and a subscription can repeat itself. Either way, no second copy. */
        if (profile_duplicate(&fetched[i]) >= 0) continue;
        memset(&g_prof.items[g_prof.count], 0, sizeof g_prof.items[0]);
        g_prof.items[g_prof.count].link = fetched[i];
        StringCchCopyA(g_prof.items[g_prof.count].source, PROFILE_SRC, url8);
        g_prof.count++;
        added++;
    }

    StringCchCopyA(g_prof.subscription, PROFILE_SRC, url8);

    g_prof.active = g_prof.count ? 0 : -1;
    if (had_active) {
        for (i = 0; i < g_prof.count; i++) {
            const link_profile *o = &g_prof.items[i].link;
            if (o->proto == was_active.proto && o->port == was_active.port &&
                strcmp(o->server, was_active.server) == 0 &&
                strcmp(o->uuid, was_active.uuid) == 0 &&
                strcmp(o->password, was_active.password) == 0) {
                g_prof.active = i;
                break;
            }
        }
    }

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Подписка загружена, но сохранить её не удалось");

    /* Skipped lines are routine - panels carry protocols we do not support -
       so the count in the header is enough. A full list is worth a warning. */
    if (dropped && !silent) {
        StringCchPrintfW(msg, 320,
                         L"Загружено профилей: %d, ещё %d не поместилось — "
                         L"список заполнен.", added, dropped);
        problem(hwnd, msg);
    }
    (void)skipped;

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

void act_subscription(HWND hwnd)
{
    wchar_t  url[2048];
    wchar_t  current[PROFILE_SRC];
    sub_job *job;
    HANDLE   th;

    if (g_sub_busy) return;

    to_wide(g_prof.subscription, current, PROFILE_SRC);
    if (!ask_string(hwnd, L"Подписка",
                    L"Адрес подписки — профили из неё будут обновляться целиком, "
                    L"добавленные вручную останутся",
                    current[0] ? current : NULL, url, 2048))
        return;

    job = (sub_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd = hwnd;
    StringCchCopyW(job->url, 2048, url);

    th = CreateThread(NULL, 0, sub_thread, job, 0, NULL);
    if (!th) {
        free(job);
        problem(hwnd, L"Не удалось запустить загрузку");
        return;
    }
    CloseHandle(th);

    g_sub_busy = 1;
    layout(hwnd);
}

/* Refresh the subscription on its own once the chosen interval has passed.
   Called from a one-minute timer and once at startup. */
void sub_auto_check(HWND hwnd)
{
    sub_job  *job;
    HANDLE    th;
    long long now = _time64(NULL);
    long long due;

    if (g_sub_busy || !g_prof.subscription[0]) return;
    if (g_sub_retry && now < g_sub_retry) return;

    settings_load(&g_set);
    due = g_set.sub_last + (long long)settings_sub_hours[g_set.sub_interval] * 3600;
    if (g_set.sub_last && now < due) return;

    job = (sub_job *)calloc(1, sizeof *job);
    if (!job) return;
    job->hwnd   = hwnd;
    job->silent = 1;
    to_wide(g_prof.subscription, job->url, 2048);

    th = CreateThread(NULL, 0, sub_thread, job, 0, NULL);
    if (!th) { free(job); return; }
    CloseHandle(th);

    g_sub_busy = 1;
    layout(hwnd);
}

static DWORD WINAPI install_thread(LPVOID param)
{
    install_job *job = (install_job *)param;

    job->ok = singbox_install(job->msg, SB_MSG_MAX);
    PostMessageW(job->hwnd, WM_APP_INSTALL, 0, (LPARAM)job);
    return 0;
}

/* Asks first. The download is 21 MB from GitHub, and a client that reaches out
   on its own the first time it starts is not something to do silently. */
void offer_install(HWND hwnd)
{
    wchar_t      question[900];
    install_job *job;
    HANDLE       th;

    if (g_installing) return;

    /* Present but not the pinned release - an older version left over, or a
       modified file. Launching is refused either way, so offer the fix here
       instead of leaving only an error at the moment of switching on. */
    if (singbox_present()) {
        if (singbox_verified()) return;
        if (singbox_running()) {
            problem(hwnd, L"Установленный sing-box не совпадает с нужной версией. "
                          L"Выключите VPN, и программа предложит скачать правильную.");
            return;
        }
        StringCchPrintfW(question, 900,
            L"Установленный sing-box не совпадает с версией %s — это старая "
            L"версия или изменённый файл. Запускать его программа не будет.\n\n"
            L"Скачать правильную версию сейчас?", singbox_version());
        if (MessageBoxW(hwnd, question, L"sing-box",
                        MB_ICONWARNING | MB_YESNO) != IDYES)
            return;
        goto start_install;
    }

    StringCchPrintfW(question, 900,
        L"Не найден sing-box — без него VPN работать не может.\n\n"
        L"Скачать его сейчас?\n\n"
        L"Если не хотите скачивать автоматически — загрузите sing-box %s "
        L"для Windows x64 и положите его файлы в папку sing-box рядом с "
        L"программой. На других версиях работоспособность не гарантируется.",
        singbox_version());

    if (MessageBoxW(hwnd, question, L"Первый запуск",
                    MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;

start_install:
    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd = hwnd;

    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return; }
    CloseHandle(th);

    g_installing = 1;
    layout(hwnd);
}

int vpn_refresh(void)
{
    int now = singbox_running();
    if (now == g_vpn_on) return 0;
    g_vpn_on = now;
    return 1;
}

/* Everything the VPN worker needs, copied on the UI thread: paths, the
   enabled application files, the settings, and the profiles themselves. The
   profiles carry credentials, so the job wipes this block before freeing it. */
typedef struct {
    char          base[1024];
    char          overlay[32][MAX_PATH * 2];
    const char   *overlay_ptr[32];
    profile_store store;
    genconf_input in;
} vpn_inputs;

static void work_vpn_on(long_job *j)
{
    vpn_inputs *v = (vpn_inputs *)j->extra;
    char        err[256] = { 0 };
    char       *config = NULL;

    if (!genconf_build(&v->in, &config, err, sizeof err)) {
        to_wide(err, j->msg, SB_MSG_MAX);
        return;
    }
    if (singbox_check(config, j->msg, SB_MSG_MAX))
        j->ok = singbox_start(config, j->msg, SB_MSG_MAX);
    genconf_text_free(config);
}

static void work_vpn_off(long_job *j)
{
    j->ok = singbox_stop(j->msg, SB_MSG_MAX);
}

static void done_vpn(HWND hwnd, long_job *j)
{
    if (!j->ok)          problem(hwnd, j->msg[0] ? j->msg : L"Не удалось переключить VPN");
    else if (j->msg[0])  problem(hwnd, j->msg);     /* stopped, but had to be killed */
    vpn_refresh();
    tray_set_state(g_vpn_on);
}

void act_vpn(HWND hwnd)
{
    long_job   *j;
    vpn_inputs *v;

    if (g_busy) return;

    if (g_vpn_on) {
        job_start(hwnd, L"выключаю VPN…", job_new(work_vpn_off, done_vpn));
        return;
    }

    if (g_prof.count == 0 || g_prof.active < 0) {
        problem(hwnd, L"Сначала добавьте профиль");
        return;
    }

    /* sing-box first: without it even the site list cannot be built, so
       pointing at the list would only lead to a second error. */
    if (!singbox_present()) {
        offer_install(hwnd);
        return;
    }

    {
        wchar_t root[MAX_PATH * 2], srs[MAX_PATH * 2];
        if (singbox_root(root, MAX_PATH * 2) &&
            SUCCEEDED(StringCchPrintfW(srs, MAX_PATH * 2, L"%slist\\general.srs", root)) &&
            GetFileAttributesW(srs) == INVALID_FILE_ATTRIBUTES) {
            problem(hwnd, L"Список сайтов ещё не собран. Откройте «Список сайтов…», "
                          L"впишите адреса и нажмите «Сохранить».");
            return;
        }
    }

    j = job_new(work_vpn_on, done_vpn);
    v = (vpn_inputs *)calloc(1, sizeof *v);
    if (!j || !v) {
        free(v);
        job_free(j);
        problem(hwnd, L"Не хватило памяти");
        return;
    }
    j->extra      = v;
    j->extra_size = sizeof *v;

    if (!singbox_base_utf8(v->base, sizeof v->base)) {
        job_free(j);
        problem(hwnd, L"Не удалось определить пути к конфигурации");
        return;
    }

    /* Enabled application lists, read now: the user may have toggled one. */
    {
        wchar_t          mask[MAX_PATH * 2], full[MAX_PATH * 2], dir[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        HANDLE           h;
        int              n = 0;

        if (root_file(L"list\\applications\\active", dir, MAX_PATH * 2) &&
            root_file(L"list\\applications\\active\\*.json", mask, MAX_PATH * 2)) {
            h = FindFirstFileW(mask, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (n >= 32) break;
                    if (FAILED(StringCchPrintfW(full, MAX_PATH * 2, L"%s\\%s",
                                                dir, fd.cFileName))) continue;
                    if (WideCharToMultiByte(CP_UTF8, 0, full, -1, v->overlay[n],
                                            MAX_PATH * 2, NULL, NULL) == 0) continue;
                    v->overlay_ptr[n] = v->overlay[n];
                    n++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        v->in.overlays      = n ? v->overlay_ptr : NULL;
        v->in.overlay_count = n;
    }

    v->store = g_prof;
    settings_load(&g_set);

    v->in.base_path     = v->base;
    v->in.rule_set_path = "list/general.srs";
    v->in.mtu           = g_set.mtu;
    v->in.log_level     = settings_log_levels[g_set.log_level];
    v->in.stack         = settings_stacks[g_set.stack];
    v->in.dns_host      = settings_dns[g_set.dns].host;
    v->in.store         = &v->store;

    job_start(hwnd, L"проверяю конфигурацию и запускаю sing-box…", j);
}

static void profile_add_parsed(HWND hwnd, const link_profile *parsed)
{
    if (g_prof.count >= PROFILES_MAX) {
        problem(hwnd, L"Больше профилей не помещается");
        return;
    }
    if (profile_duplicate(parsed) >= 0) {
        problem(hwnd, L"Такой профиль уже есть в списке");
        return;
    }

    memset(&g_prof.items[g_prof.count], 0, sizeof g_prof.items[0]);
    g_prof.items[g_prof.count].link = *parsed;
    if (g_prof.active < 0) g_prof.active = g_prof.count;
    g_prof.count++;

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль добавлен, но сохранить его не удалось");

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

static void profile_add_link(HWND hwnd)
{
    wchar_t      wide[2048];
    char         utf8[2048];
    char         err[256];
    link_profile parsed;
    wchar_t      msg[320];

    if (!ask_string(hwnd, L"Добавить профиль",
                    L"Ссылка vless, vmess, hysteria2, ss, trojan или wireguard",
                    NULL, wide, 2048))
        return;

    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof utf8,
                            NULL, NULL) == 0) {
        problem(hwnd, L"Ссылка слишком длинная");
        return;
    }

    if (!link_parse(utf8, &parsed, err, sizeof err)) {
        to_wide(err, msg, 320);
        problem(hwnd, msg);
        return;
    }
    profile_add_parsed(hwnd, &parsed);
}

static int pick_conf(HWND owner, wchar_t *out, size_t cap)
{
    static const COMDLG_FILTERSPEC types[] = {
        { L"Конфигурация WireGuard (*.conf)", L"*.conf" },
        { L"Все файлы", L"*.*" }
    };
    IFileDialog *fd   = NULL;
    IShellItem  *item = NULL;
    PWSTR        wide = NULL;
    int          ok   = 0;

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&fd)))
        return 0;
    IFileDialog_SetFileTypes(fd, 2, types);
    IFileDialog_SetTitle(fd, L"Файл WireGuard");
    if (SUCCEEDED(IFileDialog_Show(fd, owner)) &&
        SUCCEEDED(IFileDialog_GetResult(fd, &item)) &&
        SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide)))
        ok = SUCCEEDED(StringCchCopyW(out, cap, wide));
    if (wide) CoTaskMemFree(wide);
    if (item) IShellItem_Release(item);
    IFileDialog_Release(fd);
    return ok;
}

/* wg-quick configuration: the file name, without .conf, names the profile. */
static void profile_add_wgconf(HWND hwnd)
{
    wchar_t       path[MAX_PATH], name[MAX_PATH], msg[320];
    static char   text[65536];
    size_t        got = 0;
    char          err[256];
    link_profile  parsed;
    const wchar_t *base, *dot;

    if (!pick_conf(hwnd, path, MAX_PATH)) return;
    if (!file_read(path, text, sizeof text - 1, &got)) {
        problem(hwnd, L"Не удалось прочитать файл (или он больше 64 КБ)");
        return;
    }
    text[got] = '\0';

    if (!link_parse_wgconf(text, got, &parsed, err, sizeof err)) {
        to_wide(err, msg, 320);
        problem(hwnd, msg);
        return;
    }

    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    StringCchCopyW(name, MAX_PATH, base);
    dot = wcsrchr(name, L'.');
    if (dot) name[dot - name] = L'\0';
    if (!WideCharToMultiByte(CP_UTF8, 0, name, -1, parsed.name, (int)sizeof parsed.name,
                             NULL, NULL))
        parsed.name[0] = '\0';

    profile_add_parsed(hwnd, &parsed);
}

void act_profile_add(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    RECT  r;
    int   cmd;

    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Вставить ссылку…");
    AppendMenuW(menu, MF_STRING, 2, L"Файл WireGuard (.conf)…");
    GetWindowRect(g_prof_add, &r);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                         r.left, r.top, 0, hwnd, NULL);
    DestroyMenu(menu);

    if (cmd == 1) profile_add_link(hwnd);
    else if (cmd == 2) profile_add_wgconf(hwnd);
}

void act_profile_delete(HWND hwnd)
{
    int i = profile_selected();

    if (i < 0) return;

    memmove(&g_prof.items[i], &g_prof.items[i + 1],
            (size_t)(g_prof.count - i - 1) * sizeof g_prof.items[0]);
    g_prof.count--;

    if (g_prof.active == i)      g_prof.active = g_prof.count ? 0 : -1;
    else if (g_prof.active > i)  g_prof.active--;

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль удалён, но сохранить изменение не удалось");

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

void act_profile_activate(HWND hwnd)
{
    int i = profile_selected();

    if (i < 0 || i == g_prof.active) return;
    g_prof.active = i;
    if (!profiles_save(&g_prof))
        problem(hwnd, L"Профиль выбран, но сохранить выбор не удалось");
    InvalidateRect(g_plist, NULL, TRUE);
    layout(hwnd);
}
