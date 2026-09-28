/* Short mutation run over the parsers: nothing accepted may carry a byte that
   could break a config line, and the sanitizers must stay quiet. */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "link.h"
#include "parson.h"

static void mutate(char *b, size_t *n, size_t cap)
{
    size_t pos = (size_t)rand() % (*n ? *n : 1);
    switch (rand() % 3) {
    case 0: b[pos] = (char)(rand() % 256); break;
    case 1: if (*n > 1) { memmove(b + pos, b + pos + 1, *n - pos); (*n)--; } break;
    case 2: if (*n + 2 < cap) { memmove(b + pos + 1, b + pos, *n - pos + 1); b[pos] = "=\n x-"[rand() % 5]; (*n)++; } break;
    }
    b[*n] = 0;
}

int main(void)
{
    static const char *seed = "[Interface]\nPrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\nAddress = 10.8.1.2/32\n"
        "Jc = 4\nJmin = 40\nJmax = 70\nS1 = 15\nS2 = 20\nH1 = 100-200\nH2 = 3000\nH3 = 4000\nH4 = 5000\nI1 = <b 0xc0ffee><r 16>\nMTU = 1280\n"
        "[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\nEndpoint = vpn.example:51820\nPersistentKeepalive = 20-30\n";
    static char b[70000]; static link_profile p; char err[256];
    JSON_Value *cv = json_parse_file("fixtures/vpn_cases.json");
    const char *vpn = json_object_get_string(json_value_get_object(cv), "fields_only");
    long it;
    size_t n;

    srand(1);
    for (it = 0; it < 40000; it++) {
        int k = 1 + rand() % 6;
        n = strlen(seed); memcpy(b, seed, n + 1);
        while (k--) mutate(b, &n, sizeof b);
        if (link_parse_wgconf(b, n, &p, err, sizeof err)) {
            const char *q;
            for (q = p.awg; *q; q++) CHECK(!((*q < 0x20 && *q != '\n') || *q == 0x7f));
            CHECK(link_awg_valid(p.awg));
        }
    }
    for (it = 0; vpn && it < 20000; it++) {
        int k = 1 + rand() % 4;
        n = strlen(vpn); memcpy(b, vpn, n + 1);
        while (k--) mutate(b, &n, sizeof b);
        link_parse(b, &p, err, sizeof err);
    }
    json_value_free(cv);
    return DONE("fuzz");
}
