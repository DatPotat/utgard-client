#include "settings.h"

#include <windows.h>
#include <strsafe.h>
#include "fileio.h"
#include <stdlib.h>
#include <string.h>

const char *const settings_log_levels[] = {
    "trace", "debug", "info", "warn", "error", "fatal", "panic"
};
const int settings_log_level_count =
    (int)(sizeof settings_log_levels / sizeof settings_log_levels[0]);

const char *const settings_stacks[] = { "system", "gvisor", "mixed" };
const int settings_stack_count =
    (int)(sizeof settings_stacks / sizeof settings_stacks[0]);

const int settings_sub_hours[] = { 3, 6, 12 };
const int settings_sub_count =
    (int)(sizeof settings_sub_hours / sizeof settings_sub_hours[0]);

/* NextDNS without a profile ID: the path stays "/", since a path segment
   there is read as the profile. HTTP/3 has its own host at NextDNS. */
const dns_preset settings_dns[] = {
    { "dns.google",          "h3",    NULL, L"Google — HTTP/3" },
    { "dns.google",          "https", NULL, L"Google — HTTP/2" },
    { "cloudflare-dns.com",  "h3",    NULL, L"Cloudflare — HTTP/3" },
    { "cloudflare-dns.com",  "https", NULL, L"Cloudflare — HTTP/2" },
    { "dns.quad9.net",       "h3",    NULL, L"Quad9 — HTTP/3" },
    { "dns.quad9.net",       "https", NULL, L"Quad9 — HTTP/2" },
    { "doh3.dns.nextdns.io", "h3",    "/",  L"NextDNS — HTTP/3" },
    { "dns.nextdns.io",      "https", "/",  L"NextDNS — HTTP/2" }
};
const int settings_dns_count =
    (int)(sizeof settings_dns / sizeof settings_dns[0]);

void settings_defaults(app_settings *s)
{
    s->zapret_path[0] = '\0';
    s->mtu           = SETTINGS_MTU_DEFAULT;
    s->log_level     = SETTINGS_LOG_DEFAULT;
    s->stack         = SETTINGS_STACK_DEFAULT;
    s->dns           = SETTINGS_DNS_DEFAULT;
    s->tray_on_close = 1;
    s->update_check  = 1;
    s->theme         = SETTINGS_THEME_UNSET;
    s->sub_interval  = SETTINGS_SUB_DEFAULT;
    s->sub_last      = 0;
    s->pac_last      = 0;
}

static int settings_path(wchar_t *out, size_t cap)
{
    wchar_t  dir[MAX_PATH * 2];
    wchar_t *slash;
    DWORD    n;

    n = GetModuleFileNameW(NULL, dir, (DWORD)(sizeof dir / sizeof dir[0]));
    if (n == 0 || n >= sizeof dir / sizeof dir[0]) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%ssettings.txt", dir));
}

