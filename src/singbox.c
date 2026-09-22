#include "singbox.h"

#include <tlhelp32.h>
#include <bcrypt.h>

#include "net.h"
#include "fileio.h"
#include <strsafe.h>
#include <string.h>

static int say(wchar_t *msg, size_t cap, const wchar_t *text)
{
    if (msg && cap) StringCchCopyW(msg, cap, text);
    return 0;
}

/* ---- layout --------------------------------------------------------- */

/* Everything lives beside the executable: sing-box\, list\, logs\, and the
   client's own files. One folder, no layers, nothing written outside it. */
int singbox_root(wchar_t *out, size_t cap)
{
    wchar_t  exe[MAX_PATH * 2];
    wchar_t *slash;
    DWORD    n;
    size_t   len;

    n = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    if (n == 0 || n >= sizeof exe / sizeof exe[0]) return 0;

    slash = wcsrchr(exe, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';

    if (FAILED(StringCchCopyW(out, cap, exe))) return 0;
    len = wcslen(out);
    if (len && out[len - 1] != L'\\' && FAILED(StringCchCatW(out, cap, L"\\")))
        return 0;
    return 1;
}

static int under_root(const wchar_t *tail, wchar_t *out, size_t cap)
{
    wchar_t root[MAX_PATH * 2];

    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%s", root, tail));
}

int singbox_exe(wchar_t *out, size_t cap)
{
    return under_root(L"sing-box\\sing-box.exe", out, cap);
}

static int to_utf8(const wchar_t *src, char *out, size_t cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, out, (int)cap, NULL, NULL) != 0;
}

int singbox_base_utf8(char *out, size_t cap)
{
    wchar_t p[MAX_PATH * 2];
    if (!under_root(L"sing-box\\config.json", p, MAX_PATH * 2)) return 0;
    return to_utf8(p, out, cap);
}

int singbox_generated_utf8(char *out, size_t cap)
{
    wchar_t p[MAX_PATH * 2];
    if (!under_root(L"sing-box\\config.generated.json", p, MAX_PATH * 2)) return 0;
    return to_utf8(p, out, cap);
}

/* ---- is it running -------------------------------------------------- */

