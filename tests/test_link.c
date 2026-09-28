/* link.c: wg-quick configs with AmneziaWG, vpn:// in every form the Amnezia
   client accepts, files, subscriptions. Fixtures are synthetic keys. */
#include <string.h>
#include "check.h"
#include "link.h"
#include "parson.h"

#define PK  "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="
#define PUB "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="
static link_profile p;
static char err[300], buf[20000];

static int conf(const char *iface, const char *peer)
{
    snprintf(buf, sizeof buf, "[Interface]\nPrivateKey = " PK "\nAddress = 10.8.1.2/32\n%s[Peer]\nPublicKey = " PUB
             "\nEndpoint = 203.0.113.10:51820\nAllowedIPs = 0.0.0.0/0\n%s", iface, peer);
    err[0] = 0;
    return link_parse_wgconf(buf, strlen(buf), &p, err, sizeof err);
}

int main(void)
{
    JSON_Value *cv = json_parse_file("fixtures/vpn_cases.json");
    JSON_Object *c = json_value_get_object(cv);
    static char body[1 << 20];
    static link_profile subs[8];
    int skipped, r;
    size_t i;
    const char *must_fail[] = { "newline_injection", "api_key", "auth_data", "backup", "xray_payload",
                                "openvpn_payload", "full_access", "h_overlap" };

    CHECK(c != NULL);
    /* .conf */
    CHECK(conf("", "") && !link_is_awg(&p));
    CHECK(conf("Jc = 4\nJmin = 40\nJmax = 70\nH1 = 5\nH2 = 6\nH3 = 7\nH4 = 8\n", "") && link_is_awg(&p));
    CHECK(strstr(p.awg, "jc = 4\n") && strstr(p.awg, "h4 = 8\n"));
    CHECK(!conf("Jc = 70000\n", ""));                              /* uint16 */
    CHECK(!conf("Jmin = 80\nJmax = 70\n", ""));
    CHECK(!conf("Jc = 4\njc = 5\n", ""));                          /* duplicate */
    CHECK(!conf("H1 = 2\n", ""));                                  /* overlaps default H2 */
    CHECK(!conf("H1 = 100-300\nH2 = 200\nH3 = 5\nH4 = 6\n", ""));
    CHECK(conf("S1 = 12\nS2 = 12\nS3 = 12\nS4 = 12\nHeaderProtectionKey = " PUB "\n", ""));
    CHECK(!conf("S1 = 12\nS2 = 12\nS3 = 12\nS4 = 11\nHeaderProtectionKey = " PUB "\n", ""));
    CHECK(conf("MTU = 1280\n", "") && p.mtu == 1280);
    CHECK(!conf("MTU = 1279\n", "") && !conf("MTU = 1501\n", ""));
    CHECK(conf("", "PersistentKeepalive = off\n") && p.keepalive == 0);
    CHECK(conf("", "PersistentKeepalive = 25\n") && p.keepalive == 25 && !link_is_awg(&p));
    CHECK(conf("", "PersistentKeepalive = 20-30\n") && link_is_awg(&p) && strstr(p.awg, "persistentkeepalive = 20-30\n"));
    CHECK(!conf("", "PersistentKeepalive = 65536\n") && !conf("", "PersistentKeepalive = -5\n"));
    CHECK(conf("I1 = <b 0xc0ffee><r 16>\nI2 =\n", "") && strstr(p.awg, "i1 = <b 0xc0ffee><r 16>\n") && !strstr(p.awg, "i2"));
    CHECK(!conf("I1 = <b 0x01>\tx\n", ""));                         /* control char */
    snprintf(buf, sizeof buf, "\xEF\xBB\xBF[Interface]\nPrivateKey = " PK "\nAddress = 10.8.1.2/32\n[Peer]\nPublicKey = " PUB "\nEndpoint = 1.2.3.4:5\n");
    CHECK(link_parse_wgconf(buf, strlen(buf), &p, err, sizeof err));   /* BOM */
    CHECK(link_awg_valid("jc = 4\nh1 = 5\n") && !link_awg_valid("jc = 4\njc = 5\n") && !link_awg_valid("postup = x\n"));

    /* vpn:// */
    for (i = 0; i < json_object_get_count(c); i++) {
        const char *name = json_object_get_name(c, i), *link = json_object_get_string(c, name);
        size_t k; int bad = 0;
        for (k = 0; k < sizeof must_fail / sizeof must_fail[0]; k++) if (!strcmp(name, must_fail[k])) bad = 1;
        r = link_parse(link, &p, err, sizeof err);
        if (r == bad) printf("  vpn:// case %s: got %d\n", name, r);
        CHECK(r == !bad);
    }
    CHECK(link_parse(json_object_get_string(c, "fields_beat_text"), &p, err, sizeof err) &&
          !strcmp(p.server, "198.51.100.4") && strstr(p.awg, "jc = 4\n"));
    CHECK(link_parse(json_object_get_string(c, "awg_default_mtu"), &p, err, sizeof err) && p.mtu == 1376);
    CHECK(link_parse(json_object_get_string(c, "mtu_lifted"), &p, err, sizeof err) && p.mtu == 1280);
    CHECK(link_parse(json_object_get_string(c, "awg_in_container"), &p, err, sizeof err) && strstr(p.awg, "jc = 6\n"));
    CHECK(!link_parse("hello world", &p, err, sizeof err));

    /* files */
    CHECK(link_parse_file(buf, strlen(buf), &p, err, sizeof err));
    snprintf(body, sizeof body, "\n  %s  \r\n", json_object_get_string(c, "fields_only"));
    CHECK(link_parse_file(body, strlen(body), &p, err, sizeof err) && link_is_awg(&p));
    { const char *xr = "{\"inbounds\":[],\"outbounds\":[]}", *ov = "client\ndev tun\n";
      CHECK(!link_parse_file(xr, strlen(xr), &p, err, sizeof err) && strstr(err, "Xray"));
      CHECK(!link_parse_file(ov, strlen(ov), &p, err, sizeof err) && strstr(err, "OpenVPN")); }

    /* subscription: a long vpn:// line among others */
    snprintf(body, sizeof body, "%s\nss://YWVzLTI1Ni1nY206cA@198.51.100.9:8388#ss\n", json_object_get_string(c, "fields_only"));
    r = link_parse_subscription(body, strlen(body), subs, 8, &skipped, err, sizeof err);
    CHECK(r == 2 && link_is_awg(&subs[0]) && subs[1].proto == LINK_SS);

    /* Values must be well-formed UTF-8, or parson drops them from the config:
       broken (C3 28), overlong (C0 AF) and surrogate (ED A0 80) are refused. */
    CHECK(!link_parse("trojan://p%C3%28ss@example.com:443?security=tls&sni=example.com#t", &p, err, sizeof err) &&
          strstr(err, "UTF-8"));
    CHECK(!link_parse("trojan://p%C0%AFss@example.com:443?security=tls&sni=example.com#t", &p, err, sizeof err));
    CHECK(!link_parse("trojan://p%ED%A0%80@example.com:443?security=tls&sni=example.com#t", &p, err, sizeof err));
    CHECK(!link_parse("vless://00000000-0000-0000-0000-000000000000@example.com:443?type=ws&path=%2F%FF#t",
                      &p, err, sizeof err));
    CHECK(link_parse("trojan://p%C3%A9ss@example.com:443?security=tls&sni=example.com#t", &p, err, sizeof err) &&
          !strcmp(p.password, "p\xC3\xA9ss"));
    /* the name is exempt: the generator cuts it to valid UTF-8 itself */
    CHECK(link_parse("trojan://pass@example.com:443?security=tls&sni=example.com#%FF", &p, err, sizeof err));

    json_value_free(cv);
    return DONE("link");
}
