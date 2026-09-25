#include "link.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "parson.h"
#include "puff.h"

/* ---- small bounded helpers ------------------------------------------ */

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

/* Copy len bytes and terminate. Fails rather than truncating: a silently
   cut uuid or password would produce a config that looks fine and does not
   work. */
static int put(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) return 0;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent-decoding. A stray '%' is kept literally, the way browsers do:
   panels emit remarks with bare percent signs.
   On overflow the destination is emptied rather than left half-written: a
   caller that ignores the return value must still find a valid string. */
static int pct_decode(const char *src, size_t len, char *dst, size_t cap)
{
    size_t i, n = 0;

    if (!cap) return 0;
    dst[0] = '\0';

    for (i = 0; i < len; i++) {
        int hi, lo;
        if (src[i] == '%' && i + 2 < len &&
            (hi = hexval(src[i + 1])) >= 0 && (lo = hexval(src[i + 2])) >= 0) {
            if (n + 1 >= cap) { dst[0] = '\0'; return 0; }
            dst[n++] = (char)((hi << 4) | lo);
            i += 2;
        } else {
            if (n + 1 >= cap) { dst[0] = '\0'; return 0; }
            dst[n++] = src[i];
        }
    }
    dst[n] = '\0';
    return 1;
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;    /* '-' and '_': URL-safe alphabet */
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* Tolerant base64: whitespace ignored, padding optional, both alphabets.
   Returns bytes written, or -1 when the input is not base64 at all. */
static long b64_decode(const char *src, size_t len, char *dst, size_t cap)
{
    unsigned int acc = 0;
    int          bits = 0;
    size_t       i, n = 0;

    for (i = 0; i < len; i++) {
        int v;
        char c = src[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == '=') continue;
        v = b64val(c);
        if (v < 0) return -1;
        acc = (acc << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n + 1 >= cap) return -1;
            dst[n++] = (char)((acc >> bits) & 0xFF);
        }
    }
    if (n >= cap) return -1;
    dst[n] = '\0';
    return (long)n;
}

/* Value of one query parameter. Unknown keys are ignored by construction,
   which matters: 3x-ui adds non-standard blobs such as fm={...} and
   extra={...} that other clients choke on. */
static int query_get(const char *query, size_t qlen, const char *key,
                     char *dst, size_t cap)
{
    size_t klen = strlen(key);
    const char *p = query;
    const char *end = query + qlen;

    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *stop = amp ? amp : end;
        const char *eq = memchr(p, '=', (size_t)(stop - p));

        if (eq && (size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            /* An over-long value yields an empty string, never a raw buffer:
               every caller here treats the result as a C string. */
            if (!pct_decode(eq + 1, (size_t)(stop - eq - 1), dst, cap)) {
                if (cap) dst[0] = '\0';
                return 0;
            }
            return 1;
        }

        if (!amp) break;
        p = amp + 1;
    }
    dst[0] = '\0';
    return 0;
}

static int str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int parse_port(const char *s, size_t len, int *out)
{
    int v = 0;
    size_t i;

    if (len == 0 || len > 5) return 0;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    if (v < 1 || v > 65535) return 0;
    *out = v;
    return 1;
}

/* host[:port], with [v6]:port tolerated. */
static int split_hostport(const char *s, size_t len, char *host, size_t hcap, int *port)
{
    const char *colon;

    if (len && s[0] == '[') {
        const char *close = memchr(s, ']', len);
        if (!close) return 0;
        if (!put(host, hcap, s + 1, (size_t)(close - s - 1))) return 0;
        colon = (close + 1 < s + len && close[1] == ':') ? close + 1 : NULL;
    } else {
        colon = memchr(s, ':', len);
        if (!put(host, hcap, s, colon ? (size_t)(colon - s) : len)) return 0;
    }

    if (colon) {
        if (!parse_port(colon + 1, (size_t)(s + len - colon - 1), port)) return 0;
    }
    return host[0] != '\0';
}

/* ---- format checks ---------------------------------------------------
   Each rule below mirrors what sing-box 1.14.1 itself accepts, checked
   against the binary rather than assumed: rejecting what sing-box would take
   breaks working profiles, accepting what it refuses only moves the error to
   the moment the VPN is switched on. The UUID is deliberately NOT checked
   for UUID form - sing-box takes any string there, as Xray does. */

static int valid_host(const char *h)
{
    size_t n = strlen(h), i;
    if (n == 0 || n > 253) return 0;
    for (i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':'))
            return 0;                 /* a name or an address, never a space */
    }
    return 1;
}

static int valid_id(const char *id)
{
    const char *p;
    if (!id[0]) return 0;
    for (p = id; *p; p++)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7F) return 0;
    return 1;
}

/* 32 bytes in unpadded base64url: exactly 43 characters. sing-box refuses
   padding and the '+' '/' alphabet here. */
static int valid_reality_key(const char *k)
{
    size_t i, n = strlen(k);
    if (n != 43) return 0;
    for (i = 0; i < n; i++) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

/* Hex, even length, at most 16 characters. Past 16 sing-box 1.14.1 does not
   just refuse - it panics - so this check also keeps it from crashing. */
static int valid_short_id(const char *s)
{
    size_t i, n = strlen(s);
    if (n > 16 || (n % 2)) return 0;
    for (i = 0; i < n; i++)
        if (hexval(s[i]) < 0) return 0;
    return 1;
}

/* The 2022 ciphers take a base64 key of an exact size; the older AEAD ones
   take any password. */
static int valid_ss_password(const char *method, const char *pw)
{
    char   key[128];
    long   n;
    size_t want;

    if (strncmp(method, "2022-", 5) != 0) return 1;
    want = strstr(method, "aes-128") ? 16 : 32;
    n = b64_decode(pw, strlen(pw), key, sizeof key);
    return n == (long)want;
}

static void lower(char *s);

/* ---- V2Ray transport (vless, trojan) -------------------------------------
   The Xray share-link parameters: type, path, host, serviceName, alpn. Only
   the transports sing-box implements are accepted - ws, grpc, http (h2),
   httpupgrade, quic - checked against sing-box 1.14.1, which refuses xhttp,
   splithttp and kcp as unknown transport types. Such a link is refused with a
   reason: generated as plain TCP it would not error, it would just never
   connect. */
static int parse_transport(const char *query, size_t qlen, link_profile *out,
                           char *err, size_t errcap)
{
    char type[32], scratch[256];

    query_get(query, qlen, "alpn", out->alpn, sizeof out->alpn);
    query_get(query, qlen, "type", type, sizeof type);
    lower(type);

    /* tcp, and raw - Xray's newer name for the same transport. */
    if (!type[0] || str_eq(type, "tcp") || str_eq(type, "raw")) {
        query_get(query, qlen, "headerType", scratch, sizeof scratch);
        lower(scratch);
        if (scratch[0] && !str_eq(scratch, "none"))
            return oops(err, errcap, "маскировка TCP под HTTP не поддерживается ядром sing-box");
        return 1;
    }

    if (str_eq(type, "h2")) put(type, sizeof type, "http", 4);

    if (str_eq(type, "xhttp") || str_eq(type, "splithttp"))
        return oops(err, errcap, "транспорт XHTTP не поддерживается ядром sing-box — "
                                 "нужна ссылка с tcp, ws, grpc, http или httpupgrade");
    if (!(str_eq(type, "ws") || str_eq(type, "grpc") || str_eq(type, "http") ||
          str_eq(type, "httpupgrade") || str_eq(type, "quic")))
        return oops(err, errcap, "этот транспорт не поддерживается ядром sing-box — "
                                 "работают tcp, ws, grpc, http, httpupgrade и quic");

    if (str_eq(type, "quic")) {
        query_get(query, qlen, "quicSecurity", scratch, sizeof scratch);
        lower(scratch);
        if (scratch[0] && !str_eq(scratch, "none"))
            return oops(err, errcap, "QUIC с дополнительным шифрованием не поддерживается ядром sing-box");
    }

    put(out->transport, sizeof out->transport, type, strlen(type));
    query_get(query, qlen, "path", out->path, sizeof out->path);
    query_get(query, qlen, "host", out->host, sizeof out->host);
    query_get(query, qlen, "serviceName", out->service_name, sizeof out->service_name);

    /* Xray puts WebSocket early data in the path as ?ed=N; sing-box wants
       fields, with the Sec-WebSocket-Protocol header for Xray compatibility
       (sing-box documentation, V2Ray Transport). */
    if (str_eq(type, "ws")) {
        char *ed = strstr(out->path, "?ed=");
        long  n  = 0;
        if (ed) { n = strtol(ed + 4, NULL, 10); *ed = '\0'; }
        else if (query_get(query, qlen, "ed", scratch, sizeof scratch)) n = strtol(scratch, NULL, 10);
        out->early_data = (n > 0 && n <= 65536) ? (int)n : 0;
    }
    return 1;
}

/* ---- vmess ---------------------------------------------------------- */

/* Append key=value to a query string, percent-encoding the value, so the
   VMess JSON can go through the same transport checks as a VLESS link. */
static int q_add(char *q, size_t cap, const char *key, const char *val)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(q);

    if (!val || !val[0]) return 1;
    if (n + strlen(key) + 2 >= cap) return 0;
    if (n) q[n++] = '&';
    memcpy(q + n, key, strlen(key)); n += strlen(key);
    q[n++] = '=';
    for (; *val; val++) {
        unsigned char c = (unsigned char)*val;
        int plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                    c == '~' || c == '/' || c == ',';
        if (n + 4 >= cap) return 0;
        if (plain) q[n++] = (char)c;
        else { q[n++] = '%'; q[n++] = hex[c >> 4]; q[n++] = hex[c & 15]; }
    }
    q[n] = '\0';
    return 1;
}

