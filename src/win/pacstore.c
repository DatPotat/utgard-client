#include "pacstore.h"
#include "fileio.h"
#include "parson.h"
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

#define PAC_STORE_MAX (48u * 1024u * 1024u)

static int store_path(wchar_t path[1024])
{
    wchar_t *slash;
    DWORD n = GetModuleFileNameW(NULL, path, 1024);
    if (!n || n >= 1024 || !(slash = wcsrchr(path, L'\\'))) return 0;
    return SUCCEEDED(StringCchCopyW(slash + 1, 1024 - (slash + 1 - path), L"pac.json"));
}

static int load_item(JSON_Object *o, pac_item *item)
{
    const char *source = o ? json_object_get_string(o, "source") : NULL;
    const char *script = o ? json_object_get_string(o, "script") : NULL;
    size_t length;
    if (!source || !script || !json_object_has_value_of_type(o, "enabled", JSONBoolean)) return 0;
    length = strlen(script);
    if (!length || length > PAC_MAX ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, item->source, 2048)) return 0;
    item->text = (char *)malloc(length + 1);
    if (!item->text) return 0;
    memcpy(item->text, script, length + 1);
    item->enabled = json_object_get_boolean(o, "enabled") == 1;
    item->via_vpn = json_object_has_value_of_type(o, "via_vpn", JSONBoolean) &&
                    json_object_get_boolean(o, "via_vpn") == 1;
    return 1;
}

int pacstore_load(pac_store *s)
{
    wchar_t path[1024];
    char *buffer;
    JSON_Value *v;
    JSON_Object *root;
    JSON_Array *items;
    size_t i, count;
    int ok = 0;
    ZeroMemory(s, sizeof *s);
    if (!store_path(path)) return 0;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
    buffer = (char *)malloc(PAC_STORE_MAX + 1u);
    if (!buffer) return 0;
    if (file_read(path, buffer, PAC_STORE_MAX + 1u, NULL) != 1) { free(buffer); return 0; }
    v = json_parse_string(buffer);
    free(buffer);
    root = json_value_get_object(v);
    if (!root) goto done;
    items = json_object_get_array(root, "items");
    if (!items) {
        s->count = 1;
        ok = load_item(root, &s->items[0]);
        goto done;
    }
    count = json_array_get_count(items);
    if (count > PAC_ITEMS_MAX) goto done;
    for (i = 0; i < count; i++)
        if (!load_item(json_array_get_object(items, i), &s->items[i])) goto done;
    s->count = (int)count;
    ok = 1;
done:
    json_value_free(v);
    if (!ok) pacstore_free(s);
    return ok;
}

int pacstore_save(const pac_store *s)
{
    wchar_t path[1024];
    JSON_Value *root, *list;
    char *text = NULL;
    int i, ok = 0;
    if (!s || s->count < 0 || s->count > PAC_ITEMS_MAX || !store_path(path)) return 0;
    root = json_value_init_object();
    list = json_value_init_array();
    if (!root || !list) { json_value_free(root); json_value_free(list); return 0; }
    for (i = 0; i < s->count; i++) {
        char source[8192];
        JSON_Value *value;
        JSON_Object *item;
        if (!s->items[i].text || !s->items[i].text[0] ||
            !WideCharToMultiByte(CP_UTF8, 0, s->items[i].source, -1,
                                 source, sizeof source, NULL, NULL)) goto done;
        value = json_value_init_object();
        if (!value) goto done;
        item = json_value_get_object(value);
        json_object_set_string(item, "source", source);
        json_object_set_string(item, "script", s->items[i].text);
        json_object_set_boolean(item, "enabled", s->items[i].enabled);
        json_object_set_boolean(item, "via_vpn", s->items[i].via_vpn);
        if (json_array_append_value(json_value_get_array(list), value) != JSONSuccess) {
            json_value_free(value); goto done;
        }
    }
    json_object_set_number(json_value_get_object(root), "version", 1);
    json_object_set_value(json_value_get_object(root), "items", list);
    list = NULL;
    text = json_serialize_to_string(root);
    ok = text && file_write(path, text, strlen(text));
done:
    json_free_serialized_string(text);
    json_value_free(list);
    json_value_free(root);
    return ok;
}

void pacstore_free(pac_store *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < PAC_ITEMS_MAX; i++) free(s->items[i].text);
    ZeroMemory(s, sizeof *s);
}

int pacstore_enabled(const pac_store *s)
{
    int i;
    for (i = 0; s && i < s->count; i++) if (s->items[i].enabled) return 1;
    return 0;
}
