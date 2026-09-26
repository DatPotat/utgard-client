/*
 * Utgard client - Profiles, subscription, ping, sing-box download, VPN on/off.
 */

#include "adapter.h"
#include "vpnswitch.h"
#include "awgcore.h"
#include "awgconf.h"
#include "awgsvc.h"
#include "coredir.h"
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
    /* For WireGuard and AmneziaWG every setting of the tunnel counts: the
       same keys with other obfuscation parameters, address or MTU are a
       different profile, not a copy to drop. */
    return a->proto == b->proto && a->port == b->port &&
           strcmp(a->server, b->server) == 0 &&
           strcmp(a->uuid, b->uuid) == 0 &&
           strcmp(a->password, b->password) == 0 &&
           strcmp(a->wg_private_key, b->wg_private_key) == 0 &&
           strcmp(a->wg_peer_key, b->wg_peer_key) == 0 &&
           strcmp(a->wg_psk, b->wg_psk) == 0 &&
           strcmp(a->wg_address, b->wg_address) == 0 &&
           strcmp(a->wg_reserved, b->wg_reserved) == 0 &&
           a->mtu == b->mtu && a->keepalive == b->keepalive &&
           strcmp(a->awg, b->awg) == 0;
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
            /* The same server and credentials: the same profile, even if
               the refresh changed a setting. WireGuard keys are part of the
               credentials - several WG profiles can share one server. */
            if (o->proto == was_active.proto && o->port == was_active.port &&
                strcmp(o->server, was_active.server) == 0 &&
                strcmp(o->uuid, was_active.uuid) == 0 &&
                strcmp(o->password, was_active.password) == 0 &&
                strcmp(o->wg_private_key, was_active.wg_private_key) == 0 &&
                strcmp(o->wg_peer_key, was_active.wg_peer_key) == 0) {
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

    /* A job may be switching profiles by index: no renumbering meanwhile. */
    if (g_sub_busy || g_busy) return;

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

    if (g_sub_busy || g_busy || !g_prof.subscription[0]) return;   /* next tick */
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

    job->ok = job->awg ? awgcore_install(job->msg, SB_MSG_MAX)
                       : singbox_install(job->msg, SB_MSG_MAX);
    PostMessageW(job->hwnd, WM_APP_INSTALL, 0, (LPARAM)job);
    return 0;
}

/* Asks first. The download is 21 MB from GitHub, and a client that reaches out
   on its own the first time it starts is not something to do silently. */
int offer_install(HWND hwnd)
{
    wchar_t      question[900];
    install_job *job;
    HANDLE       th;

    if (g_installing) return 0;

    /* Present but not the pinned release - an older version left over, or a
       modified file. Launching is refused either way, so offer the fix here
       instead of leaving only an error at the moment of switching on. */
    if (singbox_present()) {
        wchar_t why[400];
        int     reinstall = 0;
        if (singbox_verify(why, 400, &reinstall)) return 0;
        /* The folder itself is wrong - its permissions, a link, a stray
           file: a fresh download changes none of that, so say what it is. */
        if (!reinstall) {
            problem(hwnd, why);
            return 0;
        }
        if (singbox_running()) {
            problem(hwnd, L"Установленный sing-box не совпадает с нужной версией. "
                          L"Выключите VPN, и программа предложит скачать правильную.");
            return 0;
        }
        StringCchPrintfW(question, 900,
            L"Установленный sing-box не совпадает с версией %s — это старая "
            L"версия или изменённый файл. Запускать его программа не будет.\n\n"
            L"Скачать правильную версию сейчас?", singbox_version());
        if (modal_box(hwnd, question, L"sing-box",
                      MB_ICONWARNING | MB_YESNO) != IDYES)
            return 0;
        goto start_install;
    }

    StringCchPrintfW(question, 900,
        L"Не найден sing-box — без него VPN работать не может.\n\n"
        L"Скачать его сейчас?\n\n"
        L"Если не хотите скачивать автоматически — загрузите с GitHub архив %s "
        L"и положите его файлы в папку sing-box рядом с программой. "
        L"Другие версии и сборки программа не запустит.",
        singbox_archive());

    if (modal_box(hwnd, question, L"Первый запуск",
                  MB_ICONQUESTION | MB_YESNO) != IDYES)
        return 0;

start_install:
    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return 0; }
    job->hwnd = hwnd;

    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return 0; }
    CloseHandle(th);

    g_installing = 1;
    layout(hwnd);
    return 1;
}

