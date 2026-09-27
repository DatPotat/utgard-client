#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#include "pacbridge.h"
#include "pacdns.h"
#include "pacstatus.h"
#include "pacudp.h"
#include "paclogic.h"
#include <bcrypt.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SOCKET listener = INVALID_SOCKET;
static unsigned short listen_port, vpn_port;
static char secret[65], own_path[2048];
static SRWLOCK script_lock = SRWLOCK_INIT;
static pac_script *current_scripts[PAC_ITEMS_MAX];
static int current_count;
static volatile LONG generation, workers, enabled;
static volatile LONG decision_generation;
static SRWLOCK decision_lock = SRWLOCK_INIT;
static struct {
    wchar_t url[600];
    LONG generation;
    ULONGLONG until, serial;
    int result;
} decision_cache[512];
static ULONGLONG decision_serial;

static int cached_decision(const wchar_t *url, int *result)
{
    size_t i;
    LONG gen = InterlockedCompareExchange(&decision_generation, 0, 0);
    ULONGLONG now = GetTickCount64();
    int found = 0;
    AcquireSRWLockShared(&decision_lock);
    for (i = 0; i < 512; i++)
        if (decision_cache[i].generation == gen && decision_cache[i].until > now &&
            !wcscmp(decision_cache[i].url, url)) {
            *result = decision_cache[i].result; found = 1; break;
        }
    ReleaseSRWLockShared(&decision_lock);
    return found;
}

static void cache_decision(const wchar_t *url, int result, LONG expected_generation)
{
    size_t i, slot = 0;
    AcquireSRWLockExclusive(&decision_lock);
    if (InterlockedCompareExchange(&decision_generation, 0, 0) != expected_generation) {
        ReleaseSRWLockExclusive(&decision_lock);
        return;
    }
    for (i = 0; i < 512; i++) {
        if (!decision_cache[i].url[0] || !wcscmp(decision_cache[i].url, url)) {
            slot = i; break;
        }
        if (decision_cache[i].serial < decision_cache[slot].serial) slot = i;
    }
    StringCchCopyW(decision_cache[slot].url, 600, url);
    decision_cache[slot].generation = expected_generation;
    decision_cache[slot].until = GetTickCount64() + 60000;
    decision_cache[slot].serial = ++decision_serial;
    decision_cache[slot].result = result;
    ReleaseSRWLockExclusive(&decision_lock);
}

typedef struct { const wchar_t *url; DWORD error; } script_context;
static int script_evaluate(size_t index, void *opaque)
{
    script_context *context = (script_context *)opaque;
    DWORD error = 0;
    int result = pac_query(current_scripts[index], context->url, &error);
    if (result < 0) context->error = error;
    return result;
}

void pacbridge_flush_decisions(void)
{
    AcquireSRWLockExclusive(&decision_lock);
    InterlockedIncrement(&decision_generation);
    ZeroMemory(decision_cache, sizeof decision_cache);
    decision_serial = 0;
    ReleaseSRWLockExclusive(&decision_lock);
}

static int transfer(SOCKET s, void *data, int length, int writing)
{
    char *p = (char *)data;
    while (length > 0) {
        int n = writing ? send(s, p, length, 0) : recv(s, p, length, 0);
        if (n <= 0) return 0;
        p += n;
        length -= n;
    }
    return 1;
}

static void timeout_socket(SOCKET s)
{
    DWORD ms = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof ms);
}

