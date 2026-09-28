#ifndef UTGARD_PACDNS_H
#define UTGARD_PACDNS_H
#include <windows.h>
#include <stddef.h>
/* Observe real system DNS answers; never synthesize fake addresses, because
   PAC dnsResolve/isInNet must see real addresses too. */
int pacdns_start(int *port, unsigned short vpn_port, unsigned short sys_port,
                 wchar_t *err, size_t cap);
int pacdns_names(const unsigned char *address, int ipv6,
                 char names[][256], int max_names);
#endif
