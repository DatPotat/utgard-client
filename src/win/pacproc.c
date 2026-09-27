#include "pacproc.h"
#include "pacproto.h"
#include <sddl.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

static SRWLOCK proxy_lock = SRWLOCK_INIT;
static unsigned short current_port;
static char current_password[65];

static int say(wchar_t *err, size_t cap, const wchar_t *text)
{
    if (err && cap) StringCchCopyW(err, cap, text);
    return 0;
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

static int helper_paths(wchar_t path[2048], wchar_t dir[2048], char utf8[2048])
{
    wchar_t *slash;
    if (!GetModuleFileNameW(NULL, dir, 2048)) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';
    if (FAILED(StringCchPrintfW(path, 2048, L"%sutgard-pac-helper.exe", dir)) ||
        GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ||
        !WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, 2048, NULL, NULL)) return 0;
    return 1;
}

static HANDLE restricted_token(void)
{
    HANDLE source = NULL, token = NULL;
    PSID low = NULL;
    TOKEN_MANDATORY_LABEL label;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY |
                          TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &source)) return NULL;
    if (!CreateRestrictedToken(source, DISABLE_MAX_PRIVILEGE,
                               0, NULL, 0, NULL, 0, NULL, &token)) goto done;
    if (!ConvertStringSidToSidW(L"S-1-16-4096", &low)) goto fail;
    ZeroMemory(&label, sizeof label);
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = low;
    if (!SetTokenInformation(token, TokenIntegrityLevel, &label,
            sizeof label + GetLengthSid(low))) goto fail;
done:
    LocalFree(low);
    CloseHandle(source);
    return token;
fail:
    CloseHandle(token); token = NULL;
    goto done;
}

void pacproc_cancel(pac_process *p)
{
    if (!p) return;
    if (p->command) { CloseHandle(p->command); p->command = NULL; }
    if (p->job) { CloseHandle(p->job); p->job = NULL; }
    if (p->process) {
        if (WaitForSingleObject(p->process, 3000) == WAIT_TIMEOUT) {
            TerminateProcess(p->process, 1);
            WaitForSingleObject(p->process, 3000);
        }
        CloseHandle(p->process); p->process = NULL;
    }
    SecureZeroMemory(p->password, sizeof p->password);
    p->prepared = 0;
}

int pacproc_prepare(pac_process *p, genconf_input *in, wchar_t *err, size_t cap)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit;
    HANDLE ready_rd = NULL, ready_wr = NULL, command_rd = NULL, command_wr = NULL;
    HANDLE token = NULL;
    HANDLE inherit[3];
    SIZE_T bytes = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    wchar_t path[2048], dir[2048], cmd[4096];
    pacproc_ready ready;
    BOOL started = FALSE;

    if (!p || !in) return say(err, cap, L"Внутренняя ошибка запуска PAC");
    ZeroMemory(p, sizeof *p);
    ZeroMemory(&ready, sizeof ready);
    if (!helper_paths(path, dir, p->helper_path))
        return say(err, cap, L"Не найден utgard-pac-helper.exe рядом с Utgard");
    if (!CreatePipe(&ready_rd, &ready_wr, &sa, 0) ||
        !CreatePipe(&command_rd, &command_wr, &sa, 0))
        goto fail;
    SetHandleInformation(ready_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(command_wr, HANDLE_FLAG_INHERIT, 0);

    p->job = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&limit, sizeof limit);
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!p->job || !SetInformationJobObject(p->job, JobObjectExtendedLimitInformation,
                                             &limit, sizeof limit)) goto fail;
    SetHandleInformation(p->job, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &bytes)) goto fail;
    inherit[0] = ready_wr; inherit[1] = command_rd; inherit[2] = p->job;
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherit, sizeof inherit, NULL, NULL)) goto fail;
    token = restricted_token();
    if (!token) goto fail;
    if (FAILED(StringCchPrintfW(cmd, 4096, L"\"%s\" %llu %llu %llu", path,
            (unsigned long long)(UINT_PTR)ready_wr,
            (unsigned long long)(UINT_PTR)command_rd,
            (unsigned long long)(UINT_PTR)p->job))) goto fail;
    ZeroMemory(&si, sizeof si); si.StartupInfo.cb = sizeof si; si.lpAttributeList = attrs;
    ZeroMemory(&pi, sizeof pi);
    started = CreateProcessAsUserW(token, path, cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL, dir, &si.StartupInfo, &pi);
    if (!started) {
        DWORD code = GetLastError();
        if (err && cap) StringCchPrintfW(err, cap,
            L"Не удалось запустить ограниченный PAC-процесс (ошибка Windows %lu)",
            (unsigned long)code);
        goto fail;
    }
    p->process = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(ready_wr); ready_wr = NULL;
    CloseHandle(command_rd); command_rd = NULL;
    {
        int received = read_all(ready_rd, &ready, sizeof ready);
        if (!received && WaitForSingleObject(p->process, 1000) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(p->process, &exit_code);
            if (err && cap) StringCchPrintfW(err, cap,
                L"Изолированный PAC-процесс завершился до запуска (код 0x%08lX)",
                (unsigned long)exit_code);
        }
        if (!received || ready.magic != PACPROC_MAGIC ||
            !ready.ok || ready.reserved != 3) {
            if (ready.magic == PACPROC_MAGIC && ready.error[0]) say(err, cap, ready.error);
            else if (ready.magic == PACPROC_MAGIC && ready.ok)
                say(err, cap, L"PAC-процесс запущен без требуемых ограничений безопасности");
            else if (!err || !cap || !err[0])
                say(err, cap, L"Изолированный PAC-процесс не смог запуститься");
            goto fail;
        }
    }
    p->command = command_wr; command_wr = NULL;
    p->proxy_port = ready.proxy_port;
    memcpy(p->password, ready.password, sizeof p->password);
    in->pac_port = ready.pac_port;
    in->pac_dns_port = ready.dns_port;
    in->vpn_proxy_port = ready.proxy_port;
    in->proxy_password = p->password;
    in->client_exe = p->helper_path;
    p->prepared = 1;
    CloseHandle(ready_rd); CloseHandle(token);
    DeleteProcThreadAttributeList(attrs); free(attrs);
    return 1;
