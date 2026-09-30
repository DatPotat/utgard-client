#include "profstore.h"

#include <windows.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <string.h>

#include "fileio.h"

#define BLOB_MAX (1024 * 1024)

/* Mixed into the DPAPI key so the blob is not interchangeable with other
   DPAPI data of the same user. It lives in the binary, so it raises the bar
   rather than providing secrecy. */
static const char DPAPI_SALT[] = "utgard-profiles-v1";

static int state_path(wchar_t *buf, size_t cap)
{
    wchar_t  dir[1024];
    wchar_t *slash;
    DWORD    n;

    n = GetModuleFileNameW(NULL, dir, (DWORD)(sizeof dir / sizeof dir[0]));
    if (n == 0 || n >= sizeof dir / sizeof dir[0]) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';

    return SUCCEEDED(StringCchPrintfW(buf, cap, L"%sprofiles.dat", dir));
}

/* Set when an unreadable file could not be moved aside: saving would
   overwrite the only copy. */
static int g_save_blocked;

int profiles_save(const profile_store *s)
{
    static unsigned char plain[BLOB_MAX];
    wchar_t   path[1024];
    DATA_BLOB in, entropy, out;
    size_t    n;
    int       ok;

    if (g_save_blocked) return 0;
    n = profiles_pack(s, plain, sizeof plain);
    if (n == 0) return 0;
    if (!state_path(path, 1024)) return 0;

    in.pbData      = plain;
    in.cbData      = (DWORD)n;
    entropy.pbData = (BYTE *)DPAPI_SALT;
    entropy.cbData = (DWORD)(sizeof DPAPI_SALT - 1);
    out.pbData     = NULL;
    out.cbData     = 0;

    if (!CryptProtectData(&in, L"utgard profiles", &entropy, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        SecureZeroMemory(plain, n);
        return 0;
    }
    SecureZeroMemory(plain, n);

    /* Atomic: the profiles are the one file whose loss hurts most. */
    ok = file_write(path, out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
}

/* The name carries the time so a second failure never replaces the first. */
static void set_aside(const wchar_t *path, wchar_t *aside, size_t cap)
{
    SYSTEMTIME t;

    GetLocalTime(&t);
    if (FAILED(StringCchPrintfW(aside, cap, L"%s.unreadable-%04u%02u%02u-%02u%02u%02u",
                                path, t.wYear, t.wMonth, t.wDay,
                                t.wHour, t.wMinute, t.wSecond)) ||
        !MoveFileExW(path, aside, MOVEFILE_WRITE_THROUGH)) {
        aside[0] = L'\0';
        g_save_blocked = 1;
    }
}

int profiles_load(profile_store *s, wchar_t *aside, size_t aside_cap)
{
    static unsigned char raw[BLOB_MAX];
    wchar_t   path[1024];
    DATA_BLOB in, entropy, out;
    size_t    got = 0;
    int       ok;

    memset(s, 0, sizeof *s);
    s->active = -1;
    if (aside_cap) aside[0] = L'\0';

    if (!state_path(path, 1024)) { g_save_blocked = 1; return 0; }

    /* No file yet means an empty store. Any other failure to read is a file
       we must not lose. */
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_FILE_NOT_FOUND)
        return 1;
    if (!file_read(path, raw, sizeof raw, &got)) {
        set_aside(path, aside, aside_cap);
        return 0;
    }
    if (got == 0) return 1;     /* nothing in it to lose */

    in.pbData      = raw;
    in.cbData      = (DWORD)got;
    entropy.pbData = (BYTE *)DPAPI_SALT;
    entropy.cbData = (DWORD)(sizeof DPAPI_SALT - 1);
    out.pbData     = NULL;
    out.cbData     = 0;

    if (!CryptUnprotectData(&in, NULL, &entropy, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        /* another account, another machine, or a damaged file */
        set_aside(path, aside, aside_cap);
        return 0;
    }

    ok = profiles_unpack(out.pbData, out.cbData, s);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (ok) return 1;
    /* a newer format or damage inside the encrypted blob */
    set_aside(path, aside, aside_cap);
    return 0;
}
