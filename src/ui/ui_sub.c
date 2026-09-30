/*
 * Utgard client - Subscription: fetching it off the UI thread, applying it, refreshing it.
 */

#include "ui.h"

/* State owned by this file (declared in ui.h). */
int g_sub_busy;
long long g_sub_retry;   /* after a failed automatic refresh, not before */

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
int subscription_apply(HWND hwnd, const wchar_t *url,
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
        return 0;
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
        SecureZeroMemory(fetched, sizeof fetched);
        return 0;
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
                         L"Загружено серверов: %d, ещё %d не поместилось — "
                         L"список заполнен.", added, dropped);
        problem(hwnd, msg);
    }
    (void)skipped;

    /* Credentials passed through these; wipe them like the profiles. */
    SecureZeroMemory(fetched, sizeof fetched);
    SecureZeroMemory(keep, sizeof keep);
    SecureZeroMemory(&was_active, sizeof was_active);

    profiles_reload();
    ping_start(hwnd);
    exc_check_start(hwnd);
    layout(hwnd);
    return 1;
}

void act_subscription(HWND hwnd)
{
    wchar_t  url[2048];
    sub_job *job;
    HANDLE   th;

    /* A job may be switching profiles by index: no renumbering meanwhile. */
    if (g_sub_busy || g_busy) return;

    /* The field starts empty: the old address stays in use until a new one
       is entered, and is not put in front of the user. */
    if (!ask_string(hwnd, L"Подписка",
                    L"Адрес подписки — серверы из неё будут обновляться целиком, "
                    L"добавленные вручную останутся",
                    NULL, url, 2048))
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
