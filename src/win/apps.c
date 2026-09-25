#include "apps.h"
#include "singbox.h"
#include "parson.h"
#include "fileio.h"

#include <strsafe.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <stdlib.h>
#include <string.h>

static int apps_dir(const wchar_t *tail, wchar_t *out, size_t cap)
{
    wchar_t root[MAX_PATH * 2];

    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    if (!tail || !tail[0])
        return SUCCEEDED(StringCchPrintfW(out, cap, L"%slist\\applications", root));
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%slist\\applications\\%s",
                                      root, tail));
}

/* Create the whole chain, parents first. CreateDirectoryW makes one level at
   a time, so asking for list\applications\active before list\ exists quietly
   fails - which is exactly what happened on a fresh folder, because the caller
   created list\ only after calling this. Nothing here relies on call order
   any more. */
static int ensure_dirs(void)
{
    wchar_t root[MAX_PATH * 2], path[MAX_PATH * 2];
    static const wchar_t *chain[] = {
        L"list", L"list\\applications",
        L"list\\applications\\active", L"list\\applications\\inactive"
    };
    size_t i;

    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    for (i = 0; i < sizeof chain / sizeof chain[0]; i++) {
        DWORD a;
        if (FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s%s", root, chain[i])))
            return 0;
        CreateDirectoryW(path, NULL);
        a = GetFileAttributesW(path);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
            return 0;
    }
    return 1;
}

int apps_prepare(void)
{
    wchar_t          base[MAX_PATH * 2], act[MAX_PATH * 2];
    wchar_t          mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;

    if (!ensure_dirs()) return 0;
    if (!apps_dir(NULL, base, MAX_PATH * 2) ||
        !apps_dir(L"active", act, MAX_PATH * 2))
        return 0;

    /* Files from the earlier flat layout are enabled, which is what they were:
       back then everything in the folder was applied. */
    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*.json", base))) return 1;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    do {
        wchar_t from[MAX_PATH * 2], to[MAX_PATH * 2];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (FAILED(StringCchPrintfW(from, MAX_PATH * 2, L"%s\\%s", base, fd.cFileName)) ||
            FAILED(StringCchPrintfW(to, MAX_PATH * 2, L"%s\\%s", act, fd.cFileName)))
            continue;
        if (GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES) MoveFileW(from, to);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}


/* ---- the window ----------------------------------------------------- */

static app_entry g_apps[APPS_MAX];
static int       g_apps_n;

static int cmp_name(const void *a, const void *b)
{
    return _wcsicmp(((const app_entry *)a)->name, ((const app_entry *)b)->name);
}

static void scan_dir(const wchar_t *which, int enabled)
{
    wchar_t          dir[MAX_PATH * 2], mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;

    if (!apps_dir(which, dir, MAX_PATH * 2)) return;
    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*.json", dir))) return;

    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t *dot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (g_apps_n >= APPS_MAX) break;
        if (FAILED(StringCchCopyW(g_apps[g_apps_n].name, APPS_NAME_MAX, fd.cFileName)))
            continue;
        dot = wcsrchr(g_apps[g_apps_n].name, L'.');
        if (dot) *dot = L'\0';
        g_apps[g_apps_n].enabled = enabled;
        g_apps_n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int apps_scan(app_entry *out, int max)
{
    int n;

    g_apps_n = 0;
    scan_dir(L"active", 1);
    scan_dir(L"inactive", 0);
    if (g_apps_n > 1) qsort(g_apps, (size_t)g_apps_n, sizeof g_apps[0], cmp_name);

    n = g_apps_n < max ? g_apps_n : max;
    if (out && n > 0) memcpy(out, g_apps, (size_t)n * sizeof g_apps[0]);
    return n;
}

int apps_set_enabled(const app_entry *e, int enable)
{
    wchar_t from[MAX_PATH * 2], to[MAX_PATH * 2];
    wchar_t src[MAX_PATH * 2], dst[MAX_PATH * 2];

    if (!e || !e->name[0]) return 0;
    ensure_dirs();          /* the folders may have been removed meanwhile */
    if (!apps_dir(e->enabled ? L"active" : L"inactive", from, MAX_PATH * 2) ||
        !apps_dir(enable ? L"active" : L"inactive", to, MAX_PATH * 2))
        return 0;
    if (FAILED(StringCchPrintfW(src, MAX_PATH * 2, L"%s\\%s.json", from, e->name)) ||
        FAILED(StringCchPrintfW(dst, MAX_PATH * 2, L"%s\\%s.json", to, e->name)))
        return 0;

    if (e->enabled == enable) return 1;
    DeleteFileW(dst);
    return MoveFileW(src, dst) ? 1 : 0;
}

int apps_delete(const app_entry *e)
{
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];

    if (!e || !e->name[0]) return 0;
    if (!apps_dir(e->enabled ? L"active" : L"inactive", dir, MAX_PATH * 2)) return 0;
    if (FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\%s.json", dir, e->name)))
        return 0;
    return DeleteFileW(path) ? 1 : 0;
}


/* ---- names and writing ---------------------------------------------- */

int apps_name_ok(const wchar_t *name)
{
    const wchar_t *p;

    if (!name || !name[0] || wcslen(name) >= APPS_NAME_MAX) return 0;
    for (p = name; *p; p++) {
        wchar_t c = *p;
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'-' || c == L'_'))
            return 0;
    }
    return 1;
}