/* The AmneziaWG core, asked for when a profile first needs it. On success
   the switch-on the user asked for carries on by itself. */
void offer_awg_install(HWND hwnd, int resume)
{
    install_job *job;
    HANDLE       th;
    wchar_t      dir[MAX_PATH * 2];

    if (g_installing || g_awg_ready) return;
    /* On FAT32 it can never run; asking at start would only nag. */
    if (!resume && (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2) || !coredir_volume_has_acl(dir)))
        return;
    if (modal_box(hwnd, resume
            ? L"Для профиля AmneziaWG нужно ядро AmneziaWG — официальный пакет "
              L"amneziawg-windows-client с GitHub.\n\n"
              L"Программа проверит его контрольную сумму и возьмёт из него два файла "
              L"в папку amneziawg рядом с собой. В систему ничего не устанавливается; "
              L"во время работы туннеля программа создаёт службу и сетевой адаптер "
              L"и удаляет их при выключении.\n\nСкачать сейчас?"
            : L"Не найдено ядро AmneziaWG — без него не работают профили AmneziaWG.\n\n"
              L"Скачать его сейчас? Это официальный пакет amneziawg-windows-client с "
              L"GitHub: программа проверит его контрольную сумму и возьмёт из него два "
              L"файла в папку amneziawg. В систему ничего не устанавливается.\n\n"
              L"Если профили AmneziaWG не нужны, можно отказаться — программа спросит "
              L"снова, когда такой профиль понадобится.",
            L"AmneziaWG", MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;

    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd   = hwnd;
    job->awg    = 1;
    job->resume = resume;
    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return; }
    CloseHandle(th);
    g_installing = 2;
    layout(hwnd);
}

static int active_is_awg(void)
{
    return g_prof.active >= 0 && g_prof.active < g_prof.count &&
           link_is_awg(&g_prof.items[g_prof.active].link);
}

int vpn_refresh(void)
{
    int now  = singbox_running();
    int lost = now && active_is_awg() && !awgsvc_running();

    if (now == g_vpn_on && lost == g_awg_lost) return 0;
    g_vpn_on   = now;
    g_awg_lost = lost;
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
    /* The profile to run, and - for a switch - the one running now, to come
       back to. Indices into store; the lists cannot change meanwhile: while a
       job runs, profiles and the subscription are locked. */
    int           target, prev, switching;
    long_job     *job;                  /* for the status line (job_stage) */
    char          awg_exe[3 * MAX_PATH * 2];
    char          awg_iface[32];
    /* One prepared profile each for VPN_NEW and VPN_OLD. awg_conf holds the
       private key; the whole block is wiped before it is freed. */
    struct {
        int   awg;
        char  awg_ip[16];
        char  awg_conf[AWGCONF_MAX];
        char *config;                   /* sing-box's, from genconf */
    } plan[2];
} vpn_inputs;

/* The config always declares the site list's rule set, and config.json's DNS
   rules refer to it. With only applications listed it may never have been
   built: build it now from list\\hosts - empty lists compile to a rule
   that matches nothing - so sing-box finds the file. */
static int ensure_rule_set(long_job *j)
{
    static char text[LIST_TEXT_MAX], json[LIST_TEXT_MAX];
    wchar_t     srs[MAX_PATH * 2], hosts[MAX_PATH * 2], jpath[MAX_PATH * 2];
    lists_stats st;

    if (!root_file(L"list\\general.srs", srs, MAX_PATH * 2)) return 0;
    if (GetFileAttributesW(srs) != INVALID_FILE_ATTRIBUTES) return 1;
    job_stage(j, L"Сборка списка сайтов…");
    text[0] = '\0';
    if (root_file(L"list\\hosts", hosts, MAX_PATH * 2)) file_read(hosts, text, LIST_TEXT_MAX, NULL);
    if (!lists_build_text(text, json, LIST_TEXT_MAX, &st) ||
        !root_file(L"list\\general.json", jpath, MAX_PATH * 2) ||
        !file_write(jpath, json, strlen(json)) ||
        !singbox_compile_list(j->msg, SB_MSG_MAX)) {
        if (!j->msg[0]) StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось собрать список сайтов");
        return 0;
    }
    DeleteFileW(jpath);
    return 1;
}

/* Build and check everything a profile needs, touching nothing that runs:
   for AmneziaWG the server address (system resolver, IPv4 as the TUN and
   the DNS strategy are) and the service's config, then sing-box's config,
   checked by sing-box itself. */