/* Generators write port and aid either as numbers or as strings. */
static void json_field(const JSON_Object *o, const char *key, char *dst, size_t cap)
{
    const JSON_Value *v = json_object_get_value(o, key);
    dst[0] = '\0';
    if (!v || cap == 0) return;
    if (json_value_get_type(v) == JSONString) {
        const char *sv = json_value_get_string(v);
        size_t      n  = strlen(sv);
        if (n < cap) memcpy(dst, sv, n + 1);
    } else if (json_value_get_type(v) == JSONNumber) {
        double d = json_value_get_number(v);
        if (d >= 0 && d <= 1e9) snprintf(dst, cap, "%.0f", d);
    }
}

/* vmess://base64(JSON), the v2rayN share format ("v": "2"): ps, add, port,
   id, aid, scy, net, type, host, path, tls, sni, alpn, fp, insecure. For
   grpc, path carries the service name. */
static int parse_vmess(const char *b64, link_profile *out, char *err, size_t errcap)
{
    static const char *SECURITY[] = { "auto", "none", "zero", "aes-128-cfb",
                                      "aes-128-gcm", "chacha20-poly1305" };
    char        json[4096], f[512], net[32], q[2048];
    long        n;
    JSON_Value *root;
    JSON_Object *o;
    size_t      i;
    int         ok = 0;

    n = b64_decode(b64, strlen(b64), json, sizeof json - 1);
    if (n <= 0) return oops(err, errcap, "ссылку vmess не удалось раскодировать");
    json[n] = '\0';

    root = json_parse_string(json);
    o = json_value_get_object(root);
    if (!o) { json_value_free(root); return oops(err, errcap, "ссылка vmess не содержит JSON"); }

    json_field(o, "ps", f, sizeof f);
    put(out->name, sizeof out->name, f, strlen(f));

    json_field(o, "add", f, sizeof f);
    if (!f[0] || !put(out->server, sizeof out->server, f, strlen(f)) || !valid_host(out->server)) {
        oops(err, errcap, "в ссылке vmess неверный адрес сервера");
        goto done;
    }
    json_field(o, "port", f, sizeof f);
    if (!parse_port(f, strlen(f), &out->port)) {
        oops(err, errcap, "в ссылке vmess неверный порт");
        goto done;
    }
    json_field(o, "id", f, sizeof f);
    if (!put(out->uuid, sizeof out->uuid, f, strlen(f)) || !valid_id(out->uuid)) {
        oops(err, errcap, "в ссылке vmess нет идентификатора (id) или он повреждён");
        goto done;
    }
    json_field(o, "aid", f, sizeof f);
    out->alter_id = f[0] ? atoi(f) : 0;
    if (out->alter_id < 0 || out->alter_id > 65535) out->alter_id = 0;

    json_field(o, "scy", f, sizeof f);
    lower(f);
    if (!f[0]) strcpy(f, "auto");
    for (i = 0; i < sizeof SECURITY / sizeof SECURITY[0]; i++)
        if (str_eq(f, SECURITY[i])) break;
    if (i == sizeof SECURITY / sizeof SECURITY[0]) {
        oops(err, errcap, "шифрование vmess (scy) не поддерживается ядром sing-box");
        goto done;
    }
    put(out->method, sizeof out->method, f, strlen(f));

    /* The transport goes through the VLESS checks as a synthetic query. */
    q[0] = '\0';
    json_field(o, "net", net, sizeof net);
    lower(net);
    if (!q_add(q, sizeof q, "type", net)) goto toolong;
    json_field(o, "type", f, sizeof f);
    if (!str_eq(net, "grpc") && !q_add(q, sizeof q, "headerType", f)) goto toolong;
    json_field(o, "host", f, sizeof f);
    if (!q_add(q, sizeof q, "host", f)) goto toolong;
    json_field(o, "path", f, sizeof f);
    if (!q_add(q, sizeof q, str_eq(net, "grpc") ? "serviceName" : "path", f)) goto toolong;
    json_field(o, "alpn", f, sizeof f);
    if (!q_add(q, sizeof q, "alpn", f)) goto toolong;
    if (!parse_transport(q, strlen(q), out, err, errcap)) goto done;

    json_field(o, "tls", f, sizeof f);
    lower(f);
    if (str_eq(f, "reality")) {
        oops(err, errcap, "vmess с reality не поддерживается — нужна vless-ссылка");
        goto done;
    }
    out->tls = str_eq(f, "tls");
    if (out->tls) {
        json_field(o, "sni", f, sizeof f);
        if (!f[0]) json_field(o, "host", f, sizeof f);
        if (!f[0]) put(f, sizeof f, out->server, strlen(out->server));
        put(out->sni, sizeof out->sni, f, strlen(f));
        json_field(o, "fp", f, sizeof f);
        if (f[0]) put(out->fingerprint, sizeof out->fingerprint, f, strlen(f));
        json_field(o, "insecure", f, sizeof f);
        out->insecure = str_eq(f, "1") || str_eq(f, "true");
    } else {
        out->alpn[0] = '\0';
    }
    ok = 1;
    goto done;

toolong:
    oops(err, errcap, "в ссылке vmess слишком длинные параметры транспорта");
done:
    json_value_free(root);
    return ok;
}

/* ---- wireguard ------------------------------------------------------ */

/* Standard base64 of exactly 32 bytes: 44 characters ending in '='. */
static int valid_wg_key(const char *k)
{
    char buf[48];
    size_t i, n = strlen(k);
    if (n != 44 || k[43] != '=') return 0;
    for (i = 0; i < 43; i++) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '/'))
            return 0;
    }
    return b64_decode(k, n, buf, sizeof buf) == 32;
}

/* "10.0.0.2/32, fd00::2" -> "10.0.0.2/32,fd00::2/128". A bare address gets
   its single-host prefix; anything but digits, hex, dots, colons and one
   slash is refused. */
static int wg_addresses(const char *in, char *out, size_t cap)
{
    size_t used = 0;
    out[0] = '\0';

    while (*in) {
        char   tok[64];
        size_t n = 0, i;
        int    v6, slash = 0;
        long   bits = -1;

        while (*in == ',' || *in == ' ' || *in == '\t') in++;
        if (!*in) break;
        while (in[n] && in[n] != ',' && in[n] != ' ' && in[n] != '\t') n++;
        if (n >= sizeof tok) return 0;
        memcpy(tok, in, n);
        tok[n] = '\0';
        in += n;

        for (i = 0; tok[i]; i++) {
            char c = tok[i];
            if (c == '/') { if (slash++) return 0; bits = strtol(tok + i + 1, NULL, 10); continue; }
            if (slash) { if (c < '0' || c > '9') return 0; continue; }
            if (!(hexval(c) >= 0 || c == '.' || c == ':')) return 0;
        }
        v6 = strchr(tok, ':') != NULL;
        if (!v6 && !strchr(tok, '.')) return 0;
        if (slash && (bits < 0 || bits > (v6 ? 128 : 32) || tok[strlen(tok) - 1] == '/')) return 0;

        if (used + n + 6 >= cap) return 0;
        if (used) out[used++] = ',';
        memcpy(out + used, tok, n);
        used += n;
        if (!slash) {
            const char *pfx = v6 ? "/128" : "/32";
            memcpy(out + used, pfx, strlen(pfx));
            used += strlen(pfx);
        }
        out[used] = '\0';
    }
    return used > 0;
}

