#include <winsock2.h>
#include <ws2tcpip.h>
#include "pacloop.h"
#include "pac.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct pac_script {
    SOCKET listener;
    HANDLE thread, stop;
    char *text;
    size_t length;
    char token[65];
    wchar_t url[160];
    HINTERNET session;
};

static int send_all(SOCKET s, const char *p, int n)
{
    while (n > 0) {
        int k = send(s, p, n, 0);
        if (k <= 0) return 0;
        p += k;
        n -= k;
    }
    return 1;
}

/* WinHTTP does not support file:// PAC URLs. Serve the immutable snapshot
   on loopback, with an unpredictable path, without any filesystem mapping. */
static DWORD WINAPI serve(void *arg)
{
    pac_script *p = (pac_script *)arg;
    while (WaitForSingleObject(p->stop, 0) != WAIT_OBJECT_0) {
        fd_set f;
        struct timeval tv = { 0, 200000 };
        SOCKET s;
        char request[2048], expected[96], header[256];
        int n = 0;
        DWORD timeout = 1000;
        ULONGLONG deadline;
        FD_ZERO(&f);
        FD_SET(p->listener, &f);
        {
            pacloop_state ready = pacloop_select(select(0, &f, NULL, NULL, &tv));
            /* The script must stay served: pause until the next try or stop. */
            if (ready == PACLOOP_ERROR) { WaitForSingleObject(p->stop, 200); continue; }
            if (ready == PACLOOP_IDLE) continue;
        }
        s = accept(p->listener, NULL, NULL);
        if (s == INVALID_SOCKET) continue;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
        deadline = GetTickCount64() + 3000;
        while (n < (int)sizeof request - 1 && GetTickCount64() < deadline) {
            int k = recv(s, request + n, (int)sizeof request - 1 - n, 0);
            if (k <= 0) break;
            n += k;
            request[n] = 0;
            if (strstr(request, "\r\n\r\n")) break;
        }
        request[n] = 0;
        snprintf(expected, sizeof expected, "GET /%s.pac HTTP/1.", p->token);
        if (strstr(request, "\r\n\r\n") && !strncmp(request, expected, strlen(expected))) {
            int h = snprintf(header, sizeof header,
                "HTTP/1.1 200 OK\r\nContent-Type: application/x-ns-proxy-autoconfig\r\n"
                "Content-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)p->length);
            if (send_all(s, header, h)) send_all(s, p->text, (int)p->length);
        } else {
            static const char denied[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(s, denied, sizeof denied - 1);
        }
        closesocket(s);
    }
    return 0;
}

typedef struct {
    HANDLE done;
    LONG refs;
    DWORD error;
    int decision;
} query;

static void release_query(query *q)
{
    if (InterlockedDecrement(&q->refs) == 0) {
        CloseHandle(q->done);
        free(q);
    }
}

static void CALLBACK complete(HINTERNET h, DWORD_PTR context, DWORD status,
                              void *info, DWORD length)
{
    query *q = (query *)context;
    (void)length;
    if (!q) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        release_query(q);
    } else if (status == WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE) {
        WINHTTP_PROXY_RESULT result;
        ZeroMemory(&result, sizeof result);
        q->error = WinHttpGetProxyResult(h, &result);
        if (q->error == ERROR_SUCCESS) {
            if (result.cEntries) q->decision = result.pEntries[0].fProxy ? 1 : 0;
            else q->error = ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
            WinHttpFreeProxyResult(&result);
        }
        SetEvent(q->done);
    } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        q->error = ((WINHTTP_ASYNC_RESULT *)info)->dwError;
        SetEvent(q->done);
    }
}

