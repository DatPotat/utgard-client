/* genconf.c: how the generator treats its inputs, and the exact config it
   produces for fixed inputs. Written before file reading left src/core, and
   kept to show later changes alter nothing unintended:
     - every error about the inputs' content keeps its message (bad JSON,
       overlay faults named by file); errors about reading the files come
       from src/win/confread.c and are checked by
       win/test_genconf_paths_win.c;
     - four scenarios - plain, overlays, PAC, AmneziaWG - must produce the
       same config as the files in fixtures/genconf/ (compared re-serialised
       pretty, so a difference reads as a line diff).
   After an intended change of the output: UPDATE_GOLDEN=1 sh tests/run.sh
   rewrites the fixtures; review their diff before committing.
   Only build() below knows how inputs reach the generator: it reads the
   files here, as confread does on Windows. The generated configs are
   written to out/golden_*.json for "sing-box check" (run.sh, SINGBOX=). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "genconf.h"
#include "defconfig.h"
#include "parson.h"

#define PUB_STD "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="
#define PUB_URL "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg"
#define PK      "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="

static int write_file(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (n && fwrite(text, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}
static int put(const char *path, const char *text) { return write_file(path, text, strlen(text)); }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  size;
    char *buf = NULL;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = (char *)malloc((size_t)size + 1)) != NULL)
        buf[fread(buf, 1, (size_t)size, f)] = '\0';
    fclose(f);
    return buf;
}

static int build(genconf_input *in, const char *base, const char **overlays, int n,
                 char **text, char *err, size_t cap)
{
    genconf_file files[8];
    int i, ok = 1;
    err[0] = '\0';
    *text = NULL;
    in->base.name = base;
    in->base.text = slurp(base);
    for (i = 0; i < n && i < 8; i++) {
        files[i].name = overlays[i];
        files[i].text = slurp(overlays[i]);
        if (!files[i].text) ok = 0;
    }
    if (!in->base.text || !ok) {
        snprintf(err, cap, "test setup: an input file is missing");
        CHECK(0);
    } else {
        in->overlays = n ? files : NULL;
        in->overlay_count = n;
        ok = genconf_build(in, text, err, cap);
    }
    free((char *)in->base.text);
    in->base.text = NULL;
    for (i = 0; i < n && i < 8; i++) free((char *)files[i].text);
    in->overlays = NULL;
    in->overlay_count = 0;
    return ok && in->base.text == NULL && *text != NULL;
}

static int add_link(profile_store *s, const char *uri)
{
    char err[200];
    if (s->count >= PROFILES_MAX || !link_parse(uri, &s->items[s->count].link, err, sizeof err)) {
        printf("  link not parsed: %s (%s)\n", uri, err);
        return 0;
    }
    s->count++;
    return 1;
}

static int add_conf(profile_store *s, const char *conf, const char *name)
{
    char err[200];
    if (s->count >= PROFILES_MAX ||
        !link_parse_wgconf(conf, strlen(conf), &s->items[s->count].link, err, sizeof err)) {
        printf("  conf not parsed: %s (%s)\n", name, err);
        return 0;
    }
    strcpy(s->items[s->count].link.name, name);
    s->count++;
    return 1;
}

/* A vmess:// link is base64 of the v2rayN JSON. */
static void vmess_uri(char *out, size_t cap)
{
    static const char json[] =
        "{\"v\":\"2\",\"ps\":\"vmess\",\"add\":\"vmess.example.com\",\"port\":\"443\","
        "\"id\":\"00000000-0000-0000-0000-000000000002\",\"aid\":\"0\",\"scy\":\"auto\","
        "\"net\":\"ws\",\"path\":\"/ws\",\"host\":\"vmess.example.com\",\"tls\":\"tls\","
        "\"sni\":\"vmess.example.com\"}";
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0, n = sizeof json - 1;
    o += (size_t)snprintf(out, cap, "vmess://");
    for (i = 0; i < n && o + 5 < cap; i += 3) {
        unsigned v = (unsigned char)json[i] << 16;
        if (i + 1 < n) v |= (unsigned char)json[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)json[i + 2];
        out[o++] = b64[(v >> 18) & 63];
        out[o++] = b64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? b64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? b64[v & 63] : '=';
    }
    out[o] = '\0';
}

