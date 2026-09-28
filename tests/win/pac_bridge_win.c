/* Integration test: only loopback sockets. No TUN or machine proxy changes.
   Usage: pac-bridge-test.exe <sing-box.exe> (run from a disposable directory).
   Link pac.c pacbridge.c net.c with winhttp ws2_32 bcrypt iphlpapi. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include "pacbridge.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int proxy_mode;
static int destination(unsigned char *p, unsigned short port)
{
    const char name[] = "pac-test.invalid";
    int n;
    if (proxy_mode) { p[0] = 3; p[1] = sizeof name - 1; memcpy(p + 2, name, sizeof name - 1); n = sizeof name + 1; }
    else { p[0] = 1; p[1] = 127; p[2] = p[3] = 0; p[4] = 1; n = 5; }
    p[n++] = (unsigned char)(port >> 8); p[n++] = (unsigned char)port;
    return n;
}

static int io(SOCKET s, void *data, int n, int write)
{
    char *p = (char *)data;
    while (n) { int k = write ? send(s, p, n, 0) : recv(s, p, n, 0); if (k <= 0) return 0; p += k; n -= k; }
    return 1;
}
static SOCKET local(int type, unsigned short *port)
{
    struct sockaddr_in a;
    int size = sizeof a;
    DWORD timeout = 4000;
    SOCKET s = socket(AF_INET, type, 0);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || getsockname(s, (struct sockaddr *)&a, &size)) return INVALID_SOCKET;
    *port = ntohs(a.sin_port);
    return s;
}
static SOCKET dial(unsigned short port)
{
    unsigned short unused;
    SOCKET s = local(SOCK_STREAM, &unused);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { closesocket(s); return INVALID_SOCKET; }
    return s;
}
static DWORD WINAPI tcp_echo(void *arg)
{
    SOCKET listener = *(SOCKET *)arg;
    for (;;) {
        SOCKET s = accept(listener, NULL, NULL);
        char b[4096];
        int n;
        if (s == INVALID_SOCKET) break;
        n = recv(s, b, sizeof b, 0);
        if (n > 0) io(s, b, n, 1);
        closesocket(s);
    }
    return 0;
}
static DWORD WINAPI udp_echo(void *arg)
{
    SOCKET s = *(SOCKET *)arg;
    for (;;) {
        struct sockaddr_in a;
        int size = sizeof a;
        char b[65536];
        int n = recvfrom(s, b, sizeof b, 0, (struct sockaddr *)&a, &size);
        if (n >= 0) sendto(s, b, n, 0, (struct sockaddr *)&a, size);
        else if (WSAGetLastError() != WSAETIMEDOUT) break;
    }
    return 0;
}
static DWORD WINAPI http_server(void *arg)
{
    SOCKET listener = *(SOCKET *)arg;
    for (;;) {
        SOCKET s = accept(listener, NULL, NULL);
        char b[4096];
        static char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\npac-test";
        if (s == INVALID_SOCKET) break;
        if (recv(s, b, sizeof b, 0) > 0) io(s, response, sizeof response - 1, 1);
        closesocket(s);
    }
    return 0;
}
static SOCKET socks(const genconf_input *in, int command, unsigned short port, unsigned char reply[10])
{
    SOCKET s = dial((unsigned short)in->pac_port);
    unsigned char b[80] = { 5, 1, 2 }, r[2];
    if (s == INVALID_SOCKET) return s;
    if (!io(s, b, 3, 1) || !io(s, r, 2, 0) || r[1] != 2) goto fail;
    b[0] = 1; b[1] = 6; memcpy(b + 2, "utgard", 6); b[8] = 64; memcpy(b + 9, in->proxy_password, 64);
    if (!io(s, b, 73, 1) || !io(s, r, 2, 0) || r[1]) goto fail;
    memset(b, 0, sizeof b); b[0] = 5; b[1] = (unsigned char)command;
    { int n = 3 + destination(b + 3, port);
      if (!io(s, b, n, 1) || !io(s, reply, 10, 0) || reply[1]) goto fail; }
    return s;
fail:
    closesocket(s); return INVALID_SOCKET;
}
static int test_tcp(const genconf_input *in, unsigned short port)
{
    unsigned char reply[10];
    char payload[] = "PAC TCP test", received[sizeof payload];
    SOCKET s = socks(in, 1, port, reply);
    int ok = s != INVALID_SOCKET && io(s, payload, sizeof payload, 1) &&
             io(s, received, sizeof received, 0) && !memcmp(payload, received, sizeof payload);
    if (s != INVALID_SOCKET) closesocket(s);
    return ok;
}
static int test_udp(const genconf_input *in, unsigned short port)
{
    unsigned char reply[10], packet[1400], received[65536];
    unsigned short unused;
    SOCKET s = socks(in, 3, 0, reply), udp;
    struct sockaddr_in a;
    int n, ok, header, returned_header;
    if (s == INVALID_SOCKET) return 0;
    udp = local(SOCK_DGRAM, &unused);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    memcpy(&a.sin_port, reply + 8, 2);
    memset(packet, 0x5a, sizeof packet);
    packet[0] = packet[1] = packet[2] = 0;
    header = 3 + destination(packet + 3, port);
    sendto(udp, (char *)packet, sizeof packet, 0, (struct sockaddr *)&a, sizeof a);
    n = recv(udp, (char *)received, sizeof received, 0);
    returned_header = n > 4 ? (received[3] == 1 ? 10 : received[3] == 4 ? 22 : 7 + received[4]) : 0;
    ok = returned_header && n - returned_header == (int)sizeof packet - header &&
         !memcmp(packet + header, received + returned_header, sizeof packet - header);
    closesocket(s); closesocket(udp);
    return ok;
}
static void activate(int proxy)
{
    const char *text = proxy ? "function FindProxyForURL(u,h){return 'PROXY unused.invalid:9; DIRECT';}" :
                              "function FindProxyForURL(u,h){return 'DIRECT';}";
    pac_script *p = pac_open(text, strlen(text), NULL, 0);
    if (!p) exit(3);
    pacbridge_disconnect();
    pacbridge_activate(&p, 1);
    proxy_mode = proxy;
}

static void activate_pair(void)
{
    static const char direct[] = "function FindProxyForURL(u,h){return 'DIRECT';}";
    static const char proxy[] = "function FindProxyForURL(u,h){return 'SOCKS5 ignored.invalid:9';}";
    pac_script *scripts[2];
    scripts[0] = pac_open(direct, sizeof direct - 1, NULL, 0);
    scripts[1] = pac_open(proxy, sizeof proxy - 1, NULL, 0);
    if (!scripts[0] || !scripts[1]) exit(3);
    pacbridge_disconnect();
    pacbridge_activate(scripts, 2);
    proxy_mode = 1;
}
int main(int argc, char **argv)
{
    genconf_input in = { 0 };
    wchar_t err[256], url[100];
    SOCKET tcp, udp, http;
    unsigned short tp, up, hp;
    HANDLE threads[3];
    STARTUPINFOA si;
    PROCESS_INFORMATION process;
    char command[4096], *body = NULL;
    FILE *f;
    size_t length;
    int ok = 1, result;
    WSADATA data;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (argc != 2 || WSAStartup(MAKEWORD(2, 2), &data)) return 2;
    if (!pacbridge_prepare(&in, 1, 1, err, 256)) return 3;
    tcp = local(SOCK_STREAM, &tp); udp = local(SOCK_DGRAM, &up); http = local(SOCK_STREAM, &hp);
    listen(tcp, 8); listen(http, 8);
    threads[0] = CreateThread(NULL, 0, tcp_echo, &tcp, 0, NULL);
    threads[1] = CreateThread(NULL, 0, udp_echo, &udp, 0, NULL);
    threads[2] = CreateThread(NULL, 0, http_server, &http, 0, NULL);
    f = fopen("pac-test-backend.json", "wb");
    if (!f) return 3;
    fprintf(f, "{\"log\":{\"disabled\":true},\"dns\":{\"servers\":[{\"type\":\"hosts\",\"tag\":\"hosts\",\"predefined\":{\"pac-test.invalid\":\"127.0.0.1\"}}]},\"route\":{\"default_domain_resolver\":\"hosts\"},\"inbounds\":[{\"type\":\"mixed\",\"listen\":\"127.0.0.1\",\"listen_port\":%d,\"users\":[{\"username\":\"utgard\",\"password\":\"%s\"}]}],\"outbounds\":[{\"type\":\"direct\"}]}", in.vpn_proxy_port, in.proxy_password);
    fclose(f);
    snprintf(command, sizeof command, "\"%s\" run -c pac-test-backend.json", argv[1]);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &process)) return 4;
    for (int i = 0; i < 30; i++) { SOCKET s = dial((unsigned short)in.vpn_proxy_port); if (s != INVALID_SOCKET) { closesocket(s); break; } Sleep(100); }
    activate(1);
    result = test_tcp(&in, tp); printf("VPN TCP: %d\n", result); ok &= result;
    result = test_udp(&in, up); printf("VPN UDP: %d\n", result); ok &= result;
    activate_pair();
    result = test_tcp(&in, tp); printf("Multiple PAC (DIRECT + proxy): %d\n", result); ok &= result;
    swprintf(url, 100, L"http://127.0.0.1:%u/test.pac?version=1", hp);
    result = net_fetch_pac(url, &body, &length, err, 256);
    result = result && length == 8 && !memcmp(body, "pac-test", 8);
    printf("Direct PAC download: %d\n", result); ok &= result; free(body); body = NULL;
    result = pacbridge_count() == 2;
    printf("Active PAC lists: %d (count=%d)\n", result, pacbridge_count());
    ok &= result;
    if (!TerminateProcess(process.hProcess, 0)) { printf("TerminateProcess failed: %lu\n", (unsigned long)GetLastError()); ok = 0; }
    printf("Backend wait: %lu\n", (unsigned long)WaitForSingleObject(process.hProcess, 5000));
    { DWORD exit_code = 0; GetExitCodeProcess(process.hProcess, &exit_code); printf("Backend exit: %lu\n", (unsigned long)exit_code); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    result = !test_tcp(&in, tp); printf("VPN unavailable, no TCP fallback: %d\n", result); ok &= result;
    activate(0);
    result = test_tcp(&in, tp); printf("DIRECT TCP: %d\n", result); ok &= result;
    result = test_udp(&in, up); printf("DIRECT UDP: %d\n", result); ok &= result;
    closesocket(tcp); closesocket(udp); closesocket(http);
    WaitForMultipleObjects(3, threads, TRUE, 5000);
    for (int i = 0; i < 3; i++) CloseHandle(threads[i]);
    remove("pac-test-backend.json");
    return ok ? 0 : 1;
}
