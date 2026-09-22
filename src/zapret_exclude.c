/* Teaching zapret to leave the VPN servers alone.

   Two things were checked against the real zapret-discord-youtube repository
   before this was written:

     - lists\ipset-exclude.txt is tracked by git and holds the project's own
       private ranges; an update overwrites it. lists\ipset-exclude-user.txt is
       in .gitignore, absent from the repository, and created by service.bat
       itself - that is the file meant for the user, and the one we write.
     - the shipped ipset files contain no comment lines at all, and nothing
       documents comment support for them, so no marker is written into the
       file. What we added is remembered on our side instead.

   The file is ASCII with CRLF, one subnet per line, and service.bat refuses to
   leave it empty - it seeds a placeholder. We keep that behaviour. */

#include "zapret.h"

#include <strsafe.h>

#include "fileio.h"
#include <shellapi.h>

#include "net.h"

#define EXC_MAX      64
#define EXC_TEXT_MAX 65536
#define PLACEHOLDER  "203.0.113.113/32"

static int exc_path(const wchar_t *dir, const wchar_t *name,
                    wchar_t *out, size_t cap)
{
    wchar_t base[ZAPRET_PATH_MAX];
    size_t  len;

    if (!dir || !dir[0]) return 0;
    if (FAILED(StringCchCopyW(base, ZAPRET_PATH_MAX, dir))) return 0;
    len = wcslen(base);
    if (len && base[len - 1] != L'\\' &&
        FAILED(StringCchCatW(base, ZAPRET_PATH_MAX, L"\\"))) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%slists\\%s", base, name));
}



/* Does this exact line appear in the text? */
static int has_line(const char *text, const char *want)
{
    const char *p = text;
    size_t      wl = strlen(want);

    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '\n') len++;
        {
            size_t n = len;
            while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
            if (n == wl && memcmp(p, want, wl) == 0) return 1;
        }
        p += len;
        if (*p == '\n') p++;
    }
    return 0;
}

/* Our own record of what we put in, kept beside the executable rather than as
   a marker inside a file whose comment syntax is unknown. */
static int memo_path(wchar_t *out, size_t cap)
{
    wchar_t  dir[ZAPRET_PATH_MAX];
    wchar_t *slash;
    DWORD    n;

    n = GetModuleFileNameW(NULL, dir, (DWORD)(sizeof dir / sizeof dir[0]));
    if (n == 0 || n >= sizeof dir / sizeof dir[0]) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%szapret-excluded.txt", dir));
}

static int memo_read(char lines[][20], int max)
{
    static char buf[4096];
    wchar_t     path[ZAPRET_PATH_MAX];
    const char *p;
    int         n = 0;

    if (!memo_path(path, ZAPRET_PATH_MAX)) return 0;
    if (!file_read(path, buf, sizeof buf, NULL)) return 0;

    p = buf;
    while (*p && n < max) {
        size_t len = 0, cut;
        while (p[len] && p[len] != '\n') len++;
        cut = len;
        while (cut && (p[cut - 1] == '\r' || p[cut - 1] == ' ')) cut--;
        if (cut && cut < 20) {
            memcpy(lines[n], p, cut);
            lines[n][cut] = '\0';
            n++;
        }
        p += len;
        if (*p == '\n') p++;
    }
    return n;
}

static void memo_write(char lines[][20], int n)
{
    static char buf[4096];
    wchar_t     path[ZAPRET_PATH_MAX];
    size_t      used = 0;
    int         i;

    buf[0] = '\0';
    for (i = 0; i < n; i++) {
        size_t len = strlen(lines[i]);
        if (used + len + 3 >= sizeof buf) break;
        memcpy(buf + used, lines[i], len);
        used += len;
        buf[used++] = '\r';
        buf[used++] = '\n';
    }
    buf[used] = '\0';

    if (memo_path(path, ZAPRET_PATH_MAX)) file_write(path, buf, strlen(buf));
}

