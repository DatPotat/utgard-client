#include "genconf.h"

#include "parson.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Paths here are UTF-8. On Windows fopen reads them in the active code page,
   so a folder named in Cyrillic - "D:\Новая папка" - turns into a different
   name and the file is reported missing. Everything goes through this. */
static FILE *open_utf8(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wpath[1024], wmode[8];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) == 0) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8) == 0) return NULL;
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

/* parson's own file helpers use fopen, so reading and writing is done here. */
static JSON_Value *parse_utf8_file(const char *path)
{
    FILE       *f = open_utf8(path, "rb");
    char       *buf;
    long        size;
    size_t      got;
    JSON_Value *v;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0 || size > 16 * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f);

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    v = json_parse_string(buf);
    free(buf);
    return v;
}

static int write_utf8_file(const char *path, const char *text)
{
    FILE  *f = open_utf8(path, "wb");
    size_t len = strlen(text);
    size_t put;

    if (!f) return 0;
    put = fwrite(text, 1, len, f);
    return fclose(f) == 0 && put == len;
}

#define SELECTOR_TAG  "utgard"
#define RULE_SET_TAG  "general"

static int oops(char *err, size_t cap, const char *msg)
{
    if (err && cap) {
        size_t n = strlen(msg);
        if (n >= cap) n = cap - 1;
        memcpy(err, msg, n);
        err[n] = '\0';
    }
    return 0;
}

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

/* sing-box accepts non-ASCII tags - verified against 1.13.14 - so the profile
   name is kept as the user wrote it. Only what would break the config or make
   log lines unreadable is removed: control characters, quotes, backslashes,
   and runs of whitespace, which become a single dash. */
static void sanitize(const char *src, char *dst, size_t cap)
{
    size_t n = 0;

    for (; *src && n + 1 < cap; src++) {
        unsigned char c = (unsigned char)*src;

        if (c < 0x20 || c == 0x7F || c == '"' || c == '\\') continue;
        if (c == ' ' || c == '\t') {
            if (n && dst[n - 1] != '-') dst[n++] = '-';
            continue;
        }
        dst[n++] = (char)c;
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

void genconf_tag(const profile_store *s, int index, char *out, size_t cap)
{
    char base[96], other[96];
    int  i, clash = 0;

    if (!s || index < 0 || index >= s->count || cap < 8) {
        if (cap) out[0] = '\0';
        return;
    }

    tag_base(s, index, base, sizeof base);

    /* Compare like with like: an earlier version compared a raw name against
       an already-substituted fallback and so never detected a clash. */
    for (i = 0; i < index; i++) {
        tag_base(s, i, other, sizeof other);
        if (strcmp(other, base) == 0) clash = 1;
    }

    if (clash) snprintf(out, cap, "%s-%d", base, index + 1);
    else       snprintf(out, cap, "%s", base);
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
                    snprintf(err, errcap, "В %s у DNS-сервера нет тега", in->overlays[i]);
                return 0;
            }
            for (j = 0; j < have; j++) {
                const char *t = json_object_get_string(json_array_get_object(servers, j), "tag");
                if (t && strcmp(t, tag) == 0) {
                    if (err && errcap)
                        snprintf(err, errcap, "В %s DNS-сервер с тегом «%s» уже объявлен",
                                 in->overlays[i], tag);
                    return 0;
                }
            }
            json_array_append_value(servers,
                json_value_deep_copy(json_array_get_value(add, k)));
        }
    }
    return 1;
}

/* ---- the build ------------------------------------------------------ */

