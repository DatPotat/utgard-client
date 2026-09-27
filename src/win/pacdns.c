#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "pacdns.h"
#include "tunnames.h"
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static SRWLOCK cache_lock = SRWLOCK_INIT;
static struct { unsigned char ip[16]; int family; char name[256]; ULONGLONG until, serial; } cache[2048];
static ULONGLONG serial;
static SOCKADDR_STORAGE resolver;
static int resolver_size, local_port;
static SRWLOCK resolver_lock = SRWLOCK_INIT;
static volatile LONG jobs;

static unsigned get16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }

static int dns_name(const unsigned char *p, size_t size, size_t *at, char name[256])
{
    size_t pos = *at, written = 0;
    int jumps = 0, indirect = 0;
    while (pos < size) {
        unsigned n = p[pos++];
        if (!n) { if (!indirect) *at = pos; name[written] = 0; return written != 0; }
        if ((n & 0xc0) == 0xc0) {
            if (pos >= size || ++jumps > 16) return 0;
            if (!indirect) *at = pos + 1;
            pos = ((n & 63) << 8) | p[pos]; indirect = 1;
        } else {
            unsigned i;
            if (n > 63 || pos + n > size || written + n + 1 >= 256) return 0;
            if (written) name[written++] = '.';
            for (i = 0; i < n; i++) {
                unsigned char c = p[pos++];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
                name[written++] = (char)c;
            }
        }
    }
    return 0;
}

static void remember(const unsigned char *packet, int length)
{
    size_t at = 12;
    char name[256], owner[256];
    unsigned i, answers;
    if (length < 12 || get16(packet + 4) != 1 || !(packet[2] & 0x80) || (packet[3] & 15) ||
        !dns_name(packet, length, &at, name) || at + 4 > (size_t)length) return;
    at += 4;
    answers = get16(packet + 6);
    for (i = 0; i < answers; i++) {
        unsigned type, bytes, ttl;
        size_t k, slot = 0;
        if (!dns_name(packet, length, &at, owner) || at + 10 > (size_t)length) return;
        type = get16(packet + at); bytes = get16(packet + at + 8);
        ttl = ((unsigned)packet[at + 4] << 24) | ((unsigned)packet[at + 5] << 16) |
              ((unsigned)packet[at + 6] << 8) | packet[at + 7];
        if (get16(packet + at + 2) != 1) return;
        at += 10;
        if (at + bytes > (size_t)length) return;
        if ((type == 1 && bytes == 4) || (type == 28 && bytes == 16)) {
            AcquireSRWLockExclusive(&cache_lock);
            for (k = 0; k < 2048; k++) {
                if ((cache[k].family == (int)bytes &&
                     !memcmp(cache[k].ip, packet + at, bytes) &&
                     !_stricmp(cache[k].name, name)) || !cache[k].family) {
                    slot = k; break;
                }
                if (cache[k].serial < cache[slot].serial) slot = k;
            }
            memcpy(cache[slot].ip, packet + at, bytes);
            cache[slot].family = bytes;
            StringCchCopyA(cache[slot].name, 256, name);
            cache[slot].until = GetTickCount64() + (ULONGLONG)ttl * 1000;
            cache[slot].serial = ++serial;
            ReleaseSRWLockExclusive(&cache_lock);
        }
        at += bytes;
    }
}

int pacdns_names(const unsigned char *ip, int ipv6,
                 char names[][256], int max_names)
{
    size_t i;
    int found = 0, bytes = ipv6 ? 16 : 4;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&cache_lock);
    for (i = 0; i < 2048 && found < max_names; i++)
        if (cache[i].family == bytes && cache[i].until > now &&
            !memcmp(cache[i].ip, ip, bytes))
            StringCchCopyA(names[found++], 256, cache[i].name);
    ReleaseSRWLockShared(&cache_lock);
    return found;
}