static int plan_prepare(vpn_inputs *v, int which, wchar_t *msg, size_t cap)
{
    const link_profile *p;
    char                err[256] = { 0 };
    int                 idx = which == VPN_NEW ? v->target : v->prev;

    if (idx < 0 || idx >= v->store.count) {
        StringCchCopyW(msg, cap, L"Профиль не найден");
        return 0;
    }
    job_stage(v->job, L"Проверка конфигурации…");
    v->store.active = idx;
    p = &v->store.items[idx].link;
    v->plan[which].awg  = link_is_awg(p);
    v->in.awg_interface = NULL;
    v->in.awg_server_ip = NULL;
    v->in.awg_exe       = NULL;
    if (v->plan[which].awg) {
        char ips[1][16];
        if (net_resolve4(p->server, ips, 1) < 1) {
            StringCchCopyW(msg, cap, L"Не удалось узнать IP-адрес сервера AmneziaWG");
            return 0;
        }
        StringCchCopyA(v->plan[which].awg_ip, sizeof v->plan[which].awg_ip, ips[0]);
        if (!awgconf_build(p, v->plan[which].awg_ip, v->plan[which].awg_conf,
                           sizeof v->plan[which].awg_conf, err, sizeof err)) {
            to_wide(err, msg, (int)cap);
            return 0;
        }
        v->in.awg_interface = v->awg_iface;
        v->in.awg_server_ip = v->plan[which].awg_ip;
        v->in.awg_exe       = v->awg_exe;
    }
    genconf_text_free(v->plan[which].config);
    v->plan[which].config = NULL;
    if (!genconf_build(&v->in, &v->plan[which].config, err, sizeof err)) {
        to_wide(err, msg, (int)cap);
        return 0;
    }
    return singbox_check(v->plan[which].config, msg, cap);
}

/* Tunnel first, then sing-box: its config binds to the adapter the service
   raises. No tunnel is left without sing-box steering into it. */
static int plan_up(vpn_inputs *v, int which, wchar_t *msg, size_t cap)
{
    /* Both the AmneziaWG service and sing-box create a Wintun adapter. */
    if (!netsetup_ensure(msg, cap)) return 0;
    if (v->plan[which].awg) {
        job_stage(v->job, L"Включение туннеля AmneziaWG…");
        if (!awgsvc_start(v->plan[which].awg_conf, msg, cap)) return 0;
    }
    job_stage(v->job, L"Включение VPN…");
    if (singbox_start(v->plan[which].config, msg, cap)) return 1;
    if (v->plan[which].awg) awgsvc_stop(NULL, 0);
    return 0;
}

/* sing-box, then the AmneziaWG tunnel if there is one - also one left from
   a profile no longer selected. Nothing to do without one. */
static int vpn_down(long_job *j, wchar_t *msg, size_t cap)
{
    wchar_t awg_msg[200];
    int     ok;

    job_stage(j, L"Выключение VPN…");
    ok = singbox_stop(msg, cap);
    if (awgsvc_running()) job_stage(j, L"Выключение туннеля AmneziaWG…");
    if (!awgsvc_stop(awg_msg, 200) && msg && !msg[0]) StringCchCopyW(msg, cap, awg_msg);
    return ok;
}

static void plans_wipe(vpn_inputs *v)
{
    int i;
    for (i = 0; i < 2; i++) {
        genconf_text_free(v->plan[i].config);
        v->plan[i].config = NULL;
        awgconf_wipe(v->plan[i].awg_conf, sizeof v->plan[i].awg_conf);
    }
}

static int op_prepare(void *ctx, int which, wchar_t *msg, size_t cap)
{ return plan_prepare((vpn_inputs *)ctx, which, msg, cap); }
static int op_down(void *ctx, wchar_t *msg, size_t cap)
{ return vpn_down(((vpn_inputs *)ctx)->job, msg, cap); }
static int op_up(void *ctx, int which, wchar_t *msg, size_t cap)
{ return plan_up((vpn_inputs *)ctx, which, msg, cap); }

static void work_vpn_on(long_job *j)
{
    vpn_inputs *v = (vpn_inputs *)j->extra;

    v->job = j;
    if (ensure_rule_set(j) && plan_prepare(v, VPN_NEW, j->msg, SB_MSG_MAX))
        j->ok = plan_up(v, VPN_NEW, j->msg, SB_MSG_MAX);
    plans_wipe(v);
}

static void work_vpn_off(long_job *j)
{
    j->ok = vpn_down(j, j->msg, SB_MSG_MAX);
}

