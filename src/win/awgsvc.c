/* winsock2.h must come before anything that pulls in windows.h. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include "awgsvc.h"
#include "adapter.h"
#include "coredir.h"
#include <tlhelp32.h>
#include <sddl.h>
#include <shlobj.h>
#include <strsafe.h>

#include "tunnames.h"
#define TUNNEL   UTGARD_AWG_TUN_W
#define SERVICE  L"AmneziaWGTunnel$" TUNNEL          /* fixed by amneziawg.exe */
#define PIPE     L"\\\\.\\pipe\\" TUNNEL L".conf"     /* the name must end in .conf */
#define START_WAIT_MS 30000
/* Metric of the default route we add on the tunnel's adapter; see
   add_bind_route. The service sets the adapter's interface metric to 0
   (AllowedIPs holds 0.0.0.0/0), and the full metric is route + interface,
   so the route stays far behind any ordinary default route. */
#define BIND_ROUTE_METRIC 9000
#define STOP_WAIT_MS  30000

/* Set when %ProgramFiles%\AmneziaWG did not exist before our service first
   ran in this session: the service writes its log there wherever the exe
   lives, and we take away only what we caused. */
static int g_made_pf_dir;

static int say(wchar_t *msg, size_t cap, const wchar_t *text)
{
    if (msg && cap) StringCchCopyW(msg, cap, text);
    return 0;
}

const wchar_t *awgsvc_interface(void) { return TUNNEL; }

static int bin_path(wchar_t *out, size_t cap)
{
    wchar_t dir[MAX_PATH * 2];
    if (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2)) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"\"%s\\%s\" /tunnelservice %s",
                                      dir, CORE_AWG.files[0].name, PIPE));
}

/* 1 ours, 0 absent, -1 someone else's service under our name. */
static int service_is_ours(SC_HANDLE svc)
{
    wchar_t                 want[MAX_PATH * 3];
    QUERY_SERVICE_CONFIGW  *cfg;
    DWORD                   need = 0;
    int                     ours = -1;

    if (!bin_path(want, MAX_PATH * 3)) return -1;
    QueryServiceConfigW(svc, NULL, 0, &need);
    cfg = (QUERY_SERVICE_CONFIGW *)LocalAlloc(LPTR, need ? need : 1);
    if (!cfg) return -1;
    if (QueryServiceConfigW(svc, cfg, need, &need) && cfg->lpBinaryPathName &&
        _wcsicmp(cfg->lpBinaryPathName, want) == 0)
        ours = 1;
    LocalFree(cfg);
    return ours;
}

static DWORD service_state(SC_HANDLE svc, DWORD *pid, DWORD *win32, DWORD *specific)
{
    SERVICE_STATUS_PROCESS st;
    DWORD                  got;

    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (BYTE *)&st, sizeof st, &got))
        return 0;
    if (pid) *pid = st.dwProcessId;
    if (win32) *win32 = st.dwWin32ExitCode;
    if (specific) *specific = st.dwServiceSpecificExitCode;
    return st.dwCurrentState;
}

/* The service's own error codes (amneziawg-windows services/errors.go). */
static const wchar_t *service_error(DWORD code)
{
    switch (code) {
    case 1:  return L"служба не смогла открыть свой журнал";
    case 2:  return L"служба не смогла прочитать конфигурацию";
    case 3:  return L"не удалось создать сетевой адаптер Wintun";
    case 4:  return L"не удалось открыть управляющий канал";
    case 5:  return L"не удалось разрешить адрес сервера";
    case 6:  return L"не удалось включить правила брандмауэра";
    case 7:  return L"не удалось применить настройки туннеля";
    case 8:  return L"не удалось привязать соединения к маршруту по умолчанию";
    case 9:  return L"не удалось настроить адреса сетевого адаптера";
    case 13: return L"не удалось понизить права службы";
    case 15: return L"внутренняя ошибка Windows";
    default: return L"неизвестная ошибка службы";
    }
}

static void explain_stop(DWORD win32, DWORD specific, wchar_t *msg, size_t cap)
{
    if (!msg || !cap) return;
    if (win32 == ERROR_SERVICE_SPECIFIC_ERROR)
        StringCchPrintfW(msg, cap, L"AmneziaWG не запустился: %s (код %lu).\n\n"
                         L"Подробности — в журнале службы: amneziawg\\amneziawg.exe /dumplog",
                         service_error(specific), (unsigned long)specific);
    else
        StringCchPrintfW(msg, cap, L"AmneziaWG не запустился: ошибка Windows %lu.\n\n"
                         L"Подробности — в журнале службы: amneziawg\\amneziawg.exe /dumplog",
                         (unsigned long)win32);
}

