#ifndef UTGARD_GENCONF_H
#define UTGARD_GENCONF_H

#include "profiles.h"

/* Builds the config sing-box runs with out of three inputs:

     - the user's base config, read and never written back;
     - the enabled overlay files, also read-only;
     - the profile store.

   What the client owns and always emits itself: the outbounds (direct, one
   per profile, and the selector), the rule-set declaration for the generated
   list, the anti-loop bypass rules, and the final rule that sends listed
   traffic into the selector. Everything else comes from the base file.

   The generator reads no files: it takes their contents, read by the
   platform code (src/win/confread.c on Windows). It is plain C, so it can
   be run and checked against a real sing-box binary during development. */

/* The messages for inputs that cannot be used, shared with the code that
   reads the files so a file that cannot be read and one that cannot be
   parsed read the same to the user. BAD_OVERLAY takes the overlay's name. */
#define GENCONF_MSG_BAD_BASE \
    "config.json не читается: нужен корректный JSON, без комментариев и висячих запятых"
#define GENCONF_MSG_BAD_OVERLAY "Не удалось прочитать %s"

/* One input file: its name for messages (the path) and its text,
   NUL-terminated. Both are required. */
typedef struct {
    const char *name;
    const char *text;
} genconf_file;

typedef struct {
    genconf_file base;            /* config.json beside utgard.exe */
    const genconf_file *overlays; /* enabled overlay files, in order */
    int          overlay_count;
    const char  *rule_set_path;  /* e.g. "lists/general.srs", as sing-box sees it */
    int          mtu;            /* tun MTU override; 0 keeps config.json's */
    const char  *log_level;      /* log level override; NULL keeps it */
    const char  *stack;          /* tun stack override; NULL keeps it */
    const char  *dns_host;       /* host of the "doh" DNS server; NULL keeps it */
    const char  *dns_type;       /* its type, "h3" or "https"; NULL keeps it */
    const char  *dns_path;       /* its path; NULL keeps it */
    const profile_store *store;

    /* The AmneziaWG tunnel, when the active profile needs it and it is up.
       The profile becomes a direct outbound bound to that adapter; the
       server is taken out of the TUN routes, so the tunnel's own packets
       reach it directly instead of looping back into sing-box. */
    const char  *awg_interface;  /* adapter name; NULL: no AmneziaWG tunnel */
    const char  *awg_server_ip;  /* the IP literal the service connects to */
    const char  *awg_exe;        /* full path of amneziawg.exe */
    int          pac_port;       /* authenticated C PAC decision bridge */
    int          pac_dns_port;   /* observe real DNS answers for domain PAC */
    int          pac_dns_vpn_port; /* direct inbound -> site-list DNS */
    int          pac_dns_sys_port; /* direct inbound -> original dns.final */
    int          vpn_proxy_port; /* app-owned mixed inbound -> selector */
    const char  *proxy_password;
    const char  *client_exe;     /* relay's own sockets bypass the TUN */
} genconf_input;

/* 1 on success, with the config in *out_text. It carries the server
   credentials, so it is never written to disk: it goes to sing-box through
   stdin, and genconf_text_free wipes it. On failure err holds a message
   meant for the user and *out_text is NULL. */
int  genconf_build(const genconf_input *in, char **out_text, char *err, size_t errcap);
void genconf_text_free(char *text);

/* The tag a profile gets in the generated config. Exposed so the UI can say
   which outbound is active without guessing. out needs GENCONF_TAG_MAX. */
#define GENCONF_TAG_MAX 80
void genconf_tag(const profile_store *s, int index, char *out, size_t cap);

#endif
