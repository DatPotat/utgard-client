#include "profiles.h"

#include <string.h>

/* ---- format ---------------------------------------------------------

   "UTGP" | u32 version | u32 count | i32 active | str subscription
          | count * entry

   str  = u16 length + bytes, no terminator
   entry = proto, name, server, port, uuid, flow, password, method,
           tls, insecure, sni, fingerprint, reality, public_key,
           short_id, obfs, obfs_password, source
           version 2: transport, path, host, service_name, alpn, early_data
           version 3: alter_id, wg_private_key, wg_peer_key, wg_psk,
                      wg_address, wg_reserved, mtu, keepalive
           version 4: awg

   Pack and unpack list the fields by hand in the same order. The round-trip
   test fills every field with a distinct value, so the two going out of step
   fails loudly instead of silently swapping two strings.
   --------------------------------------------------------------------- */

#define MAGIC_0 'U'
#define MAGIC_1 'T'
#define MAGIC_2 'G'
#define MAGIC_3 'P'
#define FORMAT_VERSION 4u      /* 4 added AmneziaWG; 1 to 3 still read */

typedef struct {
    unsigned char *buf;
    size_t         cap;
    size_t         n;
    int            bad;
} writer;

typedef struct {
    const unsigned char *buf;
    size_t               len;
    size_t               n;
    int                  bad;
} reader;

/* strnlen is not declared under a strict -std=c11; this keeps the module
   free of feature-test macros. */
