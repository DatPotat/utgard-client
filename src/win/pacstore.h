#ifndef UTGARD_PACSTORE_H
#define UTGARD_PACSTORE_H
#include "pac.h"

#define PAC_ITEMS_MAX 8

typedef struct {
    int enabled;
    int via_vpn;
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

#endif
