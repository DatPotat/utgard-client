#ifndef UTGARD_PACUDP_H
#define UTGARD_PACUDP_H

#include <stddef.h>

int pacudp_address_size(const unsigned char *address, size_t length);
/* Replace the address in an incoming SOCKS5 UDP datagram with original.
   Returns the new datagram length, or 0 for malformed/oversize input. */
size_t pacudp_restore(unsigned char *packet, size_t length, size_t capacity,
                      const unsigned char *original, size_t original_length);

#endif
