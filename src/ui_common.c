/*
 * Utgard client - Background jobs, the status line, error boxes, string helpers.
 */

#include "ui.h"

/* UTF-8 to UTF-16 that always leaves a usable string: on overflow the
   conversion fails and would otherwise leave the buffer untouched. */
void to_wide(const char *src, wchar_t *dst, int cap)
{
    if (cap <= 0) return;
    dst[0] = L'\0';
    if (!src || !src[0]) return;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap) == 0) {
        dst[0] = L'\0';
    }
}

/* Re-read live state; returns 1 when something changed. */
int status_refresh(void)
{
    zapret_status now;

    zapret_status_read(&now);
    if (now.mode == g_status.mode && wcscmp(now.strategy, g_status.strategy) == 0)
        return 0;

    g_status = now;
    return 1;
}

/* Failures used to go to a line in the footer; that line is gone, so they
   have to be shown outright rather than disappear. */
void problem(HWND hwnd, const wchar_t *text)
{
    MessageBoxW(hwnd, text, L"Utgard", MB_ICONWARNING | MB_OK);
}

long_job *job_new(job_work work, job_done done)
{
    long_job *j = (long_job *)calloc(1, sizeof *j);
    if (j) { j->work = work; j->done = done; }
    return j;
}

void job_free(long_job *j)
{
    if (!j) return;
    if (j->extra) {
        SecureZeroMemory(j->extra, j->extra_size);
        free(j->extra);
    }
    free(j->text);
    free(j);
}

static DWORD WINAPI job_thread(LPVOID param)
{
    long_job *j = (long_job *)param;
    j->work(j);
    PostMessageW(j->hwnd, WM_APP_JOB_DONE, 0, (LPARAM)j);
    return 0;
}

/* Takes ownership of the job in every case. */
int job_start(HWND hwnd, const wchar_t *label, long_job *j)
{
    HANDLE th;

    if (!j) { problem(hwnd, L"Не хватило памяти"); return 0; }
    if (g_busy) { job_free(j); return 0; }

    j->hwnd = hwnd;
    th = CreateThread(NULL, 0, job_thread, j, 0, NULL);
    if (!th) {
        job_free(j);
        problem(hwnd, L"Не удалось запустить фоновую задачу");
        return 0;
    }
    CloseHandle(th);
    g_busy      = 1;
    g_busy_text = label;
    layout(hwnd);
    return 1;
}

void after_action(HWND hwnd)
{
    status_refresh();
    layout(hwnd);
}