fail:
    if (ready_rd) CloseHandle(ready_rd);
    if (ready_wr) CloseHandle(ready_wr);
    if (command_rd) CloseHandle(command_rd);
    if (command_wr) CloseHandle(command_wr);
    if (token) CloseHandle(token);
    if (attrs) { DeleteProcThreadAttributeList(attrs); free(attrs); }
    pacproc_cancel(p);
    if (err && cap && !err[0]) StringCchPrintfW(err, cap,
        L"Не удалось запустить изолированный PAC-процесс (ошибка Windows %lu)",
        (unsigned long)GetLastError());
    return 0;
}

int pacproc_attach(pac_process *p, HANDLE singbox, wchar_t *err, size_t cap)
{
    pacproc_command command;
    HANDLE remote = NULL;
    BOOL in_job = FALSE;
    if (!p || !p->prepared || !p->process || !p->job || !singbox)
        return say(err, cap, L"PAC-процесс не подготовлен");
    if (!IsProcessInJob(singbox, p->job, &in_job) ||
        (!in_job && !AssignProcessToJobObject(p->job, singbox)))
        return say(err, cap, L"Не удалось связать PAC-процесс с sing-box");
    if (!DuplicateHandle(GetCurrentProcess(), singbox, p->process, &remote,
                         SYNCHRONIZE, FALSE, 0))
        return say(err, cap, L"Не удалось передать PAC-процессу контроль sing-box");
    command.magic = PACPROC_MAGIC;
    command.process_handle = (UINT_PTR)remote;
    if (!write_all(p->command, &command, sizeof command))
        return say(err, cap, L"PAC-процесс завершился во время подключения");

    AcquireSRWLockExclusive(&proxy_lock);
    current_port = p->proxy_port;
    memcpy(current_password, p->password, sizeof current_password);
    ReleaseSRWLockExclusive(&proxy_lock);
    CloseHandle(p->command); p->command = NULL;
    CloseHandle(p->job); p->job = NULL;
    CloseHandle(p->process); p->process = NULL;
    SecureZeroMemory(p->password, sizeof p->password);
    p->prepared = 0;
    return 1;
}

int pacproc_proxy(unsigned short *port, char password[65])
{
    int ok;
    AcquireSRWLockShared(&proxy_lock);
    ok = current_port != 0 && current_password[0] != '\0';
    if (ok) { *port = current_port; memcpy(password, current_password, 65); }
    ReleaseSRWLockShared(&proxy_lock);
    return ok;
}

void pacproc_proxy_clear(void)
{
    AcquireSRWLockExclusive(&proxy_lock);
    current_port = 0;
    SecureZeroMemory(current_password, sizeof current_password);
    ReleaseSRWLockExclusive(&proxy_lock);
}
