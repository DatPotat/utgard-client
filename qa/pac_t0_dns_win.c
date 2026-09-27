#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned short port; unsigned char answer; volatile LONG hits; } dns_server;

static DWORD WINAPI serve_dns(void *arg)
{
    dns_server *s = (dns_server *)arg;
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local, peer;
    int plen = sizeof peer;
    unsigned char q[512], r[544];
    int n;
    DWORD timeout = 8000;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(s->port);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout);
    if (bind(fd, (struct sockaddr *)&local, sizeof local)) return 1;
    n = recvfrom(fd, (char *)q, sizeof q, 0, (struct sockaddr *)&peer, &plen);
    if (n >= 12 && n + 16 <= (int)sizeof r) {
        memcpy(r, q, n); r[2] |= 0x80; r[3] |= 0x80; r[6] = 0; r[7] = 1;
        r[n] = 0xc0; r[n+1] = 0x0c; r[n+2] = 0; r[n+3] = 1;
        r[n+4] = 0; r[n+5] = 1; r[n+6] = r[n+7] = r[n+8] = 0; r[n+9] = 60;
        r[n+10] = 0; r[n+11] = 4; r[n+12] = 203; r[n+13] = 0;
        r[n+14] = 113; r[n+15] = s->answer;
        sendto(fd, (char *)r, n + 16, 0, (struct sockaddr *)&peer, plen);
        InterlockedIncrement(&s->hits);
    }
    closesocket(fd);
    return 0;
}

static int query_packet(unsigned char q[64])
{
    static const unsigned char body[] = {
        0x12,0x34,1,0,0,1,0,0,0,0,0,0,
        7,'e','x','a','m','p','l','e',4,'t','e','s','t',0,0,1,0,1
    };
    memcpy(q, body, sizeof body); return (int)sizeof body;
}

static int udp_query(unsigned short port, unsigned char expected)
{
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in to;
    unsigned char q[64], r[544];
    int n = query_packet(q), got;
    DWORD timeout = 4000;
    memset(&to, 0, sizeof to); to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK); to.sin_port = htons(port);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout);
    sendto(fd, (char *)q, n, 0, (struct sockaddr *)&to, sizeof to);
    got = recv(fd, (char *)r, sizeof r, 0); closesocket(fd);
    return got >= 16 && r[got - 1] == expected;
}

static int transfer(SOCKET fd, void *buf, int length, int write_mode)
{
    char *p = (char *)buf;
    while (length) { int n = write_mode ? send(fd,p,length,0) : recv(fd,p,length,0); if (n <= 0) return 0; p += n; length -= n; }
    return 1;
}

static int tcp_query(unsigned short port, unsigned char expected)
{
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in to;
    unsigned char q[64], r[544], prefix[2];
    int n = query_packet(q), got;
    DWORD timeout = 4000;
    memset(&to, 0, sizeof to); to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK); to.sin_port = htons(port);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout);
    if (connect(fd, (struct sockaddr *)&to, sizeof to)) { closesocket(fd); return 0; }
    prefix[0] = (unsigned char)(n >> 8); prefix[1] = (unsigned char)n;
    if (!transfer(fd,prefix,2,1) || !transfer(fd,q,n,1) || !transfer(fd,prefix,2,0)) { closesocket(fd); return 0; }
    got = ((int)prefix[0] << 8) | prefix[1];
    if (got > (int)sizeof r || !transfer(fd,r,got,0)) { closesocket(fd); return 0; }
    closesocket(fd); return got >= 16 && r[got - 1] == expected;
}

int main(int argc, char **argv)
{
    const unsigned short in_a = 39101, in_b = 39102;
    dns_server a = { 39111, 11, 0 }, b = { 39112, 22, 0 }, final = { 39113, 33, 0 };
    HANDLE threads[3], process;
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    FILE *f;
    char cmd[2048];
    WSADATA wsa;
    int ok;
    if (argc != 2 || WSAStartup(MAKEWORD(2,2), &wsa)) return 2;
    threads[0] = CreateThread(NULL,0,serve_dns,&a,0,NULL);
    threads[1] = CreateThread(NULL,0,serve_dns,&b,0,NULL);
    threads[2] = CreateThread(NULL,0,serve_dns,&final,0,NULL);
    Sleep(100);
    f = fopen("t0-dns.json", "wb"); if (!f) return 3;
    fprintf(f, "{\"log\":{\"level\":\"trace\"},\"inbounds\":["
      "{\"type\":\"direct\",\"tag\":\"dns-a\",\"listen\":\"127.0.0.1\",\"listen_port\":%u,\"network\":[\"tcp\",\"udp\"]},"
      "{\"type\":\"direct\",\"tag\":\"dns-b\",\"listen\":\"127.0.0.1\",\"listen_port\":%u,\"network\":[\"tcp\",\"udp\"]}],"
      "\"dns\":{\"servers\":[{\"type\":\"udp\",\"tag\":\"a\",\"server\":\"127.0.0.1\",\"server_port\":%u},{\"type\":\"udp\",\"tag\":\"b\",\"server\":\"127.0.0.1\",\"server_port\":%u},{\"type\":\"udp\",\"tag\":\"external-final\",\"server\":\"127.0.0.1\",\"server_port\":%u}],"
      "\"rules\":[{\"inbound\":[\"dns-a\"],\"action\":\"route\",\"server\":\"a\"},{\"inbound\":[\"dns-b\"],\"action\":\"route\",\"server\":\"b\"}],\"final\":\"external-final\"},"
      "\"route\":{\"default_domain_resolver\":\"a\",\"rules\":[{\"inbound\":[\"dns-a\",\"dns-b\"],\"action\":\"hijack-dns\"}]}}",
      in_a,in_b,a.port,b.port,final.port);
    fclose(f);
    snprintf(cmd,sizeof cmd,"\"%s\" run -c t0-dns.json",argv[1]);
    memset(&si,0,sizeof si); si.cb=sizeof si; memset(&pi,0,sizeof pi);
    if (!CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) return 4;
    CloseHandle(pi.hThread); process=pi.hProcess; Sleep(900);
    ok = udp_query(in_a,11) && tcp_query(in_b,22);
    {
        DWORD exit_code = STILL_ACTIVE;
        GetExitCodeProcess(process, &exit_code);
        printf("sing-box before stop=0x%08lX\n", (unsigned long)exit_code);
    }
    TerminateProcess(process,0); WaitForSingleObject(process,3000); CloseHandle(process);
    WaitForMultipleObjects(3,threads,TRUE,9000);
    printf("T0 DNS UDP/TCP=%d server-a=%ld server-b=%ld final=%ld\n",ok,a.hits,b.hits,final.hits);
    for (int i=0;i<3;i++) CloseHandle(threads[i]);
    if (ok) remove("t0-dns.json"); WSACleanup();
    return ok && a.hits==1 && b.hits==1 && final.hits==0 ? 0 : 1;
}