/* Another profile while the VPN is on: see vpnswitch.h. */
static void work_vpn_restart(long_job *j)
{
    vpn_inputs    *v   = (vpn_inputs *)j->extra;
    vpn_switch_ops ops = { NULL, op_prepare, op_down, op_up };

    ops.ctx = v;
    v->job  = j;
    if (ensure_rule_set(j)) j->ok = vpn_switch(&ops, j->msg, SB_MSG_MAX);
    plans_wipe(v);
}

static void done_vpn(HWND hwnd, long_job *j)
{
    vpn_inputs *v = (vpn_inputs *)j->extra;

    /* A switch counts only once the new profile runs; until then, and after
       a failure, the list shows the one in use. */
    if (v && v->switching) {
        if (j->ok) {
            g_prof.active = v->target;
            if (!profiles_save(&g_prof))
                problem(hwnd, L"Профиль переключён, но сохранить выбор не удалось");
        }
        g_switch_pending = -1;
        SendMessageW(g_plist, LB_SETCURSEL, (WPARAM)g_prof.active, 0);
        InvalidateRect(g_plist, NULL, TRUE);
    }
    if (!j->ok)          problem(hwnd, j->msg[0] ? j->msg : L"Не удалось переключить VPN");
    else if (j->msg[0])  problem(hwnd, j->msg);     /* stopped, but had to be killed */
    vpn_refresh();
    /* Rolled back: say so until the next action, not only in a message box. */
    if (v && v->switching && !j->ok && g_vpn_on) g_switch_note = 1;
    tray_set_state(g_vpn_on);
}

/* The job that switches the VPN on with the active profile - or, for
   work_vpn_restart, off and on again. NULL when something is missing; the
   user has been told what. */
static long_job *vpn_job(HWND hwnd, job_work work, int target)
{
    long_job   *j;
    vpn_inputs *v;

    if (g_prof.count == 0 || target < 0 || target >= g_prof.count) {
        problem(hwnd, L"Сначала добавьте профиль");
        return NULL;
    }

    /* sing-box first: without it even the site list cannot be built, so
       pointing at the list would only lead to a second error. */
    if (!singbox_present()) {
        offer_install(hwnd);
        return NULL;
    }

    /* Utgard sends only what is listed through the VPN: with neither sites
       nor applications there is nothing to switch on for. */
    lists_refresh_counts();
    if (g_host_count == 0 && g_app_count == 0) {
        problem(hwnd, L"Через VPN пока нечего пускать. Добавьте сайты в «Список сайтов…» "
                      L"или включите приложения в «Приложения…».");
        return NULL;
    }
    /* Only then the AmneziaWG core: no download for a switch-on that
       would be refused anyway. */
    if (link_is_awg(&g_prof.items[target].link) && !(g_awg_ready = awgcore_present())) {
        wchar_t dir[MAX_PATH * 2];
        /* On FAT32 say so now, rather than after a download. */
        if (coredir_path(&CORE_AWG, dir, MAX_PATH * 2) && !coredir_volume_has_acl(dir)) {
            problem(hwnd, L"AmneziaWG недоступен: папка программы на диске без прав доступа "
                          L"(FAT32 или exFAT). Перенесите Utgard на диск NTFS.");
            return NULL;
        }
        offer_awg_install(hwnd, g_vpn_on ? 2 : 1);
        return NULL;
    }

    j = job_new(work, done_vpn);
    v = (vpn_inputs *)calloc(1, sizeof *v);
    if (!j || !v) {
        free(v);
        job_free(j);
        problem(hwnd, L"Не хватило памяти");
        return NULL;
    }
    j->extra      = v;
    j->extra_size = sizeof *v;

    if (!singbox_base_utf8(v->base, sizeof v->base)) {
        job_free(j);
        problem(hwnd, L"Не удалось определить пути к конфигурации");
        return NULL;
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

    v->store  = g_prof;
    v->target = target;
    v->prev   = g_prof.active;
    settings_load(&g_set);

    v->in.base_path     = v->base;
    v->in.rule_set_path = "list/general.srs";
    v->in.mtu           = g_set.mtu;
    v->in.log_level     = settings_log_levels[g_set.log_level];
    v->in.stack         = settings_stacks[g_set.stack];
    v->in.dns_host      = settings_dns[g_set.dns].host;
    v->in.store         = &v->store;

    {   /* Either profile of a switch may be AmneziaWG: always ready. */
        wchar_t dir[MAX_PATH * 2], exe[MAX_PATH * 2];
        StringCchPrintfA(v->awg_iface, sizeof v->awg_iface, "%ls", awgsvc_interface());
        if (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2) ||
            FAILED(StringCchPrintfW(exe, MAX_PATH * 2, L"%s\\%s", dir, CORE_AWG.files[0].name)) ||
            !WideCharToMultiByte(CP_UTF8, 0, exe, -1, v->awg_exe, (int)sizeof v->awg_exe, NULL, NULL)) {
            job_free(j);
            problem(hwnd, L"Не удалось определить путь к AmneziaWG");
            return NULL;
        }
    }
    return j;
}

