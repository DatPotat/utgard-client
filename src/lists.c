#include "lists.h"

#include <string.h>
#include <stdio.h>

/* ---- one line -> one entry ------------------------------------------ */

/* Strict IPv4 or CIDR, as the PowerShell version checked it: a loose form
   would let "999.1.1.1" through and then bolt "/32" onto garbage. */
static int is_ip_cidr(const char *s)
{
    int octets = 0, digits = 0, value = 0, mask = -1;
    const char *p = s;

    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            if (++digits > 3) return 0;
            value = value * 10 + (*p - '0');
            if (value > 255) return 0;
        } else if (*p == '.' || *p == '/' || *p == '\0') {
            if (digits == 0) return 0;
            octets++;
            digits = 0;
            value = 0;
            if (*p == '/') {
                p++;
                if (!*p) return 0;
                mask = 0;
                for (; *p; p++) {
                    if (*p < '0' || *p > '9') return 0;
                    mask = mask * 10 + (*p - '0');
                    if (mask > 32) return 0;
                }
                break;
            }
            if (*p == '\0') break;
        } else {
            return 0;
        }
    }
    return octets == 4 && (mask < 0 || mask <= 32);
}

static void trim(char *s)
{
    size_t n = strlen(s);
    size_t i = 0;

    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                 s[n - 1] == '\n')) s[--n] = '\0';
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static int starts(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* Returns 1 on success, 0 when the line holds nothing, -1 when the entry is
   refused as unsafe.
   Mirrors Normalize-Domain: strip a trailing comment, the scheme, the
   ad-block decorations, everything after the first slash or space, and
   lowercase what is left. IPs and CIDRs are returned untouched, because the
   slash cleanup would turn 203.0.113.0/24 into a single host. */
static int normalize(const char *raw, char *out, size_t cap)
{
    char buf[LIST_ENTRY_MAX * 2];
    char *hash, *slash, *space;
    size_t i;

    if (strlen(raw) >= sizeof buf) return 0;
    strcpy(buf, raw);

    hash = strchr(buf, '#');
    if (hash) *hash = '\0';
    trim(buf);
    if (!buf[0]) return 0;

    if (is_ip_cidr(buf)) {
        if (strchr(buf, '/')) return snprintf(out, cap, "%s", buf) > 0;
        return snprintf(out, cap, "%s/32", buf) > 0;
    }

    if      (starts(buf, "https://")) memmove(buf, buf + 8, strlen(buf + 8) + 1);
    else if (starts(buf, "http://"))  memmove(buf, buf + 7, strlen(buf + 7) + 1);
    if (starts(buf, "||")) memmove(buf, buf + 2, strlen(buf + 2) + 1);
    if (starts(buf, "+.")) memmove(buf, buf + 2, strlen(buf + 2) + 1);
    if (starts(buf, "*.")) memmove(buf, buf + 2, strlen(buf + 2) + 1);
    while (buf[0] == '.')  memmove(buf, buf + 1, strlen(buf + 1) + 1);

    {
        size_t n = strlen(buf);
        if (n && buf[n - 1] == '^') buf[n - 1] = '\0';
    }

    slash = strchr(buf, '/');
    if (slash) *slash = '\0';
    space = strpbrk(buf, " \t");
    if (space) *space = '\0';

    for (i = 0; buf[i]; i++)
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');

    if (!buf[0] || strlen(buf) >= cap) return 0;

    /* A single label is either a typo or a bare TLD. As domain_suffix it would
       match the entire zone - one stray "com" line sends every .com site into
       the tunnel - so it is refused rather than obeyed. */
    if (!strchr(buf, '.')) return -1;

    strcpy(out, buf);
    return 1;
}

/* ---- reading -------------------------------------------------------- */

int lists_read_text(const char *in, char out[][LIST_ENTRY_MAX], int max)
{
    const char *p = in;
    int n = 0;

    if (!in) return 0;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) p += 3;

    while (*p && n < max) {
        char   line[LIST_ENTRY_MAX * 2];
        size_t len = 0;

        while (p[len] && p[len] != '\n') len++;
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            trim(line);
            if (line[0] && line[0] != '#' && strlen(line) < LIST_ENTRY_MAX) {
                strcpy(out[n], line);
                n++;
            }
        }
        p += len;
        if (*p == '\n') p++;
    }
    return n;
}

/* Is `child` a subdomain of any entry in the set? */
static int covered_by(const char *child, char set[][LIST_ENTRY_MAX], int count,
                      int self)
{
    const char *dot = child;
    int i;

    /* Walk the parents: a.b.example -> b.example -> example. The last label
       alone is never a parent, so a bare TLD in the list cannot swallow
       everything under it. */
    for (;;) {
        dot = strchr(dot, '.');
        if (!dot) return 0;
        dot++;
        if (!strchr(dot, '.')) return 0;
        for (i = 0; i < count; i++) {
            if (i == self) continue;
            if (strcmp(set[i], dot) == 0) return 1;
        }
    }
}

