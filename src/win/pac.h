#ifndef UTGARD_PAC_H
#define UTGARD_PAC_H

#include <windows.h>
#include <stddef.h>

#define PAC_MAX (4u * 1024u * 1024u)

/* A PAC is untrusted JavaScript. Windows executes it in the WinHTTP
   autoproxy service, never in the application's process. */
typedef struct pac_script pac_script;
pac_script *pac_open(const char *text, size_t length, wchar_t *err, size_t cap);
/* -1: evaluation failed, 0: DIRECT, 1: use the selected VPN. */
int pac_query(pac_script *script, const wchar_t *url, DWORD *error);
void pac_close(pac_script *script);

#endif
