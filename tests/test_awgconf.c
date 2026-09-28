/* awgconf.c: the text the AmneziaWG service gets. */
#include <string.h>
#include "check.h"
#include "awgconf.h"

#define PK  "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="
#define PUB "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="

int main(void)
{
    static link_profile p, back, bad;
    static char text[AWGCONF_MAX], err[300];
    const char *src = "[Interface]\nPrivateKey = " PK "\nAddress = 10.8.1.2/32, fd00::2/128\nMTU = 1376\nJc = 4\nH1 = 10\nH2 = 20\nH3 = 30\nH4 = 40\n"
                      "[Peer]\nPublicKey = " PUB "\nPresharedKey = " PUB "\nEndpoint = vpn.example:39100\nPersistentKeepalive = 25-35\n";

    CHECK(link_parse_wgconf(src, strlen(src), &p, err, sizeof err));
    CHECK(awgconf_build(&p, "203.0.113.10", text, sizeof text, err, sizeof err));
    CHECK(strstr(text, "Table = off\n") && strstr(text, "AllowedIPs = 0.0.0.0/0\n") && !strstr(text, "::/0"));
    CHECK(strstr(text, "Address = 10.8.1.2/32\n") && !strstr(text, "fd00"));           /* IPv4 only */
    CHECK(!strstr(text, "DNS") && !strstr(text, "PostUp") && !strstr(text, "ListenPort"));
    CHECK(strstr(strstr(text, "[Peer]"), "PersistentKeepalive = 25-35\n") && !strstr(text, "persistentkeepalive"));
    /* round trip: the text parses back into the same tunnel */
    CHECK(link_parse_wgconf(text, strlen(text), &back, err, sizeof err));
    CHECK(!strcmp(back.wg_private_key, p.wg_private_key) && !strcmp(back.wg_psk, p.wg_psk) &&
          !strcmp(back.awg, p.awg) && back.mtu == 1376 && !strcmp(back.server, "203.0.113.10") && back.port == 39100);
    CHECK(awgconf_build(&p, "2001:db8::10", text, sizeof text, err, sizeof err) && strstr(text, "Endpoint = [2001:db8::10]:39100\n"));
    CHECK(!awgconf_build(&p, "vpn.example", text, sizeof text, err, sizeof err));   /* not an IP */
    bad = p; strcpy(bad.wg_private_key + 5, "\nPostUp = calc");
    CHECK(!awgconf_build(&bad, "1.2.3.4", text, sizeof text, err, sizeof err));
    bad = p; strcat(bad.awg, "postup = calc\n");
    CHECK(!awgconf_build(&bad, "1.2.3.4", text, sizeof text, err, sizeof err));
    bad = p; strcpy(bad.wg_address, "fd00::2/128");
    CHECK(!awgconf_build(&bad, "1.2.3.4", text, sizeof text, err, sizeof err));       /* no IPv4 left */
    memset(text, 'x', sizeof text);
    CHECK(!awgconf_build(&p, "1.2.3.4", text, 200, err, sizeof err));
    { int i, clean = 1; for (i = 0; i < 200; i++) if (text[i]) clean = 0; CHECK(clean); }
    return DONE("awgconf");
}
