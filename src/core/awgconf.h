#ifndef UTGARD_AWGCONF_H
#define UTGARD_AWGCONF_H

#include <stddef.h>
#include "link.h"

/* The wg-quick text the AmneziaWG service reads for one profile. Portable:
   no Windows headers, tested on the host.

   Only what the tunnel needs goes in. Table = off, so the service adds no
   routes and no firewall of its own - sing-box decides what goes through it.
   IPv4 only: IPv6 addresses are dropped, AllowedIPs is 0.0.0.0/0.
   Never DNS, PreUp/PostUp/PreDown/PostDown or ListenPort: the service would
   run those commands or change system settings. Endpoint is the address the
   caller already resolved, the same one it excludes from the TUN routes. */

#define AWGCONF_MAX (LINK_AWG_MAX + 2048)

int awgconf_build(const link_profile *p, const char *endpoint_ip,
                  char *out, size_t cap, char *err, size_t errcap);

/* The text carries the private key: clear it once handed over. */
void awgconf_wipe(char *text, size_t cap);

#endif