/* Exactly three bytes, as Cloudflare's client id: "1,2,3". */
static int wg_reserved(const char *in, char *out, size_t cap)
{
    long v[3];
    int  k;
    const char *p = in;
    char *end;

    for (k = 0; k < 3; k++) {
        while (*p == ' ') p++;
        v[k] = strtol(p, &end, 10);
        if (end == p || v[k] < 0 || v[k] > 255) return 0;
        p = end;
        while (*p == ' ') p++;
        if (k < 2) { if (*p != ',') return 0; p++; }
    }
    if (*p) return 0;
    return snprintf(out, cap, "%ld,%ld,%ld", v[0], v[1], v[2]) < (int)cap;
}

/* Shared by the link and the .conf: everything filled, now check it. */
static int wg_finish(link_profile *out, const char *address, const char *reserved,
                     char *err, size_t errcap)
{
    if (!valid_wg_key(out->wg_private_key))
        return oops(err, errcap, "закрытый ключ WireGuard повреждён — нужна строка base64 из 44 символов");
    if (!valid_wg_key(out->wg_peer_key))
        return oops(err, errcap, "открытый ключ сервера WireGuard отсутствует или повреждён");
    if (out->wg_psk[0] && !valid_wg_key(out->wg_psk))
        return oops(err, errcap, "общий ключ WireGuard (PresharedKey) повреждён");
    if (!address[0] || !wg_addresses(address, out->wg_address, sizeof out->wg_address))
        return oops(err, errcap, "не указан или неверен адрес интерфейса WireGuard (Address)");
    if (reserved[0] && !wg_reserved(reserved, out->wg_reserved, sizeof out->wg_reserved))
        return oops(err, errcap, "поле reserved WireGuard должно быть тремя числами 0-255 через запятую");
    if (out->mtu && (out->mtu < 1280 || out->mtu > 1500))
        return oops(err, errcap, "MTU WireGuard должен быть от 1280 до 1500");
    if (out->keepalive < 0 || out->keepalive > 65535) out->keepalive = 0;
    return 1;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static int key_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

/* ---- AmneziaWG ------------------------------------------------------ */

/* The [Interface] keys the AmneziaWG service for Windows accepts, and how its
   parser treats each (amneziawg-windows conf/parser.go): U16 is parsed as a
   16-bit number, HDR is a number or a range for the device, KEY is a base64
   key, ITAG an I-packet description, STR any other value the device checks. */
enum { AWG_U16, AWG_HDR, AWG_KEY, AWG_ITAG, AWG_STR, AWG_KEEPALIVE };
static const struct { const char *key; int kind; } AWG_KEYS[] = {
    { "jc", AWG_U16 }, { "jmin", AWG_U16 }, { "jmax", AWG_U16 },
    { "s1", AWG_U16 }, { "s2", AWG_U16 }, { "s3", AWG_U16 }, { "s4", AWG_U16 },
    { "h1", AWG_HDR }, { "h2", AWG_HDR }, { "h3", AWG_HDR }, { "h4", AWG_HDR },
    { "i1", AWG_ITAG }, { "i2", AWG_ITAG }, { "i3", AWG_ITAG }, { "i4", AWG_ITAG },
    { "i5", AWG_ITAG },
    { "headerprotectionkey", AWG_KEY },
    { "contentpaddingaddition", AWG_STR }, { "rekeyaftertime", AWG_STR },
    { "rekeytimeout", AWG_STR }, { "rejectaftertime", AWG_STR },
    { "keepalivetimeout", AWG_STR }, { "maxhandshakeattempts", AWG_STR },
    { "randomtrailers", AWG_STR }, { "disablecookies", AWG_STR },
    /* [Peer] PersistentKeepalive as a range "a-b" (AmneziaWG 3): the
       service passes it to the device as text (parsePersistentKeepalive).
       A single number stays in the profile's keepalive field. */
    { "persistentkeepalive", AWG_KEEPALIVE }
};

static int awg_kind(const char *key)
{
    size_t i;
    for (i = 0; i < sizeof AWG_KEYS / sizeof AWG_KEYS[0]; i++)
        if (key_is(key, AWG_KEYS[i].key)) return AWG_KEYS[i].kind;
    return -1;
}

/* Decimal, no sign, no leading junk; *v gets the value. */
static int awg_uint(const char *s, size_t n, unsigned long max, unsigned long *v)
{
    size_t i;
    *v = 0;
    if (n == 0 || n > 10) return 0;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        *v = *v * 10 + (unsigned long)(s[i] - '0');
    }
    return *v <= max;
}

/* Every value goes back into a config file line, so nothing in it may end
   the line or smuggle in another key: printable ASCII only. */
static int awg_printable(const char *s, size_t max)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > max) return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return 0;
    return 1;
}

static int awg_value_ok(int kind, const char *v)
{
    unsigned long a, b;
    const char   *dash;

    switch (kind) {
    case AWG_U16:  return awg_uint(v, strlen(v), 65535UL, &a);
    case AWG_KEY:  return valid_wg_key(v);
    case AWG_ITAG: return awg_printable(v, LINK_AWG_ITAG_MAX);
    case AWG_STR:  return awg_printable(v, 64);
    case AWG_HDR:
        dash = strchr(v, '-');
        if (!dash) return awg_uint(v, strlen(v), 4294967295UL, &a);
        return awg_uint(v, (size_t)(dash - v), 4294967295UL, &a) &&
               awg_uint(dash + 1, strlen(dash + 1), 4294967295UL, &b) && a <= b;
    case AWG_KEEPALIVE:
        dash = strchr(v, '-');
        return dash && awg_uint(v, (size_t)(dash - v), 65535UL, &a) &&
               awg_uint(dash + 1, strlen(dash + 1), 65535UL, &b) && a <= b;
    }
    return 0;
}

/* Where key's value starts in a profile's awg text, and its length; NULL if
   the key is not there. Points into awg, copies nothing. */
static const char *awg_find(const char *awg, const char *key, size_t *vlen)
{
    size_t      klen = strlen(key);
    const char *p = awg;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t      n  = nl ? (size_t)(nl - p) : strlen(p);
        if (n > klen + 3 && strncmp(p, key, klen) == 0 && strncmp(p + klen, " = ", 3) == 0) {
            *vlen = n - klen - 3;
            return p + klen + 3;
        }
        p += n + (nl ? 1 : 0);
    }
    return NULL;
}

static int awg_cross_check(const char *awg, char *err, size_t errcap);

/* The whole awg block, re-checked line by line as it was on parsing: it is
   about to go into another program's config, and a profile file is only
   as trustworthy as the disk it sits on. */
int link_awg_valid(const char *awg)
{
    const char *p = awg;
    char        key[32], val[LINK_AWG_ITAG_MAX + 1];

    while (*p) {
        const char *nl = strchr(p, '\n'), *eq;
        size_t      n  = nl ? (size_t)(nl - p) : strlen(p), vlen;
        int         kind;

        if (!nl) return 0;                       /* every line ends in \n */
        eq = memchr(p, '=', n);
        if (!eq || eq - p < 2 || eq[-1] != ' ' || eq + 1 >= p + n || eq[1] != ' ') return 0;
        if (!put(key, sizeof key, p, (size_t)(eq - p - 1))) return 0;
        vlen = n - (size_t)(eq + 2 - p);
        if (!put(val, sizeof val, eq + 2, vlen)) return 0;
        kind = awg_kind(key);
        if (kind < 0 || !awg_value_ok(kind, val)) return 0;
        {   /* no key twice: the first occurrence must be this one */
            size_t      l;
            const char *first = awg_find(awg, key, &l);
            if (first != eq + 2) return 0;
        }
        p = nl + 1;
    }
    return awg_cross_check(awg, NULL, 0);
}

int link_is_awg(const link_profile *p)
{
    return p && p->proto == LINK_WG && p->awg[0] != '\0';
}

static int awg_add(link_profile *out, const char *key, const char *val,
                   char *err, size_t errcap)
{
    char   canon[32], msg[160];
    size_t i, have = strlen(out->awg), need, vlen;
    int    kind = awg_kind(key);

    for (i = 0; key[i] && i + 1 < sizeof canon; i++)
        canon[i] = (key[i] >= 'A' && key[i] <= 'Z') ? (char)(key[i] - 'A' + 'a') : key[i];
    canon[i] = '\0';

    if (awg_find(out->awg, canon, &vlen)) {
        snprintf(msg, sizeof msg, "параметр AmneziaWG %.32s указан дважды", key);
        return oops(err, errcap, msg);
    }
    if (!awg_value_ok(kind, val)) {
        snprintf(msg, sizeof msg, "неверное значение параметра AmneziaWG %.32s", key);
        return oops(err, errcap, msg);
    }
    need = strlen(canon) + 3 + strlen(val) + 1;
    if (have + need >= sizeof out->awg)
        return oops(err, errcap, "слишком много параметров AmneziaWG");
    memcpy(out->awg + have, canon, strlen(canon));
    have += strlen(canon);
    memcpy(out->awg + have, " = ", 3);
    have += 3;
    memcpy(out->awg + have, val, strlen(val));
    have += strlen(val);
    out->awg[have++] = '\n';
    out->awg[have] = '\0';
    return 1;
}

