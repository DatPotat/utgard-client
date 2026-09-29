#include "pacproc.h"
#include "pacproto.h"
#include <bcrypt.h>
#include <sddl.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UTGARD_PAC_HELPER_SHA
#define UTGARD_PAC_HELPER_SHA ""
#endif

/* Write end of the serving helper's command pipe, kept after attach for
   pacproc_reload(). Guarded by its own lock: a reload may block up to 15 s. */
static SRWLOCK reload_lock = SRWLOCK_INIT;
static HANDLE reload_pipe;
static volatile LONG reload_drop;   /* VPN went off during a reload: close after it */

static int say(wchar_t *err, size_t cap, const wchar_t *text)
{ if (err && cap) StringCchCopyW(err, cap, text); return 0; }

static int read_all(HANDLE h, void *data, DWORD length)
{
    BYTE *p = (BYTE *)data;
    while (length) { DWORD got = 0; if (!ReadFile(h, p, length, &got, NULL) || !got) return 0; p += got; length -= got; }
    return 1;
}
static int write_all(HANDLE h, const void *data, DWORD length)
{
    const BYTE *p = (const BYTE *)data;
    while (length) { DWORD put = 0; if (!WriteFile(h, p, length, &put, NULL) || !put) return 0; p += put; length -= put; }
    return 1;
}

/* Sending the scripts runs on its own thread, so a helper that stops
   reading cannot hang the caller. The thread's context lives on the heap
   with its own copies of the scripts and a reference count shared with the
   caller: if the caller gives up (timeout), it hands the pipe over instead
   of closing it under a write still in progress, and whoever leaves last
   closes the pipe and frees the copies. No stack frame, store or handle of
   the caller is touched after it returns. */
typedef struct {
    HANDLE        pipe;
    pacproc_init  init;
    int           count;
    char         *text[PAC_ITEMS_MAX];
    DWORD         length[PAC_ITEMS_MAX];
    volatile LONG ok, refs, owns_pipe;
} send_context;

static void send_release(send_context *c)
{
    int i;
    if (InterlockedDecrement(&c->refs) != 0) return;
    if (InterlockedCompareExchange(&c->owns_pipe, 0, 0) && c->pipe) CloseHandle(c->pipe);
    for (i = 0; i < c->count; i++) if (c->text[i]) { SecureZeroMemory(c->text[i], c->length[i]); free(c->text[i]); }
    SecureZeroMemory(c, sizeof *c);
    free(c);
}

/* A context with copies of the enabled scripts, refs 2 (caller and sender);
   NULL when out of memory or a script is empty or too long. */
static send_context *send_new(HANDLE pipe, DWORD magic, const pac_store *store)
{
    send_context *c = (send_context *)calloc(1, sizeof *c);
    int i;
    if (!c) return NULL;
    c->pipe = pipe; c->init.magic = magic; c->refs = 1;
    for (i = 0; i < store->count; i++) if (store->items[i].enabled) {
        size_t n = store->items[i].text ? strlen(store->items[i].text) : 0;
        if (!n || n > PAC_MAX || n > MAXDWORD || c->count >= PAC_ITEMS_MAX ||
            !(c->text[c->count] = (char *)malloc(n))) { send_release(c); return NULL; }
        memcpy(c->text[c->count], store->items[i].text, n);
        c->length[c->count++] = (DWORD)n;
    }
    c->init.script_count = (DWORD)c->count;
    c->refs = 2;
    return c;
}

static DWORD WINAPI send_scripts(void *opaque)
{
    send_context *c = (send_context *)opaque; int i, ok = 1;
    if (!write_all(c->pipe, &c->init, sizeof c->init)) ok = 0;
    for (i = 0; ok && i < c->count; i++)
        if (!write_all(c->pipe, &c->length[i], sizeof c->length[i]) ||
            !write_all(c->pipe, c->text[i], c->length[i])) ok = 0;
    if (ok) InterlockedExchange(&c->ok, 1);
    send_release(c);
    return 0;
}

/* The caller's side once the sender was started. On timeout the write is
   cancelled and given two seconds; if it is still stuck, the sender keeps
   the pipe (it closes it on its way out) and *pipe_given is set: the caller
   must neither use nor close it. Returns 1 when every script was written. */
