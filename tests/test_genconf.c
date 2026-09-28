/* genconf.c: the config sing-box gets for an AmneziaWG profile. Writes it to
   out/genconf_awg.json; run.sh passes it to "sing-box check" when SINGBOX
   points at a sing-box 1.14.1 binary for the host. */
#include <string.h>
#include "check.h"
#include "genconf.h"
#include "defconfig.h"
#include "parson.h"
#include "tunnames.h"

#define PK  "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="
#define PUB "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="

int main(void)
{
    static profile_store s;
    genconf_input in;
    char err[300] = "", *text = NULL;
    const char *conf = "[Interface]\nPrivateKey = " PK "\nAddress = 10.8.1.2/32, fd00::2/128\nJc = 4\nH1 = 5\nH2 = 6\nH3 = 7\nH4 = 8\n"
                       "[Peer]\nPublicKey = " PUB "\nEndpoint = 203.0.113.10:51820\n";
    JSON_Value *root; JSON_Object *o; JSON_Array *a; size_t i; int found_awg = 0, tun_ok = 0;
    FILE *f;

    memset(&in, 0, sizeof in);
    s.count = 2;
    s.items[0].link.proto = LINK_SS; strcpy(s.items[0].link.server, "ss.example"); s.items[0].link.port = 8388;
    strcpy(s.items[0].link.method, "aes-256-gcm"); strcpy(s.items[0].link.password, "p"); strcpy(s.items[0].link.name, "ss");
    CHECK(link_parse_wgconf(conf, strlen(conf), &s.items[1].link, err, sizeof err));
    strcpy(s.items[1].link.name, "awg");
    s.active = 1;
    /* The base is the program's own default config, written as a file the
       way the program seeds config.json. */
    f = fopen("out/config.default.json", "wb");
    CHECK(f != NULL);
    if (f) { fwrite(utgard_default_config, 1, utgard_default_config_len, f); fclose(f); }
    in.base_path = "out/config.default.json"; in.store = &s; in.rule_set_path = "list/general.srs";
    /* tunnel not up: refused */
    CHECK(!genconf_build(&in, &text, err, sizeof err) && text == NULL);
    in.awg_interface = "utgard-awg-tun"; in.awg_server_ip = "203.0.113.10"; in.awg_exe = "amneziawg\\amneziawg.exe";   /* placeholder */
    CHECK(genconf_build(&in, &text, err, sizeof err));
    if (!text) return DONE("genconf");
    root = json_parse_string(text); o = json_value_get_object(root);
    a = json_object_get_array(o, "outbounds");
    for (i = 0; i < json_array_get_count(a); i++) {
        JSON_Object *ob = json_array_get_object(a, i);
        const char *bi = json_object_get_string(ob, "bind_interface");
        if (bi && !strcmp(bi, "utgard-awg-tun") && !strcmp(json_object_get_string(ob, "type"), "direct")) found_awg = 1;
    }
    CHECK(found_awg);
    CHECK(!strstr(text, PK));                                   /* the key stays with the service */
    a = json_object_get_array(o, "inbounds");
    for (i = 0; i < json_array_get_count(a); i++) {
        JSON_Object *ib = json_array_get_object(a, i);
        if (strcmp(json_object_get_string(ib, "type"), "tun")) continue;
        {
            JSON_Array *ex = json_object_get_array(ib, "route_exclude_address");
            const char *last = ex ? json_array_get_string(ex, json_array_get_count(ex) - 1) : NULL;
            tun_ok = !strcmp(json_object_get_string(ib, "interface_name"), UTGARD_SB_TUN) &&
                     last && !strcmp(last, "203.0.113.10/32");
        }
    }
    CHECK(tun_ok);
    {   /* the service's own traffic leaves directly, before any other rule */
        JSON_Object *r0 = json_array_get_object(json_object_dotget_array(o, "route.rules"), 0);
        JSON_Array  *pp = json_object_get_array(r0, "process_path");
        CHECK(pp && strstr(json_array_get_string(pp, 0), "amneziawg.exe") &&
              !strcmp(json_object_get_string(r0, "outbound"), "direct"));
    }
    f = fopen("out/genconf_awg.json", "wb");
    if (f) { fputs(text, f); fclose(f); }
    json_value_free(root);
    genconf_text_free(text);

    {   /* The base config is valid JSON and keeps the TUN address. */
        JSON_Value *d = json_parse_string(utgard_default_config);
        CHECK(d && strlen(utgard_default_config) == utgard_default_config_len);
        CHECK(d && strstr(utgard_default_config, "172.30.30.1/30"));
        CHECK(strstr(utgard_default_config, "\"level\": \"error\""));   /* errors only by default */
        json_value_free(d);
    }

    {   /* PAC: its rules come last, a work-VPN overlay before them; DNS for
           PAC names through the site-list server, the rest through the
           original final. */
        static profile_store one;
        genconf_input pin;
        const char *overlays[1] = { "out/work-overlay.json" };
        JSON_Array *rules, *dns_rules;
        size_t count, k, overlay_at = 0;
        memset(&one, 0, sizeof one);
        one.count = 1; one.active = 0;
        one.items[0].link.proto = LINK_SS; strcpy(one.items[0].link.server, "203.0.113.10");
        one.items[0].link.port = 8388; strcpy(one.items[0].link.method, "aes-256-gcm");
        strcpy(one.items[0].link.password, "p"); strcpy(one.items[0].link.name, "one");
        f = fopen("out/work-overlay.json", "wb");
        CHECK(f != NULL);
        if (f) {
            fputs("{\"route\":{\"rules\":[{\"domain_suffix\":[\"corp.example\"],\"action\":\"route\",\"outbound\":\"direct\"}]}}", f);
            fclose(f);
        }
        memset(&pin, 0, sizeof pin);
        pin.base_path = "out/config.default.json"; pin.store = &one; pin.rule_set_path = "list/general.srs";
        pin.overlays = overlays; pin.overlay_count = 1;

        CHECK(genconf_build(&pin, &text, err, sizeof err));                 /* without PAC */
        CHECK(text && !strstr(text, "utgard-pac") && !strstr(text, "utgard-vpn-proxy"));
        root = text ? json_parse_string(text) : NULL;
        CHECK(root && !strcmp(json_object_dotget_string(json_value_get_object(root), "dns.final"), "local"));
        json_value_free(root);
        genconf_text_free(text); text = NULL;

        pin.pac_port = 32101; pin.pac_dns_port = 32102; pin.vpn_proxy_port = 32103;
        pin.pac_dns_vpn_port = 32104; pin.pac_dns_sys_port = 32105;
        pin.proxy_password = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        pin.client_exe = "bin/utgard-pac-helper.exe";                       /* placeholder */
        /* DoH over HTTP/2 with NextDNS's profile-less path: host, type and
           path of the "doh" server all follow the setting. */
        pin.dns_host = "dns.nextdns.io"; pin.dns_type = "https"; pin.dns_path = "/";
        CHECK(genconf_build(&pin, &text, err, sizeof err));
        root = text ? json_parse_string(text) : NULL;
        o = json_value_get_object(root);
        rules = json_object_dotget_array(o, "route.rules");
        count = json_array_get_count(rules);
        CHECK(count > 2);
        if (count > 2) {
            JSON_Object *first = json_array_get_object(rules, 0), *last = json_array_get_object(rules, count - 1);
            CHECK(!strcmp(json_object_get_string(first, "action"), "hijack-dns") &&
                  json_array_get_count(json_object_get_array(first, "inbound")) == 2);
            CHECK(!strcmp(json_object_get_string(last, "outbound"), "utgard-pac"));
            for (k = 0; k < count; k++) {
                JSON_Array *ds = json_object_get_array(json_array_get_object(rules, k), "domain_suffix");
                if (ds && !strcmp(json_array_get_string(ds, 0), "corp.example")) overlay_at = k;
            }
            CHECK(overlay_at > 0 && overlay_at < count - 1);              /* the overlay wins over PAC */
        }
        dns_rules = json_object_dotget_array(o, "dns.rules");
        CHECK(!strcmp(json_object_dotget_string(o, "dns.final"), "utgard-pac-dns"));
        CHECK(!strcmp(json_object_get_string(json_array_get_object(dns_rules, 0), "server"), "doh"));
        CHECK(!strcmp(json_object_get_string(json_array_get_object(dns_rules, 1), "server"), "local"));
        {
            JSON_Array *servers = json_object_dotget_array(o, "dns.servers");
            size_t m; int seen = 0;
            for (m = 0; m < json_array_get_count(servers); m++) {
                JSON_Object *srv = json_array_get_object(servers, m);
                if (strcmp(json_object_get_string(srv, "tag"), "doh")) continue;
                seen = 1;
                CHECK(!strcmp(json_object_get_string(srv, "server"), "dns.nextdns.io"));
                CHECK(!strcmp(json_object_get_string(srv, "type"), "https"));
                CHECK(!strcmp(json_object_get_string(srv, "path"), "/"));
            }
            CHECK(seen);
        }
        f = fopen("out/genconf_pac.json", "wb");
        if (f && text) { fputs(text, f); fclose(f); } else if (f) fclose(f);
        json_value_free(root);
        genconf_text_free(text);
    }
    return DONE("genconf");
}
