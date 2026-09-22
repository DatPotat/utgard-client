#include "zapret.h"
#include "fileio.h"

#include <strsafe.h>
#include <tlhelp32.h>


/* Directory of the running executable, with a trailing backslash.
   Belongs in a paths module once there is one. */
static int app_dir(wchar_t *buf, size_t cap)
{
    DWORD n = GetModuleFileNameW(NULL, buf, (DWORD)cap);
    wchar_t *slash;

    if (n == 0 || n >= cap) return 0;
    slash = wcsrchr(buf, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';
    return 1;
}

static int exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int is_dir(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Read at most cap-1 bytes from the head of a file. Bytes, not text:
   the caller decides how to interpret them. */
static int read_head(const wchar_t *file, char *buf, DWORD cap)
{
    HANDLE h;
    DWORD  got = 0;
    BOOL   ok;

    if (cap < 2) return 0;
    h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    ok = ReadFile(h, buf, cap - 1, &got, NULL);
    CloseHandle(h);
    if (!ok) return 0;

    buf[got] = '\0';
    return 1;
}

/* service.bat line 2 is: set "LOCAL_VERSION=1.10.2" */
static void parse_version(const wchar_t *dir, wchar_t *out, size_t cap)
{
    static char   head[8192];
    wchar_t       file[ZAPRET_PATH_MAX];
    const char   *p;
    size_t        i = 0;

    out[0] = L'\0';
    if (FAILED(StringCchPrintfW(file, ZAPRET_PATH_MAX, L"%sservice.bat", dir))) return;
    if (!read_head(file, head, sizeof head)) return;

    p = strstr(head, "LOCAL_VERSION=");
    if (!p) return;
    p += sizeof "LOCAL_VERSION=" - 1;

    while (*p && *p != '"' && *p != '\r' && *p != '\n' && i + 1 < cap) {
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) break;
        out[i++] = (wchar_t)*p++;
    }
    out[i] = L'\0';
}

/* Drop a trailing ".bat" in place. */
static void strip_ext(wchar_t *name)
{
    wchar_t *dot = wcsrchr(name, L'.');
    if (dot && _wcsicmp(dot, L".bat") == 0) *dot = L'\0';
}

/* Strategies are the .bat files in the root, except service*.bat —
   the same rule service.bat itself uses when listing them. */
int zapret_list(const wchar_t *dir, wchar_t names[][ZAPRET_NAME_MAX], int max)
{
    wchar_t          mask[ZAPRET_PATH_MAX];
    wchar_t          base[ZAPRET_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE           h;
    size_t           len;
    int              n = 0;

    if (!dir || !dir[0]) return 0;
    if (FAILED(StringCchCopyW(base, ZAPRET_PATH_MAX, dir))) return 0;
    len = wcslen(base);
    if (len && base[len - 1] != L'\\' &&
        FAILED(StringCchCatW(base, ZAPRET_PATH_MAX, L"\\"))) return 0;

    if (FAILED(StringCchPrintfW(mask, ZAPRET_PATH_MAX, L"%s*.bat", base))) return 0;
    h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsnicmp(fd.cFileName, L"service", 7) == 0) continue;
        if (names && n < max) {
            StringCchCopyW(names[n], ZAPRET_NAME_MAX, fd.cFileName);
            strip_ext(names[n]);
        }
        n++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return n;
}

/* ---- live state ----------------------------------------------------- */

static int service_running(const wchar_t *name)
{
    SC_HANDLE      scm, svc;
    SERVICE_STATUS st;
    int            running = 0;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return 0;

    svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
    if (svc) {
        if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING)
            running = 1;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return running;
}

/* service.bat records the installed strategy here, as the bare file name
   without extension (%~nF); its own get_strategy_name reads this value. */
static int service_strategy(wchar_t *out, size_t cap)
{
    wchar_t  value[ZAPRET_PATH_MAX];
    DWORD    cb = sizeof value;
    wchar_t *slash;

    out[0] = L'\0';
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"System\\CurrentControlSet\\Services\\zapret",
                     L"zapret-discord-youtube", RRF_RT_REG_SZ,
                     NULL, value, &cb) != ERROR_SUCCESS)
        return 0;

    slash = wcsrchr(value, L'\\');
    if (FAILED(StringCchCopyW(out, cap, slash ? slash + 1 : value))) {
        out[0] = L'\0';
        return 0;
    }
    strip_ext(out);
    return out[0] != L'\0';
}

