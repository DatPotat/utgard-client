#include "tunnames.h"
#include "genconf.h"

#include "parson.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "errmsg.h"

void genconf_text_free(char *text)
{
    /* volatile: a plain memset before free is dead code the optimiser drops */
    volatile char *p = text;

    if (!text) return;
    while (*p) *p++ = 0;
    json_free_serialized_string(text);
}

#define SELECTOR_TAG  "utgard"
#define RULE_SET_TAG  "general"

/* ---- small helpers -------------------------------------------------- */

static int is_ipv4(const char *s)
{
    int parts = 0, digits = 0, value = 0;

    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            if (++digits > 3) return 0;
            value = value * 10 + (*s - '0');
            if (value > 255) return 0;
        } else if (*s == '.' || *s == '\0') {
            if (digits == 0) return 0;
            parts++;
            digits = 0;
            value = 0;
            if (*s == '\0') break;
        } else {
            return 0;
        }
    }
    return parts == 4;
}

static int is_ip(const char *s)
{
    return is_ipv4(s) || strchr(s, ':') != NULL;   /* ':' can only be IPv6 here */
}

/* Length of the well-formed UTF-8 sequence at s, 0 if it is not one.
   parson refuses a string with a broken sequence, and a tag it refuses is
   simply missing from the config - sing-box then fails on "missing tags". */
static size_t utf8_seq(const unsigned char *s)
{
    size_t n, i;

    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF) n = 2;
    else if (s[0] >= 0xE0 && s[0] <= 0xEF) n = 3;
    else if (s[0] >= 0xF0 && s[0] <= 0xF4) n = 4;
    else return 0;
    for (i = 1; i < n; i++) if ((s[i] & 0xC0) != 0x80) return 0;
    if (s[0] == 0xE0 && s[1] < 0xA0) return 0;     /* overlong */
    if (s[0] == 0xED && s[1] > 0x9F) return 0;     /* surrogate */
    if (s[0] == 0xF0 && s[1] < 0x90) return 0;     /* overlong */
    if (s[0] == 0xF4 && s[1] > 0x8F) return 0;     /* past U+10FFFF */
    return n;
}

/* sing-box accepts non-ASCII tags - verified against 1.13.14 - so the profile
   name is kept as the user wrote it. Only what would break the config or make
   log lines unreadable is removed: control characters, quotes, backslashes,
   broken UTF-8, and runs of whitespace, which become a single dash. A name
   that does not fit is cut between characters, never inside one. */
static void sanitize(const char *src, char *dst, size_t cap)
{
    const unsigned char *s = (const unsigned char *)src;
    size_t               n = 0;

    while (*s) {
        size_t len = utf8_seq(s);

        if (len == 0) { s++; continue; }
        if (len == 1 && (*s < 0x20 || *s == 0x7F || *s == '"' || *s == '\\')) { s++; continue; }
        if (len == 1 && (*s == ' ' || *s == '\t')) {
            if (n && dst[n - 1] != '-' && n + 1 < cap) dst[n++] = '-';
            s++;
            continue;
        }
        if (n + len >= cap) break;
        memcpy(dst + n, s, len);
        n += len;
        s += len;
    }
    while (n && dst[n - 1] == '-') n--;
    dst[n] = '\0';
}

/* Name first, then the server address, then a constant - so a profile whose
   remark sanitizes away still gets something recognisable. */
static void tag_base(const profile_store *s, int i, char *out, size_t cap)
{
    sanitize(s->items[i].link.name, out, cap);
    if (!out[0]) sanitize(s->items[i].link.server, out, cap);
    if (!out[0]) snprintf(out, cap, "profile");
}

/* Every tag must be unique among the outbounds and endpoints, or sing-box
   refuses the whole config ("duplicate outbound/endpoint tag", checked on
   1.14.1). So the tags of all earlier profiles are worked out first, and a
   name that is taken - by one of them or by our own "direct" and the
   selector - gets -2, -3, ... until it is free. The base leaves room for the
   suffix, so appending it never cuts the name. */
void genconf_tag(const profile_store *s, int index, char *out, size_t cap)
{
    char tags[PROFILES_MAX][GENCONF_TAG_MAX];
    int  i, j, k;

    if (!s || index < 0 || index >= s->count || index >= PROFILES_MAX || cap < GENCONF_TAG_MAX) {
        if (cap) out[0] = '\0';
        return;
    }

    for (i = 0; i <= index; i++) {
        char base[GENCONF_TAG_MAX - 8];

        tag_base(s, i, base, sizeof base);
        snprintf(tags[i], GENCONF_TAG_MAX, "%s", base);
        for (k = 2;; k++) {
            int taken = strcmp(tags[i], "direct") == 0 || strcmp(tags[i], SELECTOR_TAG) == 0;
            for (j = 0; j < i && !taken; j++) taken = strcmp(tags[j], tags[i]) == 0;
            if (!taken) break;
            /* k never passes PROFILES_MAX + 2; the cast tells the compiler so */
            snprintf(tags[i], GENCONF_TAG_MAX, "%s-%d", base, (unsigned char)k);
        }
    }
    snprintf(out, cap, "%s", tags[index]);
}

/* ---- outbounds ------------------------------------------------------ */

/* V2Ray transport as sing-box names its fields (sing-box documentation,
   V2Ray Transport). NULL for plain TCP. */