static int servers(profile_store *s)
{
    char vmess[1024];
    int ok = 1;
    memset(s, 0, sizeof *s);
    s->active = -1;
    vmess_uri(vmess, sizeof vmess);
    ok &= add_link(s, "vless://00000000-0000-0000-0000-000000000001@vless.example.com:443"
                      "?security=reality&sni=www.example.com&pbk=" PUB_URL "&sid=abcd&fp=chrome"
                      "&type=tcp&flow=xtls-rprx-vision#vless");
    ok &= add_link(s, vmess);
    ok &= add_link(s, "trojan://pass@trojan.example.com:443?security=tls&sni=trojan.example.com#trojan");
    ok &= add_link(s, "hysteria2://secret@198.51.100.20:8443?sni=hy2.example.com#hy2");
    ok &= add_link(s, "ss://YWVzLTI1Ni1nY206cA@198.51.100.9:8388#ss");
    ok &= add_conf(s, "[Interface]\nPrivateKey = " PK "\nAddress = 10.8.0.2/32\n"
                      "[Peer]\nPublicKey = " PUB_STD "\nEndpoint = 203.0.113.5:51820\n"
                      "AllowedIPs = 0.0.0.0/0\n", "wg");
    s->active = 0;
    return ok;
}

static void fixed_settings(genconf_input *in, const profile_store *s)
{
    memset(in, 0, sizeof *in);
    in->store = s;
    in->rule_set_path = "list/general.srs";
    in->mtu = 1400;
    in->log_level = "warn";
    in->stack = "gvisor";
    in->dns_host = "cloudflare-dns.com";
    in->dns_type = "https";
    in->dns_path = "/dns-query";
}

/* Pretty form of the generated text against the fixture. */
static void golden(const char *name, const char *text)
{
    char fixture[256], out[256];
    JSON_Value *v = json_parse_string(text);
    char *pretty = v ? json_serialize_to_string_pretty(v) : NULL;
    snprintf(fixture, sizeof fixture, "fixtures/genconf/%s.json", name);
    snprintf(out, sizeof out, "out/golden_%s.json", name);
    CHECK(pretty != NULL);
    if (!pretty) { json_value_free(v); return; }
    CHECK(put(out, text));
    if (getenv("UPDATE_GOLDEN")) {
        CHECK(put(fixture, pretty));
        printf("  golden %s rewritten\n", name);
    } else {
        FILE *f = fopen(fixture, "rb");
        long size = -1;
        char *want = NULL;
        if (f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
            (want = (char *)malloc((size_t)size + 1)) != NULL) {
            size_t got = fread(want, 1, (size_t)size, f);
            want[got] = '\0';
        }
        if (f) fclose(f);
        CHECK(want != NULL);
        if (want && strcmp(want, pretty)) {
            printf("  golden %s differs: diff fixtures/genconf/%s.json against the pretty form of out/golden_%s.json\n",
                   name, name, name);
            g_fails++;
        }
        free(want);
    }
    json_free_serialized_string(pretty);
    json_value_free(v);
}

static const char CORP[] =
    "{\"dns\":{\"servers\":[{\"tag\":\"corp\",\"type\":\"udp\",\"server\":\"10.0.0.53\"}],"
    "\"rules\":[{\"domain\":[\"sstp.corp.example\"],\"server\":\"local\"},"
    "{\"domain_suffix\":[\"corp.example\"],\"server\":\"corp\"}]},"
    "\"route\":{\"rules\":[{\"process_name\":[\"openvpn.exe\",\"openvpn-gui.exe\"],"
    "\"action\":\"route\",\"outbound\":\"direct\"},"
    "{\"domain_suffix\":[\"corp.example\"],\"action\":\"route\",\"outbound\":\"direct\"}]}}";