static int winws_running(void)
{
    PROCESSENTRY32W pe;
    HANDLE          snap;
    int             found = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"winws.exe") == 0) { found = 1; break; }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

/* A strategy .bat starts winws.exe as: start "zapret: <name>" /min ...
   so the console window title is the only place the name survives. */
typedef struct { wchar_t *out; size_t cap; int found; } title_hunt;

static BOOL CALLBACK title_cb(HWND hwnd, LPARAM lp)
{
    title_hunt      *h = (title_hunt *)lp;
    wchar_t          title[ZAPRET_NAME_MAX + 16];
    static const wchar_t prefix[] = L"zapret: ";
    const size_t     plen = sizeof prefix / sizeof prefix[0] - 1;

    if (GetWindowTextW(hwnd, title, (int)(sizeof title / sizeof title[0])) <= 0)
        return TRUE;
    if (_wcsnicmp(title, prefix, plen) != 0) return TRUE;
    if (!title[plen]) return TRUE;

    if (SUCCEEDED(StringCchCopyW(h->out, h->cap, title + plen))) {
        h->found = 1;
        return FALSE;
    }
    return TRUE;
}

static int window_strategy(wchar_t *out, size_t cap)
{
    title_hunt h;

    out[0] = L'\0';
    h.out = out;
    h.cap = cap;
    h.found = 0;
    EnumWindows(title_cb, (LPARAM)&h);
    return h.found;
}

void zapret_status_read(zapret_status *out)
{
    ZeroMemory(out, sizeof *out);

    if (service_running(L"zapret")) {
        out->mode = ZAPRET_SERVICE;
        service_strategy(out->strategy, ZAPRET_NAME_MAX);
        return;
    }

    if (winws_running()) {
        out->mode = ZAPRET_STANDALONE;
        window_strategy(out->strategy, ZAPRET_NAME_MAX);
        return;
    }

    out->mode = ZAPRET_OFF;
}

int zapret_scan(const wchar_t *dir, zapret_info *out)
{
    wchar_t base[ZAPRET_PATH_MAX];
    wchar_t probe[ZAPRET_PATH_MAX];
    size_t  len;

    ZeroMemory(out, sizeof *out);

    if (!dir || !dir[0]) {
        StringCchCopyW(out->problem, 160, L"Путь не указан");
        return 0;
    }
    if (FAILED(StringCchCopyW(base, ZAPRET_PATH_MAX, dir))) {
        StringCchCopyW(out->problem, 160, L"Слишком длинный путь");
        return 0;
    }

    len = wcslen(base);
    if (len && base[len - 1] != L'\\') {
        if (FAILED(StringCchCatW(base, ZAPRET_PATH_MAX, L"\\"))) {
            StringCchCopyW(out->problem, 160, L"Слишком длинный путь");
            return 0;
        }
    }
    StringCchCopyW(out->path, ZAPRET_PATH_MAX, base);

    StringCchPrintfW(probe, ZAPRET_PATH_MAX, L"%sservice.bat", base);
    if (!exists(probe)) {
        StringCchCopyW(out->problem, 160, L"В папке нет service.bat");
        return 0;
    }

    StringCchPrintfW(probe, ZAPRET_PATH_MAX, L"%sbin\\winws.exe", base);
    if (!exists(probe)) {
        StringCchCopyW(out->problem, 160, L"В папке нет bin\\winws.exe");
        return 0;
    }

    StringCchPrintfW(probe, ZAPRET_PATH_MAX, L"%slists", base);
    if (!is_dir(probe)) {
        StringCchCopyW(out->problem, 160, L"В папке нет каталога lists");
        return 0;
    }

    out->strategy_count = zapret_list(base, NULL, 0);
    if (out->strategy_count == 0) {
        StringCchCopyW(out->problem, 160, L"В папке не нашлось ни одной стратегии");
        return 0;
    }

    parse_version(base, out->version, 32);
    out->valid = 1;
    return 1;
}

/* ---- remembered path ------------------------------------------------ */

static int state_file(wchar_t *buf, size_t cap)
{
    wchar_t dir[ZAPRET_PATH_MAX];

    if (!app_dir(dir, ZAPRET_PATH_MAX)) return 0;
    return SUCCEEDED(StringCchPrintfW(buf, cap, L"%szapret-path.txt", dir));
}

