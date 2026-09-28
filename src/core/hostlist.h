#ifndef UTGARD_HOSTLIST_H
#define UTGARD_HOSTLIST_H

#include <stddef.h>
#include "lists.h"

/* The site lists as the window shows them: entries with their line in the
   file, sorted and searched, removed by line, added in bulk with a preview.
   The file text stays the one source of truth - comments, blank lines and
   the order of what is not touched survive every operation. No Windows
   headers: tested on the host (tests/test_hostlist.c). */

enum { HL_EXACT = 1,    /* zapret: "^name", the domain alone */
       HL_ZONE  = 2 };  /* zapret: one label, the whole zone */

typedef struct {
    int           line;                  /* 0-based line in the text */
    unsigned char flags;
    char          host[LIST_ENTRY_MAX];  /* as written, without "^" */
} hl_entry;

typedef struct {
    int  added, duplicate, covered, invalid;
    char bad[3][64];                     /* the first refused lines */
} hl_preview;

int  hostlist_parse(const char *text, int zapret, hl_entry *out, int max);
void hostlist_sort(hl_entry *e, int n);
int  hostlist_match(const hl_entry *e, const char *needle);   /* ASCII case-insensitive */

/* text + the new entries of paste, one per line. Duplicates, entries a
   listed parent already covers and lines that are no address are counted,
   not added. out may be NULL to preview only. 0 when out is too small. */
int hostlist_add(const char *text, const char *paste, int zapret,
                 char *out, size_t cap, hl_preview *pv);

/* text without the given lines (any order, repeats allowed). */
int hostlist_remove(const char *text, const int *lines, int n, char *out, size_t cap);

#endif