static DWORD find_pid(void)
{
    wchar_t         want[MAX_PATH * 2];
    PROCESSENTRY32W pe;
    HANDLE          snap;
    DWORD           found = 0;

    if (!singbox_exe(want, MAX_PATH * 2)) return 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE  h;
            wchar_t path[MAX_PATH * 2];
            DWORD   len = (DWORD)(sizeof path / sizeof path[0]);

            if (_wcsicmp(pe.szExeFile, L"sing-box.exe") != 0) continue;

            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            /* Name alone is not enough: another sing-box on this machine is
               not ours to stop. */
            if (QueryFullProcessImageNameW(h, 0, path, &len) &&
                _wcsicmp(path, want) == 0)
                found = pe.th32ProcessID;
            CloseHandle(h);
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

int singbox_running(void) { return find_pid() != 0; }

/* The base config the user owns. Written once, when it is missing, so a bare
   executable dropped into an empty folder has something to start from. It is
   never rewritten afterwards - from then on the file is the user's. */
static const char DEFAULT_CONFIG[] =
    "{\n"
    "  \"log\": {\n"
    "    \"level\": \"info\",\n"
    "    \"timestamp\": true,\n"
    "    \"output\": \"logs/sing-box.log\"\n"
    "  },\n"
    "\n"
    "  \"dns\": {\n"
    "    \"servers\": [\n"
    "      { \"tag\": \"local\", \"type\": \"local\" },\n"
    "      {\n"
    "        \"tag\": \"doh\",\n"
    "        \"type\": \"h3\",\n"
    "        \"server\": \"dns.google\",\n"
    "        \"server_port\": 443,\n"
    "        \"path\": \"/dns-query\",\n"
    "        \"domain_resolver\": \"local\"\n"
    "      }\n"
    "    ],\n"
    "    \"rules\": [\n"
    "      { \"rule_set\": [\"general\"], \"server\": \"doh\" }\n"
    "    ],\n"
    "    \"final\": \"local\",\n"
    "    \"strategy\": \"ipv4_only\"\n"
    "  },\n"
    "\n"
    "  \"inbounds\": [\n"
    "    {\n"
    "      \"type\": \"tun\",\n"
    "      \"tag\": \"tun-in\",\n"
    "      \"address\": [\"172.30.30.1/30\"],\n"
    "      \"mtu\": 1430,\n"
    "      \"auto_route\": true,\n"
    "      \"strict_route\": false,\n"
    "      \"stack\": \"system\"\n"
    "    }\n"
    "  ],\n"
    "\n"
    "  \"route\": {\n"
    "    \"auto_detect_interface\": true,\n"
    "    \"default_domain_resolver\": \"local\",\n"
    "    \"final\": \"direct\",\n"
    "    \"rules\": []\n"
    "  }\n"
    "}\n"
    "\n";

int singbox_seed_config(void)
{
    wchar_t path[MAX_PATH * 2], dir[MAX_PATH * 2];

    if (!under_root(L"sing-box", dir, MAX_PATH * 2)) return 0;
    CreateDirectoryW(dir, NULL);

    if (!under_root(L"sing-box\\config.json", path, MAX_PATH * 2)) return 0;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 1;

    /* Atomic, not merely create-only: an interrupted plain write would leave
       a truncated config.json that, since the file then exists, would never
       be re-seeded. */
    return file_write(path, DEFAULT_CONFIG, sizeof DEFAULT_CONFIG - 1);
}

int singbox_present(void)
{
    wchar_t exe[MAX_PATH * 2];
    return singbox_exe(exe, MAX_PATH * 2) &&
           GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES;
}

/* ---- first-run install ---------------------------------------------- */

/* Pinned release and its SHA-256. Both were checked against the real archive:
   32.8 MB, three files, sing-box.exe plus libcronet.dll, which the binary needs.
   Changing the URL means changing the hash in the same edit. */
#define SB_VERSION L"1.14.1"

/* One place for the version: the URL and the message the user sees are built
   from it, so they cannot drift apart. */
static const wchar_t SB_URL[] =
    L"https://github.com/SagerNet/sing-box/releases/download/v" SB_VERSION
    L"/sing-box-" SB_VERSION L"-windows-amd64.zip";
static const wchar_t SB_SHA256[] =
    L"5197f16d492d93202dc623622149a6ed040f8eca263128f91d603f2b901baa89";

const wchar_t *singbox_version(void) { return SB_VERSION; }

/* The two files that actually run, pinned individually. The archive hash
   only proves the download; these prove what is on disk every time sing-box
   is about to be started, which is when a swapped file would matter. */
static const wchar_t SB_EXE_SHA256[] =
    L"b838de45bd0b2e6ddbed1977e4745622f7dffab3b293807ff4c6b1b640fed909";
static const wchar_t SB_DLL_SHA256[] =
    L"3217c6260fbca5f16072e0b79735742f40109a63bb0ff88fd6b96dd6b54a2928";

/* Hash through a handle the caller already holds. Hashing by path and then
   using the file by path leaves a window in which it can be swapped; hashing
   the handle that stays open closes it. */
static int sha256_handle(HANDLE file, wchar_t *hex, size_t cap)
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
static int open_verified(const wchar_t *path, const wchar_t *expect, HANDLE *out)
{
    wchar_t hex[80];
    HANDLE  h;

    *out = INVALID_HANDLE_VALUE;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!sha256_handle(h, hex, 80) || _wcsicmp(hex, expect) != 0) {
        CloseHandle(h);
        return 0;
    }
    *out = h;
    return 1;
}

typedef struct { HANDLE exe, dll; } sb_lock;

