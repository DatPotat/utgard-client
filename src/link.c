#include "link.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "parson.h"

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

int link_parse_wgconf(const char *text, size_t len, link_profile *out,
                      char *err, size_t errcap)
{
    char        address[512] = "", endpoint[300] = "";
    const char *p = text, *end = text + len;
    int         section = 0, peers = 0;   /* 1 interface, 2 peer */

    if (!text || !out) return oops(err, errcap, "пустой файл");
    memset(out, 0, sizeof *out);
    out->proto = LINK_WG;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t      n  = (size_t)((nl ? nl : end) - p);
        char        line[600], *eq, *val;

        if (n < sizeof line) {
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
                if (section == 1 && key_is(line, "PrivateKey"))
                    put(out->wg_private_key, sizeof out->wg_private_key, val, strlen(val));
                else if (section == 1 && key_is(line, "Address")) {
                    size_t a = strlen(address);
                    if (a + strlen(val) + 2 < sizeof address) {
                        if (a) address[a++] = ',';
                        memcpy(address + a, val, strlen(val) + 1);
                    }
                } else if (section == 1 && key_is(line, "MTU"))
                    out->mtu = atoi(val);
                else if (section == 2 && key_is(line, "PublicKey"))
                    put(out->wg_peer_key, sizeof out->wg_peer_key, val, strlen(val));
                else if (section == 2 && key_is(line, "PresharedKey"))
                    put(out->wg_psk, sizeof out->wg_psk, val, strlen(val));
                else if (section == 2 && key_is(line, "Endpoint"))
                    put(endpoint, sizeof endpoint, val, strlen(val));
                else if (section == 2 && key_is(line, "PersistentKeepalive"))
                    out->keepalive = atoi(val);
            }
        }
        if (!nl) break;
        p = nl + 1;
    }

    if (!peers) return oops(err, errcap, "в файле нет раздела [Peer]");
    out->port = 51820;
    if (!endpoint[0] || !split_hostport(endpoint, strlen(endpoint), out->server,
                                        sizeof out->server, &out->port) ||
        !valid_host(out->server))
        return oops(err, errcap, "в разделе [Peer] нет адреса сервера (Endpoint) или он неверен");
    return wg_finish(out, address, "", err, errcap);
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
    else return oops(err, errcap, "нужна ссылка vless://, vmess://, hysteria2://, ss://, trojan:// или wireguard://");

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
    static char decoded[262144];
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
        char        line[2048];
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