static JSON_Value *transport_block(const link_profile *p)
{
    JSON_Value  *v;
    JSON_Object *o;

    if (!p->transport[0]) return NULL;
    v = json_value_init_object();
    o = json_value_get_object(v);
    json_object_set_string(o, "type", p->transport);

    if (strcmp(p->transport, "ws") == 0) {
        if (p->path[0]) json_object_set_string(o, "path", p->path);
        if (p->host[0]) {
            JSON_Value *hv = json_value_init_object();
            json_object_set_string(json_value_get_object(hv), "Host", p->host);
            json_object_set_value(o, "headers", hv);
        }
        if (p->early_data > 0) {
            json_object_set_number(o, "max_early_data", p->early_data);
            json_object_set_string(o, "early_data_header_name", "Sec-WebSocket-Protocol");
        }
    } else if (strcmp(p->transport, "grpc") == 0) {
        if (p->service_name[0]) json_object_set_string(o, "service_name", p->service_name);
    } else if (strcmp(p->transport, "http") == 0) {
        if (p->path[0]) json_object_set_string(o, "path", p->path);
        if (p->host[0]) {
            JSON_Value *hv = json_value_init_array();
            json_array_append_string(json_value_get_array(hv), p->host);
            json_object_set_value(o, "host", hv);
        }
    } else if (strcmp(p->transport, "httpupgrade") == 0) {
        if (p->path[0]) json_object_set_string(o, "path", p->path);
        if (p->host[0]) json_object_set_string(o, "host", p->host);
    }
    /* quic: no options */
    return v;
}

/* TLS as VLESS and Trojan share it: server name, optional insecure, the
   uTLS fingerprint, and REALITY when the link asks for it. */
static JSON_Value *tls_block(const link_profile *p)
{
    JSON_Value  *tv = json_value_init_object();
    JSON_Object *t  = json_value_get_object(tv);

    json_object_set_boolean(t, "enabled", 1);
    json_object_set_string(t, "server_name", p->sni);
    if (p->insecure) json_object_set_boolean(t, "insecure", 1);

    if (p->alpn[0]) {                        /* "h2,http/1.1" -> ["h2","http/1.1"] */
        JSON_Value *av = json_value_init_array();
        const char *q = p->alpn;
        while (*q) {
            char   one[sizeof p->alpn];
            size_t n = strcspn(q, ",");
            if (n && n < sizeof one) {
                memcpy(one, q, n);
                one[n] = '\0';
                json_array_append_string(json_value_get_array(av), one);
            }
            q += n;
            if (*q == ',') q++;
        }
        if (json_array_get_count(json_value_get_array(av)))
            json_object_set_value(t, "alpn", av);
        else
            json_value_free(av);
    }

    if (p->fingerprint[0]) {
        JSON_Value  *uv = json_value_init_object();
        JSON_Object *u  = json_value_get_object(uv);
        json_object_set_boolean(u, "enabled", 1);
        json_object_set_string(u, "fingerprint", p->fingerprint);
        json_object_set_value(t, "utls", uv);
    }
    if (p->reality) {
        JSON_Value  *rv = json_value_init_object();
        JSON_Object *r  = json_value_get_object(rv);
        json_object_set_boolean(r, "enabled", 1);
        json_object_set_string(r, "public_key", p->public_key);
        if (p->short_id[0]) json_object_set_string(r, "short_id", p->short_id);
        json_object_set_value(t, "reality", rv);
    }
    return tv;
}

static JSON_Value *make_outbound(const link_profile *p, const char *tag)
{
    JSON_Value  *v = json_value_init_object();
    JSON_Object *o = json_value_get_object(v);

    json_object_set_string(o, "tag", tag);
    json_object_set_string(o, "server", p->server);
    json_object_set_number(o, "server_port", p->port);

    if (p->proto == LINK_VLESS) {
        json_object_set_string(o, "type", "vless");
        json_object_set_string(o, "uuid", p->uuid);
        if (p->flow[0]) json_object_set_string(o, "flow", p->flow);
        if (p->tls) json_object_set_value(o, "tls", tls_block(p));
        if (p->transport[0]) json_object_set_value(o, "transport", transport_block(p));
    } else if (p->proto == LINK_TROJAN) {
        json_object_set_string(o, "type", "trojan");
        json_object_set_string(o, "password", p->password);
        if (p->tls) json_object_set_value(o, "tls", tls_block(p));
        if (p->transport[0]) json_object_set_value(o, "transport", transport_block(p));
    } else if (p->proto == LINK_HY2) {
        JSON_Value  *tv = json_value_init_object();
        JSON_Object *t  = json_value_get_object(tv);
        JSON_Value  *av = json_value_init_array();

        json_object_set_string(o, "type", "hysteria2");
        json_object_set_string(o, "password", p->password);

        json_array_append_string(json_value_get_array(av), "h3");
        json_object_set_boolean(t, "enabled", 1);
        json_object_set_string(t, "server_name", p->sni);
        json_object_set_value(t, "alpn", av);
        if (p->insecure) json_object_set_boolean(t, "insecure", 1);
        json_object_set_value(o, "tls", tv);

        if (p->obfs[0]) {
            JSON_Value  *ov = json_value_init_object();
            JSON_Object *ob = json_value_get_object(ov);
            json_object_set_string(ob, "type", p->obfs);
            json_object_set_string(ob, "password", p->obfs_password);
            json_object_set_value(o, "obfs", ov);
        }
    } else if (p->proto == LINK_VMESS) {
        json_object_set_string(o, "type", "vmess");
        json_object_set_string(o, "uuid", p->uuid);
        json_object_set_string(o, "security", p->method[0] ? p->method : "auto");
        if (p->alter_id > 0) json_object_set_number(o, "alter_id", p->alter_id);
        if (p->tls) json_object_set_value(o, "tls", tls_block(p));
        if (p->transport[0]) json_object_set_value(o, "transport", transport_block(p));
    } else if (p->proto == LINK_SS) {
        json_object_set_string(o, "type", "shadowsocks");
        json_object_set_string(o, "method", p->method);
        json_object_set_string(o, "password", p->password);
    } else {
        json_value_free(v);
        return NULL;
    }

    return v;
}

/* WireGuard is an endpoint in sing-box, not an outbound, but its tag works
   wherever an outbound tag does - in the selector too (checked by running
   sing-box 1.14.1). The peer takes all traffic: what reaches it is decided
   by our route rules, not by allowed_ips. */
