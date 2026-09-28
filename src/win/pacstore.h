#ifndef UTGARD_PACSTORE_H
#define UTGARD_PACSTORE_H
#include "pac.h"

#define PAC_ITEMS_MAX 8

typedef struct {
    int enabled;
    wchar_t source[2048];
    char *text;
} pac_item;

typedef struct {
    pac_item items[PAC_ITEMS_MAX];
    int count;
} pac_store;

/* Missing file = an empty store. Malformed file is an error, never silently
   treated as DIRECT. The loader accepts the original one-item format. */
int pacstore_load(pac_store *s);
int pacstore_save(const pac_store *s);
void pacstore_free(pac_store *s);
int pacstore_enabled(const pac_store *s);
/* Returns one unreadable-file notification once; path is empty when moving
   the file aside failed and saves are blocked until restart. */
int pacstore_unreadable_notice(wchar_t *path, size_t cap);

#endif
