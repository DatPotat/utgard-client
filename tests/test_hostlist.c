/* hostlist.c: the site lists as the window shows and edits them. */
#include <stdio.h>
#include <string.h>
#include "hostlist.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static hl_entry e[64];
static char     out[8192];

int main(void)
{
    const char *vpn = "# my list\r\nzeta.example\r\n\r\nalpha.example # note\r\nBeta.example\r\n";
    const char *zap = "ru\n^static.example\nmedia.example\n";
    hl_preview  pv;
    int         n, lines[2];

    n = hostlist_parse(vpn, 0, e, 64);
    CHECK(n == 3);
    CHECK(e[0].line == 1 && strcmp(e[0].host, "zeta.example") == 0);
    CHECK(e[1].line == 3 && strcmp(e[1].host, "alpha.example") == 0);
    hostlist_sort(e, n);
    CHECK(strcmp(e[0].host, "alpha.example") == 0);
    CHECK(strcmp(e[1].host, "Beta.example") == 0);
    CHECK(hostlist_match(&e[1], "BETA") && hostlist_match(&e[1], "") && !hostlist_match(&e[1], "zz"));

    n = hostlist_parse(zap, 1, e, 64);
    CHECK(n == 3);
    CHECK(e[0].flags == HL_ZONE && strcmp(e[0].host, "ru") == 0);
    CHECK(e[1].flags == HL_EXACT && strcmp(e[1].host, "static.example") == 0);
    CHECK(e[2].flags == 0);

    /* VPN: URL stripped, repeat, covered subdomain, single label refused,
       IP given /32, a fresh name added. */
    CHECK(hostlist_add(vpn, "https://new.example/watch?v=1\nzeta.example\ncdn.alpha.example\n"
                            "com\n198.51.100.7\nfresh.example\n", 0, out, sizeof out, &pv));
    CHECK(pv.added == 3 && pv.duplicate == 1 && pv.covered == 1 && pv.invalid == 1);
    CHECK(strcmp(pv.bad[0], "com") == 0);
    CHECK(strstr(out, "# my list\r\n") == out);
    CHECK(strstr(out, "\r\nnew.example\r\n") && strstr(out, "\r\n198.51.100.7/32\r\n") &&
          strstr(out, "\r\nfresh.example\r\n") && !strstr(out, "cdn.alpha"));

    /* zapret: "ru" covers any .ru; "^static" does not cover its subdomains;
       one word is fine; garbage refused. */
    CHECK(hostlist_add(zap, "news.ru\ncdn.static.example\norg\nbad_name!\n^media.example\n", 1,
                       out, sizeof out, &pv));
    CHECK(pv.covered == 1 && pv.added == 3 && pv.invalid == 1 && pv.duplicate == 0);
    CHECK(strstr(out, "media.example\n") && strstr(out, "\r\norg\r\n"));
    CHECK(hostlist_add(zap, "x.example\n", 1, NULL, 0, &pv) && pv.added == 1);

    /* A long refused line is cut to fit, on a UTF-8 character boundary. */
    {
        char paste[512];
        size_t k, n2;
        paste[0] = 0;
        for (k = 0; k < 100; k++) strcat(paste, "\xd0\xb6");   /* "ж", two bytes each */
        strcat(paste, "!\n");
        CHECK(hostlist_add(vpn, paste, 1, NULL, 0, &pv) && pv.invalid == 1);
        n2 = strlen(pv.bad[0]);
        CHECK(n2 > 0 && n2 < sizeof pv.bad[0] && n2 % 2 == 0);
    }

    /* Remove lines 1 and 3: comments and blank lines survive. */
    lines[0] = 3; lines[1] = 1;
    CHECK(hostlist_remove(vpn, lines, 2, out, sizeof out));
    CHECK(strcmp(out, "# my list\r\n\r\nBeta.example\r\n") == 0);
    CHECK(!hostlist_remove(vpn, lines, 0, out, 5));   /* too small: refused, not cut */

    if (fails) { printf("hostlist: %d FAILED\n", fails); return 1; }
    printf("hostlist: ok\n");
    return 0;
}