int apps_exists(const wchar_t *name)
{
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    static const wchar_t *where[2] = { L"active", L"inactive" };
    int i;

    for (i = 0; i < 2; i++) {
        if (!apps_dir(where[i], dir, MAX_PATH * 2)) continue;
        if (FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\%s.json", dir, name)))
            continue;
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}

static int w2u(const wchar_t *src, char *out, int cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, out, cap, NULL, NULL) != 0;
}

static void add_rule(JSON_Array *rules, const char *field, JSON_Value *values)
{
    JSON_Value  *r = json_value_init_object();
    JSON_Object *o = json_value_get_object(r);

    json_object_set_value(o, field, values);
    json_object_set_string(o, "action", "route");
    json_object_set_string(o, "outbound", "utgard");
    json_array_append_value(rules, r);
}

int apps_write(const wchar_t *name, const wchar_t names[][MAX_PATH],
               int name_count, const wchar_t paths[][MAX_PATH], int path_count,
               int enabled, wchar_t *err, size_t errcap)
{
    JSON_Value  *root, *rulesv, *nv, *pv;
    JSON_Array  *rules;
    char         buf[MAX_PATH * 3];
    char        *text;
    wchar_t      dir[MAX_PATH * 2], path[MAX_PATH * 2];
    int          i, got_names = 0, got_paths = 0;

    if (!apps_name_ok(name)) {
        if (err && errcap) StringCchCopyW(err, errcap,
            L"Имя списка — латиница, цифры, дефис и подчёркивание");
        return 0;
    }

    root   = json_value_init_object();
    rulesv = json_value_init_array();
    rules  = json_value_get_array(rulesv);
    nv     = json_value_init_array();
    pv     = json_value_init_array();

    for (i = 0; i < name_count; i++)
        if (names[i][0] && w2u(names[i], buf, (int)sizeof buf)) {
            json_array_append_string(json_value_get_array(nv), buf);
            got_names++;
        }
    for (i = 0; i < path_count; i++)
        if (paths[i][0] && w2u(paths[i], buf, (int)sizeof buf)) {
            json_array_append_string(json_value_get_array(pv), buf);
            got_paths++;
        }

    if (!got_names && !got_paths) {
        json_value_free(nv); json_value_free(pv);
        json_value_free(rulesv); json_value_free(root);
        if (err && errcap) StringCchCopyW(err, errcap, L"Список пуст");
        return 0;
    }

    if (got_names) add_rule(rules, "process_name", nv); else json_value_free(nv);
    if (got_paths) add_rule(rules, "process_path", pv); else json_value_free(pv);

    {
        JSON_Value *routev = json_value_init_object();
        json_object_set_value(json_value_get_object(routev), "rules", rulesv);
        json_object_set_value(json_value_get_object(root), "route", routev);
    }

    text = json_serialize_to_string_pretty(root);
    json_value_free(root);
    ensure_dirs();
    if (!text) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось собрать файл");
        return 0;
    }

    if (!apps_dir(enabled ? L"active" : L"inactive", dir, MAX_PATH * 2) ||
        FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\%s.json", dir, name))) {
        json_free_serialized_string(text);
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось определить путь");
        return 0;
    }

    {
        int ok = file_write(path, text, strlen(text));
        json_free_serialized_string(text);
        if (ok) return 1;
    }
    {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось записать файл");
        return 0;
    }
    return 1;
}

