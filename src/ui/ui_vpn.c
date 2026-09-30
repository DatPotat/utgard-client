/*
 * Utgard client - VPN on/off: building, checking and switching what runs.
 */

#include "adapter.h"
#include "vpnswitch.h"
#include "awgcore.h"
#include "awgconf.h"
#include "awgsvc.h"
#include "coredir.h"
#include "ui.h"
#include "pacstore.h"
#include "pacproc.h"
#include "confread.h"

/* State owned by this file (declared in ui.h). */
HWND g_row_server;
HWND g_toggle;
int g_switch_pending = -1;
int g_switch_note;
HWND g_btn_hosts, g_btn_apps, g_btn_pac;

/* State owned by this file (declared in ui.h). */
int g_vpn_on;
int g_awg_lost;
int g_awg_ready;

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
    if (g_vpn_on && !now) pacproc_vpn_off();
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
    int           overlay_count;
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
        pac_process pac;
        int pac_count;
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
        StringCchCopyW(msg, cap, L"Сервер не найден");
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
    {
        pac_store settings;
        int i;
        if (!pacstore_load(&settings)) {
            StringCchCopyW(msg, cap, L"Не удалось прочитать настройки PAC. Исправьте их перед включением VPN.");
            return 0;
        }
        for (i = 0; i < settings.count; i++)
            if (settings.items[i].enabled) v->plan[which].pac_count++;
        v->in.pac_port = v->in.pac_dns_port = v->in.pac_dns_vpn_port =
            v->in.pac_dns_sys_port = v->in.vpn_proxy_port = 0;
        v->in.proxy_password = NULL;
        v->in.client_exe = NULL;
        if (v->plan[which].pac_count &&
            !pacproc_prepare(&v->plan[which].pac, &v->in, &settings, msg, cap)) {
            pacstore_free(&settings);
            return 0;
        }
        pacstore_free(&settings);
    }
    {
        /* Read now, not when the job was set up: as before, the build sees
           the files as they are at this moment. */
        genconf_file overlays[32];
        int          built;
        if (!confread_inputs(v->base, v->overlay_ptr, v->overlay_count, &v->in.base, overlays,
                             err, sizeof err)) {
            to_wide(err, msg, (int)cap);
            return 0;
        }
        v->in.overlays      = overlays;
        v->in.overlay_count = v->overlay_count;
        built = genconf_build(&v->in, &v->plan[which].config, err, sizeof err);
        confread_free(&v->in.base, overlays, v->overlay_count);
        v->in.overlays      = NULL;
        v->in.overlay_count = 0;
        if (!built) {
            to_wide(err, msg, (int)cap);
            return 0;
        }
    }
    return singbox_check(v->plan[which].config, msg, cap);
}

/* Tunnel first, then sing-box: its config binds to the adapter the service
   raises. No tunnel is left without sing-box steering into it. */
static int plan_up(vpn_inputs *v, int which, wchar_t *msg, size_t cap)
{
    HANDLE process = NULL;
    if (v->plan[which].pac_count && singbox_running()) {
        StringCchCopyW(msg, cap, L"sing-box уже запущен; PAC нельзя безопасно подключить к существующему процессу");
        pacproc_cancel(&v->plan[which].pac);
        return 0;
    }
    /* Both the AmneziaWG service and sing-box create a Wintun adapter. */
    if (!netsetup_ensure(msg, cap)) return 0;
    if (v->plan[which].awg) {
        job_stage(v->job, L"Включение туннеля AmneziaWG…");
        if (!awgsvc_start(v->plan[which].awg_conf, msg, cap)) return 0;
    }
    job_stage(v->job, L"Включение VPN…");
    if (v->plan[which].pac_count &&
        WaitForSingleObject(v->plan[which].pac.process, 0) != WAIT_TIMEOUT) {
        StringCchCopyW(msg, cap, L"Изолированный PAC-процесс завершился до включения VPN");
        pacproc_cancel(&v->plan[which].pac);
    } else if (singbox_start(v->plan[which].config,
                            v->plan[which].pac_count ? v->plan[which].pac.job : NULL,
                            &process, msg, cap)) {
        if (!v->plan[which].pac_count ||
            pacproc_attach(&v->plan[which].pac, process, msg, cap)) {
            CloseHandle(process);
            return 1;
        }
        CloseHandle(process);
        pacproc_cancel(&v->plan[which].pac);
        singbox_stop(NULL, 0);
    } else {
        pacproc_cancel(&v->plan[which].pac);
    }
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
    pacproc_vpn_off();
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
        pacproc_cancel(&v->plan[i].pac);
        v->plan[i].pac_count = 0;
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
                problem(hwnd, L"Сервер переключён, но сохранить выбор не удалось");
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
        problem(hwnd, L"Сначала добавьте сервер");
        return NULL;
    }

    /* sing-box first: without it even the site list cannot be built, so
       pointing at the list would only lead to a second error. */
    if (!singbox_present()) {
        offer_install(hwnd);
        return NULL;
    }

    /* Refresh the summary before preparing rules. Empty site/application
       lists are valid when an enabled PAC supplies the routing decisions. */
    lists_refresh_counts();
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
        v->overlay_count = n;
    }

    v->store  = g_prof;
    v->target = target;
    v->prev   = g_prof.active;
    settings_load(&g_set);

    v->in.rule_set_path = "list/general.srs";
    v->in.mtu           = g_set.mtu;
    v->in.log_level     = settings_log_levels[g_set.log_level];
    v->in.stack         = settings_stacks[g_set.stack];
    v->in.dns_host      = settings_dns[g_set.dns].host;
    v->in.dns_type      = settings_dns[g_set.dns].type;
    v->in.dns_path      = settings_dns[g_set.dns].path;
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
    job_start(hwnd, L"Переключение сервера…", j);
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

/* The connection page's controls, in on_create's order: creation order is
   the z-order and the tab order. */
void connect_create(HWND hwnd)
{
    g_row_server = make_button_on(hwnd, L"", ID_ROW_SERVER, BK_ROW, BACK_CARD);
    g_toggle     = make_button(hwnd, L"Включить", ID_TOGGLE, BK_PRIMARY);
    g_btn_hosts = make_button_on(hwnd, L"Сайты", ID_EDIT_HOSTS, BK_ROW, BACK_CARD);
    g_btn_apps  = make_button_on(hwnd, L"Приложения", ID_EDIT_APPS, BK_ROW, BACK_CARD);
    g_btn_pac   = make_button_on(hwnd, L"Правила PAC", ID_PAC, BK_ROW, BACK_CARD);
}
