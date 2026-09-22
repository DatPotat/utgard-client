#include "link.h"

#include <string.h>
#include <stdlib.h>

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
    else return oops(err, errcap, "нужна ссылка vless://, hysteria2://, ss:// или trojan://");

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