static SOCKET loopback(int type, unsigned short *port)
{
    struct sockaddr_in a;
    int size = sizeof a, exclusive = 1;
    SOCKET s = socket(AF_INET, type, 0);
    if (s == INVALID_SOCKET) return s;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    ZeroMemory(&a, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || getsockname(s, (struct sockaddr *)&a, &size)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    *port = ntohs(a.sin_port);
    timeout_socket(s);
    return s;
}

static int loopback_pair_port(unsigned short *port)
{
    SOCKET tcp = INVALID_SOCKET, udp = INVALID_SOCKET;
    struct sockaddr_in a;
    int size = sizeof a, exclusive = 1;
    tcp = socket(AF_INET, SOCK_STREAM, 0);
    udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (tcp == INVALID_SOCKET || udp == INVALID_SOCKET) goto fail;
    setsockopt(tcp, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    setsockopt(udp, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(tcp, (struct sockaddr *)&a, sizeof a) ||
        getsockname(tcp, (struct sockaddr *)&a, &size) ||
        bind(udp, (struct sockaddr *)&a, sizeof a)) goto fail;
    *port = ntohs(a.sin_port);
    closesocket(tcp); closesocket(udp);
    return 1;
fail:
    if (tcp != INVALID_SOCKET) closesocket(tcp);
    if (udp != INVALID_SOCKET) closesocket(udp);
    return 0;
}

/* Address block: ATYP, address, network-order port (RFC 1928). */
static int read_address(SOCKET s, unsigned char *p)
{
    int rest;
    if (!transfer(s, p, 1, 0)) return 0;
    if (p[0] == 1) rest = 6;
    else if (p[0] == 4) rest = 18;
    else if (p[0] == 3) {
        if (!transfer(s, p + 1, 1, 0) || !p[1]) return 0;
        return transfer(s, p + 2, p[1] + 2, 0) ? p[1] + 4 : 0;
    } else return 0;
    return transfer(s, p + 1, rest, 0) ? rest + 1 : 0;
}

static int host_port(const unsigned char *p, int n, char *host, unsigned short *port)
{
    int k = pacudp_address_size(p, (size_t)n);
    if (!k) return 0;
    if (p[0] == 3) {
        int i;
        /* Reject URL metacharacters, embedded NULs and control characters.
           IDNs arrive from sing-box in ASCII/Punycode. */
        for (i = 0; i < p[1]; i++) {
            unsigned char c = p[2 + i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return 0;
        }
        memcpy(host, p + 2, p[1]);
        host[p[1]] = 0;
    } else if (!inet_ntop(p[0] == 1 ? AF_INET : AF_INET6, p + 1, host, 256)) return 0;
    *port = (unsigned short)((p[k - 2] << 8) | p[k - 1]);
    return *port != 0;
}

static int evaluate_host(const char *host, unsigned short port, int domain)
{
    wchar_t url[600], wide[256];
    script_context context;
    int result = 0, failed = 0;
    LONG cache_generation;
    if (!host || !host[0] ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, wide, 256))
        return 0;
    if (port == 80 || port == 443)
        StringCchPrintfW(url, 600,
            !domain && strchr(host, ':') ? L"%s://[%s]/" : L"%s://%s/",
            port == 443 ? L"https" : L"http", wide);
    else
        StringCchPrintfW(url, 600,
            !domain && strchr(host, ':') ? L"http://[%s]:%u/" : L"http://%s:%u/",
            wide, port);
    if (cached_decision(url, &result)) return result;
    cache_generation = InterlockedCompareExchange(&decision_generation, 0, 0);
    context.url = url; context.error = 0;
    AcquireSRWLockShared(&script_lock);
    result = paclogic_any((size_t)current_count, script_evaluate, &context, &failed);
    ReleaseSRWLockShared(&script_lock);
    if (failed) pacstatus_evaluation_error(context.error);
    if (!failed) cache_decision(url, result, cache_generation);
    return result;
}

typedef struct { unsigned short port; } route_context;
static int route_evaluate(const char *host, int domain, void *opaque)
{ return evaluate_host(host, ((route_context *)opaque)->port, domain); }

int pacbridge_decide_host(const char *host, unsigned short port)
{
    size_t i, length = host ? strlen(host) : 0;
    if (!length || length > 253 || !port) return 0;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return evaluate_host(host, port, 1);
}

static int decision(const unsigned char *p, int n, char proxy_host[256])
{
    char host[256], names[16][256];
    unsigned short port;
    int name_count = 0;
    route_context context;
    paclogic_result result;
    if (proxy_host) proxy_host[0] = 0;
    if (!host_port(p, n, host, &port)) return 0;
    if (p[0] == 3) {
        StringCchCopyA(names[0], 256, host);
        name_count = 1;
    } else {
        name_count = pacdns_names(p + 1, p[0] == 4, names, 16);
    }
    context.port = port;
    result = paclogic_route(p[0] == 3, host, names, name_count, route_evaluate, &context);
    if (result.rewrite && proxy_host) StringCchCopyA(proxy_host, 256, result.rewrite_host);
    return result.vpn;
}

static int domain_address(const char *host, unsigned short port, unsigned char out[259])
{
    size_t length = host ? strlen(host) : 0;
    if (!length || length > 255) return 0;
    out[0] = 3; out[1] = (unsigned char)length;
    memcpy(out + 2, host, length);
    out[2 + length] = (unsigned char)(port >> 8);
    out[3 + length] = (unsigned char)port;
    return (int)length + 4;
}

static SOCKET connect_to(const unsigned char *p, int n, int type)
{
    char host[256], service[8];
    unsigned short port;
    struct addrinfo hints, *list = NULL, *a;
    SOCKET s = INVALID_SOCKET;
    if (!host_port(p, n, host, &port)) return s;
    snprintf(service, sizeof service, "%u", port);
    ZeroMemory(&hints, sizeof hints);
    hints.ai_socktype = type;
    if (getaddrinfo(host, service, &hints, &list)) return s;
    for (a = list; a; a = a->ai_next) {
        u_long nonblock = 1;
        int status = 0, size = sizeof status;
        fd_set f;
        struct timeval tv = { 5, 0 };
        s = socket(a->ai_family, type, 0);
        if (s == INVALID_SOCKET) continue;
        ioctlsocket(s, FIONBIO, &nonblock);
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) && WSAGetLastError() != WSAEWOULDBLOCK) goto next;
        FD_ZERO(&f); FD_SET(s, &f);
        if (select(0, NULL, &f, NULL, &tv) <= 0 ||
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&status, &size) || status) goto next;
        nonblock = 0;
        ioctlsocket(s, FIONBIO, &nonblock);
        timeout_socket(s);
        break;
next:
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(list);
    return s;
}

