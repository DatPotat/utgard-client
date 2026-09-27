/* Native Windows integration test. No browser, proxy settings or VPN changes.
   Link with src/win/pac.c -lwinhttp -lws2_32 -lbcrypt. */
#include "pac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(const char *text, size_t size, const wchar_t *url, int expected)
{
    wchar_t message[256];
    DWORD error = 0;
    pac_script *p = pac_open(text, size, message, 256);
    int got;
    if (!p) { fwprintf(stderr, L"open: %ls\n", message); return 0; }
    got = pac_query(p, url, &error);
    pac_close(p);
    printf("decision=%d expected=%d error=%lu\n", got, expected, (unsigned long)error);
    return got == expected;
}

int main(int argc, char **argv)
{
    static const char direct[] = "function FindProxyForURL(url, host) { return 'DIRECT'; }";
    static const char proxy[] = "function FindProxyForURL(url, host) { return 'PROXY 127.0.0.1:1080; DIRECT'; }";
    static const char socks[] = "function FindProxyForURL(url, host) { return 'SOCKS5 127.0.0.1:1080; DIRECT'; }";
    static const char helpers[] = "function FindProxyForURL(url, host) { return dnsDomainIs(host, '.example.com') && shExpMatch(url, 'https:*') ? 'PROXY 127.0.0.1:9' : 'DIRECT'; }";
    int ok = 1;
    ok &= check(direct, strlen(direct), L"https://example.com/", 0);
    ok &= check(proxy, strlen(proxy), L"https://example.com/", 1);
    /* Windows bypasses loopback before PAC; Utgard's route frame does too. */
    ok &= check(proxy, strlen(proxy), L"http://127.0.0.1:33333/", 0);
    ok &= check(socks, strlen(socks), L"https://example.com/", 1);
    ok &= check(helpers, strlen(helpers), L"https://www.example.com/", 1);
    ok &= check(helpers, strlen(helpers), L"http://www.example.com/", 0);
    ok &= check("invalid ! script", 16, L"https://example.com/", -1);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        char *text = (char *)malloc(PAC_MAX + 1);
        size_t n;
        if (!f || !text) return 2;
        n = fread(text, 1, PAC_MAX + 1, f);
        fclose(f);
        ok &= check(text, n, L"https://googlevideo.com/", 1);
        free(text);
    }
    return ok ? 0 : 1;
}