static int send_finish(send_context *c, HANDLE thread, DWORD waited, int *pipe_given)
{
    int ok;
    *pipe_given = 0;
    if (waited != WAIT_OBJECT_0) {
        CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0) {
            InterlockedExchange(&c->owns_pipe, 1);
            *pipe_given = 1;
        }
    }
    ok = InterlockedCompareExchange(&c->ok, 0, 0) != 0;
    CloseHandle(thread);
    send_release(c);
    return ok;
}

/* The helper lives in bin\ beside utgard.exe. */
static int helper_paths(wchar_t path[2048], wchar_t dir[2048], char utf8[2048])
{
    wchar_t *slash;
    if (!GetModuleFileNameW(NULL, dir, 2048)) return 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return 0;
    slash[1] = L'\0';
    if (FAILED(StringCchPrintfW(path, 2048, L"%sbin\\utgard-pac-helper.exe", dir)) ||
        !WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, 2048, NULL, NULL)) return 0;
    return 1;
}

/* Hashes an already opened file from its start. */
static int file_sha256(HANDLE file, char hex[65])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD object_size = 0, got = 0, read = 0;
    PUCHAR object = NULL;
    BYTE digest[32], buffer[65536];
    int ok = 0, i;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) ||
        BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size, sizeof object_size, &got, 0)) goto done;
    object = (PUCHAR)malloc(object_size);
    if (!object || BCryptCreateHash(alg, &hash, object, object_size, NULL, 0, 0)) goto done;
    do {
        if (!ReadFile(file, buffer, sizeof buffer, &read, NULL)) goto done;
        if (read && BCryptHashData(hash, buffer, read, 0)) goto done;
    } while (read);
    if (BCryptFinishHash(hash, digest, sizeof digest, 0)) goto done;
    for (i = 0; i < 32; i++)
        if (FAILED(StringCchPrintfA(hex + i * 2, 3, "%02x", digest[i]))) goto done;
    ok = 1;
done:
    SecureZeroMemory(digest, sizeof digest);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    free(object);
    return ok;
}

static HANDLE restricted_token(void)
{
    HANDLE source = NULL, token = NULL; PSID low = NULL, admins = NULL;
    TOKEN_MANDATORY_LABEL label; SID_AND_ATTRIBUTES disabled;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    BYTE group_buffer[16384]; DWORD group_bytes = 0, disable_count = 0, i;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY |
                          TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &source)) return NULL;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) goto done;
    disabled.Sid = admins; disabled.Attributes = 0;
    if (GetTokenInformation(source, TokenGroups, group_buffer, sizeof group_buffer, &group_bytes)) {
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)group_buffer;
        for (i = 0; i < groups->GroupCount; i++)
            if (EqualSid(groups->Groups[i].Sid, admins) &&
                !(groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)) {
                disable_count = 1; break;
            }
    }
    if (!CreateRestrictedToken(source, DISABLE_MAX_PRIVILEGE, disable_count,
                               disable_count ? &disabled : NULL,
                               0, NULL, 0, NULL, &token)) {
        /* Test runners and managed launchers may already supply a restricted
           primary token that Windows refuses to restrict a second time. The
           helper still performs the same privilege/SID self-check. */
        if (!IsTokenRestricted(source) ||
            !DuplicateTokenEx(source, MAXIMUM_ALLOWED, NULL, SecurityImpersonation,
                              TokenPrimary, &token)) goto done;
    }
    if (!ConvertStringSidToSidW(L"S-1-16-4096", &low)) goto fail;
    ZeroMemory(&label, sizeof label); label.Label.Attributes = SE_GROUP_INTEGRITY; label.Label.Sid = low;
    if (!SetTokenInformation(token, TokenIntegrityLevel, &label, sizeof label + GetLengthSid(low))) goto fail;
done:
    LocalFree(low); if (admins) FreeSid(admins); if (source) CloseHandle(source); return token;
fail:
    CloseHandle(token); token = NULL; goto done;
}

/* logs\pac-status.bin, delete-on-close: once the helper holds the only
   handle, the file lives exactly as long as the helper. A previous helper
   may still be exiting and holding the old file: retry for up to 3 s. */
