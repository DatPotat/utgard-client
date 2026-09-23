#ifndef UTGARD_NET_H
#define UTGARD_NET_H

#include <windows.h>

#define NET_BODY_MAX (1024 * 1024)

/* Fetch an http/https URL. On success *body points at a heap block the caller
   frees with free(), NUL-terminated for convenience, with *len the byte count.
   Deliberately dumb: no parsing happens here, because the response is
   untrusted and every parser in this project lives in a module that can be
   fuzzed on the host. */
int net_fetch(const wchar_t *url, char **body, size_t *len,
              wchar_t *err, size_t errcap);

/* Round-trip time to a server in milliseconds, or -1 when it does not answer.
   TCP protocols get a real connect to the real port, which is what matters.
   Hysteria2 lives on UDP, where a TCP connect proves nothing, so it gets an
   ICMP echo instead - that measures the host, not the port, and a server that
   drops ICMP will show as unreachable while working fine. */
/* Stream a download straight to a file. Separate from net_fetch because a
   release archive is tens of megabytes and must not sit in memory. */
int net_download(const wchar_t *url, const wchar_t *path,
                 wchar_t *err, size_t errcap);

/* Where an https URL redirects to, without following it: HEAD request, the
   Location header of a 3xx answer. Used for GitHub's releases/latest. */
int net_redirect(const wchar_t *url, wchar_t *location, size_t cap,
                 wchar_t *err, size_t errcap);

/* IPv4 addresses of a host, through the system resolver - the same path the
   generated config gives the proxy servers, which route to "local" so they can
   be resolved before the tunnel exists. Returns how many were written. */
int net_resolve4(const char *host_utf8, char out[][16], int max);

int net_probe(const char *host_utf8, int port, int use_icmp, int timeout_ms);

#endif
