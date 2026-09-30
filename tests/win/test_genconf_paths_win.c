/* confread.c feeding the generator: inputs in a folder named in Cyrillic
   (the reason files are opened by UTF-16 path), and the messages for files
   that cannot be read - missing config.json, missing overlay, config.json
   over 16 MiB. Run under Windows or Wine from an empty folder; the test
   removes what it creates.
   Build (from the repository root):
     x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Isrc/core -Isrc/win \
       -Ivendor/parson -Ivendor/puff -o test_genconf_paths_win.exe \
       tests/win/test_genconf_paths_win.c src/win/confread.c src/core/genconf.c \
       src/core/defconfig.c src/core/link.c vendor/parson/parson.c vendor/puff/puff.c
   Under Wine the host needs a UTF-8 locale that is actually installed
   (LANG=C.UTF-8, or ru_RU.UTF-8 after locale-gen): with none, Wine cannot
   create the Cyrillic folder and the setup fails before genconf runs.
   Control run: the same test with the files opened by plain fopen fails. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "confread.h"
#include "defconfig.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define DIR_W  L"Новая папка"
#define DIR_U8 "Новая папка"

static int put(const wchar_t *path, const char *text, size_t n)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD done = 0;
    int ok;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, text, (DWORD)n, &done, NULL) && done == n;
    CloseHandle(f);
    return ok;
}

/* As plan_prepare does it: read, build, free. */
static int build(genconf_input *in, const char *base, const char **overlays, int n,
                 char **text, char *err, size_t cap)
{
    genconf_file files[4];
    int ok;
    err[0] = '\0';
    *text = NULL;
    if (!confread_inputs(base, overlays, n, &in->base, files, err, cap)) return 0;
    in->overlays = files;
    in->overlay_count = n;
    ok = genconf_build(in, text, err, cap);
    confread_free(&in->base, files, n);
    in->overlays = NULL;
    in->overlay_count = 0;
    return ok;
}

int main(void)
{
    static profile_store s;
    static const char overlay[] =
        "{\"dns\":{\"servers\":[{\"tag\":\"corp\",\"type\":\"udp\",\"server\":\"10.0.0.53\"}],"
        "\"rules\":[{\"domain_suffix\":[\"corp.example\"],\"server\":\"corp\"}]}}";
    genconf_input in;
    char err[512], *text = NULL;
    const char *ov[1] = { DIR_U8 "\\список.json" };

    CreateDirectoryW(DIR_W, NULL);
    CHECK(put(DIR_W L"\\config.json", utgard_default_config, utgard_default_config_len));
    CHECK(put(DIR_W L"\\список.json", overlay, sizeof overlay - 1));

    memset(&s, 0, sizeof s);
    s.count = 1; s.active = 0;
    s.items[0].link.proto = LINK_SS; strcpy(s.items[0].link.server, "198.51.100.9"); s.items[0].link.port = 8388;
    strcpy(s.items[0].link.method, "aes-256-gcm"); strcpy(s.items[0].link.password, "p"); strcpy(s.items[0].link.name, "ss");
    memset(&in, 0, sizeof in);
    in.store = &s;
    in.rule_set_path = "list/general.srs";

    CHECK(build(&in, DIR_U8 "\\config.json", ov, 1, &text, err, sizeof err));
    if (!text) printf("  generator: %s\n", err);
    CHECK(text && strstr(text, "\"tag\":\"corp\"") && strstr(text, "10.0.0.53"));
    genconf_text_free(text); text = NULL;

    /* a missing file is named in UTF-8, as the window shows it */
    CHECK(!build(&in, DIR_U8 "\\нет.json", NULL, 0, &text, err, sizeof err) && !text);
    CHECK(!strcmp(err, "Не найден файл " DIR_U8 "\\нет.json"));
    ov[0] = DIR_U8 "\\нет-списка.json";
    CHECK(!build(&in, DIR_U8 "\\config.json", ov, 1, &text, err, sizeof err));
    CHECK(!strcmp(err, "Не удалось прочитать " DIR_U8 "\\нет-списка.json"));

    /* over 16 MiB: refused as unreadable. The file is valid JSON - "{",
       spaces, the default config's body - so only the size can refuse it. */
    {
        size_t n = 16u * 1024u * 1024u + 16u;
        char *big = (char *)malloc(n);
        CHECK(big != NULL);
        if (big) {
            memset(big, ' ', n);
            memcpy(big + n - utgard_default_config_len, utgard_default_config, utgard_default_config_len);
            CHECK(put(DIR_W L"\\большой.json", big, n));
            free(big);
            CHECK(!build(&in, DIR_U8 "\\большой.json", NULL, 0, &text, err, sizeof err) && !text);
            CHECK(!strcmp(err, GENCONF_MSG_BAD_BASE));
            DeleteFileW(DIR_W L"\\большой.json");
        }
    }

    DeleteFileW(DIR_W L"\\config.json");
    DeleteFileW(DIR_W L"\\список.json");
    RemoveDirectoryW(DIR_W);
    printf("genconf_paths_win: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