int zapret_path_save(const wchar_t *path)
{
    wchar_t file[ZAPRET_PATH_MAX];
    char    utf8[ZAPRET_PATH_MAX * 3];
    int     bytes;

    if (!state_file(file, ZAPRET_PATH_MAX)) return 0;

    bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, (int)sizeof utf8, NULL, NULL);
    if (bytes <= 1) return 0;
    return file_write(file, utf8, (size_t)(bytes - 1));   /* without the NUL */
}

int zapret_path_load(wchar_t *buf, size_t cap)
{
    wchar_t file[ZAPRET_PATH_MAX];
    char    utf8[ZAPRET_PATH_MAX * 3];
    size_t  n;

    buf[0] = L'\0';
    if (!state_file(file, ZAPRET_PATH_MAX)) return 0;
    if (!read_head(file, utf8, (DWORD)sizeof utf8)) return 0;

    n = strlen(utf8);
    while (n && (utf8[n - 1] == '\r' || utf8[n - 1] == '\n' || utf8[n - 1] == ' '))
        utf8[--n] = '\0';
    if (n == 0) return 0;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, (int)cap) == 0) {
        buf[0] = L'\0';
        return 0;
    }
    return 1;
}

/* ---- control -------------------------------------------------------- */

#define ARGS_MAX 16384

static void fail(wchar_t *err, size_t cap, const wchar_t *text)
{
    if (err && cap) StringCchCopyW(err, cap, text);
}

/* service.bat :game_switch_status — the flag file holds all / tcp / anything
   else, and its absence means the game filter is off. */
static void game_filter(const wchar_t *root, wchar_t *tcp, wchar_t *udp, size_t cap)
{
    static const wchar_t OFF[]   = L"12";
    static const wchar_t RANGE[] = L"1024-65535";
    wchar_t file[ZAPRET_PATH_MAX];
    char    head[128];
    char   *p;

    StringCchCopyW(tcp, cap, OFF);
    StringCchCopyW(udp, cap, OFF);

    if (FAILED(StringCchPrintfW(file, ZAPRET_PATH_MAX,
                                L"%sutils\\game_filter.enabled", root)))
        return;
    if (!read_head(file, head, sizeof head)) return;

    for (p = head; *p && *p != '\r' && *p != '\n'; p++) { }
    *p = '\0';
    for (p = head; *p == ' ' || *p == '\t'; p++) { }

    if (_stricmp(p, "all") == 0) {
        StringCchCopyW(tcp, cap, RANGE);
        StringCchCopyW(udp, cap, RANGE);
    } else if (_stricmp(p, "tcp") == 0) {
        StringCchCopyW(tcp, cap, RANGE);
    } else {
        StringCchCopyW(udp, cap, RANGE);
    }
}

/* Copy src to dst expanding the five variables the strategy files use and
   removing caret escapes (one strategy carries --dpi-desync-fake-tls=^!).
   Returns 0 if dst is too small. */
static int expand(const wchar_t *src, const wchar_t *root,
                  const wchar_t *gtcp, const wchar_t *gudp,
                  wchar_t *dst, size_t cap)
{
    struct { const wchar_t *name; const wchar_t *value; } var[5];
    size_t n = 0;
    int    i;

    var[0].name = L"%BIN%";             var[0].value = NULL;  /* root + bin\  */
    var[1].name = L"%LISTS%";           var[1].value = NULL;  /* root + lists\ */
    var[2].name = L"%~dp0";             var[2].value = root;
    var[3].name = L"%GameFilterTCP%";   var[3].value = gtcp;
    var[4].name = L"%GameFilterUDP%";   var[4].value = gudp;

    while (*src) {
        if (*src == L'^' && src[1]) {          /* caret escapes the next char */
            if (n + 1 >= cap) return 0;
            dst[n++] = src[1];
            src += 2;
            continue;
        }

        for (i = 0; i < 5; i++) {
            size_t len = wcslen(var[i].name);
            if (_wcsnicmp(src, var[i].name, len) != 0) continue;

            if (i <= 1) {
                const wchar_t *tail = (i == 0) ? L"bin\\" : L"lists\\";
                if (FAILED(StringCchCopyW(dst + n, cap - n, root))) return 0;
                n += wcslen(root);
                if (FAILED(StringCchCopyW(dst + n, cap - n, tail))) return 0;
                n += wcslen(tail);
            } else {
                if (FAILED(StringCchCopyW(dst + n, cap - n, var[i].value))) return 0;
                n += wcslen(var[i].value);
            }
            src += len;
            break;
        }
        if (i < 5) continue;

        if (n + 1 >= cap) return 0;
        dst[n++] = *src++;
    }

    if (n >= cap) return 0;
    dst[n] = L'\0';
    return 1;
}