int genconf_build(const genconf_input *in, char *err, size_t errcap)
{
    JSON_Value  *root = NULL;
    JSON_Object *ro;
    JSON_Value  *outbounds, *route_rules, *dns_rules;
    JSON_Array  *oarr, *rarr, *darr;
    JSON_Object *route, *dns;
    JSON_Array  *user_route_rules = NULL, *user_dns_rules = NULL;
    JSON_Value  *user_route_keep = NULL, *user_dns_keep = NULL;
    const profile_store *s;
    JSON_Value **ovl = NULL;
    int          novl = 0;
    int          i, active_ok = 0;
    char         active_tag[80] = { 0 };

    if (!in || !in->base_path || !in->out_path || !in->store)
        return oops(err, errcap, "Генератору не переданы обязательные пути");
    s = in->store;
    if (s->count <= 0)
        return oops(err, errcap, "Нет ни одного профиля");
    if (s->active < 0 || s->active >= s->count)
        return oops(err, errcap, "Не выбран активный профиль");

    {
        FILE *probe = open_utf8(in->base_path, "rb");
        if (!probe) {
            if (err && errcap)
                snprintf(err, errcap, "Не найден файл %s", in->base_path);
            return 0;
        }
        fclose(probe);
    }

    root = parse_utf8_file(in->base_path);
    if (!root || json_value_get_type(root) != JSONObject) {
        if (root) json_value_free(root);
        return oops(err, errcap,
                    "config.json не читается: нужен корректный JSON, "
                    "без комментариев и висячих запятых");
    }
    ro = json_value_get_object(root);

    /* Overlays are read once, up front: both the route rules and the DNS
       part come from them. */
    if (in->overlay_count > 0) {
        ovl = (JSON_Value **)calloc((size_t)in->overlay_count, sizeof *ovl);
        if (!ovl) {
            json_value_free(root);
            return oops(err, errcap, "Не хватило памяти для оверлеев");
        }
    }
    for (novl = 0; novl < in->overlay_count; novl++) {
        ovl[novl] = parse_utf8_file(in->overlays[novl]);
        if (!ovl[novl] || json_value_get_type(ovl[novl]) != JSONObject) {
            free_overlays(ovl, novl + 1);
            json_value_free(root);
            if (err && errcap)
                snprintf(err, errcap, "Не удалось прочитать %s", in->overlays[novl]);
            return 0;
        }
    }

    /* Keep the user's own rules aside before the client's frame replaces the
       arrays they live in. */
    route = json_object_get_object(ro, "route");
    if (route) {
        JSON_Array *a = json_object_get_array(route, "rules");
        if (a) {
            user_route_keep = json_value_deep_copy(json_array_get_wrapping_value(a));
            user_route_rules = json_value_get_array(user_route_keep);
        }
    }
    dns = json_object_get_object(ro, "dns");
    if (dns) {
        JSON_Array *a = json_object_get_array(dns, "rules");
        if (a) {
            user_dns_keep = json_value_deep_copy(json_array_get_wrapping_value(a));
            user_dns_rules = json_value_get_array(user_dns_keep);
        }
    }

    if (!route) {
        json_object_set_value(ro, "route", json_value_init_object());
        route = json_object_get_object(ro, "route");
    }

    /* ---- outbounds ---- */
    outbounds = json_value_init_array();
    oarr = json_value_get_array(outbounds);
    {
        JSON_Value  *dv = json_value_init_object();
        json_object_set_string(json_value_get_object(dv), "type", "direct");
        json_object_set_string(json_value_get_object(dv), "tag", "direct");
        json_array_append_value(oarr, dv);
    }
    {
        JSON_Value *sel_list = json_value_init_array();

        for (i = 0; i < s->count; i++) {
            char        tag[80];
            JSON_Value *ov;

            genconf_tag(s, i, tag, sizeof tag);
            ov = make_outbound(&s->items[i].link, tag);
            if (!ov) continue;                     /* unknown protocol: skip */

            json_array_append_value(oarr, ov);
            json_array_append_string(json_value_get_array(sel_list), tag);
            if (i == s->active) {
                snprintf(active_tag, sizeof active_tag, "%s", tag);
                active_ok = 1;
            }
        }

        if (!active_ok) {
            json_value_free(sel_list);
            json_value_free(outbounds);
            json_value_free(user_route_keep);
            json_value_free(user_dns_keep);
            json_value_free(root);
            free_overlays(ovl, novl);
            return oops(err, errcap, "Активный профиль неизвестного типа");
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
    json_object_set_value(ro, "outbounds", outbounds);

    /* ---- rule set: ours, because we compile the list ---- */
    {
        JSON_Value  *setsv = json_value_init_array();
        JSON_Value  *sv    = json_value_init_object();
        JSON_Object *so    = json_value_get_object(sv);

        json_object_set_string(so, "type", "local");
        json_object_set_string(so, "tag", RULE_SET_TAG);
        json_object_set_string(so, "format", "binary");
        json_object_set_string(so, "path",
                               in->rule_set_path ? in->rule_set_path
                                                 : "list/general.srs");
        json_array_append_value(json_value_get_array(setsv), sv);
        json_object_set_value(route, "rule_set", setsv);
    }

    /* ---- route rules: frame, then the user's, then the overlays, then the
       rule that sends listed traffic into the selector ---- */
    route_rules = json_value_init_array();
    rarr = json_value_get_array(route_rules);

    append_frame_head(rarr, s);
    append_all(rarr, user_route_rules);

    for (i = 0; i < novl; i++) {
        JSON_Object *orr = json_object_get_object(json_value_get_object(ovl[i]), "route");
        if (orr) append_all(rarr, json_object_get_array(orr, "rules"));
    }

    {
        JSON_Value *v  = route_rule(SELECTOR_TAG);
        JSON_Value *rs = json_value_init_array();
        json_array_append_string(json_value_get_array(rs), RULE_SET_TAG);
        json_object_set_value(json_value_get_object(v), "rule_set", rs);
        json_array_append_value(rarr, v);
    }

    json_object_set_value(route, "rules", route_rules);

    /* ---- dns: the bypass for server names, then the overlays, then the
       user's. Overlays go before the user's rules for the same reason their
       route rules go before the list rule: the base config's own DNS rule
       for the list would otherwise answer first and the overlay would never
       apply. */
    if (!dns) {
        for (i = 0; i < novl; i++)
            if (json_object_get_object(json_value_get_object(ovl[i]), "dns")) break;
        if (i < novl) {
            json_object_set_value(ro, "dns", json_value_init_object());
            dns = json_object_get_object(ro, "dns");
        }
    }
    if (dns && !merge_dns_servers(dns, ovl, novl, in, err, errcap)) {
        json_value_free(user_route_keep);
        json_value_free(user_dns_keep);
        json_value_free(root);
        free_overlays(ovl, novl);
        return 0;
    }
    if (dns) {
        JSON_Value *hosts = json_value_init_array();

        for (i = 0; i < s->count; i++) {
            const char *host = s->items[i].link.server;
            if (host[0] && !is_ip(host))
                append_unique(json_value_get_array(hosts), host);
        }

        dns_rules = json_value_init_array();
        darr = json_value_get_array(dns_rules);

        if (json_array_get_count(json_value_get_array(hosts))) {
            JSON_Value  *v = json_value_init_object();
            JSON_Object *o = json_value_get_object(v);
            json_object_set_value(o, "domain", hosts);
            json_object_set_string(o, "server", "local");
            json_array_append_value(darr, v);
        } else {
            json_value_free(hosts);
        }

        for (i = 0; i < novl; i++) {
            JSON_Object *od = json_object_get_object(json_value_get_object(ovl[i]), "dns");
            if (od) append_all(darr, json_object_get_array(od, "rules"));
        }
        append_all(darr, user_dns_rules);
        json_object_set_value(dns, "rules", dns_rules);
    }

    json_value_free(user_route_keep);
    json_value_free(user_dns_keep);
    free_overlays(ovl, novl);

    /* Settings from the client window win over config.json, in the generated
       file only. The tun inbound is found by type, not by position or tag. */
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
    /* Only the server tagged "doh" is redirected: its host is what the
       setting chooses; path, port and the resolver used to find it stay. */
    if (in->dns_host && in->dns_host[0] && dns) {
        JSON_Array *servers = json_object_get_array(dns, "servers");
        size_t      k, cnt = servers ? json_array_get_count(servers) : 0;
        for (k = 0; k < cnt; k++) {
            JSON_Object *o = json_array_get_object(servers, k);
            const char  *tag = o ? json_object_get_string(o, "tag") : NULL;
            if (tag && strcmp(tag, "doh") == 0)
                json_object_set_string(o, "server", in->dns_host);
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

    /* ---- sanitising -------------------------------------------------------
       sing-box runs elevated, and config.json is a file the user - or anything
       running as the user - can edit. Several of its fields make sing-box write
       to disk wherever they point: log.output, experimental.cache_file.path,
       experimental.clash_api.external_ui with a download URL (fetches an archive
       and unpacks it), tls.acme.data_directory on inbounds, the services
       section. All were checked against sing-box 1.14.1, which accepts them.
       So the generated file is rebuilt from an allow-list rather than cleaned
       by a deny-list: only what this client uses survives, and sections that
       future sing-box versions add are dropped without anyone having to know
       about them. */
    {
        static const char *const keep[] = { "log", "dns", "inbounds", "outbounds", "route" };
        JSON_Value  *clean  = json_value_init_object();
        JSON_Object *co     = json_value_get_object(clean);
        JSON_Object *oldlog = json_object_get_object(ro, "log");
        JSON_Array  *oldin  = json_object_get_array(ro, "inbounds");
        size_t       k;

        for (k = 0; k < sizeof keep / sizeof keep[0]; k++) {
            JSON_Value *v;
            if (strcmp(keep[k], "log") == 0 || strcmp(keep[k], "inbounds") == 0) continue;
            v = json_object_get_value(ro, keep[k]);
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
            json_object_set_value(co, "inbounds", iv);
        }

        json_value_free(root);
        root = clean;
        ro   = co;
    }

    {
        char *text = json_serialize_to_string_pretty(root);
        int   ok   = text ? write_utf8_file(in->out_path, text) : 0;
        if (text) json_free_serialized_string(text);
        if (!ok) {
            json_value_free(root);
            return oops(err, errcap, "Не удалось записать config.generated.json");
        }
    }

    json_value_free(root);
    return 1;
}