int settings_load(app_settings *s)
{
    wchar_t path[MAX_PATH * 2];
    char    buf[4096];
    char   *line, *next;

    settings_defaults(s);
    if (!settings_path(path, MAX_PATH * 2)) return 0;
    if (!file_read(path, buf, sizeof buf, NULL)) return 0;

    /* Unknown keys and bad values are ignored and leave the default: a hand
       edit gone wrong should cost one setting, not the whole file. */
    for (line = buf; line && *line; line = next) {
        char *eq, *end;

        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        end = line + strlen(line);
        while (end > line && (end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';

        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        if (strcmp(line, "zapret_path") == 0) {
            /* The rest of the line: a path may hold spaces, '=' or Cyrillic. */
            size_t n = strlen(eq + 1);
            if (n < sizeof s->zapret_path) memcpy(s->zapret_path, eq + 1, n + 1);
        } else if (strcmp(line, "mtu") == 0) {
            long v = strtol(eq + 1, NULL, 10);
            if (v >= SETTINGS_MTU_MIN && v <= SETTINGS_MTU_MAX) s->mtu = (int)v;
        } else if (strcmp(line, "log_level") == 0) {
            int i;
            for (i = 0; i < settings_log_level_count; i++)
                if (strcmp(eq + 1, settings_log_levels[i]) == 0) s->log_level = i;
        } else if (strcmp(line, "stack") == 0) {
            int i;
            for (i = 0; i < settings_stack_count; i++)
                if (strcmp(eq + 1, settings_stacks[i]) == 0) s->stack = i;
        } else if (strcmp(line, "dns") == 0) {
            /* "host/type"; a bare host is from before HTTP/2 was offered and
               means that host over HTTP/3. */
            int i;
            for (i = 0; i < settings_dns_count; i++) {
                size_t n = strlen(settings_dns[i].host);
                if (strncmp(eq + 1, settings_dns[i].host, n) != 0) continue;
                if ((eq[1 + n] == '\0' && !strcmp(settings_dns[i].type, "h3")) ||
                    (eq[1 + n] == '/' && !strcmp(eq + 2 + n, settings_dns[i].type))) {
                    s->dns = i;
                    break;
                }
            }
        } else if (strcmp(line, "tray_on_close") == 0) {
            s->tray_on_close = (eq[1] == '1');
        } else if (strcmp(line, "update_check") == 0) {
            s->update_check = (eq[1] == '1');
        } else if (strcmp(line, "theme") == 0) {
            if (eq[1] >= '0' && eq[1] <= '3') s->theme = eq[1] - '0';
        } else if (strcmp(line, "sub_interval_hours") == 0) {
            long v = strtol(eq + 1, NULL, 10);
            int  i;
            for (i = 0; i < settings_sub_count; i++)
                if (settings_sub_hours[i] == v) s->sub_interval = i;
        } else if (strcmp(line, "sub_last") == 0) {
            long long v = _strtoi64(eq + 1, NULL, 10);
            if (v > 0) s->sub_last = v;
        } else if (strcmp(line, "pac_last") == 0) {
            long long v = _strtoi64(eq + 1, NULL, 10);
            if (v > 0) s->pac_last = v;
        }
    }
    return 1;
}

int settings_save(const app_settings *s)
{
    wchar_t path[MAX_PATH * 2];
    char    buf[2048];
    int     lvl = (s->log_level >= 0 && s->log_level < settings_log_level_count)
                      ? s->log_level : SETTINGS_LOG_DEFAULT;
    int     stk = (s->stack >= 0 && s->stack < settings_stack_count)
                      ? s->stack : SETTINGS_STACK_DEFAULT;
    int     dns = (s->dns >= 0 && s->dns < settings_dns_count)
                      ? s->dns : SETTINGS_DNS_DEFAULT;
    int     sub = (s->sub_interval >= 0 && s->sub_interval < settings_sub_count)
                      ? s->sub_interval : SETTINGS_SUB_DEFAULT;

    if (!settings_path(path, MAX_PATH * 2)) return 0;
    if (FAILED(StringCchPrintfA(buf, sizeof buf,
            "mtu=%d\r\nlog_level=%s\r\nstack=%s\r\ndns=%s/%s\r\ntray_on_close=%d\r\nupdate_check=%d\r\ntheme=%d\r\n"
            "sub_interval_hours=%d\r\nsub_last=%lld\r\npac_last=%lld\r\n",
            s->mtu, settings_log_levels[lvl], settings_stacks[stk],
            settings_dns[dns].host, settings_dns[dns].type, s->tray_on_close ? 1 : 0, s->update_check ? 1 : 0,
            (s->theme >= 0 && s->theme <= 3) ? s->theme : 0,
            settings_sub_hours[sub], s->sub_last, s->pac_last)))
        return 0;
    if (s->zapret_path[0] &&
        (FAILED(StringCchCatA(buf, sizeof buf, "zapret_path=")) ||
         FAILED(StringCchCatA(buf, sizeof buf, s->zapret_path)) ||
         FAILED(StringCchCatA(buf, sizeof buf, "\r\n"))))
        return 0;

    return file_write(path, buf, strlen(buf));
}
