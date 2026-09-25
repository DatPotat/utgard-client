/*
 * Utgard client - Site lists, application list, process picker, manual app editor.
 */

#include "ui.h"

static int ed_open_existing(HWND hwnd, const app_entry *e);

static int       g_pk_all_n;

static wchar_t   g_pk_checked[64][MAX_PATH];  /* ticked, remembered by path */

static int     g_ed_enabled;                 /* kept across an edit */

/* Where the second section starts depends on how many rows the first has. */
int ed_path_top(void) { return TABS_H + S(92) + g_ed_ncount * S(36) + S(14); }

/* ---- list files ------------------------------------------------------
   All file access for the lists goes through Win32, not stdio: the product
   may well live under C:\Users\<кириллица>, where fopen with a UTF-8 path
   fails. The list module itself only ever sees text. */

int root_file(const wchar_t *tail, wchar_t *out, size_t cap)
{
    wchar_t root[MAX_PATH * 2];
    if (!singbox_root(root, MAX_PATH * 2)) return 0;
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%s", root, tail));
}

static int count_entries(const char *text)
{
    static char e[LIST_MAX][LIST_ENTRY_MAX];
    return lists_read_text(text, e, LIST_MAX);
}

void lists_refresh_counts(void)
{
    static char buf[LIST_TEXT_MAX];
    wchar_t path[MAX_PATH * 2];

    g_host_count = 0;
    g_app_count  = 0;

    if (root_file(L"list\\hosts", path, MAX_PATH * 2) &&
        file_read(path, buf, LIST_TEXT_MAX, NULL))
        g_host_count = count_entries(buf);

    /* Applications are one config file each, the way the original client kept
       them; the count is simply how many files are there. */
    {
        wchar_t          mask[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        HANDLE           h;

        if (root_file(L"list\\applications\\active\\*.json", mask, MAX_PATH * 2)) {
            h = FindFirstFileW(mask, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) g_app_count++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
}

/* Bridge between the editor, which works in UTF-16, and the list module,
   which works in UTF-8. */
/* The same bridge for zapret's list, tidied by zapret's rules. */
static int zap_tidy_bridge(const wchar_t *in, wchar_t *out, size_t cap, int *removed)
{
    static char a[LIST_TEXT_MAX], b[LIST_TEXT_MAX];

    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, a, LIST_TEXT_MAX, NULL, NULL) == 0)
        return 0;
    if (!lists_tidy_zapret(a, b, LIST_TEXT_MAX, removed)) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, b, -1, out, (int)cap) != 0;
}

static int tidy_bridge(const wchar_t *in, wchar_t *out, size_t cap, int *removed)
{
    static char a[LIST_TEXT_MAX], b[LIST_TEXT_MAX];

    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, a, LIST_TEXT_MAX, NULL, NULL) == 0)
        return 0;
    if (!lists_tidy_text(a, b, LIST_TEXT_MAX, removed)) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, b, -1, out, (int)cap) != 0;
}

/* zapret's user host list, inside the zapret folder. */
static int zapret_user_list(wchar_t *out, size_t cap)
{
    size_t len;
    if (!g_zap.valid || !g_zap.path[0]) return 0;
    len = wcslen(g_zap.path);
    return SUCCEEDED(StringCchPrintfW(out, cap, L"%s%slists\\list-general-user.txt",
                                      g_zap.path,
                                      (len && g_zap.path[len - 1] == L'\\') ? L"" : L"\\"));
}

