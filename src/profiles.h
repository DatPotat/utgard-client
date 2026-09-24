#ifndef UTGARD_PROFILES_H
#define UTGARD_PROFILES_H

#include "link.h"

#define PROFILES_MAX  64
#define PROFILE_SRC   512

typedef struct {
    link_profile link;
    char source[PROFILE_SRC];   /* subscription URL, empty when added by hand */
} profile_entry;

typedef struct {
    profile_entry items[PROFILES_MAX];
    int  count;
    int  active;                        /* index into items, -1 when none */
    char subscription[PROFILE_SRC];     /* remembered subscription URL */
} profile_store;

/* Serialisation. Kept free of Windows headers so the format can be round-trip
   tested and fuzzed on the host: unpack reads a file that may be truncated,
   corrupted or hostile. Lengths are explicit, so the layout does not depend
   on how the compiler pads the structs. */
size_t profiles_pack(const profile_store *s, unsigned char *buf, size_t cap);
int    profiles_unpack(const unsigned char *buf, size_t len, profile_store *s);

#ifdef _WIN32
/* profiles.dat next to the executable, encrypted with DPAPI: the blob
   is bound to the Windows account, so a copy taken to another machine or
   another user is useless. It is NOT protected from code running as this
   same user - nothing local is.

   Load returns 1 with the profiles, or with an empty store when there is no
   file. It returns 0 when a file exists but cannot be read (another account,
   a newer format, damage): the store is empty and the file is renamed to
   the name put in aside, so a later save cannot overwrite it. If the rename
   fails, aside is empty and every save is refused until restart. */
int profiles_load(profile_store *s, wchar_t *aside, size_t aside_cap);
int profiles_save(const profile_store *s);
#endif

#endif
