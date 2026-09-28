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
    modal_box(hwnd, text, L"Utgard", MB_ICONWARNING | MB_OK);
}

/* Every message box of the window goes through here, so a question that
   comes in while one is open - the update check finishing during the first
   run, say - waits for it to close instead of stacking on top. */
int g_modal;

int modal_box(HWND hwnd, const wchar_t *text, const wchar_t *title, UINT flags)
{
    int r;

    /* The client's own window, in the theme, instead of the system box:
       the answers keep the MessageBox return values for the callers. */
    g_modal++;
    switch (flags & 0x0F) {
    case MB_YESNO:
        r = ask_message(hwnd, title, text, L"Да", L"Нет", NULL);
        r = r == 1 ? IDYES : IDNO;
        break;
    case MB_YESNOCANCEL:
        r = ask_message(hwnd, title, text, L"Да", L"Нет", L"Отмена");
        r = r == 1 ? IDYES : r == 2 ? IDNO : IDCANCEL;
        break;
    case MB_OKCANCEL:
        r = ask_message(hwnd, title, text, L"OK", L"Отмена", NULL);
        r = r == 1 ? IDOK : IDCANCEL;
        break;
    default:
        ask_message(hwnd, title, text, L"OK", NULL, NULL);
        r = IDOK;
    }
    g_modal--;
    if (!g_modal && g_upd_pending && IsWindowVisible(hwnd)) {
        g_upd_pending = 0;
        upd_prompt(hwnd);
    }
    return r;
}

void job_stage(long_job *j, const wchar_t *text)
{
    if (j && j->hwnd) PostMessageW(j->hwnd, WM_APP_JOB_STAGE, 0, (LPARAM)text);
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
    g_switch_note = 0;
    g_busy_text = label;
    layout(hwnd);
    return 1;
}

void after_action(HWND hwnd)
{
    status_refresh();
    layout(hwnd);
}

/* Russian noun after a number: 1 сайт, 3 сайта, 5 сайтов, 11 сайтов,
   21 сайт. Kept in one place so a string catalogue can replace it. */
const wchar_t *plural_ru(long n, const wchar_t *one, const wchar_t *few, const wchar_t *many)
{
    long a = n < 0 ? -n : n, d = a % 10, h = a % 100;
    if (d == 1 && h != 11) return one;
    if (d >= 2 && d <= 4 && (h < 12 || h > 14)) return few;
    return many;
}

/* One file from the Explorer open dialog: a title, one filter (plus "all
   files"), the chosen path out. 1 when a file was chosen. */
int pick_file(HWND hwnd, const wchar_t *title, const wchar_t *filter_name,
              const wchar_t *filter_spec, wchar_t *path, size_t cap)
{
    IFileOpenDialog *dialog = NULL;
    IShellItem *item = NULL;
    PWSTR selected = NULL;
    COMDLG_FILTERSPEC types[2];
    int ok = 0;
    types[0].pszName = filter_name; types[0].pszSpec = filter_spec;
    types[1].pszName = L"Все файлы"; types[1].pszSpec = L"*.*";
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dialog))) return 0;
    IFileOpenDialog_SetOptions(dialog, FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    IFileOpenDialog_SetFileTypes(dialog, 2, types);
    IFileOpenDialog_SetTitle(dialog, title);
    g_modal++;
    if (SUCCEEDED(IFileOpenDialog_Show(dialog, hwnd)) &&
        SUCCEEDED(IFileOpenDialog_GetResult(dialog, &item)) &&
        SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &selected)))
        ok = SUCCEEDED(StringCchCopyW(path, cap, selected));
    g_modal--;
    CoTaskMemFree(selected);
    if (item) IShellItem_Release(item);
    IFileOpenDialog_Release(dialog);
    return ok;
}
