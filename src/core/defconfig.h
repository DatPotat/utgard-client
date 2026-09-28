#ifndef UTGARD_DEFCONFIG_H
#define UTGARD_DEFCONFIG_H

#include <stddef.h>

/* The base sing-box config: the one copy of it. Written as config.json when
   that file is missing, and used by the tests as the generator's base. */
extern const char   utgard_default_config[];
extern const size_t utgard_default_config_len;

#endif
