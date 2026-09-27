#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>
#include "pacbridge.h"
#include "pacproto.h"
#include "pacstore.h"

static int write_all(HANDLE h, const void *data, DWORD length)
{
    const BYTE *p = (const BYTE *)data;
    while (length) {
        DWORD put = 0;
        if (!WriteFile(h, p, length, &put, NULL) || !put) return 0;
        p += put; length -= put;
    }
    return 1;
}

static int read_all(HANDLE h, void *data, DWORD length)
{
    BYTE *p = (BYTE *)data;
    while (length) {
        DWORD got = 0;
        if (!ReadFile(h, p, length, &got, NULL) || !got) return 0;
        p += got; length -= got;
    }
    return 1;
}

static unsigned security_state(void)
{
    HANDLE token = NULL;
    BYTE buffer[2048];
    DWORD got = 0;
    unsigned state = 0;
    TOKEN_MANDATORY_LABEL *label;
    TOKEN_PRIVILEGES *privileges;
    LUID traverse = { 0, 0 };
    DWORD i;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    if (GetTokenInformation(token, TokenPrivileges, buffer, sizeof buffer, &got)) {
        int enabled = 0;
        privileges = (TOKEN_PRIVILEGES *)buffer;
        int have_traverse = LookupPrivilegeValueW(NULL, SE_CHANGE_NOTIFY_NAME,
                                                  &traverse) != 0;
        for (i = 0; i < privileges->PrivilegeCount; i++)
            if ((privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) &&
                (!have_traverse ||
                 privileges->Privileges[i].Luid.LowPart != traverse.LowPart ||
                 privileges->Privileges[i].Luid.HighPart != traverse.HighPart))
                enabled = 1;
        if (!enabled) state |= 1;
    }
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof buffer, &got)) {
        DWORD count;
        label = (TOKEN_MANDATORY_LABEL *)buffer;
        count = *GetSidSubAuthorityCount(label->Label.Sid);
        if (count && *GetSidSubAuthority(label->Label.Sid, count - 1) <= SECURITY_MANDATORY_LOW_RID)
            state |= 2;
    }
    CloseHandle(token);
    return state;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    unsigned long long r = 0, c = 0, j = 0;
    HANDLE ready_pipe, command_pipe, lifetime_job, singbox = NULL;
    pacproc_ready ready;
    pacproc_command command;
    pac_store store;
    pac_script *scripts[PAC_ITEMS_MAX] = { 0 };
    genconf_input in;
    int i, count = 0, result = 1;
    (void)instance; (void)previous; (void)show;
    ZeroMemory(&ready, sizeof ready); ready.magic = PACPROC_MAGIC;
    ready.reserved = (unsigned short)security_state();
    if (swscanf(command_line, L"%llu %llu %llu", &r, &c, &j) != 3) return 2;
    ready_pipe = (HANDLE)(UINT_PTR)r;
    command_pipe = (HANDLE)(UINT_PTR)c;
    lifetime_job = (HANDLE)(UINT_PTR)j;
    ZeroMemory(&store, sizeof store);
    ZeroMemory(&in, sizeof in);
    if (!pacstore_load(&store)) {
        StringCchCopyW(ready.error, 512, L"Не удалось прочитать или проверить pac.json");
        goto report;
    }
    for (i = 0; i < store.count; i++) if (store.items[i].enabled) {
        scripts[count] = pac_open(store.items[i].text,
            store.items[i].text ? strlen(store.items[i].text) : 0,
            ready.error, 512);
        if (!scripts[count]) goto report;
        count++;
    }
    if (!count) {
        StringCchCopyW(ready.error, 512, L"Нет включённых PAC");
        goto report;
    }
    if (!pacbridge_prepare(&in, 1, 1, ready.error, 512)) goto report;
    pacbridge_activate(scripts, count);
    ZeroMemory(scripts, sizeof scripts);
    ready.pac_port = (unsigned short)in.pac_port;
    ready.dns_port = (unsigned short)in.pac_dns_port;
    ready.proxy_port = (unsigned short)in.vpn_proxy_port;
    memcpy(ready.password, in.proxy_password, sizeof ready.password);
    ready.ok = 1;
report:
    write_all(ready_pipe, &ready, sizeof ready);
    CloseHandle(ready_pipe);
    pacstore_free(&store);
    SecureZeroMemory(ready.password, sizeof ready.password);
    if (!ready.ok) goto done;
    if (!read_all(command_pipe, &command, sizeof command) ||
        command.magic != PACPROC_MAGIC || !command.process_handle) goto done;
    singbox = (HANDLE)command.process_handle;
    CloseHandle(command_pipe); command_pipe = NULL;
    if (WaitForSingleObject(singbox, INFINITE) == WAIT_OBJECT_0) result = 0;
done:
    pacbridge_disconnect();
    pacbridge_activate(NULL, 0);
    for (i = 0; i < PAC_ITEMS_MAX; i++) pac_close(scripts[i]);
    if (singbox) CloseHandle(singbox);
    if (command_pipe) CloseHandle(command_pipe);
    CloseHandle(lifetime_job);
    return result;
}
