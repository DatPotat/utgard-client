/* winsock2.h must precede windows.h, which net.h pulls in. */
#include <winsock2.h>
#include <ws2tcpip.h>

#include "net.h"

#include <winhttp.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <strsafe.h>
#include <stdlib.h>

static int fail(wchar_t *err, size_t cap, const wchar_t *text)
{
    if (err && cap) StringCchCopyW(err, cap, text);
    return 0;
}

/* The WinHTTP codes a user actually meets, in words. Meanings are Microsoft's
   own, from the WinHTTP error reference; the number is kept so it can be
   looked up. */
static const wchar_t *winhttp_reason(DWORD code)
{
    switch (code) {
    case 12002: return L"сервер не ответил за отведённое время";
    case 12007: return L"не удалось найти адрес сервера";
    case 12029: return L"не удалось подключиться к серверу";
    case 12030: return L"соединение сброшено или прервано, либо сервер "
                       L"не поддерживает HTTPS на этом адресе";
    case 12152: return L"сервер прислал ответ, который не удалось разобрать";
    case 12175: return L"сертификат сервера не принят";
    default:    return NULL;
    }
}

static int fail_code(wchar_t *err, size_t cap, const wchar_t *text, DWORD code)
{
    const wchar_t *why = winhttp_reason(code);
    if (!err || !cap) return 0;
    if (why) StringCchPrintfW(err, cap, L"%s: %s (ошибка %lu)", text, why, (unsigned long)code);
    else     StringCchPrintfW(err, cap, L"%s (ошибка %lu)", text, (unsigned long)code);
    return 0;
}

int net_fetch(const wchar_t *url, char **body, size_t *len,
              wchar_t *err, size_t errcap)
{
    URL_COMPONENTS  uc;
    wchar_t         host[256];
    wchar_t         path[2048];
    HINTERNET       session = NULL, conn = NULL, req = NULL;
    DWORD           status = 0, status_len = sizeof status;
    char           *buf = NULL;
    size_t          total = 0;
    int             ok = 0;

    if (!url || !body || !len) return fail(err, errcap, L"Пустой адрес");
    *body = NULL;
    *len = 0;

    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize     = sizeof uc;
    uc.lpszHostName     = host;
    uc.dwHostNameLength = (DWORD)(sizeof host / sizeof host[0]);
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = (DWORD)(sizeof path / sizeof path[0]);

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return fail(err, errcap, L"Адрес не похож на ссылку https://");
    /* HTTPS only. A subscription carries server addresses and keys; fetched
       over plain HTTP it can be rewritten on the way and point every profile
       at someone else's server. */
    if (uc.nScheme != INTERNET_SCHEME_HTTPS)
        return fail(err, errcap, L"Подписка принимается только по https:// — "
                                 L"по http:// её можно подменить по дороге");

    session = WinHttpOpen(L"utgard/1.0",
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return fail_code(err, errcap, L"Не удалось открыть сетевую сессию",
                                   GetLastError());

    /* Resolve 10 s, connect 10 s, send 10 s, receive 20 s: a subscription is
       small, and a hung panel should not keep the button disabled for a minute. */
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 20000);

    conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { fail_code(err, errcap, L"Не удалось подключиться к серверу",
                           GetLastError()); goto done; }

    req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS
                                 ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { fail_code(err, errcap, L"Не удалось создать запрос", GetLastError());
                goto done; }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_SECURE_FAILURE)
            fail(err, errcap, L"Сертификат сервера не принят");
        else
            fail_code(err, errcap, L"Подписка не загрузилась", e);
        goto done;
    }

    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &status_len, WINHTTP_NO_HEADER_INDEX)) {
        fail_code(err, errcap, L"Не удалось прочитать ответ", GetLastError());
        goto done;
    }
    if (status != 200) {
        if (err && errcap)
            StringCchPrintfW(err, errcap, L"Сервер ответил кодом %lu", (unsigned long)status);
        goto done;
    }

    buf = (char *)malloc(NET_BODY_MAX + 1);
    if (!buf) { fail(err, errcap, L"Не хватило памяти"); goto done; }

    for (;;) {
        DWORD avail = 0, got = 0;

        if (!WinHttpQueryDataAvailable(req, &avail)) {
            fail_code(err, errcap, L"Обрыв при чтении ответа", GetLastError());
            goto done;
        }
        if (avail == 0) break;

        if (total + avail > NET_BODY_MAX) {
            fail(err, errcap, L"Ответ подписки слишком большой");
            goto done;
        }
        if (!WinHttpReadData(req, buf + total, avail, &got)) {
            fail_code(err, errcap, L"Обрыв при чтении ответа", GetLastError());
            goto done;
        }
        if (got == 0) break;
        total += got;
    }

    if (total == 0) { fail(err, errcap, L"Сервер вернул пустой ответ"); goto done; }

    buf[total] = '\0';
    *body = buf;
    *len  = total;
    buf   = NULL;
    ok    = 1;

