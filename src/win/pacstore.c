#include "pacstore.h"
#include "pacblob.h"
#include "fileio.h"
#include "parson.h"
#include <strsafe.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#define PAC_STORE_MAX (48u * 1024u * 1024u)
static const char PAC_DPAPI_SALT[] = "utgard-pac-source-v2";
static int save_blocked, notice_pending;
static wchar_t unreadable_path[2048];
static SRWLOCK notice_lock = SRWLOCK_INIT;
_Static_assert(sizeof(wchar_t) == sizeof(uint16_t), "Windows UTF-16 required");

static int script_hash(const char *text, size_t length, char hex[65])
{
    BYTE hash[32]; DWORD size = sizeof hash; int i;
    if (length > MAXDWORD || !CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, NULL,
        (const BYTE *)text, (DWORD)length, hash, &size) || size != sizeof hash) return 0;
    for (i = 0; i < 32; i++) StringCchPrintfA(hex + 2 * i, 3, "%02x", hash[i]);
    SecureZeroMemory(hash, sizeof hash); return 1;
}

static int protect_blob(const BYTE *plain, DWORD plain_size, char out[16384])
{
    DATA_BLOB in, entropy, encrypted; DWORD chars = 16384; int ok = 0;
    ZeroMemory(&encrypted, sizeof encrypted); in.pbData = (BYTE *)plain; in.cbData = plain_size;
    entropy.pbData = (BYTE *)PAC_DPAPI_SALT; entropy.cbData = (DWORD)(sizeof PAC_DPAPI_SALT - 1);
    if (CryptProtectData(&in, L"utgard PAC source", &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        ok = CryptBinaryToStringA(encrypted.pbData, encrypted.cbData,
             CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &chars) != 0;
        SecureZeroMemory(encrypted.pbData, encrypted.cbData); LocalFree(encrypted.pbData);
    }
    return ok;
}

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

static int store_path(wchar_t path[1024])
{
    wchar_t *slash; DWORD n = GetModuleFileNameW(NULL, path, 1024);
    if (!n || n >= 1024 || !(slash = wcsrchr(path, L'\\'))) return 0;
    return SUCCEEDED(StringCchCopyW(slash + 1, 1024 - (slash + 1 - path), L"pac.json"));
}

static int load_item(JSON_Object *o, pac_item *item, int version)
{
    const char *source = o ? json_object_get_string(o, "source") : NULL;
    const char *protected_source = o ? json_object_get_string(o, "source_protected") : NULL;
    const char *script = o ? json_object_get_string(o, "script") : NULL;
    const char *saved_hash = o ? json_object_get_string(o, "sha256") : NULL;
    char hash[65]; size_t length;
    if (!script || !json_object_has_value_of_type(o, "enabled", JSONBoolean)) return 0;
    length = strlen(script); if (!length || length > PAC_MAX) return 0;
    if (version >= 3) {
        BYTE *plain; DWORD plain_size; size_t units = 0;
        if (!protected_source || !unprotect_blob(protected_source, &plain, &plain_size)) return 0;
        if (!pacblob_unpack(plain, plain_size, script, length, (uint16_t *)item->source, 2048, &units)) {
            SecureZeroMemory(plain, plain_size); LocalFree(plain); return 0;
        }
        SecureZeroMemory(plain, plain_size); LocalFree(plain);
    } else if (version >= 2) {
        BYTE *plain; DWORD plain_size;
        if (!protected_source || !saved_hash || !script_hash(script, length, hash) || _stricmp(saved_hash, hash) ||
            !unprotect_blob(protected_source, &plain, &plain_size)) return 0;
        if (plain_size < sizeof(wchar_t) || plain_size > sizeof item->source || plain_size % sizeof(wchar_t) ||
            ((wchar_t *)plain)[plain_size / sizeof(wchar_t) - 1]) {
            SecureZeroMemory(plain, plain_size); LocalFree(plain); return 0;
        }
        memcpy(item->source, plain, plain_size); SecureZeroMemory(plain, plain_size); LocalFree(plain);
    } else if (!source || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, item->source, 2048)) return 0;
    else SecureZeroMemory((void *)source, strlen(source));
    item->text = (char *)malloc(length + 1); if (!item->text) return 0; memcpy(item->text, script, length + 1);
    item->enabled = json_object_get_boolean(o, "enabled") == 1;
    item->via_vpn = json_object_has_value_of_type(o, "via_vpn", JSONBoolean) && json_object_get_boolean(o, "via_vpn") == 1;
    return 1;
}

