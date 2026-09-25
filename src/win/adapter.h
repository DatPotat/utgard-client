#ifndef UTGARD_ADAPTER_H
#define UTGARD_ADAPTER_H

#include <stddef.h>
#include <wchar.h>

/* 1 while Windows has a network interface of that name that is actually
   there - not one left behind as NotPresent. */
int adapter_present(const wchar_t *alias);

/* The LUID of that present interface, as NET_LUID.Value - a plain number,
   so this header needs no network headers (their union is named
   differently in mingw-w64 and llvm-mingw). */
int adapter_luid(const wchar_t *alias, unsigned long long *luid_value);

/* Wait until neither of Utgard's adapters exists any more, up to ms.
   A process that exited or a service that was deleted does not mean its
   Wintun adapter is gone: Windows removes it a little later. Creating the
   next one before that is what this prevents. 1 when both are gone. */
int adapters_wait_gone(unsigned long ms, wchar_t *msg, size_t cap);

#endif