static SOCKET upstream(int command, const unsigned char *addr, int n, unsigned char *bound, int *bound_size)
{
    unsigned char target[7] = { 1, 127, 0, 0, 1, 0, 0 };
    unsigned char h[80] = { 5, 1, 2 }, reply[4];
    SOCKET s;
    target[5] = (unsigned char)(vpn_port >> 8); target[6] = (unsigned char)vpn_port;
    s = connect_to(target, 7, SOCK_STREAM);
    if (s == INVALID_SOCKET) return s;
    if (!transfer(s, h, 3, 1) || !transfer(s, reply, 2, 0) || reply[0] != 5 || reply[1] != 2) goto fail;
    h[0] = 1; h[1] = 6; memcpy(h + 2, "utgard", 6); h[8] = 64; memcpy(h + 9, secret, 64);
    if (!transfer(s, h, 73, 1) || !transfer(s, reply, 2, 0) || reply[0] != 1 || reply[1]) goto fail;
    h[0] = 5; h[1] = (unsigned char)command; h[2] = 0;
    if (!transfer(s, h, 3, 1) || !transfer(s, (void *)addr, n, 1) ||
        !transfer(s, reply, 3, 0) || reply[0] != 5 || reply[1] || reply[2]) goto fail;
    *bound_size = read_address(s, bound);
    if (!*bound_size) goto fail;
    return s;
fail:
    closesocket(s);
    return INVALID_SOCKET;
}

static int authenticate(SOCKET s)
{
    unsigned char b[257], answer[2] = { 5, 2 };
    int i, found = 0;
    if (!transfer(s, b, 2, 0) || b[0] != 5 || !b[1]) return 0;
    i = b[1];
    if (!transfer(s, b, i, 0)) return 0;
    while (i--) if (b[i] == 2) found = 1;
    if (!found) answer[1] = 255;
    if (!transfer(s, answer, 2, 1) || !found) return 0;
    if (!transfer(s, b, 2, 0) || b[0] != 1 || b[1] != 6 ||
        !transfer(s, b, 7, 0) || memcmp(b, "utgard", 6) || b[6] != 64 ||
        !transfer(s, b, 64, 0)) return 0;
    found = 0;
    for (i = 0; i < 64; i++) found |= b[i] ^ (unsigned char)secret[i];
    answer[0] = 1; answer[1] = found ? 1 : 0;
    return transfer(s, answer, 2, 1) && !found;
}

static void relay_tcp(SOCKET a, SOCKET b, LONG gen)
{
    char buf[16384];
    int a_open = 1, b_open = 1;
    ULONGLONG half_closed = 0;
    BOOL keepalive = TRUE;
    setsockopt(a, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof keepalive);
    setsockopt(b, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof keepalive);
    while ((a_open || b_open) && gen == InterlockedCompareExchange(&generation, 0, 0)) {
        fd_set f;
        struct timeval tv = { 1, 0 };
        int i;
        FD_ZERO(&f); if (a_open) FD_SET(a, &f); if (b_open) FD_SET(b, &f);
        if (select(0, &f, NULL, NULL, &tv) <= 0) {
            if ((!a_open || !b_open) && half_closed &&
                GetTickCount64() - half_closed >= 60000) return;
            continue;
        }
        for (i = 0; i < 2; i++) {
            SOCKET from = i ? b : a, to = i ? a : b;
            if (FD_ISSET(from, &f)) {
                int n = recv(from, buf, sizeof buf, 0);
                if (n < 0) return;
                if (!n) {
                    if (i) b_open = 0; else a_open = 0;
                    if (!half_closed) half_closed = GetTickCount64();
                    shutdown(to, SD_SEND);
                }
                else {
                    if (half_closed) half_closed = GetTickCount64();
                    if (!transfer(to, buf, n, 1)) return;
                }
            }
        }
    }
}

