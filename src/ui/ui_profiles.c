/*
 * Utgard client - Servers page: the profile list, adding and removing servers, ping.
 */

#include "ui.h"

/* State owned by this file (declared in ui.h). */
HWND g_plist, g_prof_add, g_prof_del, g_prof_sub;
HWND g_ping_now;

/* State owned by this file (declared in ui.h). */
/* -2 not measured yet, -1 unreachable, otherwise milliseconds. Not stored:
   a latency from last week would be a lie. */
int g_ping[PROFILES_MAX];
int g_ping_gen;      /* results from an older list are discarded */
int g_ping_busy;
profile_store g_prof;

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

int profile_duplicate(const link_profile *l)
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
                     ((WPARAM)(unsigned)job->gen << 8) | (WPARAM)(i & 0xFF), (LPARAM)ms);
    }

    PostMessageW(job->hwnd, WM_APP_PING_DONE, (WPARAM)(unsigned)job->gen, 0);
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
    job->gen   = g_ping_gen;   /* whole: WPARAM is 64-bit on x64 and arm64 */
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


static void profile_add_parsed(HWND hwnd, const link_profile *parsed)
{
    if (g_prof.count >= PROFILES_MAX) {
        problem(hwnd, L"Больше серверов не помещается");
        return;
    }
    if (profile_duplicate(parsed) >= 0) {
        problem(hwnd, L"Такой сервер уже есть в списке");
        return;
    }

    memset(&g_prof.items[g_prof.count], 0, sizeof g_prof.items[0]);
    g_prof.items[g_prof.count].link = *parsed;
    if (g_prof.active < 0) g_prof.active = g_prof.count;
    g_prof.count++;

    if (!profiles_save(&g_prof))
        problem(hwnd, L"Сервер добавлен, но сохранить его не удалось");

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

    if (!ask_string(hwnd, L"Добавить сервер",
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


/* wg-quick configuration: the file name, without .conf, names the profile. */
static void profile_add_wgconf(HWND hwnd)
{
    wchar_t       path[MAX_PATH], name[MAX_PATH], msg[320];
    static char   text[65536];
    size_t        got = 0;
    char          err[256];
    link_profile  parsed;
    const wchar_t *base, *dot;

    if (!pick_file(hwnd, L"Файл WireGuard", L"Конфигурация WireGuard (*.conf)", L"*.conf", path, MAX_PATH)) return;
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
    {
        static const wchar_t *const items[2] = {
            L"Вставить ссылку…", L"Файл WireGuard / AmneziaWG (.conf)…" };
        (void)menu;
        GetWindowRect(g_prof_add, &r);
        /* Under the button, right edges lined up, rounded like the rest. */
        cmd = popup_choose(hwnd, &r, items, 2, -1, 1);
    }
    if (cmd == 0) profile_add_link(hwnd);
    else if (cmd == 1) profile_add_wgconf(hwnd);
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
        problem(hwnd, L"Сервер удалён, но сохранить изменение не удалось");

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
}

/* One click: with the VPN off the server becomes the chosen one at once;
   with it on only the row is marked - a running tunnel is switched by the
   double click, never by a stray single one. */
void act_profile_pick(HWND hwnd)
{
    int i = profile_selected();
    if (!g_vpn_on && !g_busy && !g_sub_busy && i >= 0 && i != g_prof.active) {
        g_prof.active = i;
        if (!profiles_save(&g_prof))
            problem(hwnd, L"Сервер выбран, но сохранить выбор не удалось");
        InvalidateRect(g_plist, NULL, TRUE);
    }
    layout(hwnd);
}

/* Double click: switch to the server - the running tunnel moves to it, or,
   with the VPN off, it is chosen and the VPN switched on. */
void act_profile_activate(HWND hwnd)
{
    int i = profile_selected();

    /* The subscription is being fetched: its result may renumber the list. */
    if (i < 0 || g_busy || g_sub_busy || (g_vpn_on && i == g_prof.active)) {
        SendMessageW(g_plist, LB_SETCURSEL, (WPARAM)g_prof.active, 0);
        return;
    }
    if (!g_vpn_on) {
        g_prof.active = i;
        if (!profiles_save(&g_prof))
            problem(hwnd, L"Сервер выбран, но сохранить выбор не удалось");
        InvalidateRect(g_plist, NULL, TRUE);
        act_vpn(hwnd);
        return;
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

/* The servers page's controls, in on_create's order: creation order is
   the z-order and the tab order. */
void servers_create(HWND hwnd)
{
    g_ping_now   = make_button(hwnd, L"Проверить задержку", ID_PING_NOW, BK_SECONDARY);
    g_plist = CreateWindowExW(0, L"LISTBOX", NULL,
                              WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                              LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
                              0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_PROFILES,
                              (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                              NULL);
    SendMessageW(g_plist, LB_SETITEMHEIGHT, 0, (LPARAM)scaled(44));
    SetWindowTheme(g_plist, L"DarkMode_Explorer", NULL);
    list_hover_attach(g_plist);
    g_prof_add = make_button_on(hwnd, L"Добавить сервер…", ID_PROF_ADD,
                                BK_PRIMARY, BACK_PAGE);
    g_prof_del = make_button_on(hwnd, L"Удалить", ID_PROF_DEL,
                                BK_DANGER, BACK_PAGE);
    g_prof_sub = make_button_on(hwnd, L"Подписка…", ID_PROF_SUB,
                                BK_SECONDARY, BACK_PAGE);
}