void hosts_open(HWND hwnd, int mode)
{
    static char bytes[LIST_TEXT_MAX], crlf[LIST_TEXT_MAX];
    wchar_t    *text;
    wchar_t     path[MAX_PATH * 2];
    size_t      i, n = 0;
    int         have_path, read;

    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    if (!text) { problem(hwnd, L"Не хватило памяти"); return; }

    g_hosts_mode = mode;
    have_path = (mode == HOSTS_ZAPRET) ? zapret_user_list(path, MAX_PATH * 2)
                                       : root_file(L"list\\hosts", path, MAX_PATH * 2);
    read = have_path ? file_read(path, bytes, LIST_TEXT_MAX, NULL) : 0;
    if (read == FILE_READ_PARTIAL) {
        /* Saving from the editor would write back only what fit. */
        free(text);
        problem(hwnd, L"Список больше 256 КБ — его нельзя открыть здесь, не обрезав. "
                      L"Отредактируйте файл в текстовом редакторе.");
        return;
    }
    if (read) {
        /* The edit control breaks lines on CR+LF only. */
        for (i = 0; bytes[i] && n + 2 < LIST_TEXT_MAX; i++) {
            if (bytes[i] == '\r') continue;
            if (bytes[i] == '\n') crlf[n++] = '\r';
            crlf[n++] = bytes[i];
        }
        crlf[n] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, crlf, -1, text, LIST_TEXT_MAX);
    }

    SetWindowTextW(g_hedit, text);
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    free(text);

    g_page = PAGE_HOSTS;
    layout(hwnd);
    SetFocus(g_hedit);
}

/* Writing the file is instant; compiling it runs sing-box, so the whole
   save happens on a worker. The text is copied out of the edit first. */
static void work_hosts_save(long_job *j)
{
    wchar_t     path[MAX_PATH * 2], jpath[MAX_PATH * 2], dir[MAX_PATH * 2];
    char       *json = (char *)malloc(LIST_TEXT_MAX);
    lists_stats st;

    if (!json) { StringCchCopyW(j->msg, SB_MSG_MAX, L"Не хватило памяти"); return; }

    if (root_file(L"list", dir, MAX_PATH * 2)) CreateDirectoryW(dir, NULL);
    if (!root_file(L"list\\hosts", path, MAX_PATH * 2) || !file_write(path, j->text, strlen(j->text))) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось сохранить список");
        goto out;
    }
    if (!lists_build_text(j->text, json, LIST_TEXT_MAX, &st)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Список слишком велик");
        goto out;
    }
    if (!root_file(L"list\\general.json", jpath, MAX_PATH * 2) ||
        !file_write(jpath, json, strlen(json))) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось записать general.json");
        goto out;
    }
    if (!singbox_compile_list(j->msg, SB_MSG_MAX)) {
        /* general.json stays: it is the artefact that broke. */
        if (!j->msg[0]) StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось собрать список");
        goto out;
    }
    DeleteFileW(jpath);
    j->ok = 1;

out:
    free(json);
}

static void done_hosts_save(HWND hwnd, long_job *j)
{
    if (!j->ok) { problem(hwnd, j->msg); return; }
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    lists_refresh_counts();
    /* Leave only once it really saved, and only if the user is still here:
       they may have switched tabs while it compiled. */
    if (j->flag && g_page == PAGE_HOSTS) g_page = PAGE_UTGARD;
}

/* zapret's list is written as it stands: no compiling, zapret reads the text
   itself when the strategy starts. service.bat seeds it with a placeholder
   and a note never to leave it empty; if every entry was removed, the same
   placeholder goes back. */
void hosts_save_zapret(HWND hwnd)
{
    static char   bytes[LIST_TEXT_MAX + 64];
    static char   probe[64][LIST_ENTRY_MAX];
    wchar_t      *text;
    wchar_t       path[MAX_PATH * 2];
    size_t        len;

    if (!zapret_user_list(path, MAX_PATH * 2)) {
        problem(hwnd, L"Сначала укажите папку zapret");
        return;
    }
    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    if (!text) { problem(hwnd, L"Не хватило памяти"); return; }
    GetWindowTextW(g_hedit, text, LIST_TEXT_MAX);
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes, LIST_TEXT_MAX, NULL, NULL) == 0) {
        free(text);
        problem(hwnd, L"Список слишком велик");
        return;
    }
    free(text);

    if (lists_read_text(bytes, probe, 1) == 0) {
        len = strlen(bytes);
        if (len && bytes[len - 1] != '\n')
            StringCchCatA(bytes, sizeof bytes, "\r\n");
        StringCchCatA(bytes, sizeof bytes, "domain.example.abc\r\n");
    }

    if (!file_write(path, bytes, strlen(bytes))) {
        problem(hwnd, L"Не удалось сохранить список zapret");
        return;
    }
    SendMessageW(g_hedit, EM_SETMODIFY, FALSE, 0);
    g_page = PAGE_ZAPRET;
    layout(hwnd);
}

