#ifndef UTGARD_LINK_H
#define UTGARD_LINK_H

#include <stddef.h>

/* Share-link parsing. Deliberately free of Windows headers and of wchar_t:
   the input is untrusted UTF-8 from a link or a subscription body, so this
   module is built and fuzzed on the host under ASan/UBSan as well as shipped
   in the client. */

typedef enum {
    LINK_NONE  = 0,
    LINK_VLESS = 1,
    LINK_HY2   = 2,
    LINK_SS    = 3,
    LINK_TROJAN = 4,
    LINK_VMESS = 5,
    LINK_WG    = 6
} link_proto;

/* Room for every AmneziaWG parameter at its longest: five I-packets of up
   to LINK_AWG_ITAG_MAX plus the short ones. */
/* Longest single link accepted anywhere, an Amnezia vpn:// key included. */
#define LINK_URI_MAX (64 * 1024)
/* A base64 subscription decodes to at most 3/4 of its body (NET_BODY_MAX). */
#define NET_SUBSCRIPTION_DECODED_MAX (768 * 1024 + 16)
#define LINK_AWG_ITAG_MAX 1024
#define LINK_AWG_MAX      6144

typedef struct {
    link_proto proto;

    char name[128];          /* #fragment, may be empty */
    char server[256];
    int  port;               /* 443 when the link omits it */

    char uuid[64];           /* vless, vmess */
    char flow[32];
    char password[256];      /* hysteria2, shadowsocks, trojan */
    char method[64];         /* shadowsocks cipher, vmess security */
    int  alter_id;           /* vmess, 0 for AEAD */

    int  tls;
    int  insecure;
    char sni[256];
    char fingerprint[32];    /* utls; "chrome" unless the link says otherwise */

    int  reality;
    char public_key[128];
    char short_id[32];

    char obfs[32];           /* hysteria2 */
    char obfs_password[256];

    /* V2Ray transport for vless and trojan; empty means plain TCP. Only the
       five sing-box implements: ws, grpc, http, httpupgrade, quic. */
    char transport[16];
    char path[256];          /* ws, http, httpupgrade */
    char host[256];          /* ws Host header, http / httpupgrade host */
    char service_name[128];  /* grpc */
    char alpn[64];           /* comma-separated, for the TLS block */
    int  early_data;         /* ws: bytes of early data, Xray's ?ed= */

    /* WireGuard: keys are standard base64 of 32 bytes (44 characters). */
    char wg_private_key[64];
    char wg_peer_key[64];
    char wg_psk[64];         /* optional */
    char wg_address[160];    /* comma-separated prefixes, "10.0.0.2/32,fd00::2/128" */
    char wg_reserved[16];    /* optional "1,2,3" */
    int  mtu;                /* 0: sing-box default */
    int  keepalive;          /* seconds, 0: off */

    /* AmneziaWG parameters of a WireGuard profile, one "key = value" line
       each, keys lower-case. Empty for plain WireGuard. Kept as text, not as
       fields: the AmneziaWG service parses them itself, and new protocol
       versions add keys, so we carry the ones its parser accepts verbatim,
       after checking they are safe to put back into a config. */
    char awg[LINK_AWG_MAX];
} link_profile;

/* 1 when an awg block is exactly what the parser would have produced. */
int link_awg_valid(const char *awg);

/* A WireGuard profile that needs the AmneziaWG core rather than sing-box. */
int link_is_awg(const link_profile *p);

/* Parse one share link. Returns 1 on success, 0 with a message in err. */
int link_parse(const char *uri, link_profile *out, char *err, size_t errcap);

/* Parse a WireGuard configuration file ([Interface] / [Peer], as wg-quick
   writes it). The first peer is used. The name is left empty. */
int link_parse_wgconf(const char *text, size_t len, link_profile *out,
                      char *err, size_t errcap);

/* A file chosen as a WireGuard/AmneziaWG config: a wg-quick config or a
   file holding one link; otherwise an error that says what the file is. */
int link_parse_file(const char *text, size_t len, link_profile *out, char *err, size_t errcap);

/* Parse an Amnezia vpn:// link: its AmneziaWG or WireGuard container becomes
   a WireGuard profile (with AmneziaWG parameters when it has them), named
   after the server's description. link_parse hands vpn:// links here too. */
int link_parse_amnezia(const char *uri, link_profile *out, char *err, size_t errcap);

/* Parse a subscription body: either base64 of a newline-joined link list, or
   that list in plain text. Unparsable lines are skipped, not fatal — panels
   emit protocols we do not support alongside the ones we do.
   Returns how many profiles landed in out; skipped counts the rest. */
int link_parse_subscription(const char *body, size_t len,
                            link_profile *out, int max, int *skipped,
                            char *err, size_t errcap);

#endif