static void set_aside(const wchar_t *path)
{
    SYSTEMTIME t; wchar_t aside[2048]; int moved;
    GetLocalTime(&t);
    moved = SUCCEEDED(StringCchPrintfW(aside, 2048, L"%s.unreadable-%04u%02u%02u-%02u%02u%02u",
        path, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond)) &&
        MoveFileExW(path, aside, MOVEFILE_WRITE_THROUGH);
    AcquireSRWLockExclusive(&notice_lock);
    if (moved) StringCchCopyW(unreadable_path, 2048, aside);
    else { unreadable_path[0] = 0; save_blocked = 1; }
    notice_pending = 1;
    ReleaseSRWLockExclusive(&notice_lock);
}

int pacstore_load(pac_store *s)
{
    wchar_t path[1024]; char *buffer; JSON_Value *v = NULL; JSON_Object *root; JSON_Array *items;
    size_t i, count; int ok = 0, version;
    ZeroMemory(s, sizeof *s); if (save_blocked) return 1; if (!store_path(path)) return 0;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
    buffer = (char *)malloc(PAC_STORE_MAX + 1u); if (!buffer) return 0;
    if (file_read(path, buffer, PAC_STORE_MAX + 1u, NULL) != 1) { free(buffer); set_aside(path); return 1; }
    v = json_parse_string(buffer); SecureZeroMemory(buffer, PAC_STORE_MAX + 1u); free(buffer);
    root = json_value_get_object(v); if (!root) goto unreadable;
    version = (int)json_object_get_number(root, "version"); items = json_object_get_array(root, "items");
    if (!items) { s->count = 1; ok = load_item(root, &s->items[0], 0); goto done; }
    if (!pacblob_store_version_supported(version)) goto unreadable;
    count = json_array_get_count(items); if (count > PAC_ITEMS_MAX) goto unreadable;
    for (i = 0; i < count; i++) if (!load_item(json_array_get_object(items, i), &s->items[i], version)) goto unreadable;
    s->count = (int)count; ok = 1; goto done;
unreadable:
    pacstore_free(s); set_aside(path); ok = 1;
done:
    json_value_free(v); return ok;
}

int pacstore_save(const pac_store *s)
{
    wchar_t path[1024]; JSON_Value *root, *list; char *text = NULL; int i, ok = 0;
    if (save_blocked || !s || s->count < 0 || s->count > PAC_ITEMS_MAX || !store_path(path)) return 0;
    root = json_value_init_object(); list = json_value_init_array();
    if (!root || !list) { json_value_free(root); json_value_free(list); return 0; }
    for (i = 0; i < s->count; i++) {
        unsigned char plain[33 + 2048 * 2]; char encoded[16384]; size_t length, plain_size, units;
        JSON_Value *value; JSON_Object *item;
        if (!s->items[i].text || !(length = strlen(s->items[i].text)) || length > PAC_MAX) goto done;
        units = wcslen(s->items[i].source) + 1;
        plain_size = pacblob_pack(s->items[i].text, length, (const uint16_t *)s->items[i].source, units, plain, sizeof plain);
        if (!plain_size || !protect_blob(plain, (DWORD)plain_size, encoded)) { SecureZeroMemory(plain, sizeof plain); goto done; }
        SecureZeroMemory(plain, sizeof plain); value = json_value_init_object(); if (!value) goto done; item = json_value_get_object(value);
        json_object_set_string(item, "source_protected", encoded); SecureZeroMemory(encoded, sizeof encoded);
        json_object_set_string(item, "script", s->items[i].text); json_object_set_boolean(item, "enabled", s->items[i].enabled);
        json_object_set_boolean(item, "via_vpn", s->items[i].via_vpn);
        if (json_array_append_value(json_value_get_array(list), value) != JSONSuccess) { json_value_free(value); goto done; }
    }
    json_object_set_number(json_value_get_object(root), "version", 3); json_object_set_value(json_value_get_object(root), "items", list); list = NULL;
    text = json_serialize_to_string(root); ok = text && file_write(path, text, strlen(text));
done:
    json_free_serialized_string(text); json_value_free(list); json_value_free(root); return ok;
}

int pacstore_unreadable_notice(wchar_t *path, size_t cap)
{
    int pending; AcquireSRWLockExclusive(&notice_lock); pending = notice_pending;
    if (pending) { if (path && cap) StringCchCopyW(path, cap, unreadable_path); notice_pending = 0; }
    ReleaseSRWLockExclusive(&notice_lock); return pending;
}

void pacstore_free(pac_store *s)
{
    int i; if (!s) return;
    for (i = 0; i < PAC_ITEMS_MAX; i++) { if (s->items[i].text) { SecureZeroMemory(s->items[i].text, strlen(s->items[i].text)); free(s->items[i].text); } }
    ZeroMemory(s, sizeof *s);
}
int pacstore_enabled(const pac_store *s)
{ int i; for (i = 0; s && i < s->count; i++) if (s->items[i].enabled) return 1; return 0; }