pac_script *pac_open(const char *text, size_t length, wchar_t *err, size_t cap)
{
    pac_script *p = NULL;
    WSADATA data;
    struct sockaddr_in address;
    int size = sizeof address, exclusive = 1;
    unsigned char random[32];
    size_t i;
    if (!text || !length || length > PAC_MAX || memchr(text, 0, length)) goto fail;
    if (WSAStartup(MAKEWORD(2, 2), &data)) goto fail;
    p = (pac_script *)calloc(1, sizeof *p);
    if (!p) { WSACleanup(); goto fail; }
    p->listener = INVALID_SOCKET;
    p->text = (char *)malloc(length);
    if (!p->text) goto fail;
    memcpy(p->text, text, length);
    p->length = length;
    if (BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) goto fail;
    for (i = 0; i < sizeof random; i++) snprintf(p->token + i * 2, 3, "%02x", random[i]);
    p->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!p->stop) goto fail;
    p->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (p->listener == INVALID_SOCKET) goto fail;
    setsockopt(p->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    ZeroMemory(&address, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(p->listener, (struct sockaddr *)&address, sizeof address) ||
        getsockname(p->listener, (struct sockaddr *)&address, &size) || listen(p->listener, 8)) goto fail;
    StringCchPrintfW(p->url, 160, L"http://127.0.0.1:%u/%S.pac", ntohs(address.sin_port), p->token);
    p->session = WinHttpOpen(L"Utgard PAC", WINHTTP_ACCESS_TYPE_NO_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!p->session) goto fail;
    WinHttpSetTimeouts(p->session, 5000, 5000, 5000, 10000);
    if (WinHttpSetStatusCallback(p->session, complete,
          WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE |
          WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) goto fail;
    p->thread = CreateThread(NULL, 0, serve, p, 0, NULL);
    if (!p->thread) goto fail;
    return p;
fail:
    pac_close(p);
    if (err && cap) StringCchCopyW(err, cap, L"Не удалось открыть PAC (пустой файл, размер более 4 МиБ или ошибка WinHTTP)");
    return NULL;
}

int pac_query(pac_script *p, const wchar_t *url, DWORD *error)
{
    query *q;
    HINTERNET resolver = NULL;
    WINHTTP_AUTOPROXY_OPTIONS options;
    DWORD e;
    int decision = -1;
    q = (query *)calloc(1, sizeof *q);
    if (!q) { if (error) *error = ERROR_NOT_ENOUGH_MEMORY; return -1; }
    q->refs = 1;
    q->decision = -1;
    q->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    e = q->done ? WinHttpCreateProxyResolver(p->session, &resolver) : ERROR_NOT_ENOUGH_MEMORY;
    if (e == ERROR_SUCCESS) {
        DWORD_PTR context = (DWORD_PTR)q;
        /* The handle owns a reference until HANDLE_CLOSING, including after
           a timeout. No callback ever points to a returned stack frame. */
        if (!WinHttpSetOption(resolver, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof context)) {
            e = GetLastError();
        } else {
            InterlockedIncrement(&q->refs);
            ZeroMemory(&options, sizeof options);
            /* Never allow WinHTTP's documented in-process JScript fallback:
               Utgard is elevated. If the AutoProxy service cannot evaluate
               this PAC out of process, fail the query instead. */
            options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL |
                              WINHTTP_AUTOPROXY_RUN_OUTPROCESS_ONLY;
            options.lpszAutoConfigUrl = p->url;
            options.fAutoLogonIfChallenged = FALSE;
            e = WinHttpGetProxyForUrlEx(resolver, url, &options, context);
            if (e == ERROR_IO_PENDING) {
                if (WaitForSingleObject(q->done, 3000) == WAIT_OBJECT_0) {
                    e = q->error;
                    decision = q->decision;
                } else e = ERROR_TIMEOUT;
            }
        }
    }
    if (resolver) WinHttpCloseHandle(resolver);
    release_query(q);
    if (error) *error = e;
    return e == ERROR_SUCCESS ? decision : -1;
}

void pac_close(pac_script *p)
{
    if (!p) return;
    if (p->stop) SetEvent(p->stop);
    if (p->thread) { WaitForSingleObject(p->thread, INFINITE); CloseHandle(p->thread); }
    if (p->listener != INVALID_SOCKET) closesocket(p->listener);
    if (p->session) WinHttpCloseHandle(p->session);
    if (p->stop) CloseHandle(p->stop);
    free(p->text);
    free(p);
    WSACleanup();
}