/* Pin both files for the duration of a launch. */
static int binaries_lock(sb_lock *lk, wchar_t *msg, size_t cap)
{
    wchar_t exe[MAX_PATH * 2], dll[MAX_PATH * 2];

    lk->exe = lk->dll = INVALID_HANDLE_VALUE;
    if (!singbox_exe(exe, MAX_PATH * 2) ||
        !under_root(L"sing-box\\libcronet.dll", dll, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить расположение sing-box");

    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES)
        return say(msg, cap, L"Не найден sing-box.exe в папке sing-box");

    if (!open_verified(exe, SB_EXE_SHA256, &lk->exe)) {
        if (msg && cap)
            StringCchPrintfW(msg, cap,
                L"sing-box.exe не совпадает с ожидаемой версией %s: файл изменён "
                L"или это другая версия. Запуск отменён.", SB_VERSION);
        return 0;
    }
    if (!open_verified(dll, SB_DLL_SHA256, &lk->dll)) {
        CloseHandle(lk->exe);
        lk->exe = INVALID_HANDLE_VALUE;
        if (msg && cap)
            StringCchPrintfW(msg, cap,
                L"libcronet.dll отсутствует или не совпадает с версией %s. "
                L"Запуск отменён.", SB_VERSION);
        return 0;
    }
    return 1;
}

static void binaries_unlock(sb_lock *lk)
{
    if (lk->exe != INVALID_HANDLE_VALUE) CloseHandle(lk->exe);
    if (lk->dll != INVALID_HANDLE_VALUE) CloseHandle(lk->dll);
    lk->exe = lk->dll = INVALID_HANDLE_VALUE;
}

int singbox_verified(void)
{
    sb_lock lk;
    if (!binaries_lock(&lk, NULL, 0)) return 0;
    binaries_unlock(&lk);
    return 1;
}

/* Run a command and wait. Used for the system unpacker. */
static int run_wait(const wchar_t *cmdline, DWORD *code)
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

    /* On timeout the child is killed and waited for: carrying on with the
       cleanup while it still runs would delete files out from under it. */
    if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_TIMEOUT) {
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

static void wipe_dir(const wchar_t *dir)
{
    wchar_t          mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;

    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*", dir))) return;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t item[MAX_PATH * 2];
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (FAILED(StringCchPrintfW(item, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName)))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) wipe_dir(item);
        else                                                DeleteFileW(item);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir);
}

/* Copy every file of a directory into sing-box\. The archive keeps its
   contents in one versioned subdirectory. */
static int copy_files(const wchar_t *from, const wchar_t *to)
{
    wchar_t          mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;
    int              copied = 0;

    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*", from))) return 0;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t src[MAX_PATH * 2], dst[MAX_PATH * 2];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (FAILED(StringCchPrintfW(src, MAX_PATH * 2, L"%s\\%s", from, fd.cFileName)) ||
            FAILED(StringCchPrintfW(dst, MAX_PATH * 2, L"%s\\%s", to, fd.cFileName)))
            continue;
        if (CopyFileW(src, dst, FALSE)) copied++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return copied;
}