static void append_list(JSON_Object *o, const char *key, const char *csv)
{
    JSON_Value *av = json_value_init_array();
    const char *q = csv;
    while (*q) {
        char   one[64];
        size_t n = strcspn(q, ",");
        if (n && n < sizeof one) {
            memcpy(one, q, n);
            one[n] = '\0';
            json_array_append_string(json_value_get_array(av), one);
        }
        q += n;
        if (*q == ',') q++;
    }
    json_object_set_value(o, key, av);
}

/* The same for interface addresses, IPv4 only: Utgard does not do IPv6 -
   its TUN has no IPv6 address and DNS resolves IPv4 only - so an IPv6
   address of a WireGuard profile is left out rather than half-used. */
static void append_list4(JSON_Object *o, const char *key, const char *csv)
{
    JSON_Value *av = json_value_init_array();
    const char *q = csv;
    while (*q) {
        char   one[64];
        size_t n = strcspn(q, ",");
        if (n && n < sizeof one && !memchr(q, ':', n)) {
            memcpy(one, q, n);
            one[n] = '\0';
            json_array_append_string(json_value_get_array(av), one);
        }
        q += n;
        if (*q == ',') q++;
    }
    json_object_set_value(o, key, av);
}

static JSON_Value *make_endpoint(const link_profile *p, const char *tag)
{
    JSON_Value  *v  = json_value_init_object();
    JSON_Object *o  = json_value_get_object(v);
    JSON_Value  *pv = json_value_init_object();
    JSON_Object *po = json_value_get_object(pv);
    JSON_Value  *peers = json_value_init_array();

    json_object_set_string(o, "type", "wireguard");
    json_object_set_string(o, "tag", tag);
    append_list4(o, "address", p->wg_address);
    json_object_set_string(o, "private_key", p->wg_private_key);
    if (p->mtu > 0) json_object_set_number(o, "mtu", p->mtu);

    json_object_set_string(po, "address", p->server);
    json_object_set_number(po, "port", p->port);
    json_object_set_string(po, "public_key", p->wg_peer_key);
    if (p->wg_psk[0]) json_object_set_string(po, "pre_shared_key", p->wg_psk);
    append_list(po, "allowed_ips", "0.0.0.0/0,::/0");
    if (p->keepalive > 0) json_object_set_number(po, "persistent_keepalive_interval", p->keepalive);
    if (p->wg_reserved[0]) {
        JSON_Value *rv = json_value_init_array();
        const char *q = p->wg_reserved;
        while (*q) {
            json_array_append_number(json_value_get_array(rv), strtol(q, NULL, 10));
            q += strcspn(q, ",");
            if (*q == ',') q++;
        }
        json_object_set_value(po, "reserved", rv);
    }
    json_array_append_value(json_value_get_array(peers), pv);
    json_object_set_value(o, "peers", peers);
    return v;
}

/* ---- route rules the client owns ------------------------------------ */

/* Two profiles on one host would otherwise list that host twice. */
static int array_has(JSON_Array *a, const char *s)
{
    size_t i, n = json_array_get_count(a);
    for (i = 0; i < n; i++) {
        const char *v = json_array_get_string(a, i);
        if (v && strcmp(v, s) == 0) return 1;
    }
    return 0;
}

static void append_unique(JSON_Array *a, const char *s)
{
    if (!array_has(a, s)) json_array_append_string(a, s);
}

static JSON_Value *route_rule(const char *outbound)
{
    JSON_Value  *v = json_value_init_object();
    json_object_set_string(json_value_get_object(v), "action", "route");
    json_object_set_string(json_value_get_object(v), "outbound", outbound);
    return v;
}

static void append_frame_head(JSON_Array *rules, const profile_store *s)
{
    JSON_Value  *v;
    JSON_Object *o;
    JSON_Value  *hosts = json_value_init_array();
    JSON_Value  *ips   = json_value_init_array();
    int          i;

    v = json_value_init_object();
    json_object_set_string(json_value_get_object(v), "action", "sniff");
    json_array_append_value(rules, v);

    v = json_value_init_object();
    json_object_set_string(json_value_get_object(v), "protocol", "dns");
    json_object_set_string(json_value_get_object(v), "action", "hijack-dns");
    json_array_append_value(rules, v);

    /* Traffic to the proxy servers themselves must never enter the tunnel. */
    for (i = 0; i < s->count; i++) {
        const char *host = s->items[i].link.server;
        if (!host[0]) continue;
        if (is_ip(host)) {
            char cidr[300];
            if (strchr(host, ':')) snprintf(cidr, sizeof cidr, "%s/128", host);
            else                   snprintf(cidr, sizeof cidr, "%s/32", host);
            append_unique(json_value_get_array(ips), cidr);
        } else {
            append_unique(json_value_get_array(hosts), host);
        }
    }

    if (json_array_get_count(json_value_get_array(hosts))) {
        v = route_rule("direct");
        o = json_value_get_object(v);
        json_object_set_value(o, "domain", hosts);
        json_array_append_value(rules, v);
    } else {
        json_value_free(hosts);
    }

    if (json_array_get_count(json_value_get_array(ips))) {
        v = route_rule("direct");
        o = json_value_get_object(v);
        json_object_set_value(o, "ip_cidr", ips);
        json_array_append_value(rules, v);
    } else {
        json_value_free(ips);
    }

    v = route_rule("direct");
    json_object_set_boolean(json_value_get_object(v), "ip_is_private", 1);
    json_array_append_value(rules, v);

    v = route_rule("direct");
    {
        JSON_Value *cg = json_value_init_array();
        json_array_append_string(json_value_get_array(cg), "100.64.0.0/10");
        json_object_set_value(json_value_get_object(v), "ip_cidr", cg);
    }
    json_array_append_value(rules, v);
}

/* Copy every element of src into dst. Used for the user's own rules and for
   the overlays, which land between the bypass rules and the rule-set rule -
   the position that makes an exclusion overlay actually work. */
