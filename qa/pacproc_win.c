#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "pacproc.h"
#include "pacstore.h"

static void cleanup(void)
{
    wchar_t path[1024], *slash;
    if (GetModuleFileNameW(NULL, path, 1024) && (slash = wcsrchr(path, L'\\'))) {
        wcscpy(slash + 1, L"pac.json");
        DeleteFileW(path);
    }
}

int main(void)
{
    pac_process process;
    genconf_input input;
    wchar_t error[512] = { 0 };
    STARTUPINFOW startup;
    PROCESS_INFORMATION child;
    HANDLE helper = NULL;
    wchar_t command[] = L"C:\\Windows\\System32\\ping.exe -n 30 127.0.0.1";
    pac_store store;
    ZeroMemory(&store, sizeof store);
    store.count = 1;
    store.items[0].enabled = 1;
    wcscpy(store.items[0].source, L"C:\\rules\\qa.pac");
    store.items[0].text = "function FindProxyForURL(url, host) { return 'DIRECT'; }";
    atexit(cleanup);
    if (!pacstore_save(&store)) return 7;
    ZeroMemory(&input, sizeof input);
    if (!pacproc_prepare(&process, &input, error, 512)) {
        fwprintf(stderr, L"PAC helper: %ls\n", error);
        return 1;
    }
    printf("PAC helper: bridge=%d dns=%d selector=%d restricted process=ready\n",
           input.pac_port, input.pac_dns_port, input.vpn_proxy_port);
    if (!input.pac_port || !input.pac_dns_port || !input.vpn_proxy_port ||
        !input.proxy_password || !input.client_exe) {
        pacproc_cancel(&process);
        return 2;
    }
    if (!DuplicateHandle(GetCurrentProcess(), process.process, GetCurrentProcess(),
                         &helper, PROCESS_TERMINATE | SYNCHRONIZE, FALSE, 0)) {
        pacproc_cancel(&process);
        return 3;
    }
    ZeroMemory(&startup, sizeof startup); startup.cb = sizeof startup;
    ZeroMemory(&child, sizeof child);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &child)) {
        CloseHandle(helper); pacproc_cancel(&process); return 4;
    }
    CloseHandle(child.hThread);
    if (!pacproc_attach(&process, child.hProcess, error, 512)) {
        fwprintf(stderr, L"PAC attach: %ls\n", error);
        TerminateProcess(child.hProcess, 1); CloseHandle(child.hProcess);
        CloseHandle(helper); pacproc_cancel(&process); return 5;
    }
    TerminateProcess(helper, 9);
    WaitForSingleObject(helper, 3000);
    if (WaitForSingleObject(child.hProcess, 3000) != WAIT_OBJECT_0) {
        fputs("PAC helper job: child survived helper crash\n", stderr);
        TerminateProcess(child.hProcess, 1);
        CloseHandle(child.hProcess); CloseHandle(helper); return 6;
    }
    puts("PAC helper job: helper crash stopped attached process");
    CloseHandle(child.hProcess);
    CloseHandle(helper);
    pacproc_proxy_clear();
    return 0;
}