done:
    free(buf);
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

int net_download(const wchar_t *url, const wchar_t *path,
                 wchar_t *err, size_t errcap)
{
    URL_COMPONENTS uc;
    wchar_t   host[256], upath[2048];
    HINTERNET session = NULL, conn = NULL, req = NULL;
    HANDLE    file = INVALID_HANDLE_VALUE;
    DWORD     status = 0, status_len = sizeof status;
    char     *chunk = NULL;
    int       ok = 0;

    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize     = sizeof uc;
    uc.lpszHostName     = host;
    uc.dwHostNameLength = (DWORD)(sizeof host / sizeof host[0]);
    uc.lpszUrlPath      = upath;
    uc.dwUrlPathLength  = (DWORD)(sizeof upath / sizeof upath[0]);

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return fail(err, errcap, L"Неверный адрес загрузки");
    if (uc.nScheme != INTERNET_SCHEME_HTTPS)
        return fail(err, errcap, L"Загрузка разрешена только по https");

    session = WinHttpOpen(L"utgard/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return fail_code(err, errcap, L"Не удалось открыть сессию",
                                   GetLastError());
    /* Generous receive timeout: this is tens of megabytes, not a page. */
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 120000);

    conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { fail_code(err, errcap, L"Не удалось подключиться", GetLastError());
                 goto done; }

    req = WinHttpOpenRequest(conn, L"GET", upath, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { fail_code(err, errcap, L"Не удалось создать запрос", GetLastError());
                goto done; }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_SECURE_FAILURE) fail(err, errcap, L"Сертификат сервера не принят");
        else                                   fail_code(err, errcap, L"Сервер не ответил", e);
        goto done;
    }

    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) {
        if (err && errcap)
            StringCchPrintfW(err, errcap, L"Сервер ответил кодом %lu", (unsigned long)status);
        goto done;
    }

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { fail(err, errcap, L"Не удалось создать файл");
                                        goto done; }

    chunk = (char *)malloc(65536);
    if (!chunk) { fail(err, errcap, L"Не хватило памяти"); goto done; }

    for (;;) {
        DWORD avail = 0, got = 0, written = 0;

        if (!WinHttpQueryDataAvailable(req, &avail)) {
            fail_code(err, errcap, L"Обрыв загрузки", GetLastError());
            goto done;
        }
        if (avail == 0) break;
        if (avail > 65536) avail = 65536;
        if (!WinHttpReadData(req, chunk, avail, &got) || got == 0) {
            fail_code(err, errcap, L"Обрыв загрузки", GetLastError());
            goto done;
        }
        if (!WriteFile(file, chunk, got, &written, NULL) || written != got) {
            fail(err, errcap, L"Не удалось записать файл");
            goto done;
        }
    }

    ok = 1;

done:
    free(chunk);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
    if (!ok) DeleteFileW(path);
    return ok;
}

/* ---- reachability --------------------------------------------------- */