/* One header's range; unset means the device default, WireGuard's own
   message type (amneziawg-go NewDevice: H1..H4 = 1, 2, 3, 4). */
static int awg_hdr_range(const char *awg, const char *key, unsigned long def,
                         unsigned long *lo, unsigned long *hi)
{
    size_t      n;
    const char *v = awg_find(awg, key, &n), *d;

    if (!v) { *lo = *hi = def; return 1; }
    d = memchr(v, '-', n);
    if (!d) { if (!awg_uint(v, n, 4294967295UL, lo)) return 0; *hi = *lo; return 1; }
    return awg_uint(v, (size_t)(d - v), 4294967295UL, lo) &&
           awg_uint(d + 1, n - (size_t)(d - v) - 1, 4294967295UL, hi);
}

/* Checks across parameters. The two on H and S are the ones amneziawg-go
   itself enforces when it applies the settings (uapi.go mergeWithDevice):
   "headers must not overlap", and with header protection every S must be
   at least HeaderCipherNonceSize, 12. Refused here, they explain
   themselves; refused by the service, the tunnel just does not start. */
static int awg_cross_check(const char *awg, char *err, size_t errcap)
{
    static const char *const H[4] = { "h1", "h2", "h3", "h4" };
    static const char *const S[4] = { "s1", "s2", "s3", "s4" };
    const char   *a, *b;
    size_t        alen, blen;
    unsigned long lo[4], hi[4], v;
    int           i, j;

    a = awg_find(awg, "jmin", &alen);
    b = awg_find(awg, "jmax", &blen);
    if (a && b && awg_uint(a, alen, 65535UL, &lo[0]) && awg_uint(b, blen, 65535UL, &hi[0]) &&
        lo[0] > hi[0])
        return oops(err, errcap, "параметр AmneziaWG Jmin больше Jmax");

    for (i = 0; i < 4; i++)
        if (!awg_hdr_range(awg, H[i], (unsigned long)(i + 1), &lo[i], &hi[i]))
            return oops(err, errcap, "неверное значение параметра AmneziaWG H");
    for (i = 0; i < 4; i++)
        for (j = i + 1; j < 4; j++)
            if (lo[i] <= hi[j] && lo[j] <= hi[i])
                return oops(err, errcap, "диапазоны AmneziaWG H1–H4 пересекаются — "
                                         "сервер такой конфигурации не примет");

    if (awg_find(awg, "headerprotectionkey", &alen))
        for (i = 0; i < 4; i++) {
            a = awg_find(awg, S[i], &alen);
            if (!a || !awg_uint(a, alen, 65535UL, &v) || v < 12)
                return oops(err, errcap, "с HeaderProtectionKey каждый из S1–S4 должен быть "
                                         "не меньше 12");
        }
    return 1;
}

/* MTU: 1280, the IPv6 minimum, to 1500, Ethernet - the same bounds the
   wireguard:// parser already applies. */
static int mtu_parse(const char *val, int *mtu, char *err, size_t errcap)
{
    unsigned long v;
    if (!awg_uint(val, strlen(val), 1500UL, &v) || v < 1280)
        return oops(err, errcap, "MTU должен быть от 1280 до 1500");
    *mtu = (int)v;
    return 1;
}

/* PersistentKeepalive: seconds 0..65535, "off" as the service accepts it,
   or an AmneziaWG range, which makes the profile an AmneziaWG one. */
static int keepalive_parse(link_profile *out, const char *val, char *err, size_t errcap)
{
    unsigned long v;

    if (key_is(val, "off") || key_is(val, "(off)")) { out->keepalive = 0; return 1; }
    if (val[0] != '-' && strchr(val, '-')) return awg_add(out, "persistentkeepalive", val, err, errcap);
    if (!awg_uint(val, strlen(val), 65535UL, &v))
        return oops(err, errcap, "PersistentKeepalive должен быть от 0 до 65535, off "
                                 "или диапазоном AmneziaWG вида 20-30");
    out->keepalive = (int)v;
    return 1;
}

int link_parse_wgconf(const char *text, size_t len, link_profile *out,
                      char *err, size_t errcap)
{
    char        address[512] = "", endpoint[300] = "";
    const char *p = text, *end = text + len;
    int         section = 0, peers = 0;   /* 1 interface, 2 peer */

    if (!text || !out) return oops(err, errcap, "пустой файл");
    memset(out, 0, sizeof *out);
    out->proto = LINK_WG;
    /* Notepad used to save UTF-8 with a byte order mark; left in, it hides
       the first [Interface] and nothing of it is read. */
    if (len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
    if (memchr(p, '\0', (size_t)(end - p))) return oops(err, errcap, "в файле нулевой байт — это не конфигурация");

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t      n  = (size_t)((nl ? nl : end) - p);
        char        line[LINK_AWG_ITAG_MAX + 256], *eq, *val;

        if (n >= sizeof line) {
            /* A long comment is harmless; a long setting skipped silently
               would be a parameter the server needs and we dropped. */
            const char *q = p;
            while (q < p + n && (*q == ' ' || *q == '\t')) q++;
            if (q < p + n && *q == '#') goto next;
            return oops(err, errcap, "в файле слишком длинная строка");
        }
        {
            memcpy(line, p, n);
            line[n] = '\0';
            if ((eq = strchr(line, '#')) != NULL) *eq = '\0';
            trim(line);
            if (key_is(line, "[Interface]")) section = 1;
            else if (key_is(line, "[Peer]")) { section = 2; peers++; }
            else if (line[0] == '[') section = 0;
            else if ((eq = strchr(line, '=')) != NULL && section && (section == 1 || peers == 1)) {
                *eq = '\0';
                val = eq + 1;
                trim(line);
                trim(val);
                if (section == 1 && awg_kind(line) >= 0 && awg_kind(line) != AWG_KEEPALIVE) {
                    /* The service skips an empty I-packet; so do we. */
                    if (val[0] || awg_kind(line) != AWG_ITAG)
                        if (!awg_add(out, line, val, err, errcap)) return 0;
                } else if (section == 1 && key_is(line, "PrivateKey"))
                    put(out->wg_private_key, sizeof out->wg_private_key, val, strlen(val));
                else if (section == 1 && key_is(line, "Address")) {
                    size_t a = strlen(address);
                    if (a + strlen(val) + 2 < sizeof address) {
                        if (a) address[a++] = ',';
                        memcpy(address + a, val, strlen(val) + 1);
                    }
                } else if (section == 1 && key_is(line, "MTU")) {
                    if (!mtu_parse(val, &out->mtu, err, errcap)) return 0;
                }
                else if (section == 2 && key_is(line, "PublicKey"))
                    put(out->wg_peer_key, sizeof out->wg_peer_key, val, strlen(val));
                else if (section == 2 && key_is(line, "PresharedKey"))
                    put(out->wg_psk, sizeof out->wg_psk, val, strlen(val));
                else if (section == 2 && key_is(line, "Endpoint"))
                    put(endpoint, sizeof endpoint, val, strlen(val));
                else if (section == 2 && key_is(line, "PersistentKeepalive")) {
                    if (!keepalive_parse(out, val, err, errcap)) return 0;
                }
            }
        }
    next:
        if (!nl) break;
        p = nl + 1;
    }

    if (!peers) return oops(err, errcap, "в файле нет раздела [Peer]");
    if (!awg_cross_check(out->awg, err, errcap)) return 0;
    out->port = 51820;
    if (!endpoint[0] || !split_hostport(endpoint, strlen(endpoint), out->server,
                                        sizeof out->server, &out->port) ||
        !valid_host(out->server))
        return oops(err, errcap, "в разделе [Peer] нет адреса сервера (Endpoint) или он неверен");
    return wg_finish(out, address, "", err, errcap);
}

/* ---- Amnezia vpn:// ------------------------------------------------- */

/* The format, as the Amnezia client writes and reads it (exportController,
   importController): "vpn://" + base64url without padding of either the
   JSON itself or Qt's qCompress of it - four bytes of the uncompressed size,
   big-endian, then a zlib stream. The JSON lists "containers"; an AmneziaWG
   or WireGuard one holds the wg-quick text in <protocol>.last_config, itself
   a JSON string with a "config" field. Our own code: the client is GPL. */