void hosts_save_start(HWND hwnd, int leave_after)
{
    wchar_t  *text;
    long_job *j;

    if (g_busy) return;
    text = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    j    = job_new(work_hosts_save, done_hosts_save);
    if (j) j->text = (char *)malloc(LIST_TEXT_MAX);
    if (!text || !j || !j->text) {
        free(text);
        job_free(j);
        problem(hwnd, L"Не хватило памяти");
        return;
    }

    GetWindowTextW(g_hedit, text, LIST_TEXT_MAX);
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, j->text, LIST_TEXT_MAX, NULL, NULL) == 0) {
        free(text);
        job_free(j);
        problem(hwnd, L"Список слишком велик");
        return;
    }
    free(text);

    j->flag = leave_after;
    job_start(hwnd, L"Сборка списка сайтов…", j);
}

void hosts_tidy(HWND hwnd)
{
    wchar_t *cur  = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    wchar_t *done = (wchar_t *)calloc(LIST_TEXT_MAX, sizeof(wchar_t));
    int      removed = 0;

    if (cur && done) {
        GetWindowTextW(g_hedit, cur, LIST_TEXT_MAX);
        if ((g_hosts_mode == HOSTS_ZAPRET ? zap_tidy_bridge(cur, done, LIST_TEXT_MAX, &removed)
                                          : tidy_bridge(cur, done, LIST_TEXT_MAX, &removed))
            && removed) {
            SetWindowTextW(g_hedit, done);
            SendMessageW(g_hedit, EM_SETMODIFY, TRUE, 0);
        }
    }
    free(cur);
    free(done);
    (void)hwnd;
}

/* Leaving with unsaved edits asks first; losing a pasted list silently
   would be the worst outcome of this page. */
void hosts_back(HWND hwnd)
{
    if (SendMessageW(g_hedit, EM_GETMODIFY, 0, 0)) {
        int answer = MessageBoxW(hwnd, L"Сохранить изменения в списке?",
                                 L"Utgard", MB_ICONQUESTION | MB_YESNOCANCEL);
        if (answer == IDCANCEL) return;
        if (answer == IDYES) {
            if (g_hosts_mode == HOSTS_ZAPRET) hosts_save_zapret(hwnd);
            else hosts_save_start(hwnd, 1); /* leaves the page when it succeeds */
            return;
        }
    }
    g_page = (g_hosts_mode == HOSTS_ZAPRET) ? PAGE_ZAPRET : PAGE_UTGARD;
    layout(hwnd);
}

void act_edit_hosts(HWND hwnd)
{
    hosts_open(hwnd, HOSTS_VPN);
}

/* One config file per application, as in the original client. There is no
   editor for them yet, so the button opens the folder they live in. */
void act_edit_apps(HWND hwnd)
{
    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}

void apps_reload(void)
{
    int i;

    g_appv_n = apps_scan(g_appv, APPS_MAX);
    /* Rows are rebuilt; the remembered hover would point at a different one. */
    g_app_hover_item = -1;
    g_app_hover_zone = -1;
    SendMessageW(g_alist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_appv_n; i++)
        SendMessageW(g_alist, LB_ADDSTRING, 0, (LPARAM)L"");
}

static void app_row_invalidate(HWND hwnd, int item)
{
    RECT r;
    if (item < 0 || item >= g_appv_n) return;
    if (SendMessageW(hwnd, LB_GETITEMRECT, (WPARAM)item, (LPARAM)&r) != LB_ERR)
        InvalidateRect(hwnd, &r, FALSE);
}