static int winsock_ready(void)
{
    static int started;
    WSADATA    wsa;

    if (started) return 1;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    started = 1;
    return 1;
}

static int elapsed_ms(LARGE_INTEGER from)
{
    LARGE_INTEGER now, freq;

    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0) return -1;
    return (int)((now.QuadPart - from.QuadPart) * 1000 / freq.QuadPart);
}

static int probe_tcp(const struct addrinfo *ai, int timeout_ms)
{
    SOCKET        s;
    u_long        nonblock = 1;
    fd_set        wr, ex;
    struct timeval tv;
    LARGE_INTEGER start;
    int           err = 0, len = sizeof err;
    int           ms;

    s = socket(ai->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    ioctlsocket(s, FIONBIO, &nonblock);

    QueryPerformanceCounter(&start);
    if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
        ms = elapsed_ms(start);
        closesocket(s);
        return ms;
    }
    if (WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); return -1; }

    FD_ZERO(&wr); FD_SET(s, &wr);
    FD_ZERO(&ex); FD_SET(s, &ex);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(0, NULL, &wr, &ex, &tv) <= 0) { closesocket(s); return -1; }
    if (FD_ISSET(s, &ex)) { closesocket(s); return -1; }

    /* select() reports writable for a refused connection too. */
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0) {
        closesocket(s);
        return -1;
    }

    ms = elapsed_ms(start);
    closesocket(s);
    return ms;
}

static int probe_icmp(const struct addrinfo *ai, int timeout_ms)
{
    HANDLE                h;
    char                  payload[32];
    char                  reply[sizeof(ICMP_ECHO_REPLY) + sizeof payload + 8];
    const struct sockaddr_in *sin;
    DWORD                 n;
    int                   ms = -1;

    if (ai->ai_family != AF_INET) return -1;   /* IcmpSendEcho is IPv4 only */
    sin = (const struct sockaddr_in *)ai->ai_addr;

    h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return -1;

    memset(payload, 'u', sizeof payload);
    n = IcmpSendEcho(h, sin->sin_addr.s_addr, payload, (WORD)sizeof payload,
                     NULL, reply, (DWORD)sizeof reply, (DWORD)timeout_ms);
    if (n > 0) {
        const ICMP_ECHO_REPLY *r = (const ICMP_ECHO_REPLY *)reply;
        if (r->Status == IP_SUCCESS) ms = (int)r->RoundTripTime;
    }

    IcmpCloseHandle(h);
    return ms;
}

int net_probe(const char *host_utf8, int port, int use_icmp, int timeout_ms)
{
    struct addrinfo  hints;
    struct addrinfo *ai = NULL;
    char             service[8];
    int              ms;

    if (!host_utf8 || !host_utf8[0] || !winsock_ready()) return -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = use_icmp ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    StringCchPrintfA(service, sizeof service, "%d", port > 0 ? port : 443);

    if (getaddrinfo(host_utf8, service, &hints, &ai) != 0 || !ai) return -1;

    ms = use_icmp ? probe_icmp(ai, timeout_ms) : probe_tcp(ai, timeout_ms);

    freeaddrinfo(ai);
    return ms;
}

int net_resolve4(const char *host_utf8, char out[][16], int max)
{
    struct addrinfo  hints;
    struct addrinfo *ai = NULL, *p;
    int              n = 0;

    if (!host_utf8 || !host_utf8[0] || max <= 0 || !winsock_ready()) return 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host_utf8, NULL, &hints, &ai) != 0 || !ai) return 0;

    for (p = ai; p && n < max; p = p->ai_next) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)p->ai_addr;
        int  i;
        char text[16];

        if (p->ai_family != AF_INET) continue;
        if (!inet_ntop(AF_INET, &sin->sin_addr, text, sizeof text)) continue;

        for (i = 0; i < n; i++) if (strcmp(out[i], text) == 0) break;
        if (i < n) continue;                       /* same address twice */
        StringCchCopyA(out[n], 16, text);
        n++;
    }

    freeaddrinfo(ai);
    return n;
}