/* Stop and delete; waits for both, so the adapter is gone on return. */
static int remove_service(SC_HANDLE scm, SC_HANDLE svc)
{
    SERVICE_STATUS st;
    DWORD          t;

    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (t = 0; t < STOP_WAIT_MS; t += 250) {
        DWORD s = service_state(svc, NULL, NULL, NULL);
        if (s == 0 || s == SERVICE_STOPPED) break;
        Sleep(250);
    }
    DeleteService(svc);
    CloseServiceHandle(svc);
    /* Deletion finishes once the last handle is gone; wait until it has, or
       the next CreateService fails with "marked for deletion". */
    for (t = 0; t < STOP_WAIT_MS; t += 250) {
        SC_HANDLE again = OpenServiceW(scm, SERVICE, SERVICE_QUERY_STATUS);
        if (!again) return 1;
        CloseServiceHandle(again);
        Sleep(250);
    }
    return 0;
}

static int pf_awg_dir(wchar_t *out, size_t cap)
{
    PWSTR pf = NULL;
    int   ok;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_ProgramFiles, 0, NULL, &pf))) return 0;
    ok = SUCCEEDED(StringCchPrintfW(out, cap, L"%s\\AmneziaWG", pf));
    CoTaskMemFree(pf);
    return ok;
}

/* Only if we made it, and only if the user has no AmneziaWG of their own:
   its manager service owns that folder then. */
static void tidy_program_files(SC_HANDLE scm)
{
    wchar_t   dir[MAX_PATH * 2];
    SC_HANDLE mgr;

    if (!g_made_pf_dir || !pf_awg_dir(dir, MAX_PATH * 2)) return;
    mgr = OpenServiceW(scm, L"AmneziaWGManager", SERVICE_QUERY_STATUS);
    if (mgr) { CloseServiceHandle(mgr); return; }
    coredir_wipe(dir);
    g_made_pf_dir = 0;
}

/* sing-box sends a profile's traffic into the tunnel by binding its sockets
   to the adapter (bind_interface, IP_UNICAST_IF). With Table = off the
   adapter has no route but its own address, and Windows refuses such a
   socket at once: "A socket operation was attempted to an unreachable
   network" (WSAENETUNREACH), seen in the sing-box log on a live system.
   An on-link default route on the adapter gives bound sockets a way out.
   Its metric keeps it out of reach for everything else: the physical
   adapter's default route wins for unbound traffic, sing-box's TUN routes
   are more specific, and the service binds its own UDP to the best default
   route other than its adapter. The route belongs to the adapter and goes
   with it when the service stops. */
static int add_bind_route(wchar_t *msg, size_t cap)
{
    NET_LUID           luid;
    MIB_IPFORWARD_ROW2 r;
    DWORD              e;

    /* The present adapter, not a ghost of an earlier one of the same name. */
    if (!adapter_luid(TUNNEL, &luid.Value))
        return say(msg, cap, L"Не найден сетевой адаптер AmneziaWG");
    InitializeIpForwardEntry(&r);
    r.InterfaceLuid                      = luid;
    r.DestinationPrefix.Prefix.si_family = AF_INET;   /* 0.0.0.0/0 */
    r.DestinationPrefix.PrefixLength     = 0;
    r.NextHop.si_family                  = AF_INET;   /* 0.0.0.0: on-link */
    r.Metric                             = BIND_ROUTE_METRIC;
    r.Protocol                           = MIB_IPPROTO_NETMGMT;
    e = CreateIpForwardEntry2(&r);
    if (e != NO_ERROR && e != ERROR_OBJECT_ALREADY_EXISTS) {
        if (msg && cap)
            StringCchPrintfW(msg, cap, L"Не удалось добавить маршрут через адаптер AmneziaWG "
                                       L"(ошибка %lu)", (unsigned long)e);
        return 0;
    }
    return 1;
}

int awgsvc_cleanup(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc;
    int       running = 0;

    if (!scm) return 0;
    svc = OpenServiceW(scm, SERVICE, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG |
                                     SERVICE_STOP | DELETE);
    if (svc) {
        if (service_is_ours(svc) != 1) CloseServiceHandle(svc);
        else if (service_state(svc, NULL, NULL, NULL) == SERVICE_RUNNING) {
            running = 1;
            CloseServiceHandle(svc);
        } else remove_service(scm, svc);
    }
    CloseServiceHandle(scm);
    return running;
}

