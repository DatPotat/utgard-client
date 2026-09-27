#include <winsock2.h>
#include <ws2tcpip.h>
#include "pacbridge.h"
#include "pacdns.h"
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

/* Address block: ATYP, address, network-order port (RFC 1928). */
static int address_size(const unsigned char *p, int n)
{
    int k;
    if (n < 1) return 0;
    k = p[0] == 1 ? 7 : p[0] == 4 ? 19 : p[0] == 3 && n >= 2 && p[1] ? 4 + p[1] : 0;
    return k && n >= k ? k : 0;
}

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
    int k = address_size(p, n);
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

static int decision(const unsigned char *p, int n)
{
    char host[256];
    wchar_t url[600], wide[256];
    unsigned short port;
    DWORD error;
    int result = 0, i, domain_known;
    if (!host_port(p, n, host, &port)) return -1;
    domain_known = p[0] == 3;
    {
        int mapped = p[0] != 3 && pacdns_name(p + 1, p[0] == 4, host);
        if (mapped) domain_known = 1;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, wide, 256)) return -1;
        if (mapped) {
            if (port == 80 || port == 443)
                StringCchPrintfW(url, 600, L"%s://%s/", port == 443 ? L"https" : L"http", wide);
            else StringCchPrintfW(url, 600, L"http://%s:%u/", wide, port);
        } else {
    if (port == 443 || port == 80)
        StringCchPrintfW(url, 600, p[0] == 4 ? L"%s://[%s]/" : L"%s://%s/",
                         port == 443 ? L"https" : L"http", wide);
    else
        StringCchPrintfW(url, 600, p[0] == 4 ? L"http://[%s]:%u/" : L"http://%s:%u/", wide, port);
        }
    }
    AcquireSRWLockShared(&script_lock);
    if (!current_count) result = -1;
    for (i = 0; i < current_count; i++) {
        int one = pac_query(current_scripts[i], url, &error);
        if (one < 0) { result = -1; break; }
        if (one > 0) { result = 1; break; }
    }
    /* Browsers may resolve through their own DoH connection. In that case
       the TUN gives us only an address and a domain-based PAC would return
       DIRECT for the address, silently bypassing its rule. Keep unknown web
       destinations inside the selected profile; explicit site/app and local
       network rules have already been handled before this catch-all. */
    if (!result && !domain_known && (port == 80 || port == 443)) result = 1;
    ReleaseSRWLockShared(&script_lock);
    return result;
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
    ULONGLONG last = GetTickCount64();
    while ((a_open || b_open) && gen == InterlockedCompareExchange(&generation, 0, 0) &&
           GetTickCount64() - last < 300000) {
        fd_set f;
        struct timeval tv = { 1, 0 };
        int i;
        FD_ZERO(&f); if (a_open) FD_SET(a, &f); if (b_open) FD_SET(b, &f);
        if (select(0, &f, NULL, NULL, &tv) <= 0) continue;
        for (i = 0; i < 2; i++) {
            SOCKET from = i ? b : a, to = i ? a : b;
            if (FD_ISSET(from, &f)) {
                int n = recv(from, buf, sizeof buf, 0);
                if (n < 0) return;
                if (!n) { if (i) b_open = 0; else a_open = 0; shutdown(to, SD_SEND); }
                else { if (!transfer(to, buf, n, 1)) return; last = GetTickCount64(); }
            }
        }
    }
}

/* A SOCKS UDP association may carry several destinations. Connected sockets
   pin each return path to its peer, preventing unsolicited packet injection. */