static const char ROUTES[] =
    "{\"route\":{\"rules\":[{\"process_name\":[\"game.exe\"],\"action\":\"route\",\"outbound\":\"direct\"}]}}";

static void input_errors(const profile_store *s)
{
    genconf_input in;
    char err[400], *text = (char *)1;
    const char *ov[2];

    fixed_settings(&in, s);
    /* the contract with the platform code: no base text, no build; an
       overlay without text is named like one that cannot be parsed */
    CHECK(!genconf_build(&in, &text, err, sizeof err) && text == NULL);
    CHECK(!strcmp(err, "Генератору не переданы обязательные данные"));
    {
        genconf_file base = { "config.json", utgard_default_config }, none = { "empty.json", NULL };
        in.base = base; in.overlays = &none; in.overlay_count = 1;
        CHECK(!genconf_build(&in, &text, err, sizeof err) && text == NULL);
        CHECK(!strcmp(err, "Не удалось прочитать empty.json"));
        in.base.text = NULL; in.overlays = NULL; in.overlay_count = 0;
    }

    CHECK(put("out/in_comment.json", "{ // no comments\n \"log\": {} }"));
    CHECK(!build(&in, "out/in_comment.json", NULL, 0, &text, err, sizeof err) && text == NULL);
    CHECK(!strncmp(err, "config.json не читается", strlen("config.json не читается")));

    CHECK(put("out/in_array.json", "[1, 2]"));
    CHECK(!build(&in, "out/in_array.json", NULL, 0, &text, err, sizeof err));
    CHECK(!strncmp(err, "config.json не читается", strlen("config.json не читается")));

    CHECK(put("out/in_ov_array.json", "[]"));
    ov[0] = "out/in_ov_array.json";
    CHECK(!build(&in, "out/golden_base.json", ov, 1, &text, err, sizeof err));
    CHECK(!strcmp(err, "Не удалось прочитать out/in_ov_array.json"));

    /* the second overlay is the faulty one: its name, not the first's */
    CHECK(put("out/in_ov_notag.json", "{\"dns\":{\"servers\":[{\"type\":\"udp\",\"server\":\"10.0.0.1\"}]}}"));
    ov[0] = "out/golden_ov_corp.json"; ov[1] = "out/in_ov_notag.json";
    CHECK(!build(&in, "out/golden_base.json", ov, 2, &text, err, sizeof err));
    CHECK(!strcmp(err, "В out/in_ov_notag.json у DNS-сервера нет тега"));

    CHECK(put("out/in_ov_clash.json", "{\"dns\":{\"servers\":[{\"tag\":\"doh\",\"type\":\"udp\",\"server\":\"10.0.0.1\"}]}}"));
    ov[0] = "out/in_ov_clash.json";
    CHECK(!build(&in, "out/golden_base.json", ov, 1, &text, err, sizeof err));
    CHECK(!strcmp(err, "В out/in_ov_clash.json DNS-сервер с тегом «doh» уже объявлен"));

    CHECK(put("out/in_ov_hosts.json", "{\"dns\":{\"servers\":[{\"tag\":\"h\",\"type\":\"hosts\",\"path\":[\"x\"]}]}}"));
    ov[0] = "out/in_ov_hosts.json";
    CHECK(!build(&in, "out/golden_base.json", ov, 1, &text, err, sizeof err));
    CHECK(!strncmp(err, "В out/in_ov_hosts.json DNS-сервер «h» недопустимого типа",
                   strlen("В out/in_ov_hosts.json DNS-сервер «h» недопустимого типа")));

    /* file paths in an overlay's DNS TLS are cut, the server stays */
    CHECK(put("out/in_ov_tls.json",
              "{\"dns\":{\"servers\":[{\"tag\":\"t\",\"type\":\"tls\",\"server\":\"10.0.0.2\","
              "\"tls\":{\"enabled\":true,\"certificate_path\":\"C1/cert.pem\",\"key_path\":\"C1/key.pem\","
              "\"ech\":{\"enabled\":true,\"config_path\":\"C1/ech.pem\"}}}]}}"));
    ov[0] = "out/in_ov_tls.json";
    text = NULL;
    CHECK(build(&in, "out/golden_base.json", ov, 1, &text, err, sizeof err));
    CHECK(text && strstr(text, "\"tag\":\"t\"") && !strstr(text, "C1\\/") && !strstr(text, "C1/") &&
          !strstr(text, "certificate_path") && !strstr(text, "config_path"));
    genconf_text_free(text);

    /* PAC needs to know which DNS servers answer: refused without them, and
       when a tag is longer than the generator keeps */
    in.pac_port = 40001; in.pac_dns_port = 40002; in.pac_dns_vpn_port = 40003; in.pac_dns_sys_port = 40004;
    CHECK(put("out/in_nodns.json", "{\"route\":{\"rules\":[]}}"));
    CHECK(!build(&in, "out/in_nodns.json", NULL, 0, &text, err, sizeof err) && text == NULL);
    CHECK(!strcmp(err, "Для PAC нужны dns.final и DNS-правило rule_set general"));
    CHECK(put("out/in_longtag.json",
              "{\"dns\":{\"final\":\"t0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890\","
              "\"rules\":[{\"rule_set\":[\"general\"],\"server\":\"doh\"}]},\"route\":{\"rules\":[{\"action\":\"sniff\"}]}}"));
    CHECK(!build(&in, "out/in_longtag.json", NULL, 0, &text, err, sizeof err) && text == NULL);
    CHECK(!strcmp(err, "Тег DNS-сервера для PAC слишком длинный"));
}

