/*
 * Utgard client - The site lists as a list: parse, sort, search, bulk add
 * with a preview, remove by line. See hostlist.h.
 */

#include "hostlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounded copy (see lists.c): memcpy with an explicit capacity. */
static void copy_bounded(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (!cap) return;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Copies line `from` (up to \n) into buf, trimmed, without a # comment.
   Returns where the next line starts, or NULL at the end. */
static const char *next_line(const char *from, char *buf, size_t cap)
{
    const char *end = strchr(from, '\n');
    size_t      n   = end ? (size_t)(end - from) : strlen(from);
    size_t      i = 0;
    char       *hash;

    if (n >= cap) n = cap - 1;
    memcpy(buf, from, n);
    buf[n] = 0;
    if ((hash = strchr(buf, '#')) != NULL) *hash = 0;
    n = strlen(buf);
    while (n && (buf[n - 1] == ' ' || buf[n - 1] == '\t' || buf[n - 1] == '\r')) buf[--n] = 0;
    while (buf[i] == ' ' || buf[i] == '\t') i++;
    if (i) memmove(buf, buf + i, n - i + 1);
    return end ? end + 1 : NULL;
}

int hostlist_parse(const char *text, int zapret, hl_entry *out, int max)
{
    const char *p = text;
    char        buf[LIST_ENTRY_MAX];
    int         line = 0, n = 0;

    while (p && *p && n < max) {
        p = next_line(p, buf, sizeof buf);
        if (buf[0]) {
            const char *h = buf;
            out[n].line  = line;
            out[n].flags = 0;
            if (zapret && buf[0] == '^') { out[n].flags |= HL_EXACT; h++; }
            if (zapret && h[0] && !strchr(h, '.')) out[n].flags |= HL_ZONE;
            snprintf(out[n].host, sizeof out[n].host, "%s", h);
            n++;
        }
        line++;
    }
    return n;
}

static int cmp_host(const void *a, const void *b)
{
    const char *x = ((const hl_entry *)a)->host, *y = ((const hl_entry *)b)->host;
    for (; *x && lower(*x) == lower(*y); x++, y++) {}
    if (lower(*x) != lower(*y)) return (unsigned char)lower(*x) - (unsigned char)lower(*y);
    return ((const hl_entry *)a)->line - ((const hl_entry *)b)->line;
}

void hostlist_sort(hl_entry *e, int n) { qsort(e, (size_t)n, sizeof *e, cmp_host); }

int hostlist_match(const hl_entry *e, const char *needle)
{
    const char *h;
    if (!needle || !needle[0]) return 1;
    for (h = e->host; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && lower(*a) == lower(*b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

/* ---- bulk add ---------------------------------------------------------- */

/* zapret's own rules (lists_tidy_zapret): lowercase, scheme and path gone,
   a leading ^ kept; letters, digits, dots, hyphens and non-ASCII (IDN)
   only. A single label is a legitimate zone there. */
static int zap_key(const char *raw, char *out, size_t cap)
{
    char        buf[LIST_ENTRY_MAX];
    const char *s = raw;
    const char *scheme = strstr(raw, "://");
    size_t      n = 0;
    int         exact = 0;

    if (*s == '^') { exact = 1; s++; }
    if (scheme) s = scheme + 3;
    for (; *s && *s != '/' && *s != ':' && *s != '?' && *s != ' ' && n + 1 < sizeof buf; s++)
        buf[n++] = lower(*s);
    buf[n] = 0;
    while (n && buf[n - 1] == '.') buf[--n] = 0;
    if (!n || buf[0] == '.' || buf[0] == '-') return -1;
    for (s = buf; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '.' ||
              *s == '-' || (unsigned char)*s >= 0x80))
            return -1;
    if (strlen(buf) + 2 > cap) return -1;
    snprintf(out, cap, "%s%s", exact ? "^" : "", buf);
    return 1;
}

static int key_of(const char *raw, int zapret, char *out, size_t cap)
{
    return zapret ? zap_key(raw, out, cap) : lists_normalize(raw, out, cap);
}

/* Does a listed entry already cover name? VPN entries always take their
   subdomains; in zapret only plain (non-^) ones do, a single label too. */
static int covered(const char *name, char (*set)[LIST_ENTRY_MAX], int n, int zapret)
{
    const char *p = name[0] == '^' ? name + 1 : name;
    int         i;

    if (strchr(p, '/')) return 0;                   /* an IP or a subnet */
    for (p = strchr(p, '.'); p; p = strchr(p, '.')) {
        p++;
        if (!zapret && !strchr(p, '.')) break;     /* a bare TLD is never listed */
        for (i = 0; i < n; i++)
            if (strcmp(set[i], p) == 0) return 1;
    }
    return 0;
}

/* The first refused lines, shortened to fit: copied by hand rather than
   through snprintf, which newer gcc flags (-Wformat-truncation) when a
   longer source meets a shorter buffer, and cut on a UTF-8 character
   boundary so a Cyrillic line does not end in half a letter. */
static void note_bad(hl_preview *pv, const char *raw)
{
    if (pv->invalid < 3) {
        char  *dst = pv->bad[pv->invalid];
        size_t k = strlen(raw);
        if (k > sizeof pv->bad[0] - 1) {
            k = sizeof pv->bad[0] - 1;
            while (k > 0 && ((unsigned char)raw[k] & 0xC0) == 0x80) k--;
        }
        memcpy(dst, raw, k);
        dst[k] = 0;
    }
    pv->invalid++;
}

int hostlist_add(const char *text, const char *paste, int zapret,
                 char *out, size_t cap, hl_preview *pv)
{
    static char set[LIST_MAX][LIST_ENTRY_MAX];
    char        buf[LIST_ENTRY_MAX], key[LIST_ENTRY_MAX];
    const char *p;
    size_t      len = 0;
    int         n = 0, i;

    memset(pv, 0, sizeof *pv);
    for (p = text; p && *p && n < LIST_MAX;) {
        p = next_line(p, buf, sizeof buf);
        if (buf[0] && key_of(buf, zapret, key, sizeof key) == 1) copy_bounded(set[n++], LIST_ENTRY_MAX, key);
    }
    if (out) {
        len = strlen(text);
        if (len + 1 > cap) return 0;
        memcpy(out, text, len + 1);
        if (len && out[len - 1] != '\n') {
            if (len + 3 > cap) return 0;
            memcpy(out + len, "\r\n", 3);
            len += 2;
        }
    }
    for (p = paste; p && *p;) {
        int r, dup = 0;
        p = next_line(p, buf, sizeof buf);
        if (!buf[0]) continue;
        r = key_of(buf, zapret, key, sizeof key);
        if (r == 0) continue;
        if (r < 0) { note_bad(pv, buf); continue; }
        for (i = 0; i < n && !dup; i++) dup = strcmp(set[i], key) == 0;
        if (dup) { pv->duplicate++; continue; }
        if (covered(key, set, n, zapret)) { pv->covered++; continue; }
        if (n >= LIST_MAX) return 0;
        copy_bounded(set[n++], LIST_ENTRY_MAX, key);
        pv->added++;
        if (out) {
            size_t k = strlen(key);
            if (len + k + 3 > cap) return 0;
            memcpy(out + len, key, k);
            memcpy(out + len + k, "\r\n", 3);
            len += k + 2;
        }
    }
    return 1;
}

/* ---- remove ------------------------------------------------------------ */

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

int hostlist_remove(const char *text, const int *lines, int n, char *out, size_t cap)
{
    int        *sorted = NULL;
    const char *p = text;
    size_t      len = 0;
    int         line = 0, k = 0;

    if (n > 0) {
        sorted = (int *)malloc(sizeof(int) * (size_t)n);
        if (!sorted) return 0;
        memcpy(sorted, lines, sizeof(int) * (size_t)n);
        qsort(sorted, (size_t)n, sizeof(int), cmp_int);
    }
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      span = end ? (size_t)(end - p) + 1 : strlen(p);
        while (k < n && sorted[k] < line) k++;
        if (!(k < n && sorted[k] == line)) {
            if (len + span + 1 > cap) { free(sorted); return 0; }
            memcpy(out + len, p, span);
            len += span;
        }
        p += span;
        line++;
    }
    out[len] = 0;
    free(sorted);
    return 1;
}