/* A SOCKS UDP association may carry several destinations. Connected sockets
   pin each return path to its peer, preventing unsolicited packet injection. */
typedef struct {
    SOCKET data, control;
    unsigned char addr[259];
    unsigned char route[259];
    int size, route_size, proxy;
    ULONGLONG last;
} udp_peer;

static void udp_close(udp_peer *p)
{
    if (p->data != INVALID_SOCKET) closesocket(p->data);
    if (p->control != INVALID_SOCKET) closesocket(p->control);
    p->data = p->control = INVALID_SOCKET;
}

static void relay_udp(SOCKET control, LONG gen)
{
    udp_peer peers[512];
    SOCKET incoming;
    unsigned short port;
    unsigned char reply[10] = { 5, 0, 0, 1, 127, 0, 0, 1, 0, 0 };
    unsigned char *packet = (unsigned char *)malloc(65536);
    struct sockaddr_in client;
    int have_client = 0, i;
    ULONGLONG last = GetTickCount64();
    for (i = 0; i < 512; i++) { ZeroMemory(&peers[i], sizeof peers[i]); peers[i].data = peers[i].control = INVALID_SOCKET; }
    incoming = loopback(SOCK_DGRAM, &port);
    if (incoming == INVALID_SOCKET || !packet) goto done;
    reply[8] = (unsigned char)(port >> 8); reply[9] = (unsigned char)port;
    if (!transfer(control, reply, sizeof reply, 1)) goto done;
    while (gen == InterlockedCompareExchange(&generation, 0, 0)) {
        fd_set f;
        struct timeval tv = { 1, 0 };
        FD_ZERO(&f); FD_SET(control, &f); FD_SET(incoming, &f);
        for (i = 0; i < 512; i++) if (peers[i].data != INVALID_SOCKET) FD_SET(peers[i].data, &f);
        if (select(0, &f, NULL, NULL, &tv) <= 0) continue;
        if (FD_ISSET(control, &f)) break;
        if (FD_ISSET(incoming, &f)) {
            struct sockaddr_in from;
            int flen = sizeof from, n, asize, slot = -1;
            udp_peer *p;
            n = recvfrom(incoming, (char *)packet, 65536, 0, (struct sockaddr *)&from, &flen);
            if (n < 4 || packet[0] || packet[1] || packet[2] || from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
            if (have_client && (from.sin_port != client.sin_port || from.sin_addr.s_addr != client.sin_addr.s_addr)) continue;
            asize = pacudp_address_size(packet + 3, (size_t)(n - 3));
            if (!asize) continue;
            if (!have_client) { client = from; have_client = 1; }
            for (i = 0; i < 512; i++) if (peers[i].data != INVALID_SOCKET && peers[i].size == asize && !memcmp(peers[i].addr, packet + 3, asize)) { slot = i; break; }
            if (slot < 0) {
                unsigned char bound[259], any[7] = { 1, 0, 0, 0, 0, 0, 0 };
                int bs;
                slot = 0;
                for (i = 0; i < 512; i++) if (peers[i].data == INVALID_SOCKET || peers[i].last < peers[slot].last) { slot = i; if (peers[i].data == INVALID_SOCKET) break; }
                p = &peers[slot];
                if (p->data != INVALID_SOCKET) pacstatus_udp_evict();
                udp_close(p);
                {
                    char proxy_host[256];
                    char ignored[256];
                    unsigned short target_port;
                    p->proxy = decision(packet + 3, asize, proxy_host);
                    p->route_size = asize;
                    memcpy(p->route, packet + 3, asize);
                    if (p->proxy > 0 && proxy_host[0] &&
                        host_port(packet + 3, asize, ignored, &target_port)) {
                        int routed = domain_address(proxy_host, target_port, p->route);
                        if (routed) p->route_size = routed;
                    }
                }
                if (p->proxy < 0) continue;
                p->size = asize; memcpy(p->addr, packet + 3, asize);
                if (p->proxy) {
                    p->control = upstream(3, any, sizeof any, bound, &bs);
                    /* The backend is local; never follow a UDP relay address
                       outside loopback even if a response is malformed. */
                    if (p->control == INVALID_SOCKET || bs != 7 || bound[0] != 1 ||
                        bound[1] != 127 || bound[2] || bound[3] || bound[4] != 1) { udp_close(p); continue; }
                    p->data = connect_to(bound, bs, SOCK_DGRAM);
                } else p->data = connect_to(p->addr, p->size, SOCK_DGRAM);
                if (p->data == INVALID_SOCKET) { udp_close(p); continue; }
            }
            p = &peers[slot];
            if (p->proxy) {
                int payload = n - 3 - asize;
                if (3 + p->route_size + payload > 65507) continue;
                memmove(packet + 3 + p->route_size, packet + 3 + asize, payload);
                memcpy(packet + 3, p->route, p->route_size);
                send(p->data, (char *)packet, 3 + p->route_size + payload, 0);
            }
            else send(p->data, (char *)packet + 3 + asize, n - 3 - asize, 0);
            p->last = last = GetTickCount64();
        }
        for (i = 0; i < 512; i++) if (peers[i].data != INVALID_SOCKET && FD_ISSET(peers[i].data, &f)) {
            udp_peer *p = &peers[i];
            int offset = p->proxy ? 0 : 3 + p->size;
            int n = recv(p->data, (char *)packet + offset, 65507 - offset, 0);
            if (n < 0) { udp_close(p); continue; }
            if (!p->proxy) {
                packet[0] = packet[1] = packet[2] = 0;
                memcpy(packet + 3, p->addr, p->size);
                n += offset;
            } else {
                size_t restored = pacudp_restore(packet, (size_t)n, 65536,
                                                  p->addr, (size_t)p->size);
                if (!restored) continue;
                n = (int)restored;
            }
            if (have_client) sendto(incoming, (char *)packet, n, 0, (struct sockaddr *)&client, sizeof client);
            p->last = last = GetTickCount64();
        }
    }
done:
    for (i = 0; i < 512; i++) udp_close(&peers[i]);
    if (incoming != INVALID_SOCKET) closesocket(incoming);
    free(packet);
}

typedef struct { SOCKET socket; LONG gen; } connection;
static DWORD WINAPI client_thread(void *arg)
{
    connection *c = (connection *)arg;
    SOCKET s = c->socket, remote = INVALID_SOCKET;
    LONG gen = c->gen;
    unsigned char header[3], addr[259], bound[259], reply[10] = { 5, 1, 0, 1, 127, 0, 0, 1, 0, 0 };
    int n, which, bs, target_n;
    unsigned char target[259];
    char proxy_host[256];
    free(c);
    timeout_socket(s);
    if (!authenticate(s) || !transfer(s, header, 3, 0) || header[0] != 5 || header[2]) goto done;
    n = read_address(s, addr);
    if (!n) goto done;
    if (header[1] == 3) { relay_udp(s, gen); goto done; }
    if (header[1] != 1) { reply[1] = 7; transfer(s, reply, sizeof reply, 1); goto done; }
    which = decision(addr, n, proxy_host);
    target_n = n; memcpy(target, addr, n);
    if (which == 1 && proxy_host[0]) {
        char ignored[256];
        unsigned short target_port;
        if (host_port(addr, n, ignored, &target_port)) {
            int routed = domain_address(proxy_host, target_port, target);
            if (routed) target_n = routed;
        }
    }
    if (which == 0) remote = connect_to(addr, n, SOCK_STREAM);
    else if (which == 1) remote = upstream(1, target, target_n, bound, &bs);
    if (remote != INVALID_SOCKET) reply[1] = 0;
    if (transfer(s, reply, sizeof reply, 1) && !reply[1]) relay_tcp(s, remote, gen);
done:
    if (remote != INVALID_SOCKET) closesocket(remote);
    closesocket(s);
    InterlockedDecrement(&workers);
    return 0;
}

static DWORD WINAPI accept_thread(void *unused)
{
    (void)unused;
    for (;;) {
        SOCKET s = accept(listener, NULL, NULL);
        connection *c;
        HANDLE thread;
        if (s == INVALID_SOCKET) break;
        if (InterlockedIncrement(&workers) > 4096) {
            pacstatus_worker_cap();
            InterlockedDecrement(&workers); closesocket(s); continue;
        }
        c = (connection *)malloc(sizeof *c);
        if (!c) { InterlockedDecrement(&workers); closesocket(s); continue; }
        c->socket = s; c->gen = InterlockedCompareExchange(&generation, 0, 0);
        thread = CreateThread(NULL, 0, client_thread, c, 0, NULL);
        if (thread) CloseHandle(thread);
        else { free(c); InterlockedDecrement(&workers); closesocket(s); }
    }
    return 0;
}

int pacbridge_prepare(genconf_input *in, int use_pac, int download_proxy,
                      wchar_t *err, size_t cap)
{
    in->pac_port = 0;
    in->pac_dns_port = 0;
    in->pac_dns_vpn_port = 0;
    in->pac_dns_sys_port = 0;
    in->vpn_proxy_port = 0;
    in->proxy_password = NULL;
    in->client_exe = NULL;
    if (!use_pac && !download_proxy) return 1;
    if (!vpn_port) {
        unsigned char random[32];
        wchar_t path[1024];
        WSADATA data;
        SOCKET reserve;
        int i;
        if (WSAStartup(MAKEWORD(2, 2), &data)) goto fail;
        if (BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) goto fail;
        for (i = 0; i < 32; i++) snprintf(secret + 2 * i, 3, "%02x", random[i]);
        if (!GetModuleFileNameW(NULL, path, 1024) || !WideCharToMultiByte(CP_UTF8, 0, path, -1, own_path, sizeof own_path, NULL, NULL)) goto fail;
        reserve = loopback(SOCK_STREAM, &vpn_port);
        if (reserve == INVALID_SOCKET) goto fail;
        closesocket(reserve); /* sing-box binds this; a collision fails startup */
    }
    if (use_pac && listener == INVALID_SOCKET) {
        HANDLE thread;
        listener = loopback(SOCK_STREAM, &listen_port);
        if (listener == INVALID_SOCKET || listen(listener, 32)) goto fail;
        thread = CreateThread(NULL, 0, accept_thread, NULL, 0, NULL);
        if (!thread) goto fail;
        CloseHandle(thread);
    }
    if (use_pac) {
        unsigned short dns_vpn, dns_sys;
        in->pac_port = listen_port;
        if (!loopback_pair_port(&dns_vpn) || !loopback_pair_port(&dns_sys)) return 0;
        in->pac_dns_vpn_port = dns_vpn;
        in->pac_dns_sys_port = dns_sys;
        if (
            !pacdns_start(&in->pac_dns_port,
                          (unsigned short)in->pac_dns_vpn_port,
                          (unsigned short)in->pac_dns_sys_port, err, cap)) return 0;
    }
    in->vpn_proxy_port = vpn_port;
    in->proxy_password = secret;
    in->client_exe = own_path;
    return 1;
fail:
    if (listener != INVALID_SOCKET) { closesocket(listener); listener = INVALID_SOCKET; }
    if (err && cap) StringCchCopyW(err, cap, L"Не удалось создать локальный PAC-прокси");
    return 0;
}

void pacbridge_activate(pac_script **scripts, int count)
{
    int i;
    AcquireSRWLockExclusive(&script_lock);
    for (i = 0; i < current_count; i++) pac_close(current_scripts[i]);
    ZeroMemory(current_scripts, sizeof current_scripts);
    current_count = count < 0 ? 0 : (count > PAC_ITEMS_MAX ? PAC_ITEMS_MAX : count);
    for (i = 0; i < current_count; i++) current_scripts[i] = scripts[i];
    InterlockedExchange(&enabled, current_count != 0);
    ReleaseSRWLockExclusive(&script_lock);
    pacbridge_flush_decisions();
}
void pacbridge_disconnect(void) { InterlockedIncrement(&generation); pacbridge_flush_decisions(); }
int pacbridge_active(void) { return InterlockedCompareExchange(&enabled, 0, 0) != 0; }

int pacbridge_proxy(unsigned short *port, char password[65])
{
    if (!vpn_port || !secret[0] || !port || !password) return 0;
    *port = vpn_port;
    memcpy(password, secret, 65);
    return 1;
}

int pacbridge_count(void)
{
    int count;
    AcquireSRWLockShared(&script_lock);
    count = current_count;
    ReleaseSRWLockShared(&script_lock);
    return count;
}