int zapret_exclude_present(const wchar_t *dir, const char ips[][16], int count)
{
    static char text[EXC_TEXT_MAX];
    wchar_t     path[ZAPRET_PATH_MAX];
    int         i, found = 0;

    if (count <= 0) return 0;
    if (!exc_path(dir, L"ipset-exclude-user.txt", path, ZAPRET_PATH_MAX)) return 0;

    text[0] = '\0';
    if (!file_read(path, text, EXC_TEXT_MAX, NULL)) return 0;

    for (i = 0; i < count; i++) {
        char entry[20];
        if (!ips[i][0]) continue;
        StringCchPrintfA(entry, 20, "%s/32", ips[i]);
        if (has_line(text, entry)) found++;
    }
    return found;
}

int zapret_exclude_patch(const wchar_t *dir, const char ips[][16], int count,
                         int *added, int *kept, wchar_t *err, size_t errcap)
{
    static char text[EXC_TEXT_MAX];
    static char out[EXC_TEXT_MAX];
    char        memo[EXC_MAX][20];
    char        fresh[EXC_MAX][20];
    wchar_t     path[ZAPRET_PATH_MAX], backup[ZAPRET_PATH_MAX];
    const char *p;
    size_t      used = 0;
    int         old_n, i, nfresh = 0, survived = 0, newly = 0;

    if (added) *added = 0;
    if (kept) *kept = 0;
    if (count <= 0) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Нет адресов для добавления");
        return 0;
    }
    if (!exc_path(dir, L"ipset-exclude-user.txt", path, ZAPRET_PATH_MAX)) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось определить путь");
        return 0;
    }

    for (i = 0; i < count && nfresh < EXC_MAX; i++) {
        if (!ips[i][0]) continue;
        StringCchPrintfA(fresh[nfresh], 20, "%s/32", ips[i]);
        nfresh++;
    }
    if (nfresh == 0) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Нет адресов для добавления");
        return 0;
    }

    old_n = memo_read(memo, EXC_MAX);
    text[0] = '\0';
    file_read(path, text, EXC_TEXT_MAX, NULL);

    /* Back up once, before the first change. */
    if (text[0] &&
        SUCCEEDED(StringCchPrintfW(backup, ZAPRET_PATH_MAX, L"%s.utgard.bak", path)) &&
        GetFileAttributesW(backup) == INVALID_FILE_ATTRIBUTES)
        CopyFileW(path, backup, TRUE);

    /* Keep every line that is not one we put there ourselves, and not one we
       are about to write again. */
    out[0] = '\0';
    p = text;
    while (*p) {
        char   line[64];
        size_t len = 0, cut;
        int    drop = 0;

        while (p[len] && p[len] != '\n') len++;
        cut = len;
        while (cut && (p[cut - 1] == '\r' || p[cut - 1] == ' ' || p[cut - 1] == '\t')) cut--;

        if (cut && cut < sizeof line) {
            memcpy(line, p, cut);
            line[cut] = '\0';

            for (i = 0; i < old_n; i++) if (strcmp(line, memo[i]) == 0) drop = 1;
            for (i = 0; i < nfresh; i++) if (strcmp(line, fresh[i]) == 0) drop = 1;
            if (strcmp(line, PLACEHOLDER) == 0) drop = 1;

            if (!drop) {
                size_t l = strlen(line);
                if (used + l + 3 < EXC_TEXT_MAX) {
                    memcpy(out + used, line, l);
                    used += l;
                    out[used++] = '\r';
                    out[used++] = '\n';
                    survived++;
                }
            }
        }

        p += len;
        if (*p == '\n') p++;
    }

    for (i = 0; i < nfresh; i++) {
        size_t l = strlen(fresh[i]);
        if (used + l + 3 >= EXC_TEXT_MAX) break;
        memcpy(out + used, fresh[i], l);
        used += l;
        out[used++] = '\r';
        out[used++] = '\n';
        newly++;
    }

    /* service.bat never leaves this file empty; neither do we. */
    if (used == 0) {
        StringCchCopyA(out, EXC_TEXT_MAX, PLACEHOLDER "\r\n");
        used = strlen(out);
    }
    out[used] = '\0';

    if (!file_write(path, out, strlen(out))) {
        if (err && errcap)
            StringCchCopyW(err, errcap, L"Не удалось записать ipset-exclude-user.txt");
        return 0;
    }

    memo_write(fresh, nfresh);

    if (added) *added = newly;
    if (kept) *kept = survived;
    return 1;
}