int zapret_strategy_args(const wchar_t *dir, const wchar_t *strategy,
                         wchar_t *out, size_t cap)
{
    static char     bytes[65536];
    static wchar_t  text[65536];   /* must hold whatever bytes[] converts to */
    static wchar_t  joined[ARGS_MAX];
    wchar_t         root[ZAPRET_PATH_MAX];
    wchar_t         bat[ZAPRET_PATH_MAX];
    wchar_t         gtcp[16], gudp[16];
    wchar_t        *line, *next, *mark;
    size_t          n = 0, len;

    if (!dir || !strategy || !out || cap == 0) return 0;
    out[0] = L'\0';

    if (FAILED(StringCchCopyW(root, ZAPRET_PATH_MAX, dir))) return 0;
    len = wcslen(root);
    if (len && root[len - 1] != L'\\' &&
        FAILED(StringCchCatW(root, ZAPRET_PATH_MAX, L"\\"))) return 0;

    if (FAILED(StringCchPrintfW(bat, ZAPRET_PATH_MAX, L"%s%s.bat", root, strategy)))
        return 0;
    if (!read_head(bat, bytes, sizeof bytes)) return 0;
    if (MultiByteToWideChar(CP_UTF8, 0, bytes, -1, text, 65536) == 0) return 0;

    mark = wcsstr(text, L"winws.exe\"");
    if (!mark) return 0;
    line = mark + wcslen(L"winws.exe\"");

    /* A trailing caret continues the command on the next line. */
    for (;;) {
        wchar_t *eol = wcspbrk(line, L"\r\n");
        wchar_t  keep;
        int      more;

        if (eol) { keep = *eol; *eol = L'\0'; } else { keep = 0; }

        len = wcslen(line);
        while (len && (line[len - 1] == L' ' || line[len - 1] == L'\t')) line[--len] = L'\0';
        more = (len && line[len - 1] == L'^');
        if (more) line[--len] = L'\0';

        while (*line == L' ' || *line == L'\t') { line++; len--; }

        if (n && n + 1 < ARGS_MAX) joined[n++] = L' ';
        if (n + len >= ARGS_MAX) return 0;
        memcpy(joined + n, line, len * sizeof(wchar_t));
        n += len;

        if (!more || !eol) break;

        *eol = keep;
        next = eol;
        while (*next == L'\r' || *next == L'\n') next++;
        if (!*next) break;
        line = next;
    }
    joined[n] = L'\0';

    game_filter(root, gtcp, gudp, 16);
    return expand(joined, root, gtcp, gudp, out, cap);
}

/* ---- the Windows service -------------------------------------------- */

static void kill_winws(void)
{
    PROCESSENTRY32W pe;
    HANDLE          snap;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE p;
            if (_wcsicmp(pe.szExeFile, L"winws.exe") != 0) continue;
            p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (!p) continue;
            TerminateProcess(p, 1);
            WaitForSingleObject(p, 2000);
            CloseHandle(p);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

/* Ask the service to stop, then wait for it whether or not the request was
   accepted: a service in START_PENDING refuses control codes but still ends
   up stopped, and deleting one that is still running leaves the name behind. */
static void stop_and_wait(SC_HANDLE svc)
{
    SERVICE_STATUS st;
    int            i;

    if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_STOPPED)
        return;

    ControlService(svc, SERVICE_CONTROL_STOP, &st);

    for (i = 0; i < 50; i++) {
        if (!QueryServiceStatus(svc, &st)) return;
        if (st.dwCurrentState == SERVICE_STOPPED) return;
        Sleep(100);
    }
}

/* DeleteService only marks the service for deletion. The name survives until
   every handle is closed and the process is gone, and creating the new
   service before that fails with ERROR_SERVICE_MARKED_FOR_DELETE.
   service.bat never trips over this because each sc call costs a process
   spawn; we are fast enough to hit it. */
static int wait_gone(SC_HANDLE scm)
{
    int i;

    for (i = 0; i < 50; i++) {
        SC_HANDLE svc = OpenServiceW(scm, L"zapret", SERVICE_QUERY_STATUS);
        if (!svc) return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
        CloseServiceHandle(svc);
        Sleep(100);
    }
    return 0;
}