static void append_all(JSON_Array *dst, const JSON_Array *src)
{
    size_t i, n;

    if (!src) return;
    n = json_array_get_count((JSON_Array *)src);
    for (i = 0; i < n; i++) {
        JSON_Value *copy = json_value_deep_copy(
            json_array_get_value((JSON_Array *)src, i));
        if (copy) json_array_append_value(dst, copy);
    }
}

/* ---- overlays ------------------------------------------------------- */

static void free_overlays(JSON_Value **ovl, int n)
{
    int i;
    for (i = 0; i < n; i++) json_value_free(ovl[i]);
    free(ovl);
}

/* DNS servers from the overlays join the base list. A tag must stay unique:
   a clash would make sing-box refuse the whole config with a message that
   does not name the overlay, so the overlay is named here instead. */
/* Application lists are files people pass around. A DNS server in one may
   only be a plain resolver, and may not point sing-box - which runs
   elevated - at files on this machine: the types that read files or
   start other machinery are refused, and file paths are cut from TLS. */
static int overlay_dns_server_ok(JSON_Object *so)
{
    static const char *types[] = { "local", "tcp", "udp", "tls", "quic", "https", "h3" };
    static const char *paths[] = { "certificate_path", "client_certificate_path",
                                   "client_key_path", "key_path", "certificate_provider" };
    const char  *type = json_object_get_string(so, "type");
    JSON_Object *tls  = json_object_get_object(so, "tls");
    size_t       i;
    int          known = 0;

    for (i = 0; type && i < sizeof types / sizeof types[0]; i++)
        if (strcmp(type, types[i]) == 0) known = 1;
    if (!known) return 0;
    if (tls) {
        for (i = 0; i < sizeof paths / sizeof paths[0]; i++) json_object_remove(tls, paths[i]);
        json_object_dotremove(tls, "ech.config_path");
    }
    return 1;
}

static int merge_dns_servers(JSON_Object *dns, JSON_Value **ovl, int n,
                             const genconf_input *in, char *err, size_t errcap)
{
    JSON_Array *servers = json_object_get_array(dns, "servers");
    int         i;

    if (!servers) {
        json_object_set_value(dns, "servers", json_value_init_array());
        servers = json_object_get_array(dns, "servers");
    }
    for (i = 0; i < n; i++) {
        JSON_Object *od  = json_object_get_object(json_value_get_object(ovl[i]), "dns");
        JSON_Array  *add = od ? json_object_get_array(od, "servers") : NULL;
        size_t       k, cnt = add ? json_array_get_count(add) : 0;

        for (k = 0; k < cnt; k++) {
            JSON_Object *so  = json_array_get_object(add, k);
            const char  *tag = so ? json_object_get_string(so, "tag") : NULL;
            size_t       j, have = json_array_get_count(servers);

            if (!tag || !tag[0]) {
                if (err && errcap)
                    snprintf(err, errcap, "В %s у DNS-сервера нет тега", in->overlays[i].name);
                return 0;
            }
            for (j = 0; j < have; j++) {
                const char *t = json_object_get_string(json_array_get_object(servers, j), "tag");
                if (t && strcmp(t, tag) == 0) {
                    if (err && errcap)
                        snprintf(err, errcap, "В %s DNS-сервер с тегом «%s» уже объявлен",
                                 in->overlays[i].name, tag);
                    return 0;
                }
            }
            {
                JSON_Value *copy = json_value_deep_copy(json_array_get_value(add, k));
                if (!overlay_dns_server_ok(json_value_get_object(copy))) {
                    json_value_free(copy);
                    if (err && errcap)
                        snprintf(err, errcap, "В %s DNS-сервер «%s» недопустимого типа — "
                                 "в списках приложений разрешены local, tcp, udp, tls, quic, https, h3",
                                 in->overlays[i].name, tag);
                    return 0;
                }
                json_array_append_value(servers, copy);
            }
        }
    }
    return 1;
}

/* What the stages of genconf_build share. root and the kept copies are
   freed once, by genconf_build; a stage that fails frees only its own. */
typedef struct {
    const genconf_input *in;
    const profile_store *s;
    JSON_Value  *root;
    JSON_Object *ro;
    JSON_Object *route, *dns;
    JSON_Value  *user_route_keep, *user_dns_keep;
    JSON_Array  *user_route_rules, *user_dns_rules;
    JSON_Value **ovl;
    int          novl;
    char         pac_dns_vpn_server[80];
    char         pac_dns_system_server[80];
    int          pac_dns_tag_too_long;
} build_state;

/* config.json and the overlays, parsed once, up front: both the route rules
   and the DNS part come from the overlays. */
static int parse_inputs(build_state *b, char *err, size_t errcap)
{
    const genconf_input *in = b->in;

    b->root = json_parse_string(in->base.text);
    if (!b->root || json_value_get_type(b->root) != JSONObject)
        return oops(err, errcap, GENCONF_MSG_BAD_BASE);
    b->ro = json_value_get_object(b->root);

    if (in->overlay_count > 0) {
        b->ovl = (JSON_Value **)calloc((size_t)in->overlay_count, sizeof *b->ovl);
        if (!b->ovl) return oops(err, errcap, "Не хватило памяти для оверлеев");
    }
    for (b->novl = 0; b->novl < in->overlay_count; b->novl++) {
        JSON_Value *v = in->overlays[b->novl].text ? json_parse_string(in->overlays[b->novl].text) : NULL;
        b->ovl[b->novl] = v;
        if (!v || json_value_get_type(v) != JSONObject) {
            if (err && errcap)
                snprintf(err, errcap, GENCONF_MSG_BAD_OVERLAY, in->overlays[b->novl].name);
            b->novl++;                           /* the failed one is freed too */
            return 0;
        }
    }
    return 1;
}

/* The user's own rules, kept aside before the client's frame replaces the
   arrays they live in; for PAC, the DNS servers its two inbounds answer
   with: dns.final, and the server of the rule for the site list. */