static HANDLE create_status_file(void)
{
    wchar_t dir[2048], logs[2048], path[2048], *slash;
    ULONGLONG deadline = GetTickCount64() + 3000;
    HANDLE file;
    if (!GetModuleFileNameW(NULL, dir, 2048) || !(slash = wcsrchr(dir, L'\\'))) return INVALID_HANDLE_VALUE;
    slash[1] = L'\0';
    if (FAILED(StringCchPrintfW(logs, 2048, L"%slogs", dir)) ||
        (!CreateDirectoryW(logs, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) ||
        FAILED(StringCchPrintfW(path, 2048, L"%slogs\\pac-status.bin", dir))) return INVALID_HANDLE_VALUE;
    for (;;) {
        DWORD error;
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (file != INVALID_HANDLE_VALUE) return file;
        error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) ||
            GetTickCount64() >= deadline) return INVALID_HANDLE_VALUE;
        Sleep(50);
    }
}

void pacproc_cancel(pac_process *p)
{
    if (!p) return;
    if (p->command) { CloseHandle(p->command); p->command = NULL; }
    if (p->job) { CloseHandle(p->job); p->job = NULL; }
    if (p->process) {
        if (WaitForSingleObject(p->process, 3000) == WAIT_TIMEOUT) { TerminateProcess(p->process, 1); WaitForSingleObject(p->process, 3000); }
        CloseHandle(p->process); p->process = NULL;
    }
    SecureZeroMemory(p->password, sizeof p->password); p->prepared = 0;
}

