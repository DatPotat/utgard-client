#include "pacstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy(const char *s)
{
    char *p = (char *)malloc(strlen(s) + 1);
    if (p) memcpy(p, s, strlen(s) + 1);
    return p;
}

int main(void)
{
    pac_store before, after;
    wchar_t path[1024], *slash;
    int ok;
    memset(&before, 0, sizeof before);
    before.count = 2;
    before.items[0].enabled = 1;
    wcscpy(before.items[0].source, L"C:\\rules\\local.pac");
    before.items[0].text = copy("function FindProxyForURL(u,h){return 'DIRECT';}");
    wcscpy(before.items[1].source, L"https://example.com/proxy.pac");
    before.items[1].text = copy("function FindProxyForURL(u,h){return 'PROXY ignored:9';}");
    before.items[1].via_vpn = 1;
    ok = before.items[0].text && before.items[1].text && pacstore_save(&before) &&
         pacstore_load(&after) && after.count == 2 && after.items[0].enabled &&
         !after.items[1].enabled &&
         after.items[1].via_vpn &&
         !wcscmp(after.items[0].source, before.items[0].source) &&
         !strcmp(after.items[1].text, before.items[1].text);
    printf("PAC store round-trip: %d\n", ok);
    pacstore_free(&before);
    pacstore_free(&after);
    if (GetModuleFileNameW(NULL, path, 1024) && (slash = wcsrchr(path, L'\\'))) {
        wcscpy(slash + 1, L"pac.json");
        DeleteFileW(path);
    }
    return ok ? 0 : 1;
}
