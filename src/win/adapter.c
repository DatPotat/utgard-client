/* winsock2.h must come before anything that pulls in windows.h. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <strsafe.h>

#include "adapter.h"
#include "tunnames.h"

/* The interface table keeps interfaces whose device is gone: a Wintun
   adapter left by a reboot with the VPN on stays there, hidden from the
   network connections, its name still taken and its status NotPresent
   ("down because some component is not present"). Looking the name up
   alone (ConvertInterfaceAliasToLuid) finds that ghost and would wait for
   it forever. Only an interface that is actually there counts. */
int adapter_luid(const wchar_t *alias, unsigned long long *luid_value)
{
    MIB_IF_TABLE2 *t = NULL;
    ULONG          i;
    int            found = 0;

    if (GetIfTable2(&t) != NO_ERROR || !t) return 0;
    for (i = 0; i < t->NumEntries && !found; i++) {
        const MIB_IF_ROW2 *r = &t->Table[i];
        if (r->OperStatus != IfOperStatusNotPresent && _wcsicmp(r->Alias, alias) == 0) {
            if (luid_value) *luid_value = r->InterfaceLuid.Value;
            found = 1;
        }
    }
    FreeMibTable(t);
    return found;
}

int adapter_present(const wchar_t *alias)
{
    return adapter_luid(alias, NULL);
}

int adapters_wait_gone(unsigned long ms, wchar_t *msg, size_t cap)
{
    static const wchar_t *const names[] = { UTGARD_SB_TUN_W, UTGARD_AWG_TUN_W };
    ULONGLONG start = GetTickCount64();
    size_t    i;

    for (;;) {
        const wchar_t *left = NULL;
        for (i = 0; i < sizeof names / sizeof names[0] && !left; i++)
            if (adapter_present(names[i])) left = names[i];
        if (!left) return 1;
        if (GetTickCount64() - start >= ms) {
            if (msg && cap)
                StringCchPrintfW(msg, cap, L"Сетевой адаптер %s предыдущего подключения не удалён "
                                           L"системой за %lu с. Подождите и попробуйте снова; если "
                                           L"не поможет — перезагрузите компьютер.",
                                 left, (unsigned long)(ms / 1000));
            return 0;
        }
        Sleep(200);
    }
}
