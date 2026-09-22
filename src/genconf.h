#ifndef UTGARD_GENCONF_H
#define UTGARD_GENCONF_H

#include "profiles.h"

/* Builds sing-box/config.generated.json out of three inputs:

     - the user's base config, read and never written back;
     - the enabled overlay files, also read-only;
     - the profile store.

   What the client owns and always emits itself: the outbounds (direct, one
   per profile, and the selector), the rule-set declaration for the generated
   list, the anti-loop bypass rules, and the final rule that sends listed
   traffic into the selector. Everything else comes from the base file.

   Nothing here touches Windows: the generator is plain C so it can be run and
   checked against a real sing-box binary during development. */

typedef struct {
    const char  *base_path;      /* sing-box/config.json */
    const char  *out_path;       /* sing-box/config.generated.json */
    const char **overlays;       /* enabled overlay files, in order */
    int          overlay_count;
    const char  *rule_set_path;  /* e.g. "lists/general.srs", as sing-box sees it */
    int          mtu;            /* tun MTU override; 0 keeps config.json's */
    const char  *log_level;      /* log level override; NULL keeps it */
    const char  *stack;          /* tun stack override; NULL keeps it */
    const char  *dns_host;       /* host of the "doh" DNS server; NULL keeps it */
    const profile_store *store;
} genconf_input;

/* 1 on success. On failure err holds a message meant for the user. */
int genconf_build(const genconf_input *in, char *err, size_t errcap);

/* The tag a profile gets in the generated config. Exposed so the UI can say
   which outbound is active without guessing. */
void genconf_tag(const profile_store *s, int index, char *out, size_t cap);

#endif