/* ---- game filter ----------------------------------------------------- */

static int util_path(const wchar_t *dir, const wchar_t *tail,
                     wchar_t *out, size_t cap)
{
    wchar_t base[ZAPRET_PATH_MAX];
    size_t  len;

    if (!dir || !dir[0]) return 0;
    if (FAILED(StringCchCopyW(base, ZAPRET_PATH_MAX, dir))) return 0;
    len = wcslen(base);
    if (len && base[len - 1] != L'\\' &&
        FAILED(StringCchCatW(base, ZAPRET_PATH_MAX, L"\\"))) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%s", base, tail));
}

zapret_game_mode zapret_game_get(const wchar_t *dir)
{
    wchar_t path[ZAPRET_PATH_MAX];
    char    buf[64];
    char   *p;

    if (!util_path(dir, L"utils\\game_filter.enabled", path, ZAPRET_PATH_MAX))
        return GAME_OFF;
    if (!file_read(path, buf, sizeof buf, NULL)) return GAME_OFF;

    for (p = buf; *p && *p != '\r' && *p != '\n'; p++) { }
    *p = '\0';
    for (p = buf; *p == ' ' || *p == '\t'; p++) { }

    if (_stricmp(p, "all") == 0) return GAME_ALL;
    if (_stricmp(p, "tcp") == 0) return GAME_TCP;
    if (_stricmp(p, "udp") == 0) return GAME_UDP;
    return GAME_OFF;
}

int zapret_game_set(const wchar_t *dir, zapret_game_mode mode)
{
    wchar_t path[ZAPRET_PATH_MAX], utils[ZAPRET_PATH_MAX];

    if (!util_path(dir, L"utils", utils, ZAPRET_PATH_MAX)) return 0;
    CreateDirectoryW(utils, NULL);
    if (!util_path(dir, L"utils\\game_filter.enabled", path, ZAPRET_PATH_MAX))
        return 0;

    if (mode == GAME_OFF) {
        DeleteFileW(path);
        return 1;
    }
    {
        const char *word = mode == GAME_ALL ? "all\r\n" : mode == GAME_TCP ? "tcp\r\n" : "udp\r\n";
        return file_write(path, word, strlen(word));
    }
}

/* ---- ipset filter ---------------------------------------------------- */

zapret_ipset_mode zapret_ipset_get(const wchar_t *dir)
{
    static char text[EXC_TEXT_MAX];
    wchar_t     path[ZAPRET_PATH_MAX];

    if (!exc_path(dir, L"ipset-all.txt", path, ZAPRET_PATH_MAX)) return IPSET_LOADED;
    text[0] = '\0';
    if (!file_read(path, text, EXC_TEXT_MAX, NULL) || text[0] == '\0') return IPSET_ANY;
    if (has_line(text, PLACEHOLDER)) return IPSET_NONE;
    return IPSET_LOADED;
}

/* loaded -> none -> any -> loaded, the same rotation service.bat performs,
   including keeping the real list in ipset-all.txt.backup. */
int zapret_ipset_cycle(const wchar_t *dir, wchar_t *err, size_t errcap)
{
    wchar_t           path[ZAPRET_PATH_MAX], backup[ZAPRET_PATH_MAX];
    zapret_ipset_mode mode = zapret_ipset_get(dir);

    if (!exc_path(dir, L"ipset-all.txt", path, ZAPRET_PATH_MAX) ||
        FAILED(StringCchPrintfW(backup, ZAPRET_PATH_MAX, L"%s.backup", path))) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось определить путь");
        return 0;
    }

    if (mode == IPSET_LOADED) {
        DeleteFileW(backup);
        if (!MoveFileW(path, backup)) {
            if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось сохранить список");
            return 0;
        }
        return file_write(path, PLACEHOLDER "\r\n", strlen(PLACEHOLDER "\r\n"));
    }

    if (mode == IPSET_NONE) return file_write(path, "", 0);

    /* any -> loaded */
    if (GetFileAttributesW(backup) == INVALID_FILE_ATTRIBUTES) {
        if (err && errcap)
            StringCchCopyW(err, errcap,
                L"Нет резервной копии списка. Сначала обновите список IPSet.");
        return 0;
    }
    DeleteFileW(path);
    if (!MoveFileW(backup, path)) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось восстановить список");
        return 0;
    }
    return 1;
}

