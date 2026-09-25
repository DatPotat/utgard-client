#ifndef UTGARD_SHELLOPEN_H
#define UTGARD_SHELLOPEN_H

#include <windows.h>

/* Open a URL or file as the signed-in user, not with our elevated token.
   0 when Explorer is not there to ask (no shell, or it failed). COM must be
   initialised on the calling thread. */
int shell_open_unelevated(const wchar_t *target);

#endif