LRESULT CALLBACK alist_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEMOVE) {
        int     x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
        int     item = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        int     zone = -1;
        RECT    rc;

        GetClientRect(hwnd, &rc);
        if (item >= 0 && item < g_appv_n) zone = app_zone_at(x, rc.right);
        else                              item = -1;

        /* Repaint only the rows that changed, not the whole list. */
        if (item != g_app_hover_item || zone != g_app_hover_zone) {
            int old = g_app_hover_item;
            g_app_hover_item = item;
            g_app_hover_zone = zone;
            app_row_invalidate(hwnd, old);
            if (item != old) app_row_invalidate(hwnd, item);
        }

        {
            TRACKMOUSEEVENT tme;
            tme.cbSize      = sizeof tme;
            tme.dwFlags     = TME_LEAVE;
            tme.hwndTrack   = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
    } else if (msg == WM_MOUSELEAVE) {
        int old = g_app_hover_item;
        g_app_hover_item = -1;
        g_app_hover_zone = -1;
        app_row_invalidate(hwnd, old);
    } else if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT &&
               g_app_hover_item >= 0) {
        /* Everything in a row is clickable: the name opens the editor. */
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }

    if (msg == WM_LBUTTONDOWN) {
        LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0,
                                   MAKELPARAM(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        int  i = (int)LOWORD(hit);
        RECT rc;

        GetClientRect(hwnd, &rc);
        if (!HIWORD(hit) && i >= 0 && i < g_appv_n) {
            int zone = app_zone_at(GET_X_LPARAM(lp), rc.right);

            if (zone == 0) {
                app_entry e = g_appv[i];
                ed_open_existing(GetParent(hwnd), &e);
                return 0;
            }
            if (zone == 1) {
                if (!apps_set_enabled(&g_appv[i], !g_appv[i].enabled))
                    MessageBoxW(hwnd, L"Не удалось переместить файл списка",
                                L"Utgard", MB_ICONWARNING | MB_OK);
                apps_reload();
                InvalidateRect(hwnd, NULL, TRUE);
                InvalidateRect(GetParent(hwnd), NULL, TRUE);
                return 0;
            }
            if (zone == 2) {
                wchar_t q[320];
                StringCchPrintfW(q, 320,
                    L"Удалить список «%s»?\n\n"
                    L"Это сотрёт сохранённые настройки обхода для этих "
                    L"приложений. Отменить будет нельзя.", g_appv[i].name);
                if (MessageBoxW(hwnd, q, L"Удаление списка",
                                MB_ICONWARNING | MB_YESNO) == IDYES) {
                    if (!apps_delete(&g_appv[i]))
                        MessageBoxW(hwnd, L"Не удалось удалить файл",
                                    L"Utgard", MB_ICONWARNING | MB_OK);
                    apps_reload();
                    InvalidateRect(hwnd, NULL, TRUE);
                    InvalidateRect(GetParent(hwnd), NULL, TRUE);
                }
                return 0;
            }
        }
    }
    return CallWindowProcW(g_alist_prev, hwnd, msg, wp, lp);
}

int pk_is_checked(const wchar_t *path)
{
    int i;
    if (!path[0]) return 0;
    for (i = 0; i < g_pk_checked_n; i++)
        if (_wcsicmp(g_pk_checked[i], path) == 0) return 1;
    return 0;
}

void pk_toggle(const wchar_t *path)
{
    int i;

    if (!path[0]) return;                       /* unreadable: cannot be picked */
    for (i = 0; i < g_pk_checked_n; i++) {
        if (_wcsicmp(g_pk_checked[i], path) == 0) {
            memmove(&g_pk_checked[i], &g_pk_checked[i + 1],
                    (size_t)(g_pk_checked_n - i - 1) * sizeof g_pk_checked[0]);
            g_pk_checked_n--;
            return;
        }
    }
    if (g_pk_checked_n < 64)
        StringCchCopyW(g_pk_checked[g_pk_checked_n++], MAX_PATH, path);
}

/* Re-read the processes and re-apply the filter. The ticks survive because
   they are keyed by path, and the scroll position is put back so a refresh
   every two seconds does not yank the list under the user. */
