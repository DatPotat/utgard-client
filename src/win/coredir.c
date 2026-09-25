#include "coredir.h"
#include "singbox.h"

#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <stdlib.h>
#include <strsafe.h>

/* SYSTEM and Administrators full control, Users read and execute. */
static const wchar_t CORE_SDDL[] =
    L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";

/* Rights that let their holder change the object or what is inside it. */
#define WRITING_RIGHTS (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |   \
                        FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |   \
                        WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL)

static int say(wchar_t *msg, size_t cap, const wchar_t *text)
{
    if (msg && cap) StringCchCopyW(msg, cap, text);
    return 0;
}

int coredir_path(const core_desc *c, wchar_t *out, size_t cap)
{
    wchar_t root[MAX_PATH * 2];
    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%s", root, c->dir));
}

int coredir_volume_has_acl(const wchar_t *path)
{
    wchar_t volume[MAX_PATH + 1];
    DWORD   flags = 0;

    if (!GetVolumePathNameW(path, volume, MAX_PATH + 1)) return 0;
    if (!GetVolumeInformationW(volume, NULL, 0, NULL, NULL, &flags, NULL, 0)) return 0;
    return (flags & FILE_PERSISTENT_ACLS) != 0;
}

static int is_admin_or_system(PSID sid)
{
    return sid && (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) ||
                   IsWellKnownSid(sid, WinLocalSystemSid));
}

int coredir_sd_safe(PSECURITY_DESCRIPTOR sd)
{
    PSID  owner = NULL;
    PACL  dacl = NULL;
    BOOL  defaulted = FALSE, present = FALSE;
    DWORD i;

    if (!sd || !GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !is_admin_or_system(owner))
        return 0;
    /* No DACL at all means everyone may do anything. */
    if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) || !present || !dacl)
        return 0;
    for (i = 0; i < dacl->AceCount; i++) {
        ACE_HEADER *ace;
        if (!GetAce(dacl, i, (void **)&ace)) return 0;
        if (ace->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (ace->AceType != ACCESS_ALLOWED_ACE_TYPE) return 0;   /* object, callback: not ours */
        if ((((ACCESS_ALLOWED_ACE *)ace)->Mask & WRITING_RIGHTS) &&
            !is_admin_or_system((PSID)&((ACCESS_ALLOWED_ACE *)ace)->SidStart))
            return 0;
    }
    return 1;
}

/* Open without following a reparse point and refuse if it is one: every
   later step then works on this handle, so nothing can be swapped under it.
   No FILE_SHARE_DELETE, so it cannot be renamed or deleted while open. */
static HANDLE open_plain(const wchar_t *path, DWORD access, int dir)
{
    BY_HANDLE_FILE_INFORMATION info;
    DWORD  a = GetFileAttributesW(path);
    HANDLE h;

    /* Checked by path as well: some file systems follow a link even when
       asked not to. The handle check below closes the gap in between. */
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
        return INVALID_HANDLE_VALUE;
    h = CreateFileW(path, access, FILE_SHARE_READ | (dir ? FILE_SHARE_WRITE : 0), NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | (dir ? FILE_FLAG_BACKUP_SEMANTICS : 0),
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != !!dir) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static int apply_sd(HANDLE h, PSECURITY_DESCRIPTOR sd)
{
    PSID owner = NULL;
    PACL dacl = NULL;
    BOOL present = FALSE, defaulted = FALSE;

    if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted) ||
        !GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted))
        return 0;
    return SetSecurityInfo(h, SE_FILE_OBJECT,
                           OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                           PROTECTED_DACL_SECURITY_INFORMATION,
                           owner, NULL, dacl, NULL) == ERROR_SUCCESS;
}

