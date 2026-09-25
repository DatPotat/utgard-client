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

/* ---- is it running -------------------------------------------------- */

/* Our sing-box, opened with the rights asked for, or NULL. The handle that
   matched the path is the one returned, so it cannot be a reused PID. */
static HANDLE open_ours(DWORD access)
{
    wchar_t         want[MAX_PATH * 2];
    PROCESSENTRY32W pe;
    HANDLE          snap;
    HANDLE          found = NULL;

    if (!singbox_exe(want, MAX_PATH * 2)) return NULL;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return NULL;

    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE  h;
            wchar_t path[MAX_PATH * 2];
            DWORD   len = (DWORD)(sizeof path / sizeof path[0]);

            if (_wcsicmp(pe.szExeFile, L"sing-box.exe") != 0) continue;

            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | access, FALSE,
                            pe.th32ProcessID);
            if (!h) continue;
            /* Name alone is not enough: another sing-box on this machine is
               not ours to stop. */
            if (QueryFullProcessImageNameW(h, 0, path, &len) &&
                _wcsicmp(path, want) == 0) {
                found = h;
                break;
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

int singbox_running(void)
{
    HANDLE h = open_ours(0);
    if (!h) return 0;
    CloseHandle(h);
    return 1;
}

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

    /* Earlier versions wrote the running config here, credentials in plain
       text. It now goes through stdin; a leftover copy has no use. */
    if (under_root(L"sing-box\\config.generated.json", path, MAX_PATH * 2))
        DeleteFileW(path);

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

/* ---- bundled core verification -------------------------------------- */

/* Built together with utgard.exe; never accept an upstream binary without AWG. */
#include "core_hash.h"
#define SB_VERSION L"1.14.1-utgard-awg3"
static const wchar_t SB_EXE_SHA256[] = UTGARD_CORE_SHA256;
const wchar_t *singbox_version(void) { return SB_VERSION; }

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

typedef struct { HANDLE exe; } sb_lock;

/* Pin the verified core for the duration of a launch. */
static int binaries_lock(sb_lock *lk, wchar_t *msg, size_t cap)
{
    wchar_t exe[MAX_PATH * 2];

    lk->exe = INVALID_HANDLE_VALUE;
    if (!singbox_exe(exe, MAX_PATH * 2))
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
    return 1;
}

static void binaries_unlock(sb_lock *lk)
{
    if (lk->exe != INVALID_HANDLE_VALUE) CloseHandle(lk->exe);
    lk->exe = INVALID_HANDLE_VALUE;
}

int singbox_verified(void)
{
    sb_lock lk;
    if (!binaries_lock(&lk, NULL, 0)) return 0;
    binaries_unlock(&lk);
    return 1;
}

/* ---- running a child ------------------------------------------------ */

/* Starts cmd in root with the given standard handles. Only those handles are
   inherited - an explicit list, not everything inheritable in this process:
   a socket of the ping thread or a download in flight must not end up in
   sing-box. in/out/err may repeat; out and err may be the same handle. */
static int spawn(wchar_t *cmd, DWORD flags, HANDLE in, HANDLE out, HANDLE err,
                 PROCESS_INFORMATION *pi)
{
    wchar_t                      root[MAX_PATH * 2];
    STARTUPINFOEXW               si;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    SIZE_T                       size = 0;
    HANDLE                       list[3];
    int                          n = 0, ok = 0;

    if (!singbox_root(root, MAX_PATH * 2)) return 0;

    list[n++] = in;
    if (out != in) list[n++] = out;
    if (err != in && err != out) list[n++] = err;

    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    if (!attrs) return 0;
    if (InitializeProcThreadAttributeList(attrs, 1, 0, &size)) {
        if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      list, n * sizeof list[0], NULL, NULL)) {
            ZeroMemory(&si, sizeof si);
            si.StartupInfo.cb          = sizeof si;
            si.StartupInfo.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.StartupInfo.wShowWindow = SW_HIDE;
            si.StartupInfo.hStdInput   = in;
            si.StartupInfo.hStdOutput  = out;
            si.StartupInfo.hStdError   = err;
            si.lpAttributeList         = attrs;
            ZeroMemory(pi, sizeof *pi);
            ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                                flags | EXTENDED_STARTUPINFO_PRESENT, NULL, root,
                                &si.StartupInfo, pi) ? 1 : 0;
        }
        DeleteProcThreadAttributeList(attrs);
    }
    HeapFree(GetProcessHeap(), 0, attrs);
    return ok;
}

