#ifndef UTGARD_SETTINGS_H
#define UTGARD_SETTINGS_H

#include <wchar.h>

/* Client settings, kept in settings.txt beside the executable as key=value
   lines. They override the matching fields of the user's config.json when the
   generated config is built; config.json itself is never touched. */

#define SETTINGS_MTU_DEFAULT  1430
#define SETTINGS_MTU_MIN      1280    /* the IPv6 minimum link MTU */
#define SETTINGS_MTU_MAX      9000    /* sing-box's own default for tun */

typedef struct {
    int mtu;
    int log_level;      /* index into settings_log_levels */
    int stack;          /* index into settings_stacks */
    int dns;            /* index into settings_dns */
    int tray_on_close;  /* the cross hides to the tray instead of exiting */
    int update_check;   /* look for a newer release on start */
    int sub_interval;   /* index into settings_sub_hours */
    long long sub_last; /* when the subscription last loaded, Unix seconds */
    long long pac_last; /* when PAC URLs were last refreshed, Unix seconds */
} app_settings;

/* How often the subscription and PAC URLs are refreshed on their own. */
extern const int settings_sub_hours[];
extern const int settings_sub_count;
#define SETTINGS_SUB_DEFAULT 1     /* 6 hours */

/* TCP/IP stacks of the tun inbound. Deprecated upstream from sing-box 1.15
   and slated for removal in 1.17; valid in the pinned 1.14.1. */
extern const char *const settings_stacks[];
extern const int          settings_stack_count;
#define SETTINGS_STACK_DEFAULT 0   /* "system", as the base config has it */

/* DNS over HTTP/3 providers. Each one's HTTP/3 support is confirmed by the
   provider's own documentation, not assumed. */
/* DNS over HTTPS for the site list and PAC names. type is the sing-box DNS
   server type: "h3" (HTTP/3 over QUIC, UDP 443) or "https" (HTTP/2 over TCP
   443, for networks that drop QUIC). path NULL keeps /dns-query. */
typedef struct { const char *host; const char *type; const char *path; const wchar_t *label; } dns_preset;
extern const dns_preset settings_dns[];
extern const int        settings_dns_count;
#define SETTINGS_DNS_DEFAULT 0     /* Google, as the base config has it */

/* sing-box log levels, most talkative first. */
extern const char *const settings_log_levels[];
extern const int          settings_log_level_count;
#define SETTINGS_LOG_DEFAULT 4   /* "error" */

void settings_defaults(app_settings *s);
int  settings_load(app_settings *s);      /* missing file: defaults, returns 0 */
int  settings_save(const app_settings *s);

#endif
