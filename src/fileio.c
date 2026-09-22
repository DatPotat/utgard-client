#include "fileio.h"

#include <strsafe.h>

int file_read(const wchar_t *path, void *buf, size_t cap, size_t *got)
{
    HANDLE h;
    DWORD  n = 0;
    BOOL   ok;

    if (got) *got = 0;
    if (!buf || cap == 0) return 0;
    ((char *)buf)[0] = '\0';

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = ReadFile(h, buf, (DWORD)(cap - 1), &n, NULL);
    CloseHandle(h);
    if (!ok) return 0;

    ((char *)buf)[n] = '\0';
    if (got) *got = n;
    return 1;
}

int file_write(const wchar_t *path, const void *data, size_t len)
{
    wchar_t tmp[MAX_PATH * 2 + 8];
    HANDLE  h;
    DWORD   put = 0;
    BOOL    ok;

    if (FAILED(StringCchPrintfW(tmp, sizeof tmp / sizeof tmp[0], L"%s.tmp", path)))
        return 0;

    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = (len == 0 || (WriteFile(h, data, (DWORD)len, &put, NULL) && put == len))
         && FlushFileBuffers(h);
    CloseHandle(h);

    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return 0;
    }
    return 1;
}
