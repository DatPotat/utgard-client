#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>
#include "pac.h"
#include "pacbridge.h"
#include "pacproto.h"
#include "pacstatus.h"

static int write_all(HANDLE h, const void *data, DWORD length)
{
    const BYTE *p = (const BYTE *)data;
    while (length) { DWORD put = 0; if (!WriteFile(h, p, length, &put, NULL) || !put) return 0; p += put; length -= put; }
    return 1;
}
static int read_all(HANDLE h, void *data, DWORD length)
{
    BYTE *p = (BYTE *)data;
    while (length) { DWORD got = 0; if (!ReadFile(h, p, length, &got, NULL) || !got) return 0; p += got; length -= got; }
    return 1;
}

static unsigned security_state(void)
{
    HANDLE token = NULL; BYTE buffer[16384]; DWORD got = 0; unsigned state = 0; DWORD i;
    TOKEN_MANDATORY_LABEL *label; TOKEN_PRIVILEGES *privileges; TOKEN_GROUPS *groups;
    LUID traverse = { 0, 0 }; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY; PSID admins = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    if (GetTokenInformation(token, TokenPrivileges, buffer, sizeof buffer, &got)) {
        int enabled = 0; privileges = (TOKEN_PRIVILEGES *)buffer;
        int have_traverse = LookupPrivilegeValueW(NULL, SE_CHANGE_NOTIFY_NAME, &traverse) != 0;
        for (i = 0; i < privileges->PrivilegeCount; i++)
            if ((privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) &&
                (!have_traverse || privileges->Privileges[i].Luid.LowPart != traverse.LowPart ||
                 privileges->Privileges[i].Luid.HighPart != traverse.HighPart)) enabled = 1;
        if (!enabled) state |= 1;
    }
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof buffer, &got)) {
        DWORD count; label = (TOKEN_MANDATORY_LABEL *)buffer; count = *GetSidSubAuthorityCount(label->Label.Sid);
        if (count && *GetSidSubAuthority(label->Label.Sid, count - 1) <= SECURITY_MANDATORY_LOW_RID) state |= 2;
    }
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &admins) &&
        GetTokenInformation(token, TokenGroups, buffer, sizeof buffer, &got)) {
        int found = 0;
        groups = (TOKEN_GROUPS *)buffer;
        for (i = 0; i < groups->GroupCount; i++)
            if (EqualSid(groups->Groups[i].Sid, admins)) {
                found = 1;
                if (groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) state |= 4;
                break;
            }
        if (!found) state |= 4;
    }
    if (admins) FreeSid(admins); CloseHandle(token); return state;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    unsigned long long r = 0, c = 0; HANDLE ready_pipe, command_pipe, singbox = NULL, lifetime_job = NULL, status_file = NULL;
    pacproc_ready ready; pacproc_init init; pacproc_command command; pac_script *scripts[PAC_ITEMS_MAX] = { 0 };
    genconf_input in; int i, count = 0, result = 1;
    (void)instance; (void)previous; (void)show;
    ZeroMemory(&ready, sizeof ready); ready.magic = PACPROC_MAGIC; ready.security_state = (unsigned short)security_state();
    if (swscanf(command_line, L"%llu %llu", &r, &c) != 2) return 2;
    ready_pipe = (HANDLE)(UINT_PTR)r; command_pipe = (HANDLE)(UINT_PTR)c; ZeroMemory(&in, sizeof in);
    if (!read_all(command_pipe, &init, sizeof init) || init.magic != PACPROC_MAGIC ||
        !init.status_handle || !init.script_count || init.script_count > PAC_ITEMS_MAX) {
        StringCchCopyW(ready.error, 512, L"PAC-процесс получил неверные параметры"); goto report;
    }
    status_file = (HANDLE)init.status_handle;
    for (i = 0; i < (int)init.script_count; i++) {
        DWORD length = 0; char *text;
        if (!read_all(command_pipe, &length, sizeof length) || !length || length > PAC_MAX) {
            StringCchCopyW(ready.error, 512, L"PAC-процесс получил PAC недопустимого размера"); goto report;
        }
        text = (char *)malloc((size_t)length + 1);
        if (!text || !read_all(command_pipe, text, length)) { free(text); StringCchCopyW(ready.error, 512, L"Не удалось передать PAC в изолированный процесс"); goto report; }
        text[length] = 0; scripts[count] = pac_open(text, length, ready.error, 512); SecureZeroMemory(text, length); free(text);
        if (!scripts[count]) goto report; count++;
    }
    if (!pacbridge_prepare(&in, 1, 1, ready.error, 512)) goto report;
    pacbridge_activate(scripts, count); ZeroMemory(scripts, sizeof scripts); pacstatus_writer(status_file, (unsigned)count);
    ready.pac_port = (unsigned short)in.pac_port; ready.dns_port = (unsigned short)in.pac_dns_port;
    ready.dns_vpn_port = (unsigned short)in.pac_dns_vpn_port; ready.dns_sys_port = (unsigned short)in.pac_dns_sys_port;
    ready.proxy_port = (unsigned short)in.vpn_proxy_port; memcpy(ready.password, in.proxy_password, sizeof ready.password); ready.ok = 1;
report:
    write_all(ready_pipe, &ready, sizeof ready); CloseHandle(ready_pipe); SecureZeroMemory(ready.password, sizeof ready.password);
    if (!ready.ok) goto done;
    if (!read_all(command_pipe, &command, sizeof command) || command.magic != PACPROC_MAGIC ||
        !command.process_handle || !command.job_handle) goto done;
    singbox = (HANDLE)command.process_handle; lifetime_job = (HANDLE)command.job_handle;
    CloseHandle(command_pipe); command_pipe = NULL;
    if (WaitForSingleObject(singbox, INFINITE) == WAIT_OBJECT_0) result = 0;
done:
    pacbridge_disconnect(); pacbridge_activate(NULL, 0);
    for (i = 0; i < PAC_ITEMS_MAX; i++) pac_close(scripts[i]);
    if (singbox) CloseHandle(singbox); if (lifetime_job) CloseHandle(lifetime_job);
    if (command_pipe) CloseHandle(command_pipe); if (status_file) CloseHandle(status_file); return result;
}
