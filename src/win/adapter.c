/* winsock2.h must come before anything that pulls in windows.h. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <strsafe.h>

#include "adapter.h"

/* The interface table keeps interfaces whose device is gone: a Wintun
   adapter left by a reboot with the VPN on stays there, hidden from the
   network connections, its name still taken and its status NotPresent
   ("down because some component is not present"). Looking the name up
   alone (ConvertInterfaceAliasToLuid) finds that ghost. Only an interface
   that is actually there counts. A leftover from a stop within the same
   session can keep the name with another status (seen live); should two
   rows share the name, the one that is up wins. */
int adapter_luid(const wchar_t *alias, unsigned long long *luid_value)
{
    MIB_IF_TABLE2 *t = NULL;
    ULONG          i;
    int            found = 0;

    if (GetIfTable2(&t) != NO_ERROR || !t) return 0;
    for (i = 0; i < t->NumEntries; i++) {
        const MIB_IF_ROW2 *r = &t->Table[i];
        if (r->OperStatus == IfOperStatusNotPresent || _wcsicmp(r->Alias, alias) != 0) continue;
        if (!found || r->OperStatus == IfOperStatusUp) {
            if (luid_value) *luid_value = r->InterfaceLuid.Value;
            found = 1;
        }
        if (r->OperStatus == IfOperStatusUp) break;
    }
    FreeMibTable(t);
    return found;
}

/* NetSetupSvc installs network drivers, Wintun's adapters included. It is
   Manual (Trigger Start) and stops itself when idle; seen live stopped and
   not coming back, and an adapter created then showed up with an error and
   vanished. Only started here - its startup type is never changed. */
#define NETSETUP_WAIT_MS 15000

int netsetup_ensure(wchar_t *msg, size_t cap)
{
    SC_HANDLE                 scm, s;
    SERVICE_STATUS_PROCESS    st;
    DWORD                     n, e;
    ULONGLONG                 start = GetTickCount64();
    int                       ok    = 1;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return 1;                          /* cannot check: let creation speak */
    s = OpenServiceW(scm, L"NetSetupSvc", SERVICE_QUERY_STATUS | SERVICE_START);
    if (!s) { CloseServiceHandle(scm); return 1; }

    for (;;) {
        if (!QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, (LPBYTE)&st, sizeof st, &n)) break;
        if (st.dwCurrentState == SERVICE_RUNNING) break;
        if (st.dwCurrentState == SERVICE_STOPPED && !StartServiceW(s, 0, NULL)) {
            e = GetLastError();
            if (e != ERROR_SERVICE_ALREADY_RUNNING) {
                if (msg && cap && e == ERROR_SERVICE_DISABLED)
                    StringCchCopyW(msg, cap, L"Служба Windows NetSetupSvc (установка сетевых драйверов) "
                                             L"отключена — без неё сетевой адаптер VPN не создаётся. "
                                             L"Верните ей тип запуска «Вручную».");
                else if (msg && cap)
                    StringCchPrintfW(msg, cap, L"Не удалось запустить службу Windows NetSetupSvc "
                                               L"(установка сетевых драйверов), ошибка %lu — без неё "
                                               L"сетевой адаптер VPN не создаётся.", (unsigned long)e);
                ok = 0;
                break;
            }
        }
        if (GetTickCount64() - start >= NETSETUP_WAIT_MS) {
            if (msg && cap)
                StringCchPrintfW(msg, cap, L"Служба Windows NetSetupSvc не запустилась за %u с — "
                                           L"без неё сетевой адаптер VPN не создаётся.",
                                 (unsigned)(NETSETUP_WAIT_MS / 1000));
            ok = 0;
            break;
        }
        Sleep(100);                              /* START_PENDING / STOP_PENDING */
    }
    CloseServiceHandle(s);
    CloseServiceHandle(scm);
    return ok;
}