/* ---- reading back ----------------------------------------------------- */

static int u2w(const char *src, wchar_t *out, int cap)
{
    return MultiByteToWideChar(CP_UTF8, 0, src, -1, out, cap) != 0;
}

/* Collect one field's strings; 0 if the value is not a plain string array. */
static int take_strings(JSON_Array *arr, wchar_t out[][MAX_PATH], int *count, int max)
{
    size_t i, n = json_array_get_count(arr);

    for (i = 0; i < n; i++) {
        const char *v = json_array_get_string(arr, i);
        if (!v) return 0;
        if (*count >= max) return 0;
        if (!u2w(v, out[*count], MAX_PATH)) return 0;
        (*count)++;
    }
    return 1;
}

int apps_read(const app_entry *e, wchar_t names[][MAX_PATH], int *name_count,
              wchar_t paths[][MAX_PATH], int *path_count, int max)
{
    static char  text[65536];
    wchar_t      dir[MAX_PATH * 2], path[MAX_PATH * 2];
    JSON_Value  *root;
    JSON_Object *ro, *route;
    JSON_Array  *rules;
    size_t       i, n;
    int          ok = 0;

    *name_count = 0;
    *path_count = 0;
    if (!e || !e->name[0]) return 0;

    if (!apps_dir(e->enabled ? L"active" : L"inactive", dir, MAX_PATH * 2) ||
        FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\%s.json", dir, e->name)))
        return 0;

    if (!file_read(path, text, sizeof text, NULL)) return 0;

    root = json_parse_string(text);
    if (!root || json_value_get_type(root) != JSONObject) goto done;
    ro = json_value_get_object(root);

    /* Exactly {"route": {"rules": [...]}} - one key at each level. */
    if (json_object_get_count(ro) != 1) goto done;
    route = json_object_get_object(ro, "route");
    if (!route || json_object_get_count(route) != 1) goto done;
    rules = json_object_get_array(route, "rules");
    if (!rules) goto done;

    n = json_array_get_count(rules);
    for (i = 0; i < n; i++) {
        JSON_Object *r = json_array_get_object(rules, i);
        const char  *action, *outbound;
        JSON_Array  *names_arr, *paths_arr;

        if (!r || json_object_get_count(r) != 3) goto done;
        action   = json_object_get_string(r, "action");
        outbound = json_object_get_string(r, "outbound");
        if (!action || strcmp(action, "route") != 0) goto done;
        if (!outbound || strcmp(outbound, "utgard") != 0) goto done;

        names_arr = json_object_get_array(r, "process_name");
        paths_arr = json_object_get_array(r, "process_path");
        /* One of the two, never both: a rule with both means name AND path. */
        if ((names_arr != NULL) == (paths_arr != NULL)) goto done;

        if (names_arr && !take_strings(names_arr, names, name_count, max)) goto done;
        if (paths_arr && !take_strings(paths_arr, paths, path_count, max)) goto done;
    }
    ok = 1;

done:
    if (root) json_value_free(root);
    if (!ok) { *name_count = 0; *path_count = 0; }
    return ok;
}