#define AMNEZIA_LINK_MAX  LINK_URI_MAX
#define AMNEZIA_JSON_MAX  (256 * 1024)

static unsigned long adler32(const unsigned char *p, unsigned long n)
{
    unsigned long a = 1, b = 0;
    while (n--) { a = (a + *p++) % 65521UL; b = (b + a) % 65521UL; }
    return (b << 16) | a;
}

/* qUncompress: size prefix, zlib header, deflate, Adler-32, all checked,
   the size capped before anything is allocated. NULL when the bytes are
   not such a stream - the Amnezia client then takes them as they are
   (importController), and so does the caller. */
static char *q_uncompress(const unsigned char *in, unsigned long n)
{
    unsigned long want, got, used;
    unsigned char *out;

    if (n < 4 + 2 + 4) return NULL;
    want = ((unsigned long)in[0] << 24) | ((unsigned long)in[1] << 16) |
           ((unsigned long)in[2] << 8) | in[3];
    if (want == 0 || want > AMNEZIA_JSON_MAX) return NULL;
    if ((in[4] & 0x0F) != 8 || (in[4] >> 4) > 7 || (in[5] & 0x20) ||
        ((unsigned)in[4] * 256 + in[5]) % 31 != 0)
        return NULL;
    out = (unsigned char *)malloc(want + 1);
    if (!out) return NULL;
    got = want;
    used = n - 6;
    if (puff(out, &got, in + 6, &used) != 0 || got != want || 6 + used + 4 > n ||
        adler32(out, got) != (((unsigned long)in[6 + used] << 24) |
                               ((unsigned long)in[6 + used + 1] << 16) |
                               ((unsigned long)in[6 + used + 2] << 8) |
                               in[6 + used + 3])) {
        memset(out, 0, want);
        free(out);
        return NULL;
    }
    out[got] = '\0';
    return (char *)out;
}

/* Copy a display name, cut between UTF-8 characters, never inside one. */
static void name_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int amnezia_supported(const char *container)
{
    return strncmp(container, "amnezia-awg", 11) == 0 ||
           strcmp(container, "amnezia-wireguard") == 0;
}

/* The Amnezia client keeps these values as strings; a number is taken too.
   NULL when absent or empty. */
static const char *json_text_of(const JSON_Object *o, const char *key, char *buf, size_t cap)
{
    const JSON_Value *v = o ? json_object_get_value(o, key) : NULL;
    double            d;

    if (!v) return NULL;
    if (json_value_get_type(v) == JSONString) {
        const char *t = json_value_get_string(v);
        return t && t[0] ? t : NULL;
    }
    if (json_value_get_type(v) != JSONNumber) return NULL;
    d = json_value_get_number(v);
    if (d < 0 || d > 4294967295.0 || d != (double)(unsigned long)d) return NULL;
    snprintf(buf, cap, "%lu", (unsigned long)d);
    return buf;
}

/* One "Key = value" line of a synthetic config. A value with a line break
   would add a line of its own: refused. */
static int synth_add(char *dst, size_t cap, const char *key, const char *val)
{
    size_t n = strlen(dst);
    int    w;

    if (!val || !val[0]) return 1;
    if (strpbrk(val, "\r\n")) return 0;
    w = snprintf(dst + n, cap - n, "%s = %s\n", key, val);
    return w > 0 && (size_t)w < cap - n;
}

/* The AmneziaWG parameters by the names the client stores them under
   (configKeys.h awgProtocolKeys) - the same as the wg-quick keys. */
static const char *const AMNEZIA_AWG_KEYS[] = {
    "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4",
    "I1", "I2", "I3", "I4", "I5", "HeaderProtectionKey", "ContentPaddingAddition",
    "RekeyAfterTime", "RekeyTimeout", "RejectAfterTime", "KeepaliveTimeout",
    "MaxHandshakeAttempts", "RandomTrailers", "DisableCookies"
};

/* wg-quick text from last_config's fields - what the Amnezia desktop client
   actually connects with (localsocketcontroller.cpp: client_priv_key,
   client_ip, server_pub_key, psk_key, hostName, port, mtu,
   persistent_keep_alive, allowed_ips, and the AWG parameters by name). The
   "config" text beside them is for showing and exporting. AWG parameters
   missing from last_config are taken from the container's protocol object. */
static int amnezia_fields_text(const JSON_Object *lc, const JSON_Object *proto,
                               const JSON_Object *root, char *out, size_t cap,
                               char *err, size_t errcap)
{
    char              b1[32], b2[32], ep[320], ips[1024] = "";
    const char       *host, *port, *mtu;
    const JSON_Object *awg_src = lc;
    const JSON_Array *arr;
    size_t            i;

    host = json_text_of(lc, "hostName", b1, sizeof b1);
    if (!host) host = json_text_of(root, "hostName", b1, sizeof b1);
    port = json_text_of(lc, "port", b2, sizeof b2);
    if (!port) port = json_text_of(proto, "port", b2, sizeof b2);
    if (!host || !port) return oops(err, errcap, "в ссылке vpn:// нет адреса или порта сервера");
    snprintf(ep, sizeof ep, strchr(host, ':') ? "[%s]:%s" : "%s:%s", host, port);

    if ((arr = json_object_get_array(lc, "allowed_ips")) != NULL) {
        for (i = 0; i < json_array_get_count(arr); i++) {
            const char *ip = json_array_get_string(arr, i);
            if (!ip || strlen(ips) + strlen(ip) + 3 >= sizeof ips) continue;
            if (ips[0]) strcat(ips, ", ");
            strcat(ips, ip);
        }
    }
    if (!ips[0]) snprintf(ips, sizeof ips, "0.0.0.0/0, ::/0");

    /* The same rule for AWG parameters as for the rest: last_config first.
       Only if it has none at all, the container's own. */
    for (i = 0; i < sizeof AMNEZIA_AWG_KEYS / sizeof AMNEZIA_AWG_KEYS[0]; i++)
        if (json_text_of(lc, AMNEZIA_AWG_KEYS[i], b1, sizeof b1)) break;
    if (i == sizeof AMNEZIA_AWG_KEYS / sizeof AMNEZIA_AWG_KEYS[0] && proto) awg_src = proto;

    snprintf(out, cap, "[Interface]\n");
    if (!synth_add(out, cap, "PrivateKey", json_text_of(lc, "client_priv_key", b1, sizeof b1)) ||
        !synth_add(out, cap, "Address", json_text_of(lc, "client_ip", b1, sizeof b1)))
        return oops(err, errcap, "ссылка vpn:// повреждена");
    mtu = json_text_of(lc, "mtu", b2, sizeof b2);
    if (mtu) {
        /* The client's daemon lifts anything below 1280 to 1280. */
        unsigned long m;
        if (awg_uint(mtu, strlen(mtu), 4294967295UL, &m) && m < 1280) mtu = "1280";
        if (!synth_add(out, cap, "MTU", mtu)) return oops(err, errcap, "ссылка vpn:// повреждена");
    }
    for (i = 0; i < sizeof AMNEZIA_AWG_KEYS / sizeof AMNEZIA_AWG_KEYS[0]; i++)
        if (!synth_add(out, cap, AMNEZIA_AWG_KEYS[i],
                       json_text_of(awg_src, AMNEZIA_AWG_KEYS[i], b1, sizeof b1)))
            return oops(err, errcap, "ссылка vpn:// повреждена");
    if (strlen(out) + 8 >= cap) return oops(err, errcap, "ссылка vpn:// повреждена");
    strcat(out, "[Peer]\n");
    if (!synth_add(out, cap, "PublicKey", json_text_of(lc, "server_pub_key", b1, sizeof b1)) ||
        !synth_add(out, cap, "PresharedKey", json_text_of(lc, "psk_key", b1, sizeof b1)) ||
        !synth_add(out, cap, "Endpoint", ep) ||
        !synth_add(out, cap, "AllowedIPs", ips) ||
        !synth_add(out, cap, "PersistentKeepalive", json_text_of(lc, "persistent_keep_alive", b1, sizeof b1)))
        return oops(err, errcap, "ссылка vpn:// повреждена");
    return 1;
}

/* The AmneziaWG default MTU of the Amnezia desktop client
   (protocolConstants.h awg::defaultMtu), when nothing sets one. */
#define AMNEZIA_AWG_DEFAULT_MTU 1376