int coredir_protect(const core_desc *c, wchar_t *msg, size_t cap)
{
    wchar_t              dir[MAX_PATH * 2], file[MAX_PATH * 2];
    SECURITY_ATTRIBUTES  sa;
    PSECURITY_DESCRIPTOR sd = NULL;
    HANDLE               h;
    int                  i, ok = 0;

    if (!coredir_path(c, dir, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить папку ядра");
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(CORE_SDDL, SDDL_REVISION_1, &sd, NULL))
        return say(msg, cap, L"Не удалось подготовить права доступа");

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    if (!CreateDirectoryW(dir, &sa) && GetLastError() != ERROR_ALREADY_EXISTS) {
        say(msg, cap, L"Не удалось создать папку ядра");
        goto done;
    }

    h = open_plain(dir, READ_CONTROL | WRITE_DAC | WRITE_OWNER, 1);
    if (h == INVALID_HANDLE_VALUE) {
        StringCchPrintfW(msg, cap, L"Папка %s — это ссылка на другое место или не папка. "
                                   L"Удалите её, и программа создаст новую.", c->dir);
        goto done;
    }
    ok = apply_sd(h, sd);
    CloseHandle(h);
    if (!ok) { say(msg, cap, L"Не удалось защитить папку ядра"); goto done; }

    /* Files that are already there keep the permissions they were created
       with; set ours on each by handle, as for the folder. */
    for (i = 0; i < c->nfiles; i++) {
        if (FAILED(StringCchPrintfW(file, MAX_PATH * 2, L"%s\\%s", dir, c->files[i].name))) continue;
        h = open_plain(file, READ_CONTROL | WRITE_DAC | WRITE_OWNER, 0);
        if (h == INVALID_HANDLE_VALUE) continue;          /* absent: checked later */
        apply_sd(h, sd);
        CloseHandle(h);
    }

done:
    LocalFree(sd);
    return ok;
}

static const core_file *known(const core_desc *c, const wchar_t *name)
{
    int i;
    for (i = 0; i < c->nfiles; i++)
        if (_wcsicmp(name, c->files[i].name) == 0) return &c->files[i];
    return NULL;
}

void coredir_release(coredir_hold *h)
{
    int i;
    if (h->dir != INVALID_HANDLE_VALUE && h->dir) CloseHandle(h->dir);
    h->dir = INVALID_HANDLE_VALUE;
    for (i = 0; i < CORE_FILES_MAX; i++) {
        if (h->files[i] != INVALID_HANDLE_VALUE && h->files[i]) CloseHandle(h->files[i]);
        h->files[i] = INVALID_HANDLE_VALUE;
    }
}

int coredir_hold_verified(const core_desc *c, coredir_hold *h, wchar_t *msg, size_t cap,
                          int *reinstall_helps)
{
    wchar_t              dir[MAX_PATH * 2], path[MAX_PATH * 2], hex[80];
    WIN32_FIND_DATAW     fd;
    HANDLE               f;
    PSECURITY_DESCRIPTOR sd = NULL;
    int                  i, acl;

    h->dir = INVALID_HANDLE_VALUE;
    for (i = 0; i < CORE_FILES_MAX; i++) h->files[i] = INVALID_HANDLE_VALUE;
    if (msg && cap) msg[0] = L'\0';
    if (reinstall_helps) *reinstall_helps = 0;

    if (!coredir_path(c, dir, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить папку ядра");
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) {
        if (reinstall_helps) *reinstall_helps = 1;
        StringCchPrintfW(msg, cap, L"%s не загружен", c->title);
        return 0;
    }

    acl = coredir_volume_has_acl(dir);
    if (!acl && c->needs_acl) {
        StringCchPrintfW(msg, cap, L"%s недоступен: папка программы на диске без прав доступа "
                                   L"(FAT32 или exFAT). Перенесите Utgard на диск NTFS.", c->title);
        return 0;
    }
    if (acl && !coredir_protect(c, msg, cap)) return 0;

    /* A download interrupted halfway leaves its work folder behind. */
    if (SUCCEEDED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\install.tmp", dir)))
        coredir_wipe(path);

    h->dir = open_plain(dir, FILE_LIST_DIRECTORY | READ_CONTROL, 1);
    if (h->dir == INVALID_HANDLE_VALUE) {
        StringCchPrintfW(msg, cap, L"Папка %s — это ссылка на другое место или не папка", c->dir);
        goto fail;
    }
    if (acl) {
        int safe;
        if (GetSecurityInfo(h->dir, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION |
                            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &sd) != ERROR_SUCCESS) {
            say(msg, cap, L"Не удалось прочитать права папки ядра");
            goto fail;
        }
        safe = coredir_sd_safe(sd);
        LocalFree(sd);
        if (!safe) {
            StringCchPrintfW(msg, cap, L"Права папки %s позволяют менять её не только "
                                       L"администраторам. Запуск отменён.", c->dir);
            goto fail;
        }
    }

    /* Nothing but the core's own files: anything else beside the program -
       a DLL above all - would be loaded into it. */
    if (FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\*", dir))) goto fail;
    f = FindFirstFileW(path, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !known(c, fd.cFileName)) {
                FindClose(f);
                StringCchPrintfW(msg, cap, L"В папке %s лишний файл «%s». Запуск отменён: удалите его%s.",
                                 c->dir, fd.cFileName,
                                 acl ? L" (нужны права администратора)" : L"");
                goto fail;
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }

    for (i = 0; i < c->nfiles; i++) {
        if (FAILED(StringCchPrintfW(path, MAX_PATH * 2, L"%s\\%s", dir, c->files[i].name))) goto fail;
        h->files[i] = open_plain(path, GENERIC_READ, 0);
        if (h->files[i] == INVALID_HANDLE_VALUE) {
            if (!c->files[i].required && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                continue;
            if (reinstall_helps) *reinstall_helps = 1;
            StringCchPrintfW(msg, cap, L"%s: нет файла %s", c->title, c->files[i].name);
            goto fail;
        }
        if (!coredir_sha256_handle(h->files[i], hex, 80) ||
            _wcsicmp(hex, c->files[i].sha256) != 0) {
            if (reinstall_helps) *reinstall_helps = 1;
            StringCchPrintfW(msg, cap, L"%s: файл %s не совпадает с версией %s — изменён или "
                                       L"это другая версия. Запуск отменён.",
                             c->title, c->files[i].name, c->version);
            goto fail;
        }
    }
    return 1;

fail:
    coredir_release(h);
    return 0;
}

/* ---- shared by the installers (moved from singbox.c unchanged) ------- */

/* Hash through a handle the caller already holds. Hashing by path and then
   using the file by path leaves a window in which it can be swapped; hashing
   the handle that stays open closes it. */
int coredir_sha256_handle(HANDLE file, wchar_t *hex, size_t cap)
{
    BCRYPT_ALG_HANDLE  alg = NULL;
    BCRYPT_HASH_HANDLE h   = NULL;
    UCHAR    digest[32];
    UCHAR   *buf = NULL;
    DWORD    got;
    int      ok = 0, i;
    LARGE_INTEGER zero;

    if (cap < 65 || file == INVALID_HANDLE_VALUE) return 0;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN)) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
        return 0;
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) != 0) goto done;

    buf = (UCHAR *)malloc(65536);
    if (!buf) goto done;

    for (;;) {
        if (!ReadFile(file, buf, 65536, &got, NULL)) goto done;
        if (got == 0) break;
        if (BCryptHashData(h, buf, got, 0) != 0) goto done;
    }
    if (BCryptFinishHash(h, digest, sizeof digest, 0) != 0) goto done;

    for (i = 0; i < 32; i++)
        StringCchPrintfW(hex + i * 2, cap - (size_t)i * 2, L"%02x", digest[i]);
    ok = 1;

done:
    free(buf);
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

/* Open a file so that nobody can write to it, delete it or rename it while
   we hold it - share mode is read only - and check its hash through that
   same handle. On success the handle stays open for the caller. */
int coredir_open_verified(const wchar_t *path, const wchar_t *expect, HANDLE *out)
{
    wchar_t hex[80];
    HANDLE  h;

    *out = INVALID_HANDLE_VALUE;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!coredir_sha256_handle(h, hex, 80) || _wcsicmp(hex, expect) != 0) {
        CloseHandle(h);
        return 0;
    }
    *out = h;
    return 1;
}

void coredir_wipe(const wchar_t *dir)
{
    wchar_t          mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;
    DWORD            a = GetFileAttributesW(dir);

    if (a == INVALID_FILE_ATTRIBUTES) return;
    /* A junction or symlink is removed itself, never followed: following it
       would delete, with our rights, whatever it points at. */
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) { RemoveDirectoryW(dir); return; }

    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*", dir))) return;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t item[MAX_PATH * 2];
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (FAILED(StringCchPrintfW(item, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName)))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) coredir_wipe(item);
        else                                                DeleteFileW(item);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir);
}

/* Run a system tool and wait. On timeout the child is killed and waited for:
   carrying on with the cleanup while it still runs would delete files out
   from under it. */
int coredir_run_wait(const wchar_t *cmdline, DWORD timeout_ms, DWORD *code)
{
    STARTUPINFOW        si;
    PROCESS_INFORMATION pi;
    wchar_t             mutable_cmd[2048];

    if (FAILED(StringCchCopyW(mutable_cmd, 2048, cmdline))) return 0;

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessW(NULL, mutable_cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;

    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        *code = 1;
        return 0;
    }
    if (!GetExitCodeProcess(pi.hProcess, code)) *code = 1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