static int keep_user_rules(build_state *b, char *err, size_t errcap)
{
    b->route = json_object_get_object(b->ro, "route");
    if (b->route) {
        JSON_Array *a = json_object_get_array(b->route, "rules");
        if (a) {
            b->user_route_keep = json_value_deep_copy(json_array_get_wrapping_value(a));
            b->user_route_rules = json_value_get_array(b->user_route_keep);
        }
    }
    b->dns = json_object_get_object(b->ro, "dns");
    if (b->dns) {
        JSON_Array *a = json_object_get_array(b->dns, "rules");
        const char *final = json_object_get_string(b->dns, "final");
        if (!final) snprintf(b->pac_dns_system_server, sizeof b->pac_dns_system_server, "local");
        else if (strlen(final) < sizeof b->pac_dns_system_server)
            snprintf(b->pac_dns_system_server, sizeof b->pac_dns_system_server, "%s", final);
        else b->pac_dns_tag_too_long = 1;
        if (a) {
            size_t k, count = json_array_get_count(a);
            b->user_dns_keep = json_value_deep_copy(json_array_get_wrapping_value(a));
            b->user_dns_rules = json_value_get_array(b->user_dns_keep);
            for (k = 0; k < count && !b->pac_dns_vpn_server[0]; k++) {
                JSON_Object *rule = json_array_get_object(a, k);
                JSON_Array *sets = rule ? json_object_get_array(rule, "rule_set") : NULL;
                const char *server = rule ? json_object_get_string(rule, "server") : NULL;
                size_t j, nsets = sets ? json_array_get_count(sets) : 0;
                for (j = 0; server && j < nsets; j++) {
                    const char *set = json_array_get_string(sets, j);
                    if (set && strcmp(set, RULE_SET_TAG) == 0) {
                        if (strlen(server) < sizeof b->pac_dns_vpn_server)
                            snprintf(b->pac_dns_vpn_server, sizeof b->pac_dns_vpn_server, "%s", server);
                        else b->pac_dns_tag_too_long = 1;
                        break;
                    }
                }
            }
        }
    }

    if (b->in->pac_port && b->pac_dns_tag_too_long)
        return oops(err, errcap, "Тег DNS-сервера для PAC слишком длинный");
    if (b->in->pac_port && (!b->dns || !b->pac_dns_vpn_server[0] || !b->pac_dns_system_server[0]))
        return oops(err, errcap, "Для PAC нужны dns.final и DNS-правило rule_set general");

    if (!b->route) {
        json_object_set_value(b->ro, "route", json_value_init_object());
        b->route = json_object_get_object(b->ro, "route");
    }
    return 1;
}

/* direct, one outbound per profile (WireGuard ones as endpoints), the
   selector, and PAC's SOCKS hop. */
static int build_outbounds(build_state *b, char *err, size_t errcap)
{
    const genconf_input *in = b->in;
    const profile_store *s = b->s;
    JSON_Value *outbounds = json_value_init_array();
    JSON_Array *oarr = json_value_get_array(outbounds);
    char        active_tag[GENCONF_TAG_MAX] = { 0 };
    int         i, active_ok = 0, active_awg = 0;

    {
        JSON_Value  *dv = json_value_init_object();
        json_object_set_string(json_value_get_object(dv), "type", "direct");
        json_object_set_string(json_value_get_object(dv), "tag", "direct");
        json_array_append_value(oarr, dv);
    }
    {
        JSON_Value *sel_list  = json_value_init_array();
        JSON_Value *endpoints = json_value_init_array();

        for (i = 0; i < s->count; i++) {
            char        tag[GENCONF_TAG_MAX];
            JSON_Value *ov;

            genconf_tag(s, i, tag, sizeof tag);
            /* sing-box refuses AmneziaWG parameters: the active AmneziaWG
               profile becomes a direct outbound bound to the tunnel the
               service raised; the others stay out of the config. */
            if (link_is_awg(&s->items[i].link)) {
                JSON_Value *dv;
                if (i != s->active) continue;         /* one tunnel at a time */
                if (!in->awg_interface) { active_awg = 1; continue; }
                dv = json_value_init_object();
                json_object_set_string(json_value_get_object(dv), "type", "direct");
                json_object_set_string(json_value_get_object(dv), "tag", tag);
                json_object_set_string(json_value_get_object(dv), "bind_interface", in->awg_interface);
                json_array_append_value(oarr, dv);
                json_array_append_string(json_value_get_array(sel_list), tag);
                snprintf(active_tag, sizeof active_tag, "%s", tag);
                active_ok = 1;
                continue;
            }
            if (s->items[i].link.proto == LINK_WG) {
                json_array_append_value(json_value_get_array(endpoints),
                                        make_endpoint(&s->items[i].link, tag));
            } else {
                ov = make_outbound(&s->items[i].link, tag);
                if (!ov) continue;                 /* unknown protocol: skip */
                json_array_append_value(oarr, ov);
            }
            json_array_append_string(json_value_get_array(sel_list), tag);
            if (i == s->active) {
                snprintf(active_tag, sizeof active_tag, "%s", tag);
                active_ok = 1;
            }
        }

        /* Only ours: a user endpoint is dropped like a user outbound. */
        json_object_remove(b->ro, "endpoints");
        if (json_array_get_count(json_value_get_array(endpoints)))
            json_object_set_value(b->ro, "endpoints", endpoints);
        else
            json_value_free(endpoints);

        if (!active_ok) {
            json_value_free(sel_list);
            json_value_free(outbounds);
            return oops(err, errcap, active_awg
                ? "Туннель AmneziaWG не поднят"
                : "Активный сервер неизвестного типа");
        }

        {
            JSON_Value  *sv = json_value_init_object();
            JSON_Object *so = json_value_get_object(sv);
            json_object_set_string(so, "type", "selector");
            json_object_set_string(so, "tag", SELECTOR_TAG);
            json_object_set_value(so, "outbounds", sel_list);
            json_object_set_string(so, "default", active_tag);
            json_array_append_value(oarr, sv);
        }
    }
    if (in->pac_port && in->proxy_password) {
        JSON_Value *v = json_value_init_object();
        JSON_Object *o = json_value_get_object(v);
        json_object_set_string(o, "type", "socks");
        json_object_set_string(o, "tag", "utgard-pac");
        json_object_set_string(o, "server", "127.0.0.1");
        json_object_set_number(o, "server_port", in->pac_port);
        json_object_set_string(o, "version", "5");
        json_object_set_string(o, "username", "utgard");
        json_object_set_string(o, "password", in->proxy_password);
        json_array_append_value(oarr, v);
    }
    json_object_set_value(b->ro, "outbounds", outbounds);
    return 1;
}