static size_t bounded_len(const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

static void w_u32(writer *w, unsigned int v)
{
    if (w->bad || w->n + 4 > w->cap) { w->bad = 1; return; }
    w->buf[w->n++] = (unsigned char)(v & 0xFF);
    w->buf[w->n++] = (unsigned char)((v >> 8) & 0xFF);
    w->buf[w->n++] = (unsigned char)((v >> 16) & 0xFF);
    w->buf[w->n++] = (unsigned char)((v >> 24) & 0xFF);
}

static void w_str(writer *w, const char *s, size_t cap)
{
    size_t len = bounded_len(s, cap);
    if (w->bad || len > 0xFFFF || w->n + 2 + len > w->cap) { w->bad = 1; return; }
    w->buf[w->n++] = (unsigned char)(len & 0xFF);
    w->buf[w->n++] = (unsigned char)((len >> 8) & 0xFF);
    memcpy(w->buf + w->n, s, len);
    w->n += len;
}

static unsigned int r_u32(reader *r)
{
    unsigned int v;
    if (r->bad || r->n + 4 > r->len) { r->bad = 1; return 0; }
    v =  (unsigned int)r->buf[r->n]
      | ((unsigned int)r->buf[r->n + 1] << 8)
      | ((unsigned int)r->buf[r->n + 2] << 16)
      | ((unsigned int)r->buf[r->n + 3] << 24);
    r->n += 4;
    return v;
}

/* Always leaves dst a valid C string, including on a malformed record. */
static void r_str(reader *r, char *dst, size_t cap)
{
    size_t len;

    if (cap) dst[0] = '\0';
    if (r->bad || r->n + 2 > r->len) { r->bad = 1; return; }
    len = (size_t)r->buf[r->n] | ((size_t)r->buf[r->n + 1] << 8);
    r->n += 2;

    if (r->n + len > r->len) { r->bad = 1; return; }
    if (len >= cap) { r->bad = 1; r->n += len; return; }

    memcpy(dst, r->buf + r->n, len);
    dst[len] = '\0';
    r->n += len;
}

static void pack_entry(writer *w, const profile_entry *e)
{
    const link_profile *l = &e->link;

    w_u32(w, (unsigned int)l->proto);
    w_str(w, l->name,          sizeof l->name);
    w_str(w, l->server,        sizeof l->server);
    w_u32(w, (unsigned int)l->port);
    w_str(w, l->uuid,          sizeof l->uuid);
    w_str(w, l->flow,          sizeof l->flow);
    w_str(w, l->password,      sizeof l->password);
    w_str(w, l->method,        sizeof l->method);
    w_u32(w, (unsigned int)l->tls);
    w_u32(w, (unsigned int)l->insecure);
    w_str(w, l->sni,           sizeof l->sni);
    w_str(w, l->fingerprint,   sizeof l->fingerprint);
    w_u32(w, (unsigned int)l->reality);
    w_str(w, l->public_key,    sizeof l->public_key);
    w_str(w, l->short_id,      sizeof l->short_id);
    w_str(w, l->obfs,          sizeof l->obfs);
    w_str(w, l->obfs_password, sizeof l->obfs_password);
    w_str(w, e->source,        sizeof e->source);
    w_str(w, l->transport,     sizeof l->transport);
    w_str(w, l->path,          sizeof l->path);
    w_str(w, l->host,          sizeof l->host);
    w_str(w, l->service_name,  sizeof l->service_name);
    w_str(w, l->alpn,          sizeof l->alpn);
    w_u32(w, (unsigned int)l->early_data);
    w_u32(w, (unsigned int)l->alter_id);
    w_str(w, l->wg_private_key, sizeof l->wg_private_key);
    w_str(w, l->wg_peer_key,    sizeof l->wg_peer_key);
    w_str(w, l->wg_psk,         sizeof l->wg_psk);
    w_str(w, l->wg_address,     sizeof l->wg_address);
    w_str(w, l->wg_reserved,    sizeof l->wg_reserved);
    w_u32(w, (unsigned int)l->mtu);
    w_u32(w, (unsigned int)l->keepalive);
    w_str(w, l->awg,            sizeof l->awg);
}

static void unpack_entry(reader *r, profile_entry *e, unsigned int version)
{
    link_profile *l = &e->link;
    unsigned int  v;

    memset(e, 0, sizeof *e);

    v = r_u32(r);
    l->proto = (v == LINK_VLESS || v == LINK_HY2 || v == LINK_SS || v == LINK_TROJAN ||
                v == LINK_VMESS || v == LINK_WG)
                   ? (link_proto)v : LINK_NONE;

    r_str(r, l->name,          sizeof l->name);
    r_str(r, l->server,        sizeof l->server);
    v = r_u32(r);
    l->port = (v >= 1 && v <= 65535) ? (int)v : 443;
    r_str(r, l->uuid,          sizeof l->uuid);
    r_str(r, l->flow,          sizeof l->flow);
    r_str(r, l->password,      sizeof l->password);
    r_str(r, l->method,        sizeof l->method);
    l->tls      = r_u32(r) ? 1 : 0;
    l->insecure = r_u32(r) ? 1 : 0;
    r_str(r, l->sni,           sizeof l->sni);
    r_str(r, l->fingerprint,   sizeof l->fingerprint);
    l->reality  = r_u32(r) ? 1 : 0;
    r_str(r, l->public_key,    sizeof l->public_key);
    r_str(r, l->short_id,      sizeof l->short_id);
    r_str(r, l->obfs,          sizeof l->obfs);
    r_str(r, l->obfs_password, sizeof l->obfs_password);
    r_str(r, e->source,        sizeof e->source);

    /* Version 1 files end here: their profiles are plain TCP. */
    if (version >= 2) {
        r_str(r, l->transport,    sizeof l->transport);
        r_str(r, l->path,         sizeof l->path);
        r_str(r, l->host,         sizeof l->host);
        r_str(r, l->service_name, sizeof l->service_name);
        r_str(r, l->alpn,         sizeof l->alpn);
        v = r_u32(r);
        l->early_data = (v <= 65536) ? (int)v : 0;
    }
    if (version >= 3) {
        v = r_u32(r);
        l->alter_id = (v <= 65535) ? (int)v : 0;
        r_str(r, l->wg_private_key, sizeof l->wg_private_key);
        r_str(r, l->wg_peer_key,    sizeof l->wg_peer_key);
        r_str(r, l->wg_psk,         sizeof l->wg_psk);
        r_str(r, l->wg_address,     sizeof l->wg_address);
        r_str(r, l->wg_reserved,    sizeof l->wg_reserved);
        v = r_u32(r);
        l->mtu = (v <= 9000) ? (int)v : 0;
        v = r_u32(r);
        l->keepalive = (v <= 65535) ? (int)v : 0;
    }
    if (version >= 4)
        r_str(r, l->awg, sizeof l->awg);
}

size_t profiles_pack(const profile_store *s, unsigned char *buf, size_t cap)
{
    writer w;
    int    i, count;

    if (!s || !buf) return 0;

    w.buf = buf; w.cap = cap; w.n = 0; w.bad = 0;

    count = s->count;
    if (count < 0) count = 0;
    if (count > PROFILES_MAX) count = PROFILES_MAX;

    if (cap < 4) return 0;
    buf[w.n++] = MAGIC_0; buf[w.n++] = MAGIC_1;
    buf[w.n++] = MAGIC_2; buf[w.n++] = MAGIC_3;

    w_u32(&w, FORMAT_VERSION);
    w_u32(&w, (unsigned int)count);
    w_u32(&w, (unsigned int)s->active);
    w_str(&w, s->subscription, sizeof s->subscription);

    for (i = 0; i < count; i++) pack_entry(&w, &s->items[i]);

    return w.bad ? 0 : w.n;
}

int profiles_unpack(const unsigned char *buf, size_t len, profile_store *s)
{
    reader       r;
    unsigned int version, count;
    int          i;

    if (!buf || !s) return 0;
    memset(s, 0, sizeof *s);
    s->active = -1;

    if (len < 4 || buf[0] != MAGIC_0 || buf[1] != MAGIC_1 ||
        buf[2] != MAGIC_2 || buf[3] != MAGIC_3)
        goto broken;

    r.buf = buf; r.len = len; r.n = 4; r.bad = 0;

    version = r_u32(&r);
    /* Older formats are read, never refused: an update must not lose the
       profiles a user already has. Newer ones are refused - their layout is
       unknown here. */
    if (r.bad || version < 1 || version > FORMAT_VERSION) goto broken;

    count = r_u32(&r);
    if (r.bad || count > PROFILES_MAX) goto broken;

    s->active = (int)r_u32(&r);
    r_str(&r, s->subscription, sizeof s->subscription);
    if (r.bad) goto broken;

    for (i = 0; i < (int)count; i++) {
        unpack_entry(&r, &s->items[i], version);
        if (r.bad) goto broken;
    }

    s->count = (int)count;
    if (s->active < 0 || s->active >= s->count) s->active = s->count ? 0 : -1;
    return 1;

broken:
    /* A half-read file must not leave a store that looks usable - an active
       index pointing into records that were never filled is worse than empty. */
    memset(s, 0, sizeof *s);
    s->active = -1;
    return 0;
}

/* ---- storage -------------------------------------------------------- */

#ifdef _WIN32

#include <windows.h>
#include <wincrypt.h>
#include <strsafe.h>

#include "fileio.h"

#define BLOB_MAX (1024 * 1024)

/* Mixed into the DPAPI key so the blob is not interchangeable with other
   DPAPI data of the same user. It lives in the binary, so it raises the bar
   rather than providing secrecy. */
static const char DPAPI_SALT[] = "utgard-profiles-v1";

static int state_path(wchar_t *buf, size_t cap)
{
    wchar_t  dir[1024];
    wchar_t *slash;
    DWORD    n;

    n = GetModuleFileNameW(NULL, dir, (DWORD)(sizeof dir / sizeof dir[0]));
    if (n == 0 || n >= sizeof dir / sizeof dir[0]) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';

    return SUCCEEDED(StringCchPrintfW(buf, cap, L"%sprofiles.dat", dir));
}

/* Set when an unreadable file could not be moved aside: saving would
   overwrite the only copy. */
static int g_save_blocked;

int profiles_save(const profile_store *s)
{
    static unsigned char plain[BLOB_MAX];
    wchar_t   path[1024];
    DATA_BLOB in, entropy, out;
    size_t    n;
    int       ok;

    if (g_save_blocked) return 0;
    n = profiles_pack(s, plain, sizeof plain);
    if (n == 0) return 0;
    if (!state_path(path, 1024)) return 0;

    in.pbData      = plain;
    in.cbData      = (DWORD)n;
    entropy.pbData = (BYTE *)DPAPI_SALT;
    entropy.cbData = (DWORD)(sizeof DPAPI_SALT - 1);
    out.pbData     = NULL;
    out.cbData     = 0;

    if (!CryptProtectData(&in, L"utgard profiles", &entropy, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        SecureZeroMemory(plain, n);
        return 0;
    }
    SecureZeroMemory(plain, n);

    /* Atomic: the profiles are the one file whose loss hurts most. */
    ok = file_write(path, out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
}

/* The name carries the time so a second failure never replaces the first. */
static void set_aside(const wchar_t *path, wchar_t *aside, size_t cap)
{
    SYSTEMTIME t;

    GetLocalTime(&t);
    if (FAILED(StringCchPrintfW(aside, cap, L"%s.unreadable-%04u%02u%02u-%02u%02u%02u",
                                path, t.wYear, t.wMonth, t.wDay,
                                t.wHour, t.wMinute, t.wSecond)) ||
        !MoveFileExW(path, aside, MOVEFILE_WRITE_THROUGH)) {
        aside[0] = L'\0';
        g_save_blocked = 1;
    }
}

int profiles_load(profile_store *s, wchar_t *aside, size_t aside_cap)
{
    static unsigned char raw[BLOB_MAX];
    wchar_t   path[1024];
    DATA_BLOB in, entropy, out;
    size_t    got = 0;
    int       ok;

    memset(s, 0, sizeof *s);
    s->active = -1;
    if (aside_cap) aside[0] = L'\0';

    if (!state_path(path, 1024)) { g_save_blocked = 1; return 0; }

    /* No file yet means an empty store. Any other failure to read is a file
       we must not lose. */
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_FILE_NOT_FOUND)
        return 1;
    if (!file_read(path, raw, sizeof raw, &got)) {
        set_aside(path, aside, aside_cap);
        return 0;
    }
    if (got == 0) return 1;     /* nothing in it to lose */

    in.pbData      = raw;
    in.cbData      = (DWORD)got;
    entropy.pbData = (BYTE *)DPAPI_SALT;
    entropy.cbData = (DWORD)(sizeof DPAPI_SALT - 1);
    out.pbData     = NULL;
    out.cbData     = 0;

    if (!CryptUnprotectData(&in, NULL, &entropy, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        /* another account, another machine, or a damaged file */
        set_aside(path, aside, aside_cap);
        return 0;
    }

    ok = profiles_unpack(out.pbData, out.cbData, s);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (ok) return 1;
    /* a newer format or damage inside the encrypted blob */
    set_aside(path, aside, aside_cap);
    return 0;
}

#endif /* _WIN32 */