/* ---- build ---------------------------------------------------------- */

int lists_build_text(const char *in, char *out, size_t outcap, lists_stats *st)
{
    static char raw[LIST_MAX][LIST_ENTRY_MAX];
    static char domains[LIST_MAX][LIST_ENTRY_MAX];
    static char ips[LIST_MAX][LIST_ENTRY_MAX];
    int    count, i, nd = 0, ni = 0, dup = 0, collapsed = 0;
    size_t used = 0;
    int    wrote_any = 0, first = 1;

    if (st) memset(st, 0, sizeof *st);
    if (!in || !out || outcap == 0) return 0;
    out[0] = '\0';

    count = lists_read_text(in, raw, LIST_MAX);

    for (i = 0; i < count; i++) {
        char entry[LIST_ENTRY_MAX];
        int  j, seen = 0, r;

        r = normalize(raw[i], entry, sizeof entry);
        if (r < 0) { if (st) st->invalid++; continue; }
        if (r == 0) continue;

        if (strchr(entry, '/')) {
            for (j = 0; j < ni; j++) if (strcmp(ips[j], entry) == 0) { seen = 1; break; }
            if (seen) { dup++; continue; }
            if (ni < LIST_MAX) strcpy(ips[ni++], entry);
        } else {
            for (j = 0; j < nd; j++) if (strcmp(domains[j], entry) == 0) { seen = 1; break; }
            if (seen) { dup++; continue; }
            if (nd < LIST_MAX) strcpy(domains[nd++], entry);
        }
    }

#define PUT(text) do { \
        size_t _l = strlen(text); \
        if (used + _l + 1 >= outcap) return 0; \
        memcpy(out + used, (text), _l); used += _l; out[used] = '\0'; \
    } while (0)

    PUT("{\n  \"version\": 3,\n  \"rules\": [\n    {\n");

    for (i = 0; i < nd; i++) {
        if (covered_by(domains[i], domains, nd, i)) { collapsed++; continue; }
        if (first) { PUT("      \"domain_suffix\": [\n"); first = 0; }
        else       PUT(",\n");
        PUT("        \""); PUT(domains[i]); PUT("\"");
        wrote_any = 1;
    }
    if (!first) PUT("\n      ]");

    if (ni) {
        if (wrote_any) PUT(",\n");
        PUT("      \"ip_cidr\": [\n");
        for (i = 0; i < ni; i++) {
            PUT("        \""); PUT(ips[i]); PUT("\"");
            if (i + 1 < ni) PUT(",\n");
        }
        PUT("\n      ]");
        wrote_any = 1;
    }

    if (!wrote_any) {
        /* sing-box refuses an empty rule; the placeholder keeps the file valid
           so the rest of the client still works. */
        PUT("      \"domain_suffix\": [\n        \"invalid.placeholder.local\"\n      ]");
        if (st) st->empty = 1;
    }

    PUT("\n    }\n  ]\n}\n");
#undef PUT

    if (st) {
        st->domains    = nd - collapsed;
        st->ips        = ni;
        st->collapsed  = collapsed;
        st->duplicates = dup;
    }
    return 1;
}

/* ---- tidy ----------------------------------------------------------- */

int lists_tidy_text(const char *in, char *out, size_t outcap, int *removed)
{
    static char norm[LIST_MAX][LIST_ENTRY_MAX];
    static char kept[LIST_MAX][LIST_ENTRY_MAX];
    const char *p = in;
    const char *start;
    size_t      used = 0;
    int         n = 0, gone = 0, i;

    if (removed) *removed = 0;
    if (!in || !out || outcap == 0) return 0;
    out[0] = '\0';

    /* A leading BOM would keep the first line from being recognised as a
       comment, and the comment would then be silently dropped. */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) p += 3;
    start = p;

    /* First pass: collect the entries that survive normalising and are not
       exact repeats. Comments are not entries and are handled in pass two. */
    while (*p && n < LIST_MAX) {
        char line[LIST_ENTRY_MAX * 2];
        char entry[LIST_ENTRY_MAX];
        size_t len = 0;
        int    j, seen = 0;

        while (p[len] && p[len] != '\n') len++;
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            trim(line);
            if (line[0] && line[0] != '#') {
                if (normalize(line, entry, sizeof entry) != 1) {
                    gone++;
                } else {
                    for (j = 0; j < n; j++)
                        if (strcmp(norm[j], entry) == 0) { seen = 1; break; }
                    if (seen) gone++;
                    else      strcpy(norm[n++], entry);
                }
            }
        }
        p += len;
        if (*p == '\n') p++;
    }

    /* Second pass: drop what a parent covers. */
    {
        int k = 0;
        for (i = 0; i < n; i++) {
            if (!strchr(norm[i], '/') && covered_by(norm[i], norm, n, i)) {
                gone++;
                continue;
            }
            strcpy(kept[k++], norm[i]);
        }
        n = k;
    }

    /* Third pass: rebuild the text, keeping comments and blanks where they
       were and replacing each surviving entry with its normalised form. */
    p = start;
    i = 0;
    while (*p) {
        char   line[LIST_ENTRY_MAX * 2];
        size_t len = 0;
        const char *emit = NULL;

        while (p[len] && p[len] != '\n') len++;
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            trim(line);
            if (!line[0] || line[0] == '#') {
                emit = line;
            } else {
                char entry[LIST_ENTRY_MAX];
                if (normalize(line, entry, sizeof entry) == 1 &&
                    i < n && strcmp(kept[i], entry) == 0) {
                    emit = kept[i];
                    i++;
                }
            }
        }

        if (emit) {
            size_t el = strlen(emit);
            if (used + el + 2 >= outcap) return 0;
            memcpy(out + used, emit, el);
            used += el;
            out[used++] = '\r';
            out[used++] = '\n';
        }

        p += len;
        if (*p == '\n') p++;
    }

    out[used] = '\0';
    if (removed) *removed = gone;
    return 1;
}

