#ifndef UTGARD_ADAPTER_H
#define UTGARD_ADAPTER_H

#include <stddef.h>
#include <wchar.h>

/* The LUID, as NET_LUID.Value, of the network interface of that name that
   is actually there - not one left behind as NotPresent. A plain number,
   so this header needs no network headers (their union is named
   differently in mingw-w64 and llvm-mingw). */
int adapter_luid(const wchar_t *alias, unsigned long long *luid_value);

/* Start NetSetupSvc if it is stopped and wait until it runs (up to 15 s).
   Needed before any Wintun adapter is created. 1 when running, or when its
   state cannot even be read; 0 with msg when it is disabled or will not
   start. The startup type is never touched. */
int netsetup_ensure(wchar_t *msg, size_t cap);

#endif