int pacproc_prepare(pac_process *p, genconf_input *in, const pac_store *store, wchar_t *err, size_t cap)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; STARTUPINFOEXW si; PROCESS_INFORMATION pi;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit; HANDLE ready_rd = NULL, ready_wr = NULL, command_rd = NULL, command_wr = NULL;
    HANDLE token = NULL; HANDLE inherit[2];
    SIZE_T bytes = 0; LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL; wchar_t path[2048], dir[2048], cmd[4096];
    pacproc_ready ready; pacproc_init init; BOOL started = FALSE; char actual_sha[65]; int i, count = 0;
    send_context *sender = NULL; HANDLE send_thread = NULL, image = INVALID_HANDLE_VALUE; DWORD create_error;
    if (!p || !in || !store) return say(err, cap, L"Внутренняя ошибка запуска PAC");
    ZeroMemory(p, sizeof *p); ZeroMemory(&ready, sizeof ready);
    if (!helper_paths(path, dir, p->helper_path)) return say(err, cap, L"Слишком длинный путь к папке Utgard");
    /* Held open without write or delete sharing until the process exists:
       the file checked is the file started. */
    image = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (image == INVALID_HANDLE_VALUE) return say(err, cap, L"Не найден bin\\utgard-pac-helper.exe рядом с Utgard");
    if (!UTGARD_PAC_HELPER_SHA[0] || !file_sha256(image, actual_sha) || _stricmp(actual_sha, UTGARD_PAC_HELPER_SHA)) {
        CloseHandle(image);
        return say(err, cap, L"Контрольная сумма bin\\utgard-pac-helper.exe не совпадает со сборкой Utgard");
    }
    for (i = 0; i < store->count; i++) if (store->items[i].enabled) {
        size_t length = store->items[i].text ? strlen(store->items[i].text) : 0;
        if (!length || length > PAC_MAX) { CloseHandle(image); return say(err, cap, L"PAC-лист пуст или превышает допустимый размер"); }
        count++;
    }
    if (!count || count > PAC_ITEMS_MAX) { CloseHandle(image); return say(err, cap, L"Нет корректных включённых PAC-листов"); }
    if (!CreatePipe(&ready_rd, &ready_wr, &sa, 0) || !CreatePipe(&command_rd, &command_wr, &sa, 0)) goto fail;
    SetHandleInformation(ready_rd, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(command_wr, HANDLE_FLAG_INHERIT, 0);
    p->job = CreateJobObjectW(NULL, NULL); ZeroMemory(&limit, sizeof limit);
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!p->job || !SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limit, sizeof limit)) goto fail;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes); attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &bytes)) goto fail;
    inherit[0] = ready_wr; inherit[1] = command_rd;
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, NULL, NULL)) goto fail;
    token = restricted_token(); if (!token) goto fail;
    if (FAILED(StringCchPrintfW(cmd, 4096, L"\"%ls\" %llu %llu", path,
            (unsigned long long)(UINT_PTR)ready_wr, (unsigned long long)(UINT_PTR)command_rd))) goto fail;
    ZeroMemory(&si, sizeof si); si.StartupInfo.cb = sizeof si; si.lpAttributeList = attrs; ZeroMemory(&pi, sizeof pi);
    started = CreateProcessAsUserW(token, path, cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL, dir, &si.StartupInfo, &pi);
    create_error = started ? 0 : GetLastError();
    CloseHandle(image);
    image = INVALID_HANDLE_VALUE;
    if (!started) { if (err && cap) StringCchPrintfW(err, cap, L"Не удалось запустить ограниченный PAC-процесс (ошибка Windows %lu)", (unsigned long)create_error); goto fail; }
    p->process = pi.hProcess; CloseHandle(pi.hThread); CloseHandle(ready_wr); ready_wr = NULL; CloseHandle(command_rd); command_rd = NULL;
    init.magic = PACPROC_MAGIC; init.script_count = (DWORD)count;
    {
        HANDLE waits[2]; DWORD waited; ULONGLONG deadline = GetTickCount64() + 15000; int received = 0;
        int given = 0, sent;
        (void)init;
        sender = send_new(command_wr, PACPROC_MAGIC, store);
        if (!sender) goto fail;
        send_thread = CreateThread(NULL, 0, send_scripts, sender, 0, NULL);
        if (!send_thread) { sender->refs = 1; send_release(sender); sender = NULL; goto fail; }
        waits[0] = send_thread; waits[1] = p->process;
        waited = WaitForMultipleObjects(2, waits, FALSE, 15000);
        sent = send_finish(sender, send_thread, waited, &given);
        sender = NULL; send_thread = NULL;
        if (given) command_wr = NULL;            /* the sender closes it now */
        if (waited != WAIT_OBJECT_0 || !sent) {
            if (command_wr) { CloseHandle(command_wr); command_wr = NULL; }
            TerminateProcess(p->process, 1);
            say(err, cap, waited == WAIT_TIMEOUT ? L"Передача PAC не завершилась за 15 секунд" :
                L"PAC-процесс завершился во время передачи настроек");
            goto fail;
        }
        while (GetTickCount64() < deadline) {
            DWORD available = 0;
            if (WaitForSingleObject(p->process, 0) == WAIT_OBJECT_0) break;
            if (!PeekNamedPipe(ready_rd, NULL, 0, NULL, &available, NULL)) break;
            if (available >= sizeof ready) { received = read_all(ready_rd, &ready, sizeof ready); break; }
            Sleep(20);
        }
        if (!received) { TerminateProcess(p->process, 1); say(err, cap, L"PAC-процесс не ответил за 15 секунд или завершился"); goto fail; }
    }
    if (ready.magic != PACPROC_MAGIC || !ready.ok || ready.security_state != 7) {
        if (ready.magic == PACPROC_MAGIC && ready.error[0]) say(err, cap, ready.error);
        else if (ready.magic == PACPROC_MAGIC && ready.ok) say(err, cap, L"PAC-процесс запущен без требуемых ограничений безопасности");
        else say(err, cap, L"Изолированный PAC-процесс не смог запуститься");
        goto fail;
    }
    p->command = command_wr; command_wr = NULL; p->proxy_port = ready.proxy_port;
    memcpy(p->password, ready.password, sizeof p->password);
    in->pac_port = ready.pac_port; in->pac_dns_port = ready.dns_port;
    in->pac_dns_vpn_port = ready.dns_vpn_port; in->pac_dns_sys_port = ready.dns_sys_port;
    in->vpn_proxy_port = ready.proxy_port; in->proxy_password = p->password; in->client_exe = p->helper_path; p->prepared = 1;
    CloseHandle(ready_rd); CloseHandle(token); DeleteProcThreadAttributeList(attrs); free(attrs); return 1;
fail:
    if (send_thread) {
        /* Not reached with a live sender today; kept safe regardless. */
        int given = 0;
        send_finish(sender, send_thread, WAIT_TIMEOUT, &given);
        if (given) command_wr = NULL;
    }
    if (image != INVALID_HANDLE_VALUE) CloseHandle(image);
    if (ready_rd) CloseHandle(ready_rd);
    if (ready_wr) CloseHandle(ready_wr);
    if (command_rd) CloseHandle(command_rd);
    if (command_wr) CloseHandle(command_wr);
    if (token) CloseHandle(token);
    if (attrs) { DeleteProcThreadAttributeList(attrs); free(attrs); }
    pacproc_cancel(p);
    if (err && cap && !err[0]) StringCchPrintfW(err, cap, L"Не удалось запустить изолированный PAC-процесс (ошибка Windows %lu)", (unsigned long)GetLastError());
    return 0;
}