static int amnezia_json(const char *json_text, link_profile *out, char *err, size_t errcap)
{
    static char        synth[LINK_AWG_MAX + 4096];
    JSON_Value        *root, *lcv = NULL;
    const JSON_Object *o, *pick = NULL, *proto = NULL, *lc = NULL;
    const JSON_Array  *cs;
    const char        *def, *label;
    char               unsupported[160] = "";
    size_t             i, n;
    int                ok = 0;

    root = json_parse_string(json_text);
    o = json_value_get_object(root);
    if (!o) { oops(err, errcap, "ссылка vpn:// повреждена: внутри не JSON"); goto done; }

    if (json_object_has_value(o, "config_version") || json_object_has_value(o, "api_key") ||
        json_object_has_value(o, "auth_data") || json_object_has_value(o, "api_config")) {
        oops(err, errcap, "это ключ сервиса Amnezia (Premium/Free): такие ключи работают только "
                          "через шлюз Amnezia, Utgard их не поддерживает");
        goto done;
    }
    if (json_object_has_value_of_type(o, "format_version", JSONNumber) &&
        json_object_get_number(o, "format_version") > 1) {
        oops(err, errcap, "ссылка создана более новой версией Amnezia — такой формат Utgard пока не знает");
        goto done;
    }
    cs = json_object_get_array(o, "containers");
    if (!cs || json_array_get_count(cs) == 0) {
        oops(err, errcap, json_object_has_value(o, "userName")
            ? "это ссылка полного доступа к серверу: в ней нет настроек подключения. "
              "Попросите у владельца сервера ссылку для подключения"
            : "в ссылке vpn:// нет ни одного протокола");
        goto done;
    }

    /* The default container if we can run it, otherwise the first we can. */
    def = json_object_get_string(o, "defaultContainer");
    n = json_array_get_count(cs);
    for (i = 0; i < n; i++) {
        const JSON_Object *c = json_array_get_object(cs, i);
        const char        *name = c ? json_object_get_string(c, "container") : NULL;
        if (!name) continue;
        if (!amnezia_supported(name)) {
            if (strlen(unsupported) + strlen(name) + 3 < sizeof unsupported) {
                if (unsupported[0]) strcat(unsupported, ", ");
                strcat(unsupported, name);
            }
            continue;
        }
        if (!pick || (def && strcmp(name, def) == 0)) pick = c;
    }
    if (!pick) {
        char msg[256];
        snprintf(msg, sizeof msg, "в ссылке нет AmneziaWG или WireGuard, есть только: %s",
                 unsupported[0] ? unsupported : "неизвестные протоколы");
        oops(err, errcap, msg);
        goto done;
    }

    /* The protocol object sits under the protocol's name ("awg",
       "wireguard"); rather than keep that mapping, take the member that
       holds last_config - a JSON string, or an object as such. */
    for (i = 0; i < json_object_get_count(pick) && !lc; i++) {
        const JSON_Object *po = json_value_get_object(json_object_get_value_at(pick, i));
        const JSON_Value  *v  = po ? json_object_get_value(po, "last_config") : NULL;
        if (!v) continue;
        proto = po;
        if (json_value_get_type(v) == JSONString) {
            lcv = json_parse_string(json_value_get_string(v));
            lc  = json_value_get_object(lcv);
        } else {
            lc = json_value_get_object(v);
        }
    }
    if (!proto) {
        oops(err, errcap, "это ссылка полного доступа к серверу: в ней нет настроек подключения. "
                          "Попросите у владельца сервера ссылку для подключения");
        goto done;
    }
    if (!lc) { oops(err, errcap, "ссылка vpn:// повреждена: нет конфигурации подключения"); goto done; }

    if (json_object_has_value(lc, "client_priv_key")) {
        if (!amnezia_fields_text(lc, proto, o, synth, sizeof synth, err, errcap) ||
            !link_parse_wgconf(synth, strlen(synth), out, err, errcap))
            goto done;
    } else {
        const char *config = json_object_get_string(lc, "config");
        if (!config) { oops(err, errcap, "ссылка vpn:// повреждена: нет конфигурации подключения"); goto done; }
        if (!link_parse_wgconf(config, strlen(config), out, err, errcap)) goto done;
        if (!out->mtu) {
            char        b[32];
            const char *m = json_text_of(lc, "mtu", b, sizeof b);
            unsigned long v;
            if (m && awg_uint(m, strlen(m), 4294967295UL, &v))
                out->mtu = v < 1280 ? 1280 : (v <= 1500 ? (int)v : 0);
        }
    }
    if (!out->mtu && link_is_awg(out)) out->mtu = AMNEZIA_AWG_DEFAULT_MTU;

    label = json_object_get_string(o, "description");
    if (!label || !label[0]) label = json_object_get_string(o, "hostName");
    if (label) name_copy(out->name, sizeof out->name, label);
    ok = 1;

done:
    memset(synth, 0, sizeof synth);
    json_value_free(lcv);
    json_value_free(root);
    return ok;
}

/* What is inside, recognised the way the Amnezia client does it
   (checkConfigFormat): a backup, an Amnezia server config, a plain
   WireGuard/AmneziaWG config, an Xray or OpenVPN config. */
static int amnezia_payload(const char *text, link_profile *out, char *err, size_t errcap)
{
    if (strstr(text, "Servers/serversList"))
        return oops(err, errcap, "это резервная копия Amnezia, а не ссылка на сервер");
    if (strstr(text, "containers") || strstr(text, "api_key") || strstr(text, "auth_data") ||
        (strstr(text, "hostName") && strstr(text, "userName") && strstr(text, "password")))
        return amnezia_json(text, out, err, errcap);
    if (strstr(text, "[Interface]") && strstr(text, "[Peer]")) {
        if (!link_parse_wgconf(text, strlen(text), out, err, errcap)) return 0;
        if (!out->mtu && link_is_awg(out)) out->mtu = AMNEZIA_AWG_DEFAULT_MTU;
        return 1;
    }
    if (strstr(text, "inbounds") && strstr(text, "outbounds"))
        return oops(err, errcap, "в ссылке конфигурация Xray, а не AmneziaWG или WireGuard");
    if (strstr(text, "client") && (strstr(text, "dev tun") || strstr(text, "dev tap")))
        return oops(err, errcap, "в ссылке конфигурация OpenVPN, а не AmneziaWG или WireGuard");
    return oops(err, errcap, "не удалось распознать содержимое ссылки vpn://");
}

int link_parse_amnezia(const char *uri, link_profile *out, char *err, size_t errcap)
{
    const char    *body;
    size_t         len;
    long           raw_n;
    unsigned char *raw = NULL;
    char          *text;
    int            ok;

    if (!uri || !out) return oops(err, errcap, "пустая ссылка");
    memset(out, 0, sizeof *out);
    while (*uri == ' ' || *uri == '\t' || *uri == '\r' || *uri == '\n') uri++;
    /* The client strips "vpn://" if it is there; the key works without it. */
    body = strncmp(uri, "vpn://", 6) == 0 ? uri + 6 : uri;
    len = strlen(body);
    if (len == 0 || len > AMNEZIA_LINK_MAX) return oops(err, errcap, "ссылка vpn:// пустая или слишком длинная");

    raw = (unsigned char *)malloc(len + 1);
    if (!raw) return oops(err, errcap, "не хватило памяти");
    raw_n = b64_decode(body, len, (char *)raw, len + 1);
    if (raw_n <= 0) {
        free(raw);
        return oops(err, errcap, "ссылка vpn:// повреждена");
    }
    text = q_uncompress(raw, (unsigned long)raw_n);
    ok = amnezia_payload(text ? text : (const char *)raw, out, err, errcap);

    /* The decoded link holds the private key: wipe what we own. */
    if (text) { memset(text, 0, strlen(text)); free(text); }
    memset(raw, 0, len + 1);
    free(raw);
    return ok;
}

/* ---- shadowsocks ---------------------------------------------------- */

static const char *SS_METHODS[] = {
    "none",
    "aes-128-gcm", "aes-192-gcm", "aes-256-gcm",
    "chacha20-ietf-poly1305", "xchacha20-ietf-poly1305",
    "2022-blake3-aes-128-gcm", "2022-blake3-aes-256-gcm",
    "2022-blake3-chacha20-poly1305"
};