/* The rule set is ours, because we compile the list. */
static void build_rule_set(build_state *b)
{
    JSON_Value  *setsv = json_value_init_array();
    JSON_Value  *sv    = json_value_init_object();
    JSON_Object *so    = json_value_get_object(sv);

    json_object_set_string(so, "type", "local");
    json_object_set_string(so, "tag", RULE_SET_TAG);
    json_object_set_string(so, "format", "binary");
    json_object_set_string(so, "path",
                           b->in->rule_set_path ? b->in->rule_set_path
                                                : "list/general.srs");
    json_array_append_value(json_value_get_array(setsv), sv);
    json_object_set_value(b->route, "rule_set", setsv);
}

/* Route rules: the frame, then the user's, then the overlays, then the rule
   that sends listed traffic into the selector, then PAC's. */
static void build_route_rules(build_state *b)
{
    const genconf_input *in = b->in;
    JSON_Value *route_rules = json_value_init_array();
    JSON_Array *rarr = json_value_get_array(route_rules);
    int         i;

    if (in->pac_port && in->pac_dns_vpn_port && in->pac_dns_sys_port) {
        JSON_Value *v = json_value_init_object();
        JSON_Value *a = json_value_init_array();
        JSON_Object *o = json_value_get_object(v);
        json_array_append_string(json_value_get_array(a), "utgard-pac-dns-vpn");
        json_array_append_string(json_value_get_array(a), "utgard-pac-dns-sys");
        json_object_set_value(o, "inbound", a);
        json_object_set_string(o, "action", "hijack-dns");
        json_array_append_value(rarr, v);
    }

    /* A PAC download or relay hop explicitly selects the active profile,
       even for a private destination. It must precede process bypasses. */
    if (in->vpn_proxy_port) {
        JSON_Value *v = route_rule(SELECTOR_TAG);
        JSON_Value *a = json_value_init_array();
        json_array_append_string(json_value_get_array(a), "utgard-vpn-proxy");
        json_object_set_value(json_value_get_object(v), "inbound", a);
        json_array_append_value(rarr, v);
    }
    if (in->client_exe && in->vpn_proxy_port) {
        JSON_Value *v = route_rule("direct");
        JSON_Value *a = json_value_init_array();
        json_array_append_string(json_value_get_array(a), in->client_exe);
        json_object_set_value(json_value_get_object(v), "process_path", a);
        json_array_append_value(rarr, v);
    }

    /* The AmneziaWG service talks to its server itself; whatever it sends
       leaves directly, before any other rule can catch it. */
    if (in->awg_interface && in->awg_exe) {
        JSON_Value *v  = route_rule("direct");
        JSON_Value *pp = json_value_init_array();
        json_array_append_string(json_value_get_array(pp), in->awg_exe);
        json_object_set_value(json_value_get_object(v), "process_path", pp);
        json_array_append_value(rarr, v);
    }
    append_frame_head(rarr, b->s);
    append_all(rarr, b->user_route_rules);

    for (i = 0; i < b->novl; i++) {
        JSON_Object *orr = json_object_get_object(json_value_get_object(b->ovl[i]), "route");
        if (orr) append_all(rarr, json_object_get_array(orr, "rules"));
    }

    {
        JSON_Value *v  = route_rule(SELECTOR_TAG);
        JSON_Value *rs = json_value_init_array();
        json_array_append_string(json_value_get_array(rs), RULE_SET_TAG);
        json_object_set_value(json_value_get_object(v), "rule_set", rs);
        json_array_append_value(rarr, v);
    }

    if (in->pac_port) {
        JSON_Value *v = route_rule("utgard-pac");
        JSON_Value *a = json_value_init_array();
        json_array_append_string(json_value_get_array(a), "tcp");
        json_array_append_string(json_value_get_array(a), "udp");
        json_object_set_value(json_value_get_object(v), "network", a);
        json_array_append_value(rarr, v);
        if (b->dns) json_object_set_boolean(b->dns, "reverse_mapping", 1);
    }
    json_object_set_value(b->route, "rules", route_rules);
}

/* DNS: the servers of the overlays; rules - PAC's inbounds, the bypass for
   server names, the overlays', the user's. Overlays go before the user's
   rules for the same reason their route rules go before the list rule: the
   base config's own DNS rule for the list would otherwise answer first and
   the overlay would never apply. Then PAC's DNS observer as dns.final. */
