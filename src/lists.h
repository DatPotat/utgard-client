#ifndef UTGARD_LISTS_H
#define UTGARD_LISTS_H

#include <stddef.h>

#define LIST_ENTRY_MAX 256
#define LIST_MAX       8192

/* Domain and application lists. Free of Windows headers so the normalising,
   de-duplication and subdomain collapsing can be run and checked on the host
   against a real sing-box binary. */

typedef struct {
    int domains;      /* kept after collapsing */
    int ips;
    int collapsed;    /* subdomains dropped because a parent covers them */
    int duplicates;   /* exact repeats dropped */
    int invalid;      /* single-label entries: a lone "com" would route the
                         whole zone into the tunnel, so it is refused */
    int empty;        /* nothing usable was found */
} lists_stats;

/* Turn the host list into the rule-set source JSON that sing-box compiles.
   Text in, text out: the caller owns all file access, which keeps this module
   testable and keeps non-ASCII paths a Win32 problem rather than a libc one. */
int lists_build_text(const char *in, char *out, size_t outcap, lists_stats *st);

/* Clean a list in memory: drop exact repeats, drop subdomains a parent already
   covers, drop entries refused as unsafe. Order is kept, and so are comment
   and blank lines - they are the user's notes, not data.
   Works on text rather than on the file so the caller can show the result
   before anything is written. Returns 1 and sets removed. */
int lists_tidy_text(const char *in, char *out, size_t outcap, int *removed);

/* Tidy a zapret hostlist by zapret's own rules, which differ from ours: a
   single label such as "ru" is a legitimate entry covering the whole zone,
   a leading ^ switches subdomain matching off, and lines that are not plain
   domains are left exactly as written. Removes exact repeats (ignoring case)
   and entries a plain parent already covers. Comments and blanks stay. */
int lists_tidy_zapret(const char *in, char *out, size_t outcap, int *removed);

/* One trimmed, non-empty, non-comment line per entry. Used for the list of
   applications that go through the tunnel. */
int lists_read_text(const char *in, char out[][LIST_ENTRY_MAX], int max);

#endif