/* ---- zapret hostlists ------------------------------------------------- */

/* Letters, digits, dots and hyphens only: a name zapret matches by suffix. */
static int zap_plain(const char *e)
{
    const char *p;
    if (!e[0] || e[0] == '.' ) return 0;
    for (p = e; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '-'))
            return 0;
    return 1;
}

/* Is `name` (or, when self counts, the name itself) covered by a plain entry
   in the set? In zapret every level is a parent, a single label included:
   "ru" covers "*.ru". */
static int zap_covered(const char *name, int self, char set[][LIST_ENTRY_MAX],
                       const int *plain, int count, int skip)
{
    const char *p = name;
    int i;

    for (;;) {
        if (self || p != name)
            for (i = 0; i < count; i++)
                if (i != skip && plain[i] && strcmp(set[i], p) == 0) return 1;
        p = strchr(p, '.');
        if (!p) return 0;
        p++;
    }
}

int lists_tidy_zapret(const char *in, char *out, size_t outcap, int *removed)
{
    static char ent[LIST_MAX][LIST_ENTRY_MAX];
    static int  plain[LIST_MAX], drop[LIST_MAX];
    const char *p = in, *start;
    size_t      used = 0;
    int         n = 0, gone = 0, i, j;

    if (removed) *removed = 0;
    if (!in || !out || outcap == 0) return 0;
    out[0] = '\0';
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) p += 3;
    start = p;

    /* Pass one: every entry, lowercased, with its kind. */
    while (*p && n < LIST_MAX) {
        char   line[LIST_ENTRY_MAX * 2];
        size_t len = 0, k;

        while (p[len] && p[len] != '\n') len++;
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            trim(line);
            if (line[0] && line[0] != '#' && strlen(line) < LIST_ENTRY_MAX) {
                for (k = 0; line[k]; k++)
                    if (line[k] >= 'A' && line[k] <= 'Z') line[k] = (char)(line[k] - 'A' + 'a');
                strcpy(ent[n], line);
                plain[n] = zap_plain(line);
                drop[n]  = 0;
                n++;
            }
        }
        p += len;
        if (*p == '\n') p++;
    }

    /* Pass two: exact repeats, then what a plain parent covers. */
    for (i = 0; i < n; i++)
        for (j = 0; j < i; j++)
            if (!drop[j] && strcmp(ent[i], ent[j]) == 0) { drop[i] = 1; break; }

    for (i = 0; i < n; i++) {
        if (drop[i]) continue;
        if (plain[i]) {
            if (zap_covered(ent[i], 0, ent, plain, n, i)) drop[i] = 1;
        } else if (ent[i][0] == '^' && zap_plain(ent[i] + 1)) {
            /* ^name means exactly name; a plain name or parent covers it too. */
            if (zap_covered(ent[i] + 1, 1, ent, plain, n, i)) drop[i] = 1;
        }
    }

    /* Pass three: rebuild, comments and blanks in place, survivors verbatim
       but lowercased. */
    p = start;
    i = 0;
    while (*p) {
        char        line[LIST_ENTRY_MAX * 2];
        size_t      len = 0;
        const char *emit = NULL;

        while (p[len] && p[len] != '\n') len++;
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            trim(line);
            if (!line[0] || line[0] == '#') emit = line;
            else if (strlen(line) < LIST_ENTRY_MAX && i < n) {
                if (drop[i]) gone++;
                else         emit = ent[i];
                i++;
            }
        }
        if (emit) {
            size_t el = strlen(emit);
            if (used + el + 3 >= outcap) return 0;
            memcpy(out + used, emit, el);
            used += el;
            out[used++] = '\r';
            out[used++] = '\n';
        }
        p += len;
        if (*p == '\n') p++;
    }
    out[used] = '\0';
    if (removed) *removed = gone;
    return 1;
}