static int io(SOCKET s, void *buffer, int n, int writing)
{
    char *p = (char *)buffer;
    while (n > 0) { int k = writing ? send(s, p, n, 0) : recv(s, p, n, 0); if (k <= 0) return 0; p += k; n -= k; }
    return 1;
}
static void timeouts(SOCKET s)
{
    DWORD ms = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof ms);
}
typedef struct { SOCKET client; int tcp, length; struct sockaddr_in peer; unsigned char packet[65536]; } dns_job;

static DWORD WINAPI answer(void *arg)
{
    dns_job *j = (dns_job *)arg;
    SOCKET upstream = INVALID_SOCKET;
    unsigned char prefix[2], id[2];
    int length = j->length;
    SOCKADDR_STORAGE target;
    int target_size;
    if (j->tcp) {
        timeouts(j->client);
        if (!io(j->client, prefix, 2, 0)) goto done;
        length = get16(prefix);
        if (length < 12 || !io(j->client, j->packet, length, 0)) goto done;
    }
    if (length < 12 || j->packet[2] & 0x80) goto done;
    memcpy(id, j->packet, 2);
    AcquireSRWLockShared(&resolver_lock);
    target = resolver; target_size = resolver_size;
    ReleaseSRWLockShared(&resolver_lock);
    upstream = socket(target.ss_family, j->tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (upstream == INVALID_SOCKET) goto done;
    timeouts(upstream);
    /* A TCP connect must be bounded independently of recv timeouts. */
    {
        u_long nonblock = 1;
        fd_set f;
        struct timeval tv = { 3, 0 };
        int status, size = sizeof status;
        ioctlsocket(upstream, FIONBIO, &nonblock);
        if (connect(upstream, (struct sockaddr *)&target, target_size) && WSAGetLastError() != WSAEWOULDBLOCK) goto done;
        FD_ZERO(&f); FD_SET(upstream, &f);
        if (select(0, NULL, &f, NULL, &tv) <= 0 || getsockopt(upstream, SOL_SOCKET, SO_ERROR, (char *)&status, &size) || status) goto done;
        nonblock = 0; ioctlsocket(upstream, FIONBIO, &nonblock);
    }
    if (j->tcp && !io(upstream, prefix, 2, 1)) goto done;
    if (!io(upstream, j->packet, length, 1)) goto done;
    if (j->tcp) {
        if (!io(upstream, prefix, 2, 0)) goto done;
        length = get16(prefix);
        if (length < 12 || !io(upstream, j->packet, length, 0)) goto done;
    } else length = recv(upstream, (char *)j->packet, sizeof j->packet, 0);
    if (length < 12 || memcmp(id, j->packet, 2)) goto done;
    remember(j->packet, length);
    if (j->tcp) { if (io(j->client, prefix, 2, 1)) io(j->client, j->packet, length, 1); }
    else sendto(j->client, (char *)j->packet, length, 0, (struct sockaddr *)&j->peer, sizeof j->peer);
done:
    if (upstream != INVALID_SOCKET) closesocket(upstream);
    if (j->tcp) closesocket(j->client);
    free(j);
    InterlockedDecrement(&jobs);
    return 0;
}

typedef struct { SOCKET socket; int tcp; } dns_listener;
static DWORD WINAPI accept_dns(void *arg)
{
    dns_listener l = *(dns_listener *)arg;
    free(arg);
    for (;;) {
        dns_job *j = (dns_job *)calloc(1, sizeof *j);
        HANDLE thread;
        int size;
        if (!j) { Sleep(100); continue; }
        j->tcp = l.tcp;
        if (l.tcp) j->client = accept(l.socket, NULL, NULL);
        else {
            j->client = l.socket;
            size = sizeof j->peer;
            j->length = recvfrom(l.socket, (char *)j->packet, sizeof j->packet, 0, (struct sockaddr *)&j->peer, &size);
            if (j->length < 12) { free(j); continue; }
        }
        if (j->client == INVALID_SOCKET) { free(j); break; }
        if (InterlockedIncrement(&jobs) > 512) {
            InterlockedDecrement(&jobs); if (j->tcp) closesocket(j->client); free(j); continue;
        }
        thread = CreateThread(NULL, 0, answer, j, 0, NULL);
        if (thread) CloseHandle(thread);
        else { InterlockedDecrement(&jobs); if (j->tcp) closesocket(j->client); free(j); }
    }
    closesocket(l.socket);
    return 0;
}

int pacdns_start(int *port, wchar_t *err, size_t cap)
{
    ULONG bytes = 16384;
    IP_ADAPTER_ADDRESSES *list, *a;
    ULONG metric = ULONG_MAX;
    SOCKET tcp = INVALID_SOCKET, udp = INVALID_SOCKET;
    struct sockaddr_in local;
    SOCKADDR_STORAGE chosen;
    int chosen_size = 0;
    int size = sizeof local, exclusive = 1, i;
    ZeroMemory(&chosen, sizeof chosen);
    list = (IP_ADAPTER_ADDRESSES *)malloc(bytes);
    if (!list) goto fail;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, list, &bytes) == ERROR_BUFFER_OVERFLOW) {
        free(list); list = (IP_ADAPTER_ADDRESSES *)malloc(bytes);
        if (!list) goto fail;
    }
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, list, &bytes) != NO_ERROR) { free(list); goto fail; }
    for (a = list; a; a = a->Next) {
        IP_ADAPTER_DNS_SERVER_ADDRESS *d;
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            (a->FriendlyName && (wcsstr(a->FriendlyName, UTGARD_SB_TUN_W) || wcsstr(a->FriendlyName, UTGARD_AWG_TUN_W)))) continue;
        for (d = a->FirstDnsServerAddress; d; d = d->Next) {
            struct sockaddr_in *v4 = (struct sockaddr_in *)d->Address.lpSockaddr;
            if (v4->sin_family != AF_INET || (ntohl(v4->sin_addr.s_addr) >> 24) == 127 || !v4->sin_addr.s_addr || a->Ipv4Metric >= metric) continue;
            memcpy(&chosen, v4, sizeof *v4); ((struct sockaddr_in *)&chosen)->sin_port = htons(53);
            chosen_size = sizeof *v4; metric = a->Ipv4Metric;
        }
    }
    free(list);
    if (!chosen_size) goto fail;
    AcquireSRWLockExclusive(&resolver_lock);
    resolver = chosen; resolver_size = chosen_size;
    ReleaseSRWLockExclusive(&resolver_lock);
    if (local_port) { *port = local_port; return 1; }
    tcp = socket(AF_INET, SOCK_STREAM, 0); udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (tcp == INVALID_SOCKET || udp == INVALID_SOCKET) goto fail;
    setsockopt(tcp, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    setsockopt(udp, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    ZeroMemory(&local, sizeof local); local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(tcp, (struct sockaddr *)&local, sizeof local) || getsockname(tcp, (struct sockaddr *)&local, &size) ||
        bind(udp, (struct sockaddr *)&local, sizeof local) || listen(tcp, 16)) goto fail;
    for (i = 0; i < 2; i++) {
        dns_listener *l = (dns_listener *)malloc(sizeof *l);
        HANDLE t;
        if (!l) goto fail;
        l->socket = i ? udp : tcp; l->tcp = !i;
        t = CreateThread(NULL, 0, accept_dns, l, 0, NULL);
        if (!t) { free(l); goto fail; }
        CloseHandle(t);
        if (i) udp = INVALID_SOCKET; else tcp = INVALID_SOCKET;
    }
    local_port = ntohs(local.sin_port); *port = local_port;
    return 1;
fail:
    if (tcp != INVALID_SOCKET) closesocket(tcp);
    if (udp != INVALID_SOCKET) closesocket(udp);
    if (err && cap) StringCchCopyW(err, cap, L"Для PAC не найден системный IPv4 DNS-сервер на активном сетевом адаптере");
    return 0;
}