/* sing-box went away without "Выключить" - crashed, killed, or ended with a
   logoff - and left an AmneziaWG tunnel running on its own. The VPN is off,
   so the tunnel has nothing to carry; it only holds the core's folder.
   Stopped through a job, so it cannot cross a switch-on. */
static void work_awg_orphan(long_job *j)
{
    job_stage(j, L"Выключение туннеля AmneziaWG…");
    j->ok = awgsvc_stop(j->msg, SB_MSG_MAX);
}

static void done_awg_orphan(HWND hwnd, long_job *j)
{
    if (!j->ok && j->msg[0]) problem(hwnd, j->msg);
    layout(hwnd);
}

void vpn_reap_orphan(HWND hwnd)
{
    if (g_busy || g_vpn_on || g_installing || !awgsvc_running()) return;
    job_start(hwnd, L"Выключение туннеля AmneziaWG…", job_new(work_awg_orphan, done_awg_orphan));
}

void vpn_restart(HWND hwnd, int target)
{
    long_job *j;
    if (g_busy || !g_vpn_on) return;
    j = vpn_job(hwnd, work_vpn_restart, target);
    if (!j) return;
    ((vpn_inputs *)j->extra)->switching = 1;
    job_start(hwnd, L"Переключение профиля…", j);
}

void act_vpn(HWND hwnd)
{
    long_job *j;

    if (g_busy) return;
    if (g_vpn_on) {
        job_start(hwnd, L"Выключение VPN…", job_new(work_vpn_off, done_vpn));
        return;
    }
    j = vpn_job(hwnd, work_vpn_on, g_prof.active);
    if (j) job_start(hwnd, L"Включение VPN…", j);
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
    /* An Amnezia vpn:// link runs to kilobytes; the edit control cuts a
       paste at its limit without a word, so the limit is the parser's own.
       Static: UI thread only, and too large for the stack. UTF-8 needs up to
       three bytes per UTF-16 unit. */
    static wchar_t      wide[64 * 1024];
    static char         utf8[3 * 64 * 1024];
    static link_profile parsed;
    char                err[256];
    wchar_t             msg[320];

    if (!ask_string(hwnd, L"Добавить профиль",
                    L"Ссылка vless, vmess, hysteria2, ss, trojan, wireguard или vpn:// (Amnezia)",
                    NULL, wide, sizeof wide / sizeof wide[0]))
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
    if (file_read(path, text, sizeof text - 1, &got) != 1) {
        problem(hwnd, L"Не удалось прочитать файл (или он больше 64 КБ)");
        return;
    }
    text[got] = '\0';

    if (!link_parse_file(text, got, &parsed, err, sizeof err)) {
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
    HMENU menu;
    RECT  r;
    int   cmd;

    if (g_busy) return;
    menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Вставить ссылку…");
    AppendMenuW(menu, MF_STRING, 2, L"Файл WireGuard / AmneziaWG (.conf)…");
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

    if (i < 0 || g_busy) return;

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

    /* The subscription is being fetched: its result may renumber the list. */
    if (i < 0 || i == g_prof.active || g_busy || g_sub_busy) {
        SendMessageW(g_plist, LB_SETCURSEL, (WPARAM)g_prof.active, 0);
        return;
    }
    if (!g_vpn_on) {
        g_prof.active = i;
        if (!profiles_save(&g_prof))
            problem(hwnd, L"Профиль выбран, но сохранить выбор не удалось");
    } else {
        /* The running tunnel keeps its profile until the new one runs
           (done_vpn). A download of the AmneziaWG core, if that is what
           it waits for, carries the choice through (g_switch_pending). */
        g_switch_pending = i;
        vpn_restart(hwnd, i);
        if (!g_busy) {
            if (g_installing != 2) g_switch_pending = -1;
            SendMessageW(g_plist, LB_SETCURSEL, (WPARAM)g_prof.active, 0);
        }
    }
    InvalidateRect(g_plist, NULL, TRUE);
    layout(hwnd);
}