void pk_refresh(void)
{
    wchar_t query[128];
    int     i, top;

    GetWindowTextW(g_pk_search, query, 128);
    top = (int)SendMessageW(g_pk_list, LB_GETTOPINDEX, 0, 0);

    g_pk_all_n  = pick_snapshot(g_pk_all, PICK_MAX);
    g_pk_view_n = 0;
    for (i = 0; i < g_pk_all_n; i++)
        if (pick_match(&g_pk_all[i], query)) g_pk_view[g_pk_view_n++] = i;

    SendMessageW(g_pk_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_pk_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_pk_view_n; i++)
        SendMessageW(g_pk_list, LB_ADDSTRING, 0, (LPARAM)L"");
    if (top > 0 && top < g_pk_view_n) SendMessageW(g_pk_list, LB_SETTOPINDEX, (WPARAM)top, 0);
    list_hover_resync(g_pk_list);
    SendMessageW(g_pk_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_pk_list, NULL, TRUE);

    EnableWindow(g_pk_save, g_pk_checked_n > 0);
}

void pk_open(HWND hwnd)
{
    g_pk_checked_n = 0;
    SetWindowTextW(g_pk_search, L"");
    g_page = PAGE_PICK;
    layout(hwnd);
    pk_refresh();
    SetTimer(hwnd, TIMER_PICK, 2000, NULL);
    SetFocus(g_pk_search);
}

void pk_close(HWND hwnd)
{
    KillTimer(hwnd, TIMER_PICK);
    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}

void pk_save(HWND hwnd)
{
    static wchar_t names[64][MAX_PATH], paths[64][MAX_PATH];
    wchar_t        list_name[APPS_NAME_MAX * 2], err[256];
    int            i, n = 0;

    if (g_pk_checked_n == 0) return;

    for (;;) {
        if (!ask_string(hwnd, L"Сохранить приложение",
                        L"Имя списка: латиница, цифры, дефис и подчёркивание. "
                        L"Под этим именем набор появится в менеджере.",
                        NULL, list_name, APPS_NAME_MAX * 2))
            return;
        if (!apps_name_ok(list_name)) {
            problem(hwnd, L"Имя — только латиница, цифры, дефис и подчёркивание.");
            continue;
        }
        if (apps_exists(list_name)) {
            problem(hwnd, L"Список с таким именем уже есть. Выберите другое имя.");
            continue;
        }
        break;
    }

    /* Both the executable name and its full path, as in the reference config:
       the path pins it on this machine, the name keeps it working elsewhere. */
    for (i = 0; i < g_pk_checked_n && n < 64; i++) {
        const wchar_t *slash = wcsrchr(g_pk_checked[i], L'\\');
        StringCchCopyW(names[n], MAX_PATH, slash ? slash + 1 : g_pk_checked[i]);
        StringCchCopyW(paths[n], MAX_PATH, g_pk_checked[i]);
        n++;
    }

    if (!apps_write(list_name, names, n, paths, n, 0, err, 256)) {
        problem(hwnd, err[0] ? err : L"Не удалось сохранить список");
        return;
    }
    pk_close(hwnd);
}

/* ---- manual editor ---------------------------------------------------- */

static void ed_clear(void)
{
    int i;
    for (i = 0; i < ED_ROWS; i++) {
        SetWindowTextW(g_ed_name[i], L"");
        SetWindowTextW(g_ed_path[i], L"");
    }
    g_ed_ncount = 1;
    g_ed_pcount = 1;
}

void ed_open_new(HWND hwnd)
{
    ed_clear();
    g_ed_orig[0]  = L'\0';
    g_ed_enabled  = 0;
    g_page = PAGE_EDIT;
    layout(hwnd);
    SetFocus(g_ed_name[0]);
}

