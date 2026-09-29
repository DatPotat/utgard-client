#include "pacstatus.h"
#include <strsafe.h>
#include <string.h>

static SRWLOCK status_lock = SRWLOCK_INIT;
static HANDLE status_file = INVALID_HANDLE_VALUE;
static pac_status_record status;

static void publish(void)
{
    LARGE_INTEGER zero;
    DWORD written;
    if (status_file == INVALID_HANDLE_VALUE) return;
    status.sequence_begin++;
    status.sequence_end = status.sequence_begin;
    zero.QuadPart = 0;
    SetFilePointerEx(status_file, zero, NULL, FILE_BEGIN);
    /* No flush: a reader in another process sees the cached write, and a
       disk flush here would serialize relay threads on every event. */
    WriteFile(status_file, &status, sizeof status, &written, NULL);
}

void pacstatus_writer(HANDLE file, unsigned active_count)
{
    AcquireSRWLockExclusive(&status_lock);
    status_file = file;
    ZeroMemory(&status, sizeof status);
    status.magic = PAC_STATUS_MAGIC;
    status.version = PAC_STATUS_VERSION;
    status.active_count = active_count;
    publish();
    ReleaseSRWLockExclusive(&status_lock);
}

void pacstatus_active(unsigned active_count)
{
    AcquireSRWLockExclusive(&status_lock);
    status.active_count = active_count;
    publish();
    ReleaseSRWLockExclusive(&status_lock);
}

void pacstatus_evaluation_error(DWORD error)
{
    FILETIME now;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&now);
    value.LowPart = now.dwLowDateTime; value.HighPart = now.dwHighDateTime;
    AcquireSRWLockExclusive(&status_lock);
    status.evaluation_errors++;
    status.last_error = error;
    status.last_error_time = value.QuadPart;
    publish();
    ReleaseSRWLockExclusive(&status_lock);
}

#define COUNTER(name, field) \
    void name(void) { AcquireSRWLockExclusive(&status_lock); status.field++; publish(); ReleaseSRWLockExclusive(&status_lock); }
COUNTER(pacstatus_worker_cap, worker_cap_hits)
COUNTER(pacstatus_dns_cap, dns_cap_hits)
COUNTER(pacstatus_udp_evict, udp_evictions)
COUNTER(pacstatus_udp_verified, udp_owner_verified)
COUNTER(pacstatus_udp_no_table, udp_owner_no_table)
COUNTER(pacstatus_udp_not_listed, udp_owner_not_listed)
COUNTER(pacstatus_udp_foreign, udp_owner_foreign)

static int status_path(wchar_t path[1024])
{
    wchar_t *slash;
    if (!GetModuleFileNameW(NULL, path, 1024)) return 0;
    slash = wcsrchr(path, L'\\');
    if (!slash) return 0;
    return SUCCEEDED(StringCchCopyW(slash + 1, 1024 - (slash + 1 - path),
                                    L"logs\\pac-status.bin"));
}

int pacstatus_read(pac_status_record *record)
{
    wchar_t path[1024];
    HANDLE file;
    DWORD got;
    pac_status_record a, b;
    LARGE_INTEGER zero;
    if (!record || !status_path(path)) return 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    zero.QuadPart = 0;
    if (!ReadFile(file, &a, sizeof a, &got, NULL) || got != sizeof a ||
        !SetFilePointerEx(file, zero, NULL, FILE_BEGIN) ||
        !ReadFile(file, &b, sizeof b, &got, NULL) || got != sizeof b ||
        !pacrecord_consistent(&a, &b)) {
        CloseHandle(file); return 0;
    }
    CloseHandle(file); *record = b; return 1;
}