static int build_dns(build_state *b, char *err, size_t errcap)
{
    const genconf_input *in = b->in;
    const profile_store *s = b->s;
    int i;

    if (!b->dns) {
        for (i = 0; i < b->novl; i++)
            if (json_object_get_object(json_value_get_object(b->ovl[i]), "dns")) break;
        if (i < b->novl) {
            json_object_set_value(b->ro, "dns", json_value_init_object());
            b->dns = json_object_get_object(b->ro, "dns");
        }
    }
    if (b->dns && !merge_dns_servers(b->dns, b->ovl, b->novl, in, err, errcap)) return 0;
    if (b->dns) {
        JSON_Value *hosts = json_value_init_array();
        JSON_Value *dns_rules;
        JSON_Array *darr;

        for (i = 0; i < s->count; i++) {
            const char *host = s->items[i].link.server;
            if (host[0] && !is_ip(host))
                append_unique(json_value_get_array(hosts), host);
        }

        dns_rules = json_value_init_array();
        darr = json_value_get_array(dns_rules);

        if (in->pac_port && in->pac_dns_vpn_port && in->pac_dns_sys_port) {
            const char *tags[2] = { "utgard-pac-dns-vpn", "utgard-pac-dns-sys" };
            const char *servers[2] = { b->pac_dns_vpn_server, b->pac_dns_system_server };
            int k;
            for (k = 0; k < 2; k++) {
                JSON_Value *v = json_value_init_object();
                JSON_Value *a = json_value_init_array();
                JSON_Object *o = json_value_get_object(v);
                json_array_append_string(json_value_get_array(a), tags[k]);
                json_object_set_value(o, "inbound", a);
                json_object_set_string(o, "server", servers[k]);
                json_array_append_value(darr, v);
            }
        }

        if (json_array_get_count(json_value_get_array(hosts))) {
            JSON_Value  *v = json_value_init_object();
            JSON_Object *o = json_value_get_object(v);
            json_object_set_value(o, "domain", hosts);
            json_object_set_string(o, "server", "local");
            json_array_append_value(darr, v);
        } else {
            json_value_free(hosts);
        }

        for (i = 0; i < b->novl; i++) {
            JSON_Object *od = json_object_get_object(json_value_get_object(b->ovl[i]), "dns");
            if (od) append_all(darr, json_object_get_array(od, "rules"));
        }
        append_all(darr, b->user_dns_rules);
        json_object_set_value(b->dns, "rules", dns_rules);
    }

    if (in->pac_port && in->pac_dns_port && b->dns) {
        JSON_Value *v = json_value_init_object();
        JSON_Object *o = json_value_get_object(v);
        JSON_Array *servers = json_object_get_array(b->dns, "servers");
        json_object_set_string(o, "type", "udp");
        json_object_set_string(o, "tag", "utgard-pac-dns");
        json_object_set_string(o, "server", "127.0.0.1");
        json_object_set_number(o, "server_port", in->pac_dns_port);
        if (servers) json_array_append_value(servers, v); else json_value_free(v);
        json_object_set_string(b->dns, "final", "utgard-pac-dns");
    }
    return 1;
}

/* Settings from the client window win over config.json, in the generated
   config only. The tun inbound is found by type, not by position or tag. */
static void apply_settings(build_state *b)
{
    const genconf_input *in = b->in;
    JSON_Object *ro = b->ro;

    if (in->mtu > 0) {
        JSON_Array *inb = json_object_get_array(ro, "inbounds");
        size_t      k, cnt = inb ? json_array_get_count(inb) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(inb, k);
            const char  *type = o ? json_object_get_string(o, "type") : NULL;
            if (type && strcmp(type, "tun") == 0)
                json_object_set_number(o, "mtu", in->mtu);
        }
    }
    if (in->awg_interface && in->awg_server_ip) {
        JSON_Array *inb = json_object_get_array(ro, "inbounds");
        size_t      k, cnt = inb ? json_array_get_count(inb) : 0;
        char        prefix[64];
        snprintf(prefix, sizeof prefix, "%s/%s", in->awg_server_ip,
                 strchr(in->awg_server_ip, ':') ? "128" : "32");
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(inb, k);
            const char  *type = o ? json_object_get_string(o, "type") : NULL;
            JSON_Array  *ex;
            if (!type || strcmp(type, "tun") != 0) continue;
            ex = json_object_get_array(o, "route_exclude_address");
            if (!ex) {
                json_object_set_value(o, "route_exclude_address", json_value_init_array());
                ex = json_object_get_array(o, "route_exclude_address");
            }
            json_array_append_string(ex, prefix);
        }
    }
    /* A fixed name for sing-box's adapter (tunnames.h). */
    {
        JSON_Array *inb = json_object_get_array(ro, "inbounds");
        size_t      k, cnt = inb ? json_array_get_count(inb) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(inb, k);
            const char  *type = o ? json_object_get_string(o, "type") : NULL;
            if (type && strcmp(type, "tun") == 0)
                json_object_set_string(o, "interface_name", UTGARD_SB_TUN);
        }
    }
    if (in->stack && in->stack[0]) {
        JSON_Array *inb = json_object_get_array(ro, "inbounds");
        size_t      k, cnt = inb ? json_array_get_count(inb) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(inb, k);
            const char  *type = o ? json_object_get_string(o, "type") : NULL;
            if (type && strcmp(type, "tun") == 0)
                json_object_set_string(o, "stack", in->stack);
        }
    }
    /* Only the server tagged "doh" is redirected: host, type (HTTP/3 or
       HTTP/2) and path are what the setting chooses; port and the resolver
       used to find the host stay. */
    if (in->dns_host && in->dns_host[0] && b->dns) {
        JSON_Array *servers = json_object_get_array(b->dns, "servers");
        size_t      k, cnt = servers ? json_array_get_count(servers) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(servers, k);
            const char  *tag = o ? json_object_get_string(o, "tag") : NULL;
            if (tag && strcmp(tag, "doh") == 0) {
                json_object_set_string(o, "server", in->dns_host);
                if (in->dns_type && in->dns_type[0]) json_object_set_string(o, "type", in->dns_type);
                if (in->dns_path && in->dns_path[0]) json_object_set_string(o, "path", in->dns_path);
            }
        }
    }
    if (in->log_level && in->log_level[0]) {
        JSON_Object *log = json_object_get_object(ro, "log");
        if (!log) {
            json_object_set_value(ro, "log", json_value_init_object());
            log = json_object_get_object(ro, "log");
        }
        json_object_set_string(log, "level", in->log_level);
    }
}

/* sing-box runs elevated, and config.json is a file the user - or anything
   running as the user - can edit. Several of its fields make sing-box write
   to disk wherever they point: log.output, experimental.cache_file.path,
   experimental.clash_api.external_ui with a download URL (fetches an archive
   and unpacks it), tls.acme.data_directory on inbounds, the services
   section. All were checked against sing-box 1.14.1, which accepts them.
   So the generated config is rebuilt from an allow-list rather than cleaned
   by a deny-list: only what this client uses survives, and sections that
   future sing-box versions add are dropped without anyone having to know
   about them. Replaces b->root. */