static int remove_service(SC_HANDLE scm)
{
    SC_HANDLE svc;
    int       ok = 1;

    svc = OpenServiceW(scm, L"zapret",
                       SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;

    stop_and_wait(svc);
    if (!DeleteService(svc) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE)
        ok = 0;
    CloseServiceHandle(svc);
    return ok;
}

int zapret_service_remove(wchar_t *err, size_t errcap)
{
    SC_HANDLE scm;
    int       ok;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        fail(err, errcap, L"Нет доступа к диспетчеру служб");
        return 0;
    }

    ok = remove_service(scm);
    CloseServiceHandle(scm);
    kill_winws();

    if (!ok) fail(err, errcap, L"Не удалось удалить службу zapret");
    return ok;
}

int zapret_service_install(const wchar_t *dir, const wchar_t *strategy,
                           wchar_t *err, size_t errcap)
{
    static wchar_t args[ARGS_MAX];
    static wchar_t bin_path[ARGS_MAX];
    wchar_t        root[ZAPRET_PATH_MAX];
    SC_HANDLE      scm, svc;
    SERVICE_DESCRIPTIONW desc;
    HKEY           key;
    size_t         len;

    if (!zapret_strategy_args(dir, strategy, args, ARGS_MAX)) {
        fail(err, errcap, L"Не удалось разобрать файл стратегии");
        return 0;
    }

    if (FAILED(StringCchCopyW(root, ZAPRET_PATH_MAX, dir))) return 0;
    len = wcslen(root);
    if (len && root[len - 1] != L'\\' &&
        FAILED(StringCchCatW(root, ZAPRET_PATH_MAX, L"\\"))) return 0;

    if (FAILED(StringCchPrintfW(bin_path, ARGS_MAX, L"\"%sbin\\winws.exe\" %s",
                                root, args))) {
        fail(err, errcap, L"Слишком длинная командная строка стратегии");
        return 0;
    }

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        fail(err, errcap, L"Нет прав на создание службы");
        return 0;
    }

    /* Replace whatever was installed before, as service.bat does. */
    if (!remove_service(scm)) {
        CloseServiceHandle(scm);
        fail(err, errcap, L"Прежняя служба zapret не удалилась");
        return 0;
    }
    kill_winws();

    if (!wait_gone(scm)) {
        CloseServiceHandle(scm);
        fail(err, errcap, L"Прежняя служба ещё удаляется, повторите через несколько секунд");
        return 0;
    }

    svc = CreateServiceW(scm, L"zapret", L"zapret", SERVICE_ALL_ACCESS,
                         SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                         SERVICE_ERROR_NORMAL, bin_path,
                         NULL, NULL, NULL, NULL, NULL);
    if (!svc) {
        wchar_t msg[160];
        StringCchPrintfW(msg, 160, L"Не удалось создать службу zapret (ошибка %lu)",
                         (unsigned long)GetLastError());
        CloseServiceHandle(scm);
        fail(err, errcap, msg);
        return 0;
    }

    desc.lpDescription = (LPWSTR)L"Zapret DPI bypass software";
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    /* Record the strategy before starting, so the stored name always matches
       what is installed even if the start fails. */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"System\\CurrentControlSet\\Services\\zapret",
                      0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"zapret-discord-youtube", 0, REG_SZ,
                       (const BYTE *)strategy,
                       (DWORD)((wcslen(strategy) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }

    if (!StartServiceW(svc, 0, NULL)) {
        wchar_t msg[200];
        StringCchPrintfW(msg, 200,
                         L"Служба создана, но не запустилась (ошибка %lu). "
                         L"Она осталась установленной — проверьте: sc qc zapret",
                         (unsigned long)GetLastError());
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        fail(err, errcap, msg);
        return 0;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 1;
}

int zapret_service_restart(wchar_t *err, size_t errcap)
{
    SC_HANDLE scm, svc;
    int       ok = 0;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        fail(err, errcap, L"Нет доступа к диспетчеру служб");
        return 0;
    }

    svc = OpenServiceW(scm, L"zapret",
                       SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        fail(err, errcap, L"Служба zapret не установлена");
        return 0;
    }

    stop_and_wait(svc);
    kill_winws();

    if (StartServiceW(svc, 0, NULL)) ok = 1;
    else fail(err, errcap, L"Служба не запустилась");

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}
