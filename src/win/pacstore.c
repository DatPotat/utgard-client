#include "pacstore.h"
#include "pacblob.h"
#include "fileio.h"
#include "parson.h"
#include <strsafe.h>
#include <wincrypt.h>
#include <stdlib.h>
#include <string.h>

#define PAC_STORE_MAX (48u * 1024u * 1024u)
static const char PAC_DPAPI_SALT[] = "utgard-pac-source-v2";
static int save_blocked, notice_pending;
static wchar_t unreadable_path[2048];
static SRWLOCK notice_lock = SRWLOCK_INIT;
_Static_assert(sizeof(wchar_t) == sizeof(uint16_t), "Windows UTF-16 required");

static int unprotect_blob(const char *encoded, BYTE **data, DWORD *size)
{
    DATA_BLOB in, entropy, plain; BYTE encrypted[12288]; DWORD bytes = sizeof encrypted; int ok = 0;
    *data = NULL; *size = 0; ZeroMemory(&plain, sizeof plain);
    if (!encoded || !CryptStringToBinaryA(encoded, 0, CRYPT_STRING_BASE64, encrypted, &bytes, NULL, NULL)) return 0;
    in.pbData = encrypted; in.cbData = bytes; entropy.pbData = (BYTE *)PAC_DPAPI_SALT; entropy.cbData = (DWORD)(sizeof PAC_DPAPI_SALT - 1);
    if (CryptUnprotectData(&in, NULL, &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        *data = plain.pbData; *size = plain.cbData; ok = 1;
    }
    SecureZeroMemory(encrypted, sizeof encrypted); return ok;
}

static int legacy_path(wchar_t path[1024])
{
    wchar_t *slash; DWORD n = GetModuleFileNameW(NULL, path, 1024);
    if (!n || n >= 1024 || !(slash = wcsrchr(path, L'\\'))) return 0;
    return SUCCEEDED(StringCchCopyW(slash + 1, 1024 - (slash + 1 - path), L"pac.json"));
}

/* Only format 3 exists: the source and the SHA-256 of the stored script
   sit together inside one DPAPI blob. Anything else is unreadable. */
static int load_item(JSON_Object *o, pac_item *item)
{
    const char *protected_source = o ? json_object_get_string(o, "source_protected") : NULL;
    const char *script = o ? json_object_get_string(o, "script") : NULL;
    BYTE *plain; DWORD plain_size; size_t length, units = 0; int ok;
    if (!script || !protected_source || !json_object_has_value_of_type(o, "enabled", JSONBoolean)) return 0;
    length = strlen(script);
    if (!length || length > PAC_MAX || !unprotect_blob(protected_source, &plain, &plain_size)) return 0;
    ok = pacblob_unpack(plain, plain_size, script, length, (uint16_t *)item->source, 2048, &units);
    SecureZeroMemory(plain, plain_size);
    LocalFree(plain);
    if (!ok) return 0;
    item->text = (char *)malloc(length + 1);
    if (!item->text) return 0;
    memcpy(item->text, script, length + 1);
    item->enabled = json_object_get_boolean(o, "enabled") == 1;
    return 1;
}

/* path NULL: the file stays where it is (it may be fine, memory ran out),
   so saving is blocked for the session instead - as when the move fails. */
static void set_aside(const wchar_t *path)
{
    SYSTEMTIME t; wchar_t aside[2048]; int moved;
    GetLocalTime(&t);
    moved = path && SUCCEEDED(StringCchPrintfW(aside, 2048, L"%s.unreadable-%04u%02u%02u-%02u%02u%02u",
        path, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond)) &&
        MoveFileExW(path, aside, MOVEFILE_WRITE_THROUGH);
    AcquireSRWLockExclusive(&notice_lock);
    if (moved) StringCchCopyW(unreadable_path, 2048, aside);
    else { unreadable_path[0] = 0; save_blocked = 1; }
    notice_pending = 1;
    ReleaseSRWLockExclusive(&notice_lock);
}

/* pac.json of versions before 2.3.2 (format 3), read once to move it. */
static int legacy_load(pac_store *s)
{
    wchar_t path[1024]; char *buffer; JSON_Value *v = NULL; JSON_Object *root; JSON_Array *items;
    size_t i, count; int ok = 0, version;
    ZeroMemory(s, sizeof *s);
    if (save_blocked) return 1;
    if (!legacy_path(path)) return 0;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
    buffer = (char *)malloc(PAC_STORE_MAX + 1u); if (!buffer) return 0;
    if (file_read(path, buffer, PAC_STORE_MAX + 1u, NULL) != 1) { free(buffer); set_aside(path); return 1; }
    v = json_parse_string(buffer); SecureZeroMemory(buffer, PAC_STORE_MAX + 1u); free(buffer);
    root = json_value_get_object(v); if (!root) goto unreadable;
    version = (int)json_object_get_number(root, "version"); items = json_object_get_array(root, "items");
    if (version != 3 || !items) goto unreadable;
    count = json_array_get_count(items); if (count > PAC_ITEMS_MAX) goto unreadable;
    for (i = 0; i < count; i++) if (!load_item(json_array_get_object(items, i), &s->items[i])) goto unreadable;
    s->count = (int)count; ok = 1; goto done;
unreadable:
    pacstore_free(s); set_aside(path); ok = 1;
done:
    json_value_free(v); return ok;
}


int pacstore_unreadable_notice(wchar_t *path, size_t cap)
{
    int pending; AcquireSRWLockExclusive(&notice_lock); pending = notice_pending;
    if (pending) {
        if (path && cap) StringCchCopyW(path, cap, unreadable_path);
        notice_pending = 0;
    }
    ReleaseSRWLockExclusive(&notice_lock); return pending;
}

void pacstore_free(pac_store *s)
{
    int i; if (!s) return;
    for (i = 0; i < PAC_ITEMS_MAX; i++) { if (s->items[i].text) { SecureZeroMemory(s->items[i].text, strlen(s->items[i].text)); free(s->items[i].text); } }
    ZeroMemory(s, sizeof *s);
}
int pacstore_enabled(const pac_store *s)
{
    int i;
    for (i = 0; s && i < s->count; i++)
        if (s->items[i].enabled) return 1;
    return 0;
}

/* ---- one PAC per file: list\pac\01.pac .. 08.pac (from 2.3.2) ------------ */

#define PAC_FILE_MAX (PAC_MAX + 2 * PAC_BLOB_SOURCE_MAX + 64u * 1024u)

static int item_path(int n, wchar_t path[1024])
{
    wchar_t *slash; DWORD len = GetModuleFileNameW(NULL, path, 1024);
    if (!len || len >= 1024 || !(slash = wcsrchr(path, L'\\'))) return 0;
    if (n == 0) return SUCCEEDED(StringCchCopyW(slash + 1, 1024 - (slash + 1 - path), L"list\\pac"));
    return SUCCEEDED(StringCchPrintfW(slash + 1, 1024 - (slash + 1 - path), L"list\\pac\\%02d.pac", n));
}

/* One file into one item; the file is set aside when it cannot be read. */
static int load_file(const wchar_t *path, pac_item *item)
{
    BYTE *raw = (BYTE *)malloc(PAC_FILE_MAX); size_t got = 0; DATA_BLOB in, entropy, plain;
    const unsigned char *script; size_t script_length, units; int enabled, ok = 0, rd, oom = 0;
    if (!raw) { set_aside(NULL); return 0; }
    rd = file_read(path, raw, PAC_FILE_MAX, &got);
    ZeroMemory(&plain, sizeof plain);
    in.pbData = raw; in.cbData = (DWORD)got;
    entropy.pbData = (BYTE *)PAC_DPAPI_SALT; entropy.cbData = (DWORD)(sizeof PAC_DPAPI_SALT - 1);
    if (rd == 1 && got && CryptUnprotectData(&in, NULL, &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        if (pacitem_unpack(plain.pbData, plain.cbData, PAC_MAX, &enabled, (uint16_t *)item->source, 2048,
                           &units, &script, &script_length)) {
            if ((item->text = (char *)malloc(script_length + 1)) != NULL) {
                memcpy(item->text, script, script_length);
                item->text[script_length] = '\0';
                item->enabled = enabled;
                ok = 1;
            } else oom = 1;
        }
        SecureZeroMemory(plain.pbData, plain.cbData);
        LocalFree(plain.pbData);
    }
    free(raw);
    if (!ok) set_aside(oom ? NULL : path);
    return ok;
}

static int load_dir(pac_store *s, int *files)
{
    int k; wchar_t path[1024];
    *files = 0;
    for (k = 1; k <= PAC_ITEMS_MAX; k++) {
        if (!item_path(k, path)) return 0;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
        (*files)++;
        if (load_file(path, &s->items[s->count])) s->count++;
    }
    return 1;
}

int pacstore_load(pac_store *s)
{
    wchar_t old[1024]; int files;
    ZeroMemory(s, sizeof *s);
    if (save_blocked) return 1;
    if (!load_dir(s, &files)) return 0;
    if (files || !legacy_path(old) || GetFileAttributesW(old) == INVALID_FILE_ATTRIBUTES) return 1;
    /* Moving pac.json: read it the old way, write the files, read them back
       and compare, and only then delete it. Anything short of that keeps
       pac.json where it is. An unreadable pac.json is set aside as before. */
    if (!legacy_load(s) || !s->count) return 1;
    if (pacstore_save(s)) {
        pac_store check; int n, same;
        ZeroMemory(&check, sizeof check);
        same = load_dir(&check, &n) && check.count == s->count;
        for (n = 0; same && n < s->count; n++)
            same = check.items[n].enabled == s->items[n].enabled &&
                   !wcscmp(check.items[n].source, s->items[n].source) &&
                   !strcmp(check.items[n].text, s->items[n].text);
        pacstore_free(&check);
        if (same) DeleteFileW(old);
    }
    return 1;
}

int pacstore_save(const pac_store *s)
{
    wchar_t path[1024]; int i, ok = 1;
    if (save_blocked || !s || s->count < 0 || s->count > PAC_ITEMS_MAX || !item_path(0, path)) return 0;
    CreateDirectoryW(path, NULL);
    for (i = 0; ok && i < s->count; i++) {
        size_t length = s->items[i].text ? strlen(s->items[i].text) : 0, units = wcslen(s->items[i].source) + 1, n;
        size_t cap = 4 + units * 2 + 4 + length;
        unsigned char *plain;
        DATA_BLOB in, entropy, out;
        if (!length || length > PAC_MAX || !(plain = (unsigned char *)malloc(cap))) { ok = 0; break; }
        n = pacitem_pack(s->items[i].enabled, (const uint16_t *)s->items[i].source, units,
                         s->items[i].text, length, plain, cap);
        ZeroMemory(&out, sizeof out);
        in.pbData = plain; in.cbData = (DWORD)n;
        entropy.pbData = (BYTE *)PAC_DPAPI_SALT; entropy.cbData = (DWORD)(sizeof PAC_DPAPI_SALT - 1);
        ok = n && item_path(i + 1, path) &&
             CryptProtectData(&in, L"utgard PAC", &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) &&
             file_write(path, out.pbData, out.cbData);
        if (out.pbData) { SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData); }
        SecureZeroMemory(plain, cap);
        free(plain);
    }
    /* Files past the last PAC belong to ones since removed. */
    for (i = s->count + 1; ok && i <= PAC_ITEMS_MAX; i++)
        if (item_path(i, path)) DeleteFileW(path);
    return ok;
}