/* The read end of a new pipe for the child's stdin, with text already on its
   way in. The pipe is sized for the whole text, and sing-box reads stdin to
   EOF before anything else, so the write does not wait on the child. Our
   write end is closed here: EOF is what ends sing-box's read.
   ponytail: a child that never reads would block this write; sing-box reads
   first thing, a writer thread is the fix if another consumer ever appears. */
static HANDLE stdin_with(const char *text, HANDLE *writer)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE rd, wr;

    ZeroMemory(&sa, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, (DWORD)strlen(text) + 1)) return NULL;
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
    *writer = wr;
    return rd;
}

/* Called after the child started: the child now owns its copy of the read
   end, so ours goes, and the text is pushed in and the pipe closed. */
static void stdin_feed(HANDLE rd, HANDLE wr, const char *text)
{
    DWORD left = (DWORD)strlen(text), put = 0;

    CloseHandle(rd);
    while (left && WriteFile(wr, text, left, &put, NULL) && put) {
        text += put;
        left -= put;
    }
    CloseHandle(wr);
}

static int run_capture(const wchar_t *cmdline, const char *input,
                       char *out, DWORD cap, DWORD *code)
{
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION pi;
    HANDLE   rd = NULL, wr = NULL, in_rd = NULL, in_wr = NULL, nul = NULL;
    wchar_t  mutable_cmd[2048];
    DWORD    total = 0;
    int      ok = 0;

    if (cap) out[0] = '\0';
    if (FAILED(StringCchCopyW(mutable_cmd, 2048, cmdline))) return 0;

    ZeroMemory(&sa, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    if (input) in_rd = stdin_with(input, &in_wr);
    else in_rd = nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_EXISTING, 0, NULL);
    if (!in_rd || in_rd == INVALID_HANDLE_VALUE) {
        CloseHandle(rd);
        CloseHandle(wr);
        return 0;
    }

    if (!spawn(mutable_cmd, CREATE_NO_WINDOW, in_rd, wr, wr, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        CloseHandle(in_rd);
        if (in_wr) CloseHandle(in_wr);
        return 0;
    }

    if (input) stdin_feed(in_rd, in_wr, input);
    else       CloseHandle(nul);

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
    if (has(text, "read config at stdin"))
        return L"Не удалось передать конфигурацию в sing-box.";
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

int singbox_check(const char *config, wchar_t *msg, size_t cap)
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
                                L"\"%s\" check --disable-color -c stdin", exe)))
        return say(msg, cap, L"Слишком длинный путь");

    if (!binaries_lock(&lk, msg, cap)) return 0;
    ran = run_capture(cmd, config, output, (DWORD)sizeof output, &code);
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
            L"\"%s\" --disable-color rule-set compile \"list\\general.json\" -o \"list\\general.srs\"",
            exe)))
        return say(msg, cap, L"Слишком длинный путь");

    if (!binaries_lock(&lk, msg, cap)) return 0;
    ran = run_capture(cmd, NULL, output, (DWORD)sizeof output, &code);
    binaries_unlock(&lk);
    if (!ran) return say(msg, cap, L"Не удалось запустить сборку списка");

    if (code == 0) return 1;

    raw_first_line(output, msg, cap);
    return 0;
}

/* ---- start ---------------------------------------------------------- */