/* The only subdirectory of dir, if there is exactly one. */
static int only_subdir(const wchar_t *dir, wchar_t *out, size_t cap)
{
    wchar_t          mask[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE           h;
    int              found = 0;

    if (FAILED(StringCchPrintfW(mask, MAX_PATH * 2, L"%s\\*", dir))) return 0;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (SUCCEEDED(StringCchPrintfW(out, cap, L"%s\\%s", dir, fd.cFileName))) found++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found == 1;
}

int singbox_install(wchar_t *msg, size_t cap)
{
    wchar_t temp[MAX_PATH * 2], zip[MAX_PATH * 2], unpack[MAX_PATH * 2];
    wchar_t sbdir[MAX_PATH * 2], sub[MAX_PATH * 2], sys[MAX_PATH];
    wchar_t cmd[2048], hex[80];
    DWORD   code = 1;
    int     ok = 0;
    HANDLE  held = INVALID_HANDLE_VALUE;

    if (msg && cap) msg[0] = L'\0';
    if (!GetTempPathW(MAX_PATH * 2, temp)) return say(msg, cap, L"Нет временной папки");
    if (FAILED(StringCchPrintfW(zip, MAX_PATH * 2, L"%sutgard-sing-box.zip", temp)) ||
        FAILED(StringCchPrintfW(unpack, MAX_PATH * 2, L"%sutgard-sing-box", temp)))
        return say(msg, cap, L"Слишком длинный путь");

    if (!under_root(L"sing-box", sbdir, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить папку sing-box");
    CreateDirectoryW(sbdir, NULL);

    if (!net_download(SB_URL, zip, msg, cap)) goto cleanup;

    /* Verify before unpacking: what comes out of this archive runs elevated.
       The archive is opened so nobody can write, delete or rename it, hashed
       through that handle, and kept open until tar has finished reading it -
       so the file tar unpacks is the file that was checked. */
    held = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (held == INVALID_HANDLE_VALUE) { say(msg, cap, L"Не удалось открыть архив");
                                        goto cleanup; }
    if (!sha256_handle(held, hex, 80)) { say(msg, cap, L"Не удалось посчитать контрольную сумму");
                                         goto cleanup; }
    if (_wcsicmp(hex, SB_SHA256) != 0) {
        if (msg && cap)
            StringCchPrintfW(msg, cap,
                L"Контрольная сумма не совпала. Ожидалась %s, получена %s. "
                L"Файл повреждён или подменён.", SB_SHA256, hex);
        goto cleanup;
    }

    wipe_dir(unpack);
    if (!CreateDirectoryW(unpack, NULL)) { say(msg, cap, L"Не удалось создать временную папку");
                                           goto cleanup; }

    /* The system unpacker, by full path on purpose: a GNU tar on PATH would
       read "C:" as a remote host and silently extract nothing. */
    if (!GetSystemDirectoryW(sys, MAX_PATH)) { say(msg, cap, L"Нет доступа к System32");
                                               goto cleanup; }
    if (FAILED(StringCchPrintfW(cmd, 2048, L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"",
                                sys, zip, unpack))) {
        say(msg, cap, L"Слишком длинный путь");
        goto cleanup;
    }
    if (!run_wait(cmd, &code) || code != 0) {
        say(msg, cap, L"Не удалось распаковать архив");
        goto cleanup;
    }

    if (!only_subdir(unpack, sub, MAX_PATH * 2))
        StringCchCopyW(sub, MAX_PATH * 2, unpack);

    if (copy_files(sub, sbdir) == 0) { say(msg, cap, L"В архиве не нашлось файлов");
                                       goto cleanup; }
    if (!singbox_present()) { say(msg, cap, L"В архиве не нашлось sing-box.exe");
                              goto cleanup; }
    ok = 1;

cleanup:
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);   /* before deleting it */
    DeleteFileW(zip);
    wipe_dir(unpack);
    return ok;
}

/* ---- running a child and reading what it says ----------------------- */

static int run_capture(const wchar_t *cmdline, char *out, DWORD cap, DWORD *code)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW        si;
    PROCESS_INFORMATION pi;
    HANDLE   rd = NULL, wr = NULL;
    wchar_t  root[MAX_PATH * 2];
    wchar_t  mutable_cmd[2048];
    DWORD    total = 0;
    int      ok = 0;

    if (cap) out[0] = '\0';
    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    if (FAILED(StringCchCopyW(mutable_cmd, 2048, cmdline))) return 0;

    ZeroMemory(&sa, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = NULL;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessW(NULL, mutable_cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, root, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return 0;
    }

    /* The child holds the only other copy of the write end; closing ours is
       what lets the read below ever reach EOF. */
    CloseHandle(wr);

    /* Read without blocking: look whether data is waiting, and otherwise wait
       for the process in short steps. A plain ReadFile would block until the
       child closes its output, so a hung sing-box would hang the caller for
       good. Past the deadline the child is killed. Output beyond the buffer
       is drained and dropped, so a talkative child cannot stall on a full
       pipe either. */
    {
        DWORD started = GetTickCount();
        int   exited = 0, timed_out = 0;

        for (;;) {
            DWORD avail = 0, got = 0;

            if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;   /* closed, empty */
            if (avail) {
                char  sink[512];
                DWORD room = cap - 1 - total;
                char *dst  = room ? out + total : sink;
                DWORD want = room ? (avail < room ? avail : room)
                                  : (avail < sizeof sink ? avail : (DWORD)sizeof sink);
                if (!ReadFile(rd, dst, want, &got, NULL) || got == 0) break;
                if (room) total += got;
                continue;
            }
            if (exited) break;
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) { exited = 1; continue; }
            if (GetTickCount() - started > 30000) {
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 5000);
                timed_out = 1;
                break;
            }
        }
        out[total] = '\0';

        if (timed_out) {
            StringCchCopyA(out, cap, "sing-box не завершился за 30 секунд и был остановлен");
            *code = 1;
            ok = 1;
        } else {
            ok = GetExitCodeProcess(pi.hProcess, code) ? 1 : 0;
        }
    }

    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok;
}