int pacproc_attach(pac_process *p, HANDLE singbox, wchar_t *err, size_t cap)
{
    pacproc_command command; HANDLE remote_process = NULL, remote_job = NULL, remote_status = NULL, status; BOOL in_job = FALSE;
    if (!p || !p->prepared || !p->process || !p->job || !singbox) return say(err, cap, L"PAC-процесс не подготовлен");
    if (!IsProcessInJob(singbox, p->job, &in_job) || (!in_job && !AssignProcessToJobObject(p->job, singbox)))
        return say(err, cap, L"Не удалось связать PAC-процесс с sing-box");
    /* Only the helper that is actually serving sing-box gets the status
       file; a standby helper for a rollback never writes it. */
    status = create_status_file();
    if (status == INVALID_HANDLE_VALUE) return say(err, cap, L"Не удалось создать logs\\pac-status.bin");
    if (!DuplicateHandle(GetCurrentProcess(), singbox, p->process, &remote_process, SYNCHRONIZE, FALSE, 0) ||
        !DuplicateHandle(GetCurrentProcess(), p->job, p->process, &remote_job, SYNCHRONIZE, FALSE, 0) ||
        !DuplicateHandle(GetCurrentProcess(), status, p->process, &remote_status, GENERIC_WRITE, FALSE, 0)) {
        CloseHandle(status);
        return say(err, cap, L"Не удалось передать PAC-процессу контроль sing-box");
    }
    /* From here the helper's copy is the only one. */
    CloseHandle(status);
    command.magic = PACPROC_MAGIC; command.process_handle = (UINT_PTR)remote_process;
    command.job_handle = (UINT_PTR)remote_job; command.status_handle = (UINT_PTR)remote_status;
    if (!write_all(p->command, &command, sizeof command)) return say(err, cap, L"PAC-процесс завершился во время подключения");
    AcquireSRWLockExclusive(&reload_lock);
    if (reload_pipe) CloseHandle(reload_pipe);
    InterlockedExchange(&reload_drop, 0);
    reload_pipe = p->command; p->command = NULL;
    ReleaseSRWLockExclusive(&reload_lock);
    CloseHandle(p->job); p->job = NULL; CloseHandle(p->process); p->process = NULL;
    SecureZeroMemory(p->password, sizeof p->password); p->prepared = 0; return 1;
}

void pacproc_vpn_off(void)
{
    /* Called on the UI thread: never wait behind a reload that may take 15 s. */
    if (TryAcquireSRWLockExclusive(&reload_lock)) {
        if (reload_pipe) { CloseHandle(reload_pipe); reload_pipe = NULL; }
        ReleaseSRWLockExclusive(&reload_lock);
    } else {
        InterlockedExchange(&reload_drop, 1);
    }
}

int pacproc_reload(const pac_store *store)
{
    send_context *sender;
    HANDLE thread;
    int i, count = 0, ok = 0, given = 0;
    if (!store) return 0;
    for (i = 0; i < store->count; i++) if (store->items[i].enabled) count++;
    if (!count || count > PAC_ITEMS_MAX) return 0;
    AcquireSRWLockExclusive(&reload_lock);
    if (reload_pipe && (sender = send_new(reload_pipe, PACPROC_RELOAD_MAGIC, store)) != NULL) {
        thread = CreateThread(NULL, 0, send_scripts, sender, 0, NULL);
        if (thread) {
            ok = send_finish(sender, thread, WaitForSingleObject(thread, 15000), &given);
        } else {
            sender->refs = 1; send_release(sender);
        }
        /* A broken or stuck pipe is not used again: the caller reconnects.
           A pipe handed to a stuck sender is closed by it, not here. */
        if (given) reload_pipe = NULL;
        else if (!ok || InterlockedExchange(&reload_drop, 0)) { CloseHandle(reload_pipe); reload_pipe = NULL; }
    }
    ReleaseSRWLockExclusive(&reload_lock);
    return ok;
}