/* ---- updates from the project repository ----------------------------- */

static const wchar_t IPSET_URL[] =
    L"https://raw.githubusercontent.com/Flowseal/zapret-discord-youtube/"
    L"refs/heads/main/.service/ipset-service.txt";
static const wchar_t HOSTS_URL[] =
    L"https://raw.githubusercontent.com/Flowseal/zapret-discord-youtube/"
    L"refs/heads/main/.service/hosts";

int zapret_ipset_update(const wchar_t *dir, wchar_t *err, size_t errcap)
{
    wchar_t path[ZAPRET_PATH_MAX], tmp[ZAPRET_PATH_MAX];

    if (!exc_path(dir, L"ipset-all.txt", path, ZAPRET_PATH_MAX)) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось определить путь");
        return 0;
    }
    if (FAILED(StringCchPrintfW(tmp, ZAPRET_PATH_MAX, L"%s.download", path))) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Слишком длинный путь");
        return 0;
    }

    /* Download beside the list and swap it in only once it has arrived: a
       broken connection must not leave the user with half a list. */
    if (!net_download(IPSET_URL, tmp, err, errcap)) return 0;

    DeleteFileW(path);
    if (!MoveFileW(tmp, path)) {
        DeleteFileW(tmp);
        if (err && errcap) StringCchCopyW(err, errcap, L"Не удалось заменить ipset-all.txt");
        return 0;
    }
    return 1;
}

/* First and last line of the downloaded file, as service.bat compares them. */
static int line_present(const char *hay, const char *text, int last)
{
    char        line[512];
    const char *p = text;
    size_t      len = 0, cut;

    line[0] = '\0';
    while (*p) {
        len = 0;
        while (p[len] && p[len] != '\n') len++;
        cut = len;
        while (cut && (p[cut - 1] == '\r' || p[cut - 1] == ' ')) cut--;
        if (cut && cut < sizeof line) {
            memcpy(line, p, cut);
            line[cut] = '\0';
            if (!last) break;
        }
        p += len;
        if (*p == '\n') p++;
    }
    if (!line[0]) return 1;
    return strstr(hay, line) != NULL;
}

int zapret_hosts_check(wchar_t *temp_path, size_t temp_cap, int *needs_update,
                       wchar_t *err, size_t errcap)
{
    static char downloaded[EXC_TEXT_MAX];
    static char system_hosts[EXC_TEXT_MAX];
    wchar_t     temp_dir[ZAPRET_PATH_MAX], sysdir[MAX_PATH], hosts[ZAPRET_PATH_MAX];

    if (needs_update) *needs_update = 0;
    if (!GetTempPathW(ZAPRET_PATH_MAX, temp_dir) ||
        FAILED(StringCchPrintfW(temp_path, temp_cap, L"%szapret_hosts.txt", temp_dir))) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Нет временной папки");
        return 0;
    }

    if (!net_download(HOSTS_URL, temp_path, err, errcap)) return 0;
    if (!file_read(temp_path, downloaded, EXC_TEXT_MAX, NULL)) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Скачанный файл не читается");
        return 0;
    }

    if (!GetSystemDirectoryW(sysdir, MAX_PATH) ||
        FAILED(StringCchPrintfW(hosts, ZAPRET_PATH_MAX,
                                L"%s\\drivers\\etc\\hosts", sysdir))) {
        if (err && errcap) StringCchCopyW(err, errcap, L"Не найден системный hosts");
        return 0;
    }

    system_hosts[0] = '\0';
    file_read(hosts, system_hosts, EXC_TEXT_MAX, NULL);

    if (!line_present(system_hosts, downloaded, 0) ||
        !line_present(system_hosts, downloaded, 1))
        if (needs_update) *needs_update = 1;

    return 1;
}
