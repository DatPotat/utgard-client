/* pacstore.c against real DPAPI: run under Windows or Wine, from an empty
   folder that has a list\ subfolder (pacstore works beside its exe).
   Build (from the repository root):
     x86_64-w64-mingw32-gcc -std=c11 -Isrc/core -Isrc/win -Ivendor/parson \\
       -o test_pacstore_win.exe tests/win/test_pacstore_win.c src/win/pacstore.c \\
       src/win/fileio.c src/core/pacblob.c vendor/parson/parson.c -lcrypt32 */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include "pacstore.h"
#include "pacblob.h"
#include "parson.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char S1[] = "function FindProxyForURL(u,h){return \"PROXY secret-one:1\";}";
static const char S2[] = "function FindProxyForURL(u,h){return \"DIRECT\";}";

static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* A pac.json exactly as 2.3.1 wrote it (format 3). */
static int write_legacy(void)
{
    static const char salt[] = "utgard-pac-source-v2";
    const wchar_t *src[2] = { L"https://example.test/one.pac", L"C:\\pac\\two.pac" };
    const char *txt[2] = { S1, S2 };
    JSON_Value *root = json_value_init_object(), *list = json_value_init_array();
    int i; char *text; FILE *f;
    for (i = 0; i < 2; i++) {
        unsigned char plain[33 + 2048 * 2]; char encoded[16384]; DWORD chars = sizeof encoded;
        size_t n = pacblob_pack(txt[i], strlen(txt[i]), (const uint16_t *)src[i], wcslen(src[i]) + 1, plain, sizeof plain);
        DATA_BLOB in = { (DWORD)n, plain }, ent = { sizeof salt - 1, (BYTE *)salt }, out = { 0, NULL };
        JSON_Value *v = json_value_init_object(); JSON_Object *o = json_value_get_object(v);
        if (!n || !CryptProtectData(&in, L"t", &ent, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return 0;
        CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded, &chars);
        LocalFree(out.pbData);
        json_object_set_string(o, "source_protected", encoded);
        json_object_set_string(o, "script", txt[i]);
        json_object_set_boolean(o, "enabled", i == 0);
        json_array_append_value(json_value_get_array(list), v);
    }
    json_object_set_number(json_value_get_object(root), "version", 3);
    json_object_set_value(json_value_get_object(root), "items", list);
    text = json_serialize_to_string(root);
    f = fopen("pac.json", "wb");
    if (!f || !text) return 0;
    fwrite(text, 1, strlen(text), f); fclose(f);
    json_free_serialized_string(text); json_value_free(root);
    return 1;
}

static int file_has(const char *path, const char *needle)
{
    static char buf[1 << 20]; size_t n; FILE *f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    {   /* no memmem in the Windows CRT */
        size_t k = strlen(needle), i;
        for (i = 0; i + k <= n; i++) if (!memcmp(buf + i, needle, k)) return 1;
        return 0;
    }
}

int main(void)
{
    pac_store s; wchar_t aside[2048];
    CreateDirectoryW(L"list", NULL);   /* main.c creates it at startup */
    DeleteFileW(L"list\\pac\\01.pac"); DeleteFileW(L"list\\pac\\02.pac"); DeleteFileW(L"pac.json");

    /* 1. pac.json of 2.3.1 moves into list\pac, and goes. */
    CHECK(write_legacy());
    CHECK(pacstore_load(&s));
    CHECK(s.count == 2);
    CHECK(s.count == 2 && s.items[0].enabled == 1 && s.items[1].enabled == 0);
    CHECK(s.count == 2 && !wcscmp(s.items[0].source, L"https://example.test/one.pac") && !strcmp(s.items[0].text, S1));
    CHECK(s.count == 2 && !wcscmp(s.items[1].source, L"C:\\pac\\two.pac") && !strcmp(s.items[1].text, S2));
    CHECK(exists(L"list\\pac\\01.pac") && exists(L"list\\pac\\02.pac"));
    CHECK(!exists(L"pac.json"));
    /* 2. Nothing readable in the clear: neither the script nor the source. */
    CHECK(file_has("list\\pac\\01.pac", "secret-one") == 0);
    CHECK(file_has("list\\pac\\01.pac", "example.test") == 0);

    /* 3. One removed: its file goes, the rest reads back. */
    free(s.items[1].text); s.items[1].text = NULL; s.count = 1;
    CHECK(pacstore_save(&s));
    CHECK(!exists(L"list\\pac\\02.pac"));
    pacstore_free(&s);
    CHECK(pacstore_load(&s) && s.count == 1 && !strcmp(s.items[0].text, S1));

    /* 4. A damaged file is set aside; the others still load. */
    {
        pac_store two; ZeroMemory(&two, sizeof two);
        two.count = 2;
        two.items[0] = s.items[0]; s.items[0].text = NULL;
        two.items[1].enabled = 1; wcscpy(two.items[1].source, L"https://b.test/x.pac");
        two.items[1].text = _strdup(S2);
        CHECK(pacstore_save(&two));
        pacstore_free(&two); pacstore_free(&s);
        { FILE *f = fopen("list\\pac\\01.pac", "r+b"); CHECK(f != NULL); if (f) { fseek(f, 40, SEEK_SET); fputc(0x5A, f); fclose(f); } }
        CHECK(pacstore_load(&s) && s.count == 1 && !strcmp(s.items[0].text, S2));
        CHECK(pacstore_unreadable_notice(aside, 2048) && wcsstr(aside, L"01.pac.unreadable-") != NULL);
        CHECK(!exists(L"list\\pac\\01.pac"));
        pacstore_free(&s);
    }

    if (fails) { printf("pacstore(win): %d FAILED\n", fails); return 1; }
    printf("pacstore(win): ok\n");
    return 0;
}