static int ss_method_known(const char *m)
{
    size_t i;
    for (i = 0; i < sizeof SS_METHODS / sizeof SS_METHODS[0]; i++)
        if (str_eq(m, SS_METHODS[i])) return 1;
    return 0;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

/* userinfo is either "method:password" percent-encoded, or base64 of it. */
static int ss_userinfo(const char *info, size_t len, link_profile *p,
                       char *err, size_t errcap)
{
    char  buf[512];
    char *colon;

    if (!pct_decode(info, len, buf, sizeof buf))
        return oops(err, errcap, "слишком длинная ссылка ss");

    if (!strchr(buf, ':')) {
        char decoded[512];
        long n = b64_decode(buf, strlen(buf), decoded, sizeof decoded);
        if (n <= 0 || !strchr(decoded, ':'))
            return oops(err, errcap, "в ссылке ss нет пары метод:пароль");
        memcpy(buf, decoded, (size_t)n + 1);
    }

    colon = strchr(buf, ':');
    if (!put(p->method, sizeof p->method, buf, (size_t)(colon - buf)))
        return oops(err, errcap, "слишком длинное имя метода шифрования");
    lower(p->method);

    if (!put(p->password, sizeof p->password, colon + 1, strlen(colon + 1)))
        return oops(err, errcap, "слишком длинный пароль");

    if (!ss_method_known(p->method))
        return oops(err, errcap, "метод шифрования не поддерживается — "
                                 "нужен современный ss (AEAD или 2022-blake3)");
    if (!p->password[0] && !str_eq(p->method, "none"))
        return oops(err, errcap, "в ссылке ss:// нет пароля");
    if (!valid_ss_password(p->method, p->password))
        return oops(err, errcap, "пароль ss не подходит к методу шифрования — "
                                 "для 2022-blake3 нужен ключ base64 нужной длины");
    return 1;
}

/* ---- one link ------------------------------------------------------- */


static int looks_like_bare_key(const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        char c = *s;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '+' || c == '/' || c == '='))
            return 0;
        n++;
    }
    return n >= 64;
}
int link_parse(const char *uri, link_profile *out, char *err, size_t errcap)
{
    const char *body, *frag, *query, *at, *hostpart;
    size_t      len, qlen, bodylen;
    char        scratch[512];

    if (!uri || !out) return oops(err, errcap, "пустая ссылка");
    memset(out, 0, sizeof *out);
    out->port = 443;
    strcpy(out->fingerprint, "chrome");

    while (*uri == ' ' || *uri == '\t' || *uri == '\r' || *uri == '\n') uri++;

    if (strncmp(uri, "vless://", 8) == 0)          { out->proto = LINK_VLESS; body = uri + 8; }
    else if (strncmp(uri, "hysteria2://", 12) == 0) { out->proto = LINK_HY2;  body = uri + 12; }
    else if (strncmp(uri, "hy2://", 6) == 0)        { out->proto = LINK_HY2;  body = uri + 6; }
    else if (strncmp(uri, "ss://", 5) == 0)         { out->proto = LINK_SS;   body = uri + 5; }
    else if (strncmp(uri, "trojan://", 9) == 0)     { out->proto = LINK_TROJAN; body = uri + 9; }
    else if (strncmp(uri, "vmess://", 8) == 0) {
        char b64[4096];
        size_t n = strlen(uri + 8);
        out->proto = LINK_VMESS;
        while (n && (uri[8 + n - 1] == ' ' || uri[8 + n - 1] == '\r' || uri[8 + n - 1] == '\n')) n--;
        if (!put(b64, sizeof b64, uri + 8, n)) return oops(err, errcap, "слишком длинная ссылка vmess");
        return parse_vmess(b64, out, err, errcap);
    }
    else if (strncmp(uri, "wireguard://", 12) == 0) { out->proto = LINK_WG; body = uri + 12; out->port = 51820; }
    else if (strncmp(uri, "vpn://", 6) == 0) return link_parse_amnezia(uri, out, err, errcap);
    /* The Amnezia client also takes its key without "vpn://": a long run of
       base64 and nothing else is tried as one. */
    else if (looks_like_bare_key(uri)) return link_parse_amnezia(uri, out, err, errcap);
    else return oops(err, errcap, "нужна ссылка vless://, vmess://, hysteria2://, ss://, trojan://, wireguard:// или vpn:// (Amnezia)");

    len = strlen(body);

    frag = memchr(body, '#', len);
    if (frag) {
        if (!pct_decode(frag + 1, len - (size_t)(frag - body) - 1,
                        out->name, sizeof out->name))
            out->name[0] = '\0';          /* an over-long remark is not fatal */
        len = (size_t)(frag - body);
    }

    query = memchr(body, '?', len);
    if (query) {
        qlen = len - (size_t)(query - body) - 1;
        query++;
        len = (size_t)(query - body) - 1;
    } else {
        qlen = 0;
        query = "";
    }

    at = memchr(body, '@', len);

    /* A path may sit between the address and the query, as in
       host:port/?outline=1. It is dropped: the address ends at the first '/'
       after the userinfo, never inside it. Without this the port parse chokes
       on "444/". */
    {
        const char *host_start = at ? at + 1 : body;
        const char *slash = memchr(host_start, '/', len - (size_t)(host_start - body));
        if (slash) len = (size_t)(slash - body);
    }
    bodylen = len;


    /* Legacy ss://base64(method:password@host:port) has no '@' in the clear. */
    if (!at && out->proto == LINK_SS) {
        char decoded[512];
        long n = b64_decode(body, bodylen, decoded, sizeof decoded);
        const char *dat;
        if (n <= 0) return oops(err, errcap, "ссылку ss не удалось разобрать");
        dat = strchr(decoded, '@');
        if (!dat) return oops(err, errcap, "в ссылке ss нет адреса сервера");
        if (!ss_userinfo(decoded, (size_t)(dat - decoded), out, err, errcap)) return 0;
        if (!split_hostport(dat + 1, strlen(dat + 1), out->server,
                            sizeof out->server, &out->port))
            return oops(err, errcap, "в ссылке ss неверный адрес или порт");
        if (!valid_host(out->server))
            return oops(err, errcap, "в адресе сервера недопустимые символы");
        return 1;
    }

    if (!at) return oops(err, errcap, "в ссылке нет символа @ перед адресом сервера");
    hostpart = at + 1;

    if (!split_hostport(hostpart, bodylen - (size_t)(hostpart - body),
                        out->server, sizeof out->server, &out->port))
        return oops(err, errcap, "неверный адрес сервера или порт");
    if (!valid_host(out->server))
        return oops(err, errcap, "в адресе сервера недопустимые символы");

    switch (out->proto) {
    case LINK_SS:
        return ss_userinfo(body, (size_t)(at - body), out, err, errcap);

    case LINK_HY2: {
        if (!pct_decode(body, (size_t)(at - body), out->password,
                        sizeof out->password))
            return oops(err, errcap, "слишком длинный пароль");
        if (!out->password[0])
            return oops(err, errcap, "в ссылке hysteria2 нет пароля");

        out->tls = 1;
        query_get(query, qlen, "sni", out->sni, sizeof out->sni);
        if (!out->sni[0]) put(out->sni, sizeof out->sni, out->server, strlen(out->server));

        query_get(query, qlen, "obfs", out->obfs, sizeof out->obfs);
        if (out->obfs[0])
            query_get(query, qlen, "obfs-password",
                      out->obfs_password, sizeof out->obfs_password);

        query_get(query, qlen, "insecure", scratch, sizeof scratch);
        out->insecure = str_eq(scratch, "1");
        return 1;
    }

    case LINK_VLESS: {
        char security[32];

        if (!pct_decode(body, (size_t)(at - body), out->uuid, sizeof out->uuid))
            return oops(err, errcap, "слишком длинный идентификатор");
        if (!out->uuid[0])
            return oops(err, errcap, "в ссылке vless нет идентификатора (uuid)");

        /* VLESS's own encryption layer: sing-box 1.14.1 has no such field. */
        query_get(query, qlen, "encryption", security, sizeof security);
        lower(security);
        if (security[0] && !str_eq(security, "none"))
            return oops(err, errcap, "шифрование VLESS (encryption) не поддерживается ядром sing-box");

        if (!parse_transport(query, qlen, out, err, errcap)) return 0;

        query_get(query, qlen, "sni", out->sni, sizeof out->sni);
        if (!out->sni[0]) put(out->sni, sizeof out->sni, out->server, strlen(out->server));
        query_get(query, qlen, "flow", out->flow, sizeof out->flow);
        query_get(query, qlen, "pbk", out->public_key, sizeof out->public_key);
        query_get(query, qlen, "sid", out->short_id, sizeof out->short_id);
        query_get(query, qlen, "fp", scratch, sizeof scratch);
        if (scratch[0]) put(out->fingerprint, sizeof out->fingerprint,
                            scratch, strlen(scratch));

        /* security=reality|tls|none, and when it is absent, infer the same way
           the PowerShell client does: forcing TLS on a plain server produces a
           config that fails rather than one that merely differs. */
        query_get(query, qlen, "security", security, sizeof security);
        lower(security);
        if (str_eq(security, "reality")) {
            out->reality = 1;
            out->tls = 1;
        } else if (str_eq(security, "tls")) {
            out->tls = 1;
        } else if (str_eq(security, "none") || security[0]) {
            out->tls = 0;
        } else if (out->public_key[0]) {
            out->reality = 1;
            out->tls = 1;
        } else {
            char sni_q[256];
            query_get(query, qlen, "sni", sni_q, sizeof sni_q);
            out->tls = (sni_q[0] != '\0' || out->flow[0] != '\0');
        }

        if (!valid_id(out->uuid))
            return oops(err, errcap, "идентификатор vless содержит пробелы "
                                     "или непечатаемые символы");
        if (out->reality && !out->public_key[0])
            return oops(err, errcap, "в ссылке reality нет ключа (pbk)");
        if (out->reality && !valid_reality_key(out->public_key))
            return oops(err, errcap, "ключ reality (pbk) повреждён — нужна "
                                     "строка из 43 символов base64url");
        if (out->reality && !valid_short_id(out->short_id))
            return oops(err, errcap, "short_id reality повреждён — нужно до 16 "
                                     "шестнадцатеричных символов чётной длины");
        if (!out->tls) out->flow[0] = '\0';   /* vision is meaningless without TLS */
        return 1;
    }

    case LINK_TROJAN: {
        /* trojan://password@host:port, the password URI-encoded, TLS on by
           default and SNI defaulting to the host (Trojan-Go URL scheme). The
           query parameters are the ones the Xray ecosystem adds, the same set
           VLESS uses; every combination below is one sing-box 1.14.1 accepts. */
        char security[32];

        if (!pct_decode(body, (size_t)(at - body), out->password, sizeof out->password))
            return oops(err, errcap, "слишком длинный пароль");
        if (!out->password[0])
            return oops(err, errcap, "в ссылке trojan нет пароля");

        if (!parse_transport(query, qlen, out, err, errcap)) return 0;

        query_get(query, qlen, "sni", out->sni, sizeof out->sni);
        if (!out->sni[0]) put(out->sni, sizeof out->sni, out->server, strlen(out->server));
        query_get(query, qlen, "pbk", out->public_key, sizeof out->public_key);
        query_get(query, qlen, "sid", out->short_id, sizeof out->short_id);
        query_get(query, qlen, "fp", scratch, sizeof scratch);
        if (scratch[0]) put(out->fingerprint, sizeof out->fingerprint, scratch, strlen(scratch));

        query_get(query, qlen, "allowInsecure", scratch, sizeof scratch);
        out->insecure = str_eq(scratch, "1") || str_eq(scratch, "true");
        if (!out->insecure) {
            query_get(query, qlen, "insecure", scratch, sizeof scratch);
            out->insecure = str_eq(scratch, "1") || str_eq(scratch, "true");
        }

        query_get(query, qlen, "security", security, sizeof security);
        lower(security);
        if (str_eq(security, "reality"))   { out->reality = 1; out->tls = 1; }
        else if (str_eq(security, "none")) { out->tls = 0; }
        else                               { out->tls = 1; }    /* trojan's default */

        if (out->reality && !out->public_key[0])
            return oops(err, errcap, "в ссылке reality нет ключа (pbk)");
        if (out->reality && !valid_reality_key(out->public_key))
            return oops(err, errcap, "ключ reality (pbk) повреждён — нужна "
                                     "строка из 43 символов base64url");
        if (out->reality && !valid_short_id(out->short_id))
            return oops(err, errcap, "short_id reality повреждён — нужно до 16 "
                                     "шестнадцатеричных символов чётной длины");
        return 1;
    }

    case LINK_WG: {
        /* wireguard://<private key>@host:port?publickey=&presharedkey=&address=
           &reserved=&mtu=, the v2rayN share format. */
        char address[512], reserved[64];

        if (!pct_decode(body, (size_t)(at - body), out->wg_private_key, sizeof out->wg_private_key))
            return oops(err, errcap, "закрытый ключ WireGuard повреждён — нужна строка base64 из 44 символов");
        query_get(query, qlen, "publickey", out->wg_peer_key, sizeof out->wg_peer_key);
        query_get(query, qlen, "presharedkey", out->wg_psk, sizeof out->wg_psk);
        query_get(query, qlen, "address", address, sizeof address);
        query_get(query, qlen, "reserved", reserved, sizeof reserved);
        query_get(query, qlen, "mtu", scratch, sizeof scratch);
        out->mtu = scratch[0] ? atoi(scratch) : 0;
        return wg_finish(out, address, reserved, err, errcap);
    }

    default:
        return oops(err, errcap, "неизвестный тип ссылки");
    }
}