int awgsvc_running(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc;
    int       running = 0;

    if (!scm) return 0;
    svc = OpenServiceW(scm, SERVICE, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (svc) {
        running = service_is_ours(svc) == 1 &&
                  service_state(svc, NULL, NULL, NULL) == SERVICE_RUNNING;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return running;
}

/* FlushFileBuffers returns once the client has read everything; on a pipe
   that is the only documented way not to lose the data. Run apart, so a
   service that never reads cannot hang us: see flush_or_kill. */
static DWORD WINAPI flush_thread(LPVOID pipe)
{
    FlushFileBuffers((HANDLE)pipe);
    return 0;
}

static int flush_or_kill(HANDLE pipe, DWORD pid)
{
    HANDLE t = CreateThread(NULL, 0, flush_thread, pipe, 0, NULL);
    int    ok = 1;

    if (!t) return 0;
    if (WaitForSingleObject(t, 10000) == WAIT_TIMEOUT) {
        /* Killing the reader closes its end, which ends the flush. */
        HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (p) { TerminateProcess(p, 1); CloseHandle(p); }
        WaitForSingleObject(t, 5000);
        ok = 0;
    }
    CloseHandle(t);
    return ok;
}

int awgsvc_start(const char *conf, wchar_t *msg, size_t cap)
{
    wchar_t              bin[MAX_PATH * 3], probe[MAX_PATH * 2];
    coredir_hold         hold;
    SC_HANDLE            scm = NULL, svc = NULL;
    SECURITY_ATTRIBUTES  sa;
    PSECURITY_DESCRIPTOR sd = NULL;
    SERVICE_SID_INFO     sid = { SERVICE_SID_TYPE_UNRESTRICTED };
    HANDLE               pipe[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
    OVERLAPPED           ov[2];
    DWORD                t, pid = 0, cpid = 0, win32 = 0, specific = 0, put = 0;
    int                  i, written = 0, ok = 0, created = 0;

    if (msg && cap) msg[0] = L'\0';
    ZeroMemory(ov, sizeof ov);
    if (!coredir_hold_verified(&CORE_AWG, &hold, msg, cap, NULL)) return 0;
    if (!bin_path(bin, MAX_PATH * 3)) { say(msg, cap, L"Слишком длинный путь"); goto out; }

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) { say(msg, cap, L"Нет доступа к диспетчеру служб Windows"); goto out; }

    svc = OpenServiceW(scm, SERVICE, SERVICE_ALL_ACCESS);
    if (svc) {
        if (service_is_ours(svc) != 1) {
            say(msg, cap, L"Служба с именем AmneziaWGTunnel$utgard-awg уже есть и принадлежит "
                          L"другой программе. Utgard её не трогает.");
            goto out;
        }
        remove_service(scm, svc);        /* ours, left over: start clean */
        svc = NULL;
    }

    /* Two instances: the service reads the config through one, then opens
       the same path again to look up its owner. The first instance is made
       with FILE_FLAG_FIRST_PIPE_INSTANCE: if another program already holds
       the name, this fails instead of the service reading from that program. */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                                              SDDL_REVISION_1, &sd, NULL)) {
        say(msg, cap, L"Не удалось подготовить права канала");
        goto out;
    }
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    for (i = 0; i < 2; i++) {
        pipe[i] = CreateNamedPipeW(PIPE, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED |
                                         (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                   PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   2, 65536, 0, 0, &sa);
        if (pipe[i] == INVALID_HANDLE_VALUE) {
            say(msg, cap, L"Канал для конфигурации AmneziaWG занят другой программой");
            goto out;
        }
        ov[i].hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov[i].hEvent) { say(msg, cap, L"Не удалось создать событие"); goto out; }
        if (!ConnectNamedPipe(pipe[i], &ov[i])) {
            DWORD e = GetLastError();
            if (e == ERROR_PIPE_CONNECTED) SetEvent(ov[i].hEvent);
            else if (e != ERROR_IO_PENDING) { say(msg, cap, L"Не удалось открыть канал"); goto out; }
        }
    }

    g_made_pf_dir |= pf_awg_dir(probe, MAX_PATH * 2) &&
                     GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES;

    /* As amneziawg.exe itself installs a tunnel (manager/install.go), but
       started on demand: nothing of ours runs at the next boot. */
    svc = CreateServiceW(scm, SERVICE, L"AmneziaWG Tunnel: " TUNNEL L" (Utgard)",
                         SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                         SERVICE_ERROR_NORMAL, bin, NULL, NULL, L"Nsi\0TcpIp\0", NULL, NULL);
    if (!svc) { say(msg, cap, L"Не удалось создать службу AmneziaWG"); goto out; }
    created = 1;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_SERVICE_SID_INFO, &sid);
    if (!StartServiceW(svc, 0, NULL)) { say(msg, cap, L"Не удалось запустить службу AmneziaWG"); goto out; }

    for (t = 0; t < START_WAIT_MS; t += 100) {
        DWORD s;
        /* A client lands on whichever instance is free, so the config goes to
           the first instance connected, not to instance 0; the later
           connection only looks up the owner and gets nothing. */
        for (i = 0; i < 2 && !written; i++) {
            OVERLAPPED wov;
            size_t     len = strlen(conf);

            if (pipe[i] == INVALID_HANDLE_VALUE ||
                WaitForSingleObject(ov[i].hEvent, 0) != WAIT_OBJECT_0)
                continue;

            /* Only the process the service manager started for us gets it. */
            service_state(svc, &pid, NULL, NULL);
            if (!GetNamedPipeClientProcessId(pipe[i], &cpid) || !pid || cpid != pid) {
                say(msg, cap, L"К каналу конфигурации подключилась чужая программа. Запуск отменён.");
                goto out;
            }
            ZeroMemory(&wov, sizeof wov);
            wov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            if (!wov.hEvent) goto out;
            if (!WriteFile(pipe[i], conf, (DWORD)len, NULL, &wov) && GetLastError() != ERROR_IO_PENDING) {
                CloseHandle(wov.hEvent);
                say(msg, cap, L"Не удалось передать конфигурацию службе");
                goto out;
            }
            if (WaitForSingleObject(wov.hEvent, 5000) != WAIT_OBJECT_0 ||
                !GetOverlappedResult(pipe[i], &wov, &put, FALSE) || put != len) {
                CancelIo(pipe[i]);
                CloseHandle(wov.hEvent);
                say(msg, cap, L"Не удалось передать конфигурацию службе");
                goto out;
            }
            CloseHandle(wov.hEvent);
            if (!flush_or_kill(pipe[i], pid)) {
                say(msg, cap, L"Служба AmneziaWG не прочитала конфигурацию и была остановлена");
                goto out;
            }
            /* Closing, not disconnecting: the reader's end of file. */
            CloseHandle(pipe[i]);
            pipe[i] = INVALID_HANDLE_VALUE;
            written = 1;
        }
        s = service_state(svc, NULL, &win32, &specific);
        if (s == SERVICE_RUNNING && written) { ok = add_bind_route(msg, cap); break; }
        if (s == SERVICE_STOPPED) { explain_stop(win32, specific, msg, cap); break; }
        Sleep(100);
    }
    if (!ok && t >= START_WAIT_MS)
        say(msg, cap, L"Служба AmneziaWG не запустилась за 30 секунд");

