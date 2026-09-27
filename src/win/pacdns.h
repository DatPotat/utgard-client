#ifndef UTGARD_PACDNS_H
#define UTGARD_PACDNS_H
#include <windows.h>
#include <stddef.h>
/* Observe real system DNS answers; never synthesize fake addresses, because
   PAC dnsResolve/isInNet must see real addresses too. */
int pacdns_start(int *port, wchar_t *err, size_t cap);
int pacdns_name(const unsigned char *address, int ipv6, char name[256]);
#endif