/* ---- subscription --------------------------------------------------- */

static int looks_like_links(const char *s, size_t len)
{
    size_t i;
    for (i = 0; i + 5 < len && i < 64; i++) {
        if (strncmp(s + i, "vless:", 6) == 0) return 1;
        if (i + 4 < len && strncmp(s + i, "vpn:", 4) == 0) return 1;
        if (strncmp(s + i, "vmess:", 6) == 0) return 1;
        if (i + 10 < len && strncmp(s + i, "wireguard:", 10) == 0) return 1;
        if (strncmp(s + i, "ss://", 5) == 0) return 1;
        if (i + 7 < len && strncmp(s + i, "trojan:", 7) == 0) return 1;
        if (i + 9 < len && strncmp(s + i, "hysteria2", 9) == 0) return 1;
    }
    return 0;
}

int link_parse_subscription(const char *body, size_t len,
                            link_profile *out, int max, int *skipped,
                            char *err, size_t errcap)
{
    static char decoded[NET_SUBSCRIPTION_DECODED_MAX];
    const char *text;
    size_t      text_len;
    const char *p, *end;
    int         found = 0;

    if (skipped) *skipped = 0;
    if (!body || !out || max <= 0) return oops(err, errcap, "пустой ответ подписки");

    if (looks_like_links(body, len)) {
        text = body;
        text_len = len;
    } else {
        long n = b64_decode(body, len, decoded, sizeof decoded);
        if (n <= 0)
            return oops(err, errcap, "ответ не похож ни на список ссылок, ни на base64");
        text = decoded;
        text_len = (size_t)n;
    }

    p = text;
    end = text + text_len;

    while (p < end && found < max) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *stop = nl ? nl : end;
        /* An Amnezia key runs to kilobytes: a line may be as long as a
           link can be at all. Static: this runs on one worker at a time. */
        static char line[LINK_URI_MAX + 1];
        size_t      n = (size_t)(stop - p);

        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        while (n && (*p == ' ' || *p == '\t')) { p++; n--; }

        if (n && n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (link_parse(line, &out[found], NULL, 0)) found++;
            else if (skipped) (*skipped)++;
        } else if (n && skipped) {
            (*skipped)++;
        }

        if (!nl) break;
        p = nl + 1;
    }

    if (found == 0)
        return oops(err, errcap, "в подписке не нашлось поддерживаемых ссылок");
    return found;
}

/* A file picked as a WireGuard/AmneziaWG config: a wg-quick config, or a
   file that holds one link - an Amnezia key saved to a file, say. For
   anything else, say what it is rather than "cannot parse". */
int link_parse_file(const char *text, size_t len, link_profile *out, char *err, size_t errcap)
{
    const char *p = text, *end = text + len;
    size_t      n;

    if (len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    n = (size_t)(end - p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) n--;
    if (memchr(p, '\0', n)) return oops(err, errcap, "в файле нулевой байт — это не конфигурация");

    if (strstr(text, "[Interface]") || strstr(text, "[Peer]"))
        return link_parse_wgconf(text, len, out, err, errcap);
    /* One line and nothing else: a link. */
    if (n && n <= LINK_URI_MAX && !memchr(p, '\n', n) && !memchr(p, '{', n)) {
        static char one[LINK_URI_MAX + 1];
        int         ok;
        memcpy(one, p, n);
        one[n] = '\0';
        ok = link_parse(one, out, err, errcap);
        memset(one, 0, n);
        return ok;
    }
    if (strstr(text, "Servers/serversList"))
        return oops(err, errcap, "это резервная копия Amnezia, а не конфигурация WireGuard/AmneziaWG");
    if (strstr(text, "containers"))
        return oops(err, errcap, "это выгрузка Amnezia в JSON — добавьте её ссылку vpn:// через «Вставить ссылку…»");
    if (strstr(text, "inbounds") && strstr(text, "outbounds"))
        return oops(err, errcap, "это конфигурация Xray/sing-box, а не WireGuard/AmneziaWG");
    if (strstr(text, "client") && (strstr(text, "dev tun") || strstr(text, "dev tap")))
        return oops(err, errcap, "это конфигурация OpenVPN — Utgard её не поддерживает");
    return oops(err, errcap, "это не конфигурация WireGuard/AmneziaWG: нет разделов [Interface] и [Peer]");
}