out:
    for (i = 0; i < 2; i++) {
        if (pipe[i] != INVALID_HANDLE_VALUE) { CancelIo(pipe[i]); CloseHandle(pipe[i]); }
        if (ov[i].hEvent) CloseHandle(ov[i].hEvent);
    }
    if (sd) LocalFree(sd);
    /* Only what this call created is removed on failure: a service found
       under our name that is not ours is left exactly as it was. The
       service's log stays: it is what explains the failure
       (amneziawg.exe /dumplog); it goes at the next clean switch-off. */
    if (!ok && created) { remove_service(scm, svc); svc = NULL; }
    if (svc) CloseServiceHandle(svc);
    if (scm) CloseServiceHandle(scm);
    coredir_release(&hold);       /* the process has its image mapped by now */
    return ok;
}

/* amneziawg.exe processes started from our folder with no service of ours
   around them: one that outlived its service, or a /dumplog left open.
   They hold the folder; called only once our service is gone, so the
   tunnel of a running VPN is never touched. Matched by full path, so
   another AmneziaWG on this machine is not ours to end. */
static void kill_strays(void)
{
    wchar_t         dir[MAX_PATH * 2], want[MAX_PATH * 2];
    PROCESSENTRY32W pe;
    HANDLE          snap;

    if (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2) ||
        FAILED(StringCchPrintfW(want, MAX_PATH * 2, L"%s\\%s", dir, CORE_AWG.files[0].name)))
        return;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            wchar_t path[MAX_PATH * 2];
            DWORD   len = MAX_PATH * 2;
            HANDLE  h;
            if (_wcsicmp(pe.szExeFile, CORE_AWG.files[0].name) != 0) continue;
            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                            FALSE, pe.th32ProcessID);
            if (!h) continue;
            if (QueryFullProcessImageNameW(h, 0, path, &len) && _wcsicmp(path, want) == 0) {
                TerminateProcess(h, 1);
                WaitForSingleObject(h, 5000);
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

int awgsvc_stop(wchar_t *msg, size_t cap)
{
    SC_HANDLE scm, svc;
    int       ok = 1;

    if (msg && cap) msg[0] = L'\0';
    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return say(msg, cap, L"Нет доступа к диспетчеру служб Windows");
    svc = OpenServiceW(scm, SERVICE, SERVICE_ALL_ACCESS);
    if (svc) {
        if (service_is_ours(svc) != 1) CloseServiceHandle(svc);
        else if (!remove_service(scm, svc)) ok = say(msg, cap, L"Служба AmneziaWG не удалилась вовремя");
    }
    if (ok) kill_strays();
    tidy_program_files(scm);
    CloseServiceHandle(scm);
    return ok;
}