static void keep_allowed(build_state *b)
{
    static const char *const keep[] = { "log", "dns", "inbounds", "outbounds", "endpoints", "route" };
    const genconf_input *in = b->in;
    JSON_Value  *clean  = json_value_init_object();
    JSON_Object *co     = json_value_get_object(clean);
    JSON_Object *oldlog = json_object_get_object(b->ro, "log");
    JSON_Array  *oldin  = json_object_get_array(b->ro, "inbounds");
    size_t       k;

    for (k = 0; k < sizeof keep / sizeof keep[0]; k++) {
        JSON_Value *v;
        if (strcmp(keep[k], "log") == 0 || strcmp(keep[k], "inbounds") == 0) continue;
        v = json_object_get_value(b->ro, keep[k]);
        if (v) json_object_set_value(co, keep[k], json_value_deep_copy(v));
    }

    /* log: level, timestamp and disabled from the user; the output path is
       always ours, relative to the working directory sing-box runs in. */
    {
        JSON_Value  *lv = json_value_init_object();
        JSON_Object *lo = json_value_get_object(lv);
        const char  *level = oldlog ? json_object_get_string(oldlog, "level") : NULL;

        json_object_set_string(lo, "level", level ? level : "warn");
        if (oldlog && json_object_has_value_of_type(oldlog, "timestamp", JSONBoolean))
            json_object_set_boolean(lo, "timestamp", json_object_get_boolean(oldlog, "timestamp"));
        if (oldlog && json_object_has_value_of_type(oldlog, "disabled", JSONBoolean))
            json_object_set_boolean(lo, "disabled", json_object_get_boolean(oldlog, "disabled"));
        json_object_set_string(lo, "output", "logs/sing-box.log");
        json_object_set_value(co, "log", lv);
    }

    /* inbounds: the tunnel only. Any other inbound either opens a proxy to
       the network or can carry TLS with an ACME directory to write into. */
    {
        JSON_Value *iv = json_value_init_array();
        size_t      cnt = oldin ? json_array_get_count(oldin) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(oldin, k);
            const char  *type = o ? json_object_get_string(o, "type") : NULL;
            if (type && strcmp(type, "tun") == 0)
                json_array_append_value(json_value_get_array(iv),
                                        json_value_deep_copy(json_array_get_value(oldin, k)));
        }
        /* Only this app-generated, loopback and authenticated listener
           is allowed in addition to the user's TUN. */
        if (in->vpn_proxy_port && in->proxy_password) {
            JSON_Value *v = json_value_init_object(), *users = json_value_init_array();
            JSON_Value *user = json_value_init_object();
            JSON_Object *o = json_value_get_object(v);
            json_object_set_string(o, "type", "mixed");
            json_object_set_string(o, "tag", "utgard-vpn-proxy");
            json_object_set_string(o, "listen", "127.0.0.1");
            json_object_set_number(o, "listen_port", in->vpn_proxy_port);
            json_object_set_string(json_value_get_object(user), "username", "utgard");
            json_object_set_string(json_value_get_object(user), "password", in->proxy_password);
            json_array_append_value(json_value_get_array(users), user);
            json_object_set_value(o, "users", users);
            json_array_append_value(json_value_get_array(iv), v);
        }
        if (in->pac_port && in->pac_dns_vpn_port && in->pac_dns_sys_port) {
            const char *tags[2] = { "utgard-pac-dns-vpn", "utgard-pac-dns-sys" };
            int ports[2] = { in->pac_dns_vpn_port, in->pac_dns_sys_port };
            int j;
            for (j = 0; j < 2; j++) {
                JSON_Value *v = json_value_init_object();
                JSON_Object *o = json_value_get_object(v);
                json_object_set_string(o, "type", "direct");
                json_object_set_string(o, "tag", tags[j]);
                json_object_set_string(o, "listen", "127.0.0.1");
                json_object_set_number(o, "listen_port", ports[j]);
                {
                    JSON_Value *networks = json_value_init_array();
                    json_array_append_string(json_value_get_array(networks), "tcp");
                    json_array_append_string(json_value_get_array(networks), "udp");
                    json_object_set_value(o, "network", networks);
                }
                json_array_append_value(json_value_get_array(iv), v);
            }
        }
        json_object_set_value(co, "inbounds", iv);
    }

    json_value_free(b->root);
    b->root = clean;
    b->ro   = co;
}

/* ---- the build ------------------------------------------------------ */

int genconf_build(const genconf_input *in, char **out_text, char *err, size_t errcap)
{
    build_state b;
    int         ok = 0;

    if (out_text) *out_text = NULL;
    if (!in || !in->base.text || !out_text || !in->store ||
        (in->overlay_count > 0 && !in->overlays))
        return oops(err, errcap, "Генератору не переданы обязательные данные");
    if (in->store->count <= 0)
        return oops(err, errcap, "Нет ни одного сервера");
    if (in->store->active < 0 || in->store->active >= in->store->count)
        return oops(err, errcap, "Не выбран активный сервер");

    memset(&b, 0, sizeof b);
    b.in = in;
    b.s  = in->store;
    if (parse_inputs(&b, err, errcap) &&
        keep_user_rules(&b, err, errcap) &&
        build_outbounds(&b, err, errcap)) {
        build_rule_set(&b);
        build_route_rules(&b);
        if (build_dns(&b, err, errcap)) {
            apply_settings(&b);
            keep_allowed(&b);
            *out_text = json_serialize_to_string(b.root);
            ok = *out_text ? 1 : oops(err, errcap, "Не хватило памяти для конфигурации");
        }
    }
    json_value_free(b.user_route_keep);
    json_value_free(b.user_dns_keep);
    if (b.ovl) free_overlays(b.ovl, b.novl);
    json_value_free(b.root);
    return ok;
}
