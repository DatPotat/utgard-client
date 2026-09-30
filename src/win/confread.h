#ifndef UTGARD_CONFREAD_H
#define UTGARD_CONFREAD_H

#include "genconf.h"

/* Reads config.json and the enabled overlays for the generator, by UTF-8
   path: files are opened by UTF-16 path, so folders named in Cyrillic work.
   The paths become the names in the generator's messages.

   1 on success, with every text allocated. 0 with a message for the user
   and nothing allocated: config.json that cannot be opened is "not found",
   one over 16 MiB is unreadable; an overlay that cannot be read is named. */
int  confread_inputs(const char *base_path, const char *const *overlay_paths, int count,
                     genconf_file *base, genconf_file *overlays, char *err, size_t errcap);
void confread_free(genconf_file *base, genconf_file *overlays, int count);

#endif