int singbox_start(const char *config, wchar_t *msg, size_t cap)
{
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION pi;
    wchar_t exe[MAX_PATH * 2];
    wchar_t root[MAX_PATH * 2];
    wchar_t logs[MAX_PATH * 2];
    wchar_t cmd[2048];
    HANDLE  in_rd, in_wr = NULL, nul;
    DWORD   waited;

    if (msg && cap) msg[0] = L'\0';
    if (singbox_running()) return 1;

    if (!singbox_exe(exe, MAX_PATH * 2) || !singbox_root(root, MAX_PATH * 2))
        return say(msg, cap, L"Не удалось определить расположение sing-box");

    /* The config writes into logs/ relative to the working directory, and
       sing-box will not create that directory itself. */
    if (SUCCEEDED(StringCchPrintfW(logs, MAX_PATH * 2, L"%slogs", root)))
        CreateDirectoryW(logs, NULL);

    if (FAILED(StringCchPrintfW(cmd, 2048, L"\"%s\" run -c stdin", exe)))
        return say(msg, cap, L"Слишком длинный путь");

    /* Its output has nowhere useful to go: the config sends the log to
       logs/sing-box.log, and a config error was already caught by check.
       NUL rather than no handle, so writes simply succeed. */
    ZeroMemory(&sa, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE) return say(msg, cap, L"Не удалось запустить sing-box");
    in_rd = stdin_with(config, &in_wr);
    if (!in_rd) {
        CloseHandle(nul);
        return say(msg, cap, L"Не удалось передать конфигурацию в sing-box");
    }

    /* Verified and pinned right up to the launch: the file started is the
       file that was checked. The library stays pinned through the startup
       window too, since it is loaded after the process begins. */
    {
        sb_lock lk;
        BOOL    started;

        if (!binaries_lock(&lk, msg, cap)) {
            CloseHandle(nul);
            CloseHandle(in_rd);
            CloseHandle(in_wr);
            return 0;
        }

        /* A hidden console, not CREATE_NO_WINDOW: the window is what lets us
           ask the process to close later instead of killing it. */
        started = spawn(cmd, 0, in_rd, nul, nul, &pi);
        CloseHandle(nul);
        if (!started) {
            CloseHandle(in_rd);
            CloseHandle(in_wr);
            binaries_unlock(&lk);
            return say(msg, cap, L"Не удалось запустить sing-box");
        }
        stdin_feed(in_rd, in_wr, config);

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

/* sing-box 1.14.1 gives its own shutdown C.FatalStopTimeout (10 s) before
   it gives up; ours must be longer, or we kill it while it is still
   removing the TUN adapter. */
#define STOP_WAIT_MS 12000

/* Returns 1 once the process is gone, 0 if it is still running. A clean stop
   leaves msg empty; anything else - a non-zero exit code, a forced kill -
   is put in msg, because the TUN adapter may be left behind and the next
   start can fail on it. */
int singbox_stop(wchar_t *msg, size_t cap)
{
    close_hunt h;
    HANDLE     p;
    DWORD      code = 0;

    if (msg && cap) msg[0] = L'\0';

    p = open_ours(SYNCHRONIZE | PROCESS_TERMINATE);
    if (!p)
        return singbox_running() ? say(msg, cap, L"Нет доступа к процессу sing-box") : 1;

    h.pid = GetProcessId(p);
    h.posted = 0;
    EnumWindows(close_cb, (LPARAM)&h);

    /* sing-box exits 0 only after Close() returned: a SIGTERM-driven return
       from run(). log.Fatal and a killed process give other codes. */
    if (h.posted && WaitForSingleObject(p, STOP_WAIT_MS) == WAIT_OBJECT_0) {
        if (GetExitCodeProcess(p, &code) && code != 0 && msg && cap)
            StringCchPrintfW(msg, cap, L"sing-box завершился с ошибкой (код 0x%08lX) — "
                                       L"смотрите журнал logs\\sing-box.log",
                             (unsigned long)code);
        CloseHandle(p);
        return 1;
    }

    TerminateProcess(p, 1);
    if (WaitForSingleObject(p, 5000) != WAIT_OBJECT_0) {
        CloseHandle(p);
        return say(msg, cap, L"sing-box не завершился");
    }
    CloseHandle(p);

    say(msg, cap, h.posted
        ? L"sing-box не закрылся сам за 12 секунд, его пришлось завершить принудительно"
        : L"У sing-box не найдено окно консоли, его пришлось завершить принудительно");
    return 1;
}
