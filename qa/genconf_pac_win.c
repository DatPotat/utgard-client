#include "genconf.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    profile_store store;
    genconf_input input;
    char *text = NULL, error[256];
    FILE *file;
    memset(&store, 0, sizeof store);
    memset(&input, 0, sizeof input);
    store.count = 1;
    store.active = 0;
    store.items[0].link.proto = LINK_SS;
    strcpy(store.items[0].link.name, "PAC test");
    strcpy(store.items[0].link.server, "203.0.113.10");
    store.items[0].link.port = 443;
    strcpy(store.items[0].link.method, "aes-128-gcm");
    strcpy(store.items[0].link.password, "test-only-password");
    input.base_path = "sing-box/config.default.json";
    input.rule_set_path = "build/general.srs";
    input.store = &store;
    input.pac_port = 32101;
    input.pac_dns_port = 32102;
    input.pac_dns_vpn_port = 32104;
    input.pac_dns_sys_port = 32105;
    input.vpn_proxy_port = 32103;
    input.proxy_password = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    input.client_exe = "D:/Personal/utgard-client/bin/x64/utgard.exe";
    if (!genconf_build(&input, &text, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    file = fopen("build/pac-generated.json", "wb");
    if (!file) { genconf_text_free(text); return 2; }
    fwrite(text, 1, strlen(text), file);
    fclose(file);
    genconf_text_free(text);
    input.pac_port = 0;
    input.pac_dns_port = 0;
    input.pac_dns_vpn_port = 0;
    input.pac_dns_sys_port = 0;
    input.vpn_proxy_port = 0;
    input.proxy_password = NULL;
    input.client_exe = NULL;
    if (!genconf_build(&input, &text, error, sizeof error)) return 3;
    if (strstr(text, "utgard-pac") || strstr(text, "utgard-vpn-proxy")) {
        fprintf(stderr, "PAC fields leaked into a configuration without PAC\n");
        genconf_text_free(text);
        return 4;
    }
    genconf_text_free(text);
    return 0;
}