static int ed_open_existing(HWND hwnd, const app_entry *e)
{
    static wchar_t names[ED_ROWS][MAX_PATH], paths[ED_ROWS][MAX_PATH];
    int nn = 0, np = 0, i;

    if (!apps_read(e, names, &nn, paths, &np, ED_ROWS)) {
        problem(hwnd, L"Этот список содержит правила, которые редактор не умеет "
                      L"показывать, — например, регулярные выражения, исключения "
                      L"или имя и путь в одном правиле. Сохранение из формы стёрло "
                      L"бы их, поэтому его можно только включать, выключать и удалять.");
        return 0;
    }

    ed_clear();
    for (i = 0; i < nn; i++) SetWindowTextW(g_ed_name[i], names[i]);
    for (i = 0; i < np; i++) SetWindowTextW(g_ed_path[i], paths[i]);
    g_ed_ncount = nn > 0 ? nn : 1;
    g_ed_pcount = np > 0 ? np : 1;

    StringCchCopyW(g_ed_orig, APPS_NAME_MAX, e->name);
    g_ed_enabled = e->enabled;
    g_page = PAGE_EDIT;
    layout(hwnd);
    return 1;
}

/* Rows below the removed one move up; its text is gone, the others stay. */
void ed_remove(HWND *rows, int *count, int at)
{
    wchar_t buf[MAX_PATH];
    int     i;

    if (at <= 0 || at >= *count) return;
    for (i = at; i < *count - 1; i++) {
        GetWindowTextW(rows[i + 1], buf, MAX_PATH);
        SetWindowTextW(rows[i], buf);
    }
    SetWindowTextW(rows[*count - 1], L"");
    (*count)--;
}

void ed_add(HWND hwnd, HWND *rows, int *count)
{
    if (*count >= ED_ROWS) {
        problem(hwnd, L"В одном разделе помещается не больше шести строк. "
                      L"Если нужно больше — сохраните второй список.");
        return;
    }
    SetWindowTextW(rows[*count], L"");
    (*count)++;
    layout(hwnd);
    SetFocus(rows[*count - 1]);
}

void ed_save(HWND hwnd)
{
    static wchar_t names[ED_ROWS][MAX_PATH], paths[ED_ROWS][MAX_PATH];
    wchar_t        list_name[APPS_NAME_MAX * 2], err[256];
    int            nn = 0, np = 0, i;

    for (i = 0; i < g_ed_ncount; i++) {
        GetWindowTextW(g_ed_name[i], names[nn], MAX_PATH);
        if (names[nn][0]) nn++;
    }
    for (i = 0; i < g_ed_pcount; i++) {
        GetWindowTextW(g_ed_path[i], paths[np], MAX_PATH);
        if (paths[np][0]) np++;
    }
    if (nn == 0 && np == 0) {
        problem(hwnd, L"Заполните хотя бы одно поле");
        return;
    }

    for (;;) {
        if (!ask_string(hwnd, L"Сохранить список",
                        L"Имя списка: латиница, цифры, дефис и подчёркивание.",
                        g_ed_orig[0] ? g_ed_orig : NULL,
                        list_name, APPS_NAME_MAX * 2))
            return;
        if (!apps_name_ok(list_name)) {
            problem(hwnd, L"Имя — только латиница, цифры, дефис и подчёркивание.");
            continue;
        }
        /* Keeping its own name is fine; taking another list's name is not. */
        if (_wcsicmp(list_name, g_ed_orig) != 0 && apps_exists(list_name)) {
            problem(hwnd, L"Список с таким именем уже есть. Выберите другое имя.");
            continue;
        }
        break;
    }

    if (!apps_write(list_name, names, nn, paths, np, g_ed_enabled, err, 256)) {
        problem(hwnd, err[0] ? err : L"Не удалось сохранить список");
        return;
    }

    /* Renamed: the new file is written, so the old one can go. Written first
       and deleted second, a failure in between leaves a copy, not nothing. */
    if (g_ed_orig[0] && _wcsicmp(list_name, g_ed_orig) != 0) {
        app_entry old;
        StringCchCopyW(old.name, APPS_NAME_MAX, g_ed_orig);
        old.enabled = g_ed_enabled;
        apps_delete(&old);
    }

    g_page = PAGE_APPS;
    apps_reload();
    layout(hwnd);
}
