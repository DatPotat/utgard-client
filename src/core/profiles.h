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

#endif
