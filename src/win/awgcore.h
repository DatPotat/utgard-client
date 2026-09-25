#ifndef UTGARD_AWGCORE_H
#define UTGARD_AWGCORE_H

#include <stddef.h>
#include <wchar.h>

/* The AmneziaWG core: amneziawg.exe and wintun.dll from the official MSI of
   amneziawg-windows-client, unpacked without installing anything, into the
   protected amneziawg\ folder beside utgard.exe. */

/* 1 when the folder holds exactly the pinned files. */
int awgcore_present(void);

/* Download, verify and unpack. Blocking: call from a worker thread. */
int awgcore_install(wchar_t *msg, size_t cap);

#endif