/* ---- translating sing-box's complaints ------------------------------ */

static int has(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    const char *p;

    for (p = hay; *p; p++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/* Order matters, as in the PowerShell version: the config case has to be
   matched before the generic "cannot find", which otherwise swallows it and
   blames the rule set instead. */
static const wchar_t *friendly(const char *text)
{
    if (has(text, "read config at") || has(text, "config.generated.json: no such file"))
        return L"Не найден собранный конфиг — соберите его заново.";
    if (has(text, "public_key") || has(text, "invalid uuid") ||
        has(text, "changeme")   || has(text, "invalid password"))
        return L"Профиль настроен неверно или устарел. Импортируйте ссылку заново.";
    if (has(text, "permission denied") || has(text, "access is denied"))
        return L"Недостаточно прав. Запустите клиент от администратора.";
    if (has(text, "address already in use") || has(text, "bind:"))
        return L"Порт занят. Возможно, уже работает другой клиент.";
    if (has(text, ".srs"))
        return L"Не найден файл списка. Пересоберите список сайтов.";
    return NULL;
}

/* Fall back to sing-box's own first line rather than a blank shrug. */
static void raw_first_line(const char *text, wchar_t *msg, size_t cap)
{
    char    line[400];
    size_t  n = 0;

    while (text[n] && text[n] != '\r' && text[n] != '\n' && n + 1 < sizeof line) {
        line[n] = text[n];
        n++;
    }
    line[n] = '\0';

    if (n == 0) {
        StringCchCopyW(msg, cap, L"sing-box отказал без объяснения");
        return;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, line, -1, msg, (int)cap) == 0)
        StringCchCopyW(msg, cap, L"sing-box отказал без объяснения");
}

/* ---- check ---------------------------------------------------------- */

int singbox_check(wchar_t *msg, size_t cap)
{
    static char output[8192];
    wchar_t exe[MAX_PATH * 2];
    wchar_t cmd[2048];
    DWORD   code = 1;

    sb_lock lk;
    int     ran;

    if (msg && cap) msg[0] = L'\0';
    if (!singbox_exe(exe, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить расположение sing-box");
    if (FAILED(StringCchPrintfW(cmd, 2048,
                                L"\"%s\" check -c \"sing-box\\config.generated.json\"",
                                exe)))
        return say(msg, cap, L"Слишком длинный путь");

    if (!binaries_lock(&lk, msg, cap)) return 0;
    ran = run_capture(cmd, output, (DWORD)sizeof output, &code);
    binaries_unlock(&lk);
    if (!ran) return say(msg, cap, L"Не удалось запустить проверку конфигурации");

    if (code == 0) return 1;

    {
        const wchar_t *f = friendly(output);
        if (f) say(msg, cap, f);
        else   raw_first_line(output, msg, cap);
    }
    return 0;
}

int singbox_compile_list(wchar_t *msg, size_t cap)
{
    static char output[4096];
    wchar_t exe[MAX_PATH * 2];
    wchar_t cmd[2048];
    DWORD   code = 1;
    sb_lock lk;
    int     ran;

    if (msg && cap) msg[0] = L'\0';
    if (!singbox_exe(exe, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить расположение sing-box");
    if (FAILED(StringCchPrintfW(cmd, 2048,
            L"\"%s\" rule-set compile \"list\\general.json\" -o \"list\\general.srs\"",
            exe)))
        return say(msg, cap, L"Слишком длинный путь");

    if (!binaries_lock(&lk, msg, cap)) return 0;
    ran = run_capture(cmd, output, (DWORD)sizeof output, &code);
    binaries_unlock(&lk);
    if (!ran) return say(msg, cap, L"Не удалось запустить сборку списка");

    if (code == 0) return 1;

    raw_first_line(output, msg, cap);
    return 0;
}

/* ---- start ---------------------------------------------------------- */

int singbox_start(wchar_t *msg, size_t cap)
{
    STARTUPINFOW        si;
    PROCESS_INFORMATION pi;
    wchar_t exe[MAX_PATH * 2];
    wchar_t root[MAX_PATH * 2];
    wchar_t logs[MAX_PATH * 2];
    wchar_t cmd[2048];
    DWORD   waited;

    if (msg && cap) msg[0] = L'\0';
    if (singbox_running()) return 1;

    if (!singbox_exe(exe, MAX_PATH * 2) || !singbox_root(root, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить расположение sing-box");

    /* The config writes into logs/ relative to the working directory, and
       sing-box will not create that directory itself. */
    if (SUCCEEDED(StringCchPrintfW(logs, MAX_PATH * 2, L"%slogs", root)))
        CreateDirectoryW(logs, NULL);

    if (FAILED(StringCchPrintfW(cmd, 2048,
                                L"\"%s\" run -c \"sing-box\\config.generated.json\"",
                                exe)))
        return say(msg, cap, L"Слишком длинный путь");

    ZeroMemory(&si, sizeof si);
    si.cb          = sizeof si;
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof pi);

    /* Verified and pinned right up to the launch: the file started is the
       file that was checked. The library stays pinned through the startup
       window too, since it is loaded after the process begins. */
    {
        sb_lock lk;
        BOOL    started;

        if (!binaries_lock(&lk, msg, cap)) return 0;

        /* A hidden console, not CREATE_NO_WINDOW: the window is what lets us
           ask the process to close later instead of killing it. */
        started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, root, &si, &pi);
        if (!started) {
            binaries_unlock(&lk);
            return say(msg, cap, L"Не удалось запустить sing-box");
        }

        /* A config that passes check can still die at startup - most often
           the TUN adapter is busy. Give it a moment and look again. */
        waited = WaitForSingleObject(pi.hProcess, 1200);
        binaries_unlock(&lk);
    }
    CloseHandle(pi.hThread);

    if (waited == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (code == 0) return say(msg, cap, L"sing-box завершился сразу после запуска");
        return say(msg, cap, L"sing-box не смог запуститься — смотрите журнал");
    }

    CloseHandle(pi.hProcess);
    return singbox_running() ? 1 : say(msg, cap, L"sing-box не запустился");
}

/* ---- stop ----------------------------------------------------------- */

typedef struct { DWORD pid; int posted; } close_hunt;

static BOOL CALLBACK close_cb(HWND hwnd, LPARAM lp)
{
    close_hunt *h = (close_hunt *)lp;
    DWORD       pid = 0;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == h->pid) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        h->posted = 1;
    }
    return TRUE;
}

int singbox_stop(wchar_t *msg, size_t cap)
{
    close_hunt h;
    DWORD      pid;
    int        i;

    if (msg && cap) msg[0] = L'\0';

    pid = find_pid();
    if (!pid) return 1;

    h.pid = pid;
    h.posted = 0;
    EnumWindows(close_cb, (LPARAM)&h);

    for (i = 0; i < 20; i++) {          /* up to 6 s, as the old client waited */
        Sleep(300);
        if (!singbox_running()) return 1;
    }

    {
        HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (p) {
            TerminateProcess(p, 1);
            WaitForSingleObject(p, 3000);
            CloseHandle(p);
        }
    }

    if (singbox_running())
        return say(msg, cap, L"sing-box не завершился");

    /* Killed rather than closed: say so, because the TUN adapter may be left
       behind and the next start can fail on it. */
    if (!h.posted)
        say(msg, cap, L"sing-box пришлось завершить принудительно");
    return 1;
}
