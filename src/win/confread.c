#include "confread.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFREAD_MAX (16 * 1024 * 1024)

enum { READ_OK, READ_NO_OPEN, READ_BAD };

/* Shared for reading and writing, as fopen "rb" was: a config open in an
   editor must still be readable. */
static int read_text(const char *path, char **text)
{
    wchar_t       wpath[1024];
    HANDLE        h;
    LARGE_INTEGER size;
    DWORD         got = 0;
    char         *buf;

    *text = NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) == 0) return READ_NO_OPEN;
    h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return READ_NO_OPEN;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > CONFREAD_MAX ||
        !(buf = (char *)malloc((size_t)size.QuadPart + 1))) {
        CloseHandle(h);
        return READ_BAD;
    }
    if (!ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL)) {
        CloseHandle(h);
        free(buf);
        return READ_BAD;
    }
    CloseHandle(h);
    buf[got] = '\0';
    *text = buf;
    return READ_OK;
}

void confread_free(genconf_file *base, genconf_file *overlays, int count)
{
    int i;
    if (base) { free((char *)base->text); base->text = NULL; }
    for (i = 0; overlays && i < count; i++) { free((char *)overlays[i].text); overlays[i].text = NULL; }
}

int confread_inputs(const char *base_path, const char *const *overlay_paths, int count,
                    genconf_file *base, genconf_file *overlays, char *err, size_t errcap)
{
    char *text;
    int   i, r;

    base->name = base_path;
    base->text = NULL;
    for (i = 0; i < count; i++) { overlays[i].name = overlay_paths[i]; overlays[i].text = NULL; }

    r = read_text(base_path, &text);
    if (r != READ_OK) {
        if (err && errcap) {
            if (r == READ_NO_OPEN) snprintf(err, errcap, "Не найден файл %s", base_path);
            else snprintf(err, errcap, "%s", GENCONF_MSG_BAD_BASE);
        }
        return 0;
    }
    base->text = text;
    for (i = 0; i < count; i++) {
        if (read_text(overlay_paths[i], &text) != READ_OK) {
            if (err && errcap) snprintf(err, errcap, GENCONF_MSG_BAD_OVERLAY, overlay_paths[i]);
            confread_free(base, overlays, i);
            return 0;
        }
        overlays[i].text = text;
    }
    return 1;
}
