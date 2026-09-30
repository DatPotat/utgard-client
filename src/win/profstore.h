#ifndef UTGARD_PROFSTORE_H
#define UTGARD_PROFSTORE_H

#include <wchar.h>
#include "profiles.h"

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
