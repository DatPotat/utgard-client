/* profiles_load / profiles_save (src/win/profstore.c) against real DPAPI
   and the real file system. Run under Windows or Wine
   from an empty folder (profiles.dat lives beside the exe); the test
   removes what it creates.
   Build (from the repository root):
     x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Isrc/core -Isrc/win \
       -Ivendor/parson -Ivendor/puff -o test_profiles_win.exe \
       tests/win/test_profiles_win.c src/win/profstore.c src/core/profiles.c \
       src/win/fileio.c -lcrypt32
   The block-after-failed-rename case runs last: the block lasts for the
   process, as it does in the program. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include "profstore.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* Must match DPAPI_SALT in profstore.c. */
static const char SALT[] = "utgard-profiles-v1";

static wchar_t dir[MAX_PATH], dat[MAX_PATH];

static void remove_all(void)
{
    wchar_t mask[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DeleteFileW(dat);
    swprintf(mask, MAX_PATH, L"%lsprofiles.dat.unreadable-*", dir);
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { swprintf(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName); DeleteFileW(path); } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int write_raw(const void *data, DWORD n)
{
    HANDLE f = CreateFileW(dat, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    int ok;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, data, n, &put, NULL) && put == n;
    CloseHandle(f);
    return ok;
}

static int file_has(const char *needle)
{
    static char buf[1 << 20];
    HANDLE f = CreateFileW(dat, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD got = 0, i, n = (DWORD)strlen(needle);
    if (f == INVALID_HANDLE_VALUE) return -1;
    ReadFile(f, buf, sizeof buf, &got, NULL);
    CloseHandle(f);
    for (i = 0; i + n <= got; i++) if (!memcmp(buf + i, needle, n)) return 1;
    return 0;
}

static int exists(const wchar_t *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

int main(void)
{
    static profile_store s, back;
    wchar_t aside[1024], *slash;
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH || !(slash = wcsrchr(dir, L'\\'))) return 2;
    slash[1] = 0;
    swprintf(dat, MAX_PATH, L"%lsprofiles.dat", dir);
    remove_all();

    /* no file: an empty store, not an error */
    CHECK(profiles_load(&back, aside, 1024) == 1 && back.count == 0 && back.active == -1 && !aside[0]);

    /* round trip; the file holds no plain text */
    memset(&s, 0, sizeof s);
    s.count = 2; s.active = 1;
    s.items[0].link.proto = LINK_TROJAN; strcpy(s.items[0].link.server, "trojan.example.com");
    s.items[0].link.port = 443; strcpy(s.items[0].link.password, "secret-one"); strcpy(s.items[0].link.name, "t");
    s.items[1].link.proto = LINK_SS; strcpy(s.items[1].link.server, "198.51.100.9");
    s.items[1].link.port = 8388; strcpy(s.items[1].link.method, "aes-256-gcm");
    strcpy(s.items[1].link.password, "secret-two"); strcpy(s.items[1].link.name, "ss");
    CHECK(profiles_save(&s) == 1 && exists(dat));
    CHECK(file_has("secret-one") == 0 && file_has("trojan.example.com") == 0);
    CHECK(profiles_load(&back, aside, 1024) == 1 && back.count == 2 && back.active == 1 && !aside[0]);
    CHECK(!strcmp(back.items[0].link.password, "secret-one") && !strcmp(back.items[1].link.method, "aes-256-gcm") &&
          back.items[1].link.port == 8388);

    /* an empty file has nothing to lose: empty store */
    CHECK(write_raw("", 0));
    CHECK(profiles_load(&back, aside, 1024) == 1 && back.count == 0 && !aside[0]);

    /* garbage: refused, moved aside, saving still allowed */
    CHECK(write_raw("not a dpapi blob", 16));
    CHECK(profiles_load(&back, aside, 1024) == 0 && back.count == 0 && aside[0] && exists(aside) && !exists(dat));
    CHECK(wcsstr(aside, L"profiles.dat.unreadable-") != NULL);
    CHECK(profiles_save(&s) == 1);
    remove_all();

    /* a valid DPAPI blob whose content is not the format (newer or damaged) */
    {
        static const char junk[] = "\xff\xff\xff\xff not the profiles format";
        DATA_BLOB in, entropy, out;
        in.pbData = (BYTE *)junk; in.cbData = sizeof junk - 1;
        entropy.pbData = (BYTE *)SALT; entropy.cbData = sizeof SALT - 1;
        CHECK(CryptProtectData(&in, L"test", &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out));
        CHECK(write_raw(out.pbData, out.cbData));
        LocalFree(out.pbData);
        CHECK(profiles_load(&back, aside, 1024) == 0 && aside[0] && exists(aside) && !exists(dat));
        remove_all();
    }

    /* last: the move fails (file held without delete sharing) - nothing is
       moved, and every save is refused so the only copy is not overwritten */
    {
        HANDLE hold;
        CHECK(write_raw("held garbage", 12));
        hold = CreateFileW(dat, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        CHECK(hold != INVALID_HANDLE_VALUE);
        CHECK(profiles_load(&back, aside, 1024) == 0 && !aside[0] && exists(dat));
        CloseHandle(hold);
        CHECK(profiles_save(&s) == 0);
        CHECK(file_has("held garbage") == 1);
        remove_all();
    }

    printf("profiles_win: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
