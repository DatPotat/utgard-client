#include "link.h"
#include "profiles.h"
#include "genconf.h"
#include "awg.h"
#include "parson.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public, disposable test keys, never used for a real server. */
static const char base[] =
    "[Interface]\nPrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
    "Address = 10.20.30.2/32, fd00::2/128\nMTU = 1280\n";
static const char peer[] =
    "[Peer]\nPublicKey = AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\n"
    "PresharedKey = AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI=\n"
    "Endpoint = [2001:db8::1]:51820\nPersistentKeepalive = 25\n"
    "AllowedIPs = 0.0.0.0/0, ::/0\n";
static const char params[] =
    "Jc=4\nJmin=40\nJmax=70\nS1=20\nS2=30\nS3=15\nS4=10\n"
    "H1=100000-200000\nH2=300000-400000\nH3=500000-600000\nH4=700000-800000\n"
    "I1=<b 0xc000000001><r 50>\nI2=<t><rc 20>\nI3=<rd 8>\nI4=<r 32>\nI5=<b 0x1234>\n";

static link_profile parse(const char *extra, int expected)
{
    char text[20000], err[256];
    link_profile p;
    snprintf(text, sizeof text, "%s%s%s", base, extra, peer);
    int ok = link_parse_wgconf(text, strlen(text), &p, err, sizeof err);
    if (ok != expected) fprintf(stderr, "parse result %d, expected %d: %s\n", ok, expected, err);
    assert(ok == expected);
    return p;
}

int main(void)
{
    static profile_store store, round;
    static unsigned char packed[1024*1024];
    char err[256], *json = NULL;
    size_t size;
    link_profile p = parse(params, 1);
    assert(p.proto == LINK_AWG && p.keepalive == 25 && p.mtu == 1280);
    assert(strstr(p.awg,"h4=700000-800000\n"));
    assert(strstr(p.awg,"i1=<b 0xc000000001><r 50>\n"));
    assert(strcmp(p.server,"2001:db8::1") == 0);
    assert(parse("",1).proto == LINK_WG);
    assert(parse("H1=100\nH2=200\nH3=300\nH4=400\n",1).proto == LINK_AWG);
    parse("Jc=2\nJmin=100\nJmax=20\n",0);
    parse("Jc=-1\n",0); parse("S4=99999999\n",0);
    parse("H1=4294967296\n",0); parse("H1=200-100\n",0);
    parse("H1=10-20\nH2=20-30\n",0);
    parse("S3=3\ns3=4\n",0);
    parse("I1=<r -1>\n",0); parse("I1=<r 65507><r 1>\n",0);
    parse("I1=<b 0xabc>\n",0); parse("I1=<r 12\n",0);
    parse("HeaderProtectionKey=not-supported\n",0);
    parse("Jc=0\nJmin=0\nJmax=0\n",1);
    assert(!awg_validate("private_key=bad\n"));
    assert(!awg_validate("s9=2\n"));
    assert(link_parse("awg://AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=@vpn.example:51820?publickey=AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE%3D&address=10.0.0.2%2F32&H1=100-200&H2=300-400&H3=500-600&H4=700-800&S3=15&I1=%3Cr%2020%3E", &round.items[0].link, err, sizeof err));
    assert(round.items[0].link.proto == LINK_AWG);
    assert(strstr(round.items[0].link.awg,"i1=<r 20>"));
    {
        char text[20000];
        link_profile imported[2];
        int skipped = -1;
        snprintf(text, sizeof text, "\xef\xbb\xbf%s%s%s", base, params, peer);
        assert(link_parse_wgconf(text, strlen(text), &imported[0], err, sizeof err));
        assert(imported[0].proto == LINK_AWG);
        strncat(text, peer, sizeof text - strlen(text) - 1);
        assert(!link_parse_wgconf(text, strlen(text), &imported[0], err, sizeof err));
        snprintf(text, sizeof text, "%sI1=<b 0x", base);
        while (strlen(text) < 3000) strcat(text, "ab");
        strcat(text, ">\n"); strcat(text, peer);
        assert(link_parse_wgconf(text, strlen(text), &imported[0], err, sizeof err));
        assert(strlen(imported[0].awg) > 2000);
        assert(link_parse_subscription("amneziawg://AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=@vpn.example:51820?publickey=AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE%3D&address=10.0.0.2%2F32&S3=15\n", 0, imported, 2, &skipped, err, sizeof err) == 0);
        const char *uri = "amneziawg://AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=@vpn.example:51820?publickey=AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE%3D&address=10.0.0.2%2F32&S3=15\n";
        assert(link_parse_subscription(uri, strlen(uri), imported, 2, &skipped, err, sizeof err) == 1);
        assert(skipped == 0 && imported[0].proto == LINK_AWG);
    }
    store.count = 1; store.active = 0; store.items[0].link = p;
    size = profiles_pack(&store, packed, sizeof packed);
    assert(size && profiles_unpack(packed,size,&round));
    assert(memcmp(&round.items[0].link,&p,sizeof p) == 0);
    assert(!profiles_unpack(packed,size-1,&round));
    /* Version 3 compatibility: remove v4's appended string. */
    store.items[0].link = parse("",1);
    size = profiles_pack(&store, packed, sizeof packed);
    packed[4] = 3;
    assert(profiles_unpack(packed,size-2,&round));
    assert(round.items[0].link.proto == LINK_WG && !round.items[0].link.awg[0]);
    store.items[0].link = p;
    genconf_input in = {0};
    in.base_path = "sing-box/config.default.json";
    in.rule_set_path = "build/general.srs";
    in.store = &store;
    assert(genconf_build(&in,&json,err,sizeof err));
    JSON_Value *v = json_parse_string(json);
    JSON_Object *ep = json_array_get_object(json_object_get_array(json_value_get_object(v),"endpoints"),0);
    assert(strcmp(json_object_get_string(ep,"type"),"amneziawg") == 0);
    assert(strcmp(json_object_get_string(ep,"amnezia"),p.awg) == 0);
    json_value_free(v);
    FILE *f = fopen("build/awg-test-config.json","wb");
    assert(f); fputs(json,f); fclose(f);
    genconf_text_free(json);
    strcpy(store.items[0].link.awg,"private_key=bad\n");
    assert(!genconf_build(&in,&json,err,sizeof err));
    puts("AWG import, validation, storage migration and generation: OK");
    return 0;
}