int main(void)
{
    static profile_store s;
    genconf_input in;
    char err[400], *text = NULL;
    const char *ov[2] = { "out/golden_ov_corp.json", "out/golden_ov_routes.json" };

    CHECK(servers(&s));
    CHECK(write_file("out/golden_base.json", utgard_default_config, utgard_default_config_len));
    CHECK(put(ov[0], CORP) && put(ov[1], ROUTES));
    if (g_fails) return DONE("genconf_inputs");

    fixed_settings(&in, &s);
    CHECK(build(&in, "out/golden_base.json", NULL, 0, &text, err, sizeof err));
    if (text) golden("plain", text);
    genconf_text_free(text); text = NULL;

    s.active = 3;                                        /* hy2 */
    CHECK(build(&in, "out/golden_base.json", ov, 2, &text, err, sizeof err));
    if (text) golden("overlays", text);
    genconf_text_free(text); text = NULL;

    in.pac_port = 40001; in.pac_dns_port = 40002; in.pac_dns_vpn_port = 40003;
    in.pac_dns_sys_port = 40004; in.vpn_proxy_port = 40005;
    in.proxy_password = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    in.client_exe = "bin\\utgard-pac-helper.exe";
    CHECK(build(&in, "out/golden_base.json", ov, 2, &text, err, sizeof err));
    if (text) golden("pac", text);
    genconf_text_free(text); text = NULL;

    fixed_settings(&in, &s);
    CHECK(add_conf(&s, "[Interface]\nPrivateKey = " PK "\nAddress = 10.8.1.2/32\nJc = 4\nH1 = 5\nH2 = 6\nH3 = 7\nH4 = 8\n"
                       "[Peer]\nPublicKey = " PUB_STD "\nEndpoint = 203.0.113.10:51820\n", "awg"));
    s.active = s.count - 1;
    in.awg_interface = "utgard-awg-tun"; in.awg_server_ip = "203.0.113.10";
    in.awg_exe = "core\\amneziawg\\amneziawg.exe";
    CHECK(build(&in, "out/golden_base.json", NULL, 0, &text, err, sizeof err));
    if (text) golden("awg", text);
    genconf_text_free(text); text = NULL;

    s.active = 0;
    input_errors(&s);
    if (g_fails && err[0]) printf("  last generator message: %s\n", err);
    return DONE("genconf_inputs");
}
