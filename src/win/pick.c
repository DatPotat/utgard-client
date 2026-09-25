#include "pick.h"

#include <tlhelp32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <wchar.h>

static int by_name(const void *a, const void *b)
{
    const pick_proc *x = (const pick_proc *)a, *y = (const pick_proc *)b;
    int c = _wcsicmp(x->name, y->name);
    return c ? c : _wcsicmp(x->path, y->path);
}

int pick_snapshot(pick_proc *out, int max)
{
    PROCESSENTRY32W pe;
    HANDLE          snap;
    int             n = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof pe;
    if (!Process32FirstW(snap, &pe)) { CloseHandle(snap); return 0; }

    do {
        wchar_t path[MAX_PATH] = { 0 };
        DWORD   len = MAX_PATH;
        HANDLE  h;
        int     i;

        if (pe.th32ProcessID == 0) continue;          /* System Idle Process */

        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (h) {
            if (!QueryFullProcessImageNameW(h, 0, path, &len)) path[0] = L'\0';
            CloseHandle(h);
        }

        /* Fold: same path is the same application. Without a path the name
           is all there is, so those fold by name. */
        for (i = 0; i < n; i++) {
            int same = path[0] ? (_wcsicmp(out[i].path, path) == 0)
                               : (!out[i].path[0] &&
                                  _wcsicmp(out[i].name, pe.szExeFile) == 0);
            if (same) break;
        }

        if (i < n) {
            if (out[i].npids < PICK_PIDS) out[i].pids[out[i].npids++] = pe.th32ProcessID;
            out[i].count++;
            continue;
        }
        if (n >= max) continue;

        ZeroMemory(&out[n], sizeof out[n]);
        StringCchCopyW(out[n].name, MAX_PATH, pe.szExeFile);
        StringCchCopyW(out[n].path, MAX_PATH, path);
        out[n].pids[0] = pe.th32ProcessID;
        out[n].npids   = 1;
        out[n].count   = 1;
        n++;
    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);
    if (n > 1) qsort(out, (size_t)n, sizeof out[0], by_name);
    return n;
}

static int contains_ci(const wchar_t *hay, const wchar_t *needle)
{
    size_t nl = wcslen(needle), i;

    if (nl == 0) return 1;
    for (; *hay; hay++) {
        for (i = 0; i < nl; i++)
            if (towlower(hay[i]) != towlower(needle[i])) break;
        if (i == nl) return 1;
    }
    return 0;
}

int pick_match(const pick_proc *p, const wchar_t *query)
{
    const wchar_t *q;
    int            digits = 1;

    if (!query || !query[0]) return 1;
    if (contains_ci(p->name, query)) return 1;

    for (q = query; *q; q++) if (*q < L'0' || *q > L'9') { digits = 0; break; }
    if (digits) {
        DWORD want = (DWORD)wcstoul(query, NULL, 10);
        int   i;
        for (i = 0; i < p->npids; i++) if (p->pids[i] == want) return 1;
    }
    return 0;
}

#define ICON_CACHE 256

typedef struct { wchar_t path[MAX_PATH]; HICON icon; } icon_slot;
static icon_slot g_icons[ICON_CACHE];
static int       g_icons_n;

HICON pick_icon(const wchar_t *path)
{
    SHFILEINFOW info;
    int         i;

    if (!path || !path[0]) return NULL;
    for (i = 0; i < g_icons_n; i++)
        if (_wcsicmp(g_icons[i].path, path) == 0) return g_icons[i].icon;

    /* A full cache means no icon rather than an uncached one: handing out a
       fresh HICON on every repaint, two seconds apart, would leak one per
       row per refresh. */
    if (g_icons_n >= ICON_CACHE) return NULL;

    ZeroMemory(&info, sizeof info);
    if (!SHGetFileInfoW(path, 0, &info, sizeof info, SHGFI_ICON | SHGFI_SMALLICON))
        info.hIcon = NULL;

    StringCchCopyW(g_icons[g_icons_n].path, MAX_PATH, path);
    g_icons[g_icons_n].icon = info.hIcon;
    g_icons_n++;
    return info.hIcon;
}