typedef struct {
    SOCKET data, control;
    unsigned char addr[259];
    int size, proxy;
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
    udp_peer peers[32];
    SOCKET incoming;
    unsigned short port;
    unsigned char reply[10] = { 5, 0, 0, 1, 127, 0, 0, 1, 0, 0 };
    unsigned char *packet = (unsigned char *)malloc(65536);
    struct sockaddr_in client;
    int have_client = 0, i;
    ULONGLONG last = GetTickCount64();
    for (i = 0; i < 32; i++) { ZeroMemory(&peers[i], sizeof peers[i]); peers[i].data = peers[i].control = INVALID_SOCKET; }
    incoming = loopback(SOCK_DGRAM, &port);
    if (incoming == INVALID_SOCKET || !packet) goto done;
    reply[8] = (unsigned char)(port >> 8); reply[9] = (unsigned char)port;
    if (!transfer(control, reply, sizeof reply, 1)) goto done;
    while (gen == InterlockedCompareExchange(&generation, 0, 0) && GetTickCount64() - last < 120000) {
        fd_set f;
        struct timeval tv = { 1, 0 };
        FD_ZERO(&f); FD_SET(control, &f); FD_SET(incoming, &f);
        for (i = 0; i < 32; i++) if (peers[i].data != INVALID_SOCKET) FD_SET(peers[i].data, &f);
        if (select(0, &f, NULL, NULL, &tv) <= 0) continue;
        if (FD_ISSET(control, &f)) break;
        if (FD_ISSET(incoming, &f)) {
            struct sockaddr_in from;
            int flen = sizeof from, n, asize, slot = -1;
            udp_peer *p;
            n = recvfrom(incoming, (char *)packet, 65536, 0, (struct sockaddr *)&from, &flen);
            if (n < 4 || packet[0] || packet[1] || packet[2] || from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
            if (have_client && (from.sin_port != client.sin_port || from.sin_addr.s_addr != client.sin_addr.s_addr)) continue;
            asize = address_size(packet + 3, n - 3);
            if (!asize) continue;
            if (!have_client) { client = from; have_client = 1; }
            for (i = 0; i < 32; i++) if (peers[i].data != INVALID_SOCKET && peers[i].size == asize && !memcmp(peers[i].addr, packet + 3, asize)) { slot = i; break; }
            if (slot < 0) {
                unsigned char bound[259], any[7] = { 1, 0, 0, 0, 0, 0, 0 };
                int bs;
                slot = 0;
                for (i = 0; i < 32; i++) if (peers[i].data == INVALID_SOCKET || peers[i].last < peers[slot].last) { slot = i; if (peers[i].data == INVALID_SOCKET) break; }
                p = &peers[slot]; udp_close(p);
                p->proxy = decision(packet + 3, asize);
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
            if (p->proxy) send(p->data, (char *)packet, n, 0);
            else send(p->data, (char *)packet + 3 + asize, n - 3 - asize, 0);
            p->last = last = GetTickCount64();
        }
        for (i = 0; i < 32; i++) if (peers[i].data != INVALID_SOCKET && FD_ISSET(peers[i].data, &f)) {
            udp_peer *p = &peers[i];
            int offset = p->proxy ? 0 : 3 + p->size;
            int n = recv(p->data, (char *)packet + offset, 65507 - offset, 0);
            if (n < 0) { udp_close(p); continue; }
            if (!p->proxy) { packet[0] = packet[1] = packet[2] = 0; memcpy(packet + 3, p->addr, p->size); }
            if (have_client) sendto(incoming, (char *)packet, n + offset, 0, (struct sockaddr *)&client, sizeof client);
            p->last = last = GetTickCount64();
        }
    }
done:
    for (i = 0; i < 32; i++) udp_close(&peers[i]);
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
    int n, which, bs;
    free(c);
    timeout_socket(s);
    if (!authenticate(s) || !transfer(s, header, 3, 0) || header[0] != 5 || header[2]) goto done;
    n = read_address(s, addr);
    if (!n) goto done;
    if (header[1] == 3) { relay_udp(s, gen); goto done; }
    if (header[1] != 1) { reply[1] = 7; transfer(s, reply, sizeof reply, 1); goto done; }
    which = decision(addr, n);
    if (which == 0) remote = connect_to(addr, n, SOCK_STREAM);
    else if (which == 1) remote = upstream(1, addr, n, bound, &bs);
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
        if (InterlockedIncrement(&workers) > 128) { InterlockedDecrement(&workers); closesocket(s); continue; }
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
        in->pac_port = listen_port;
        if (!pacdns_start(&in->pac_dns_port, err, cap)) return 0;
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
}
void pacbridge_disconnect(void) { InterlockedIncrement(&generation); }
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
