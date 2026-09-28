#include "ui.h"
#include "pacstore.h"
#include "pacproc.h"

enum { PAC_OP_ADD = 1, PAC_OP_REFRESH, PAC_OP_TOGGLE, PAC_OP_DELETE, PAC_OP_AUTO };

typedef struct {
    int op, index;
    int changed, failed;      /* PAC_OP_AUTO: scripts replaced / not refreshed */
    int vpn_on;               /* VPN state when the task was started */
    int reloaded;             /* the running helper took the new set: no reconnect */
    wchar_t source[2048];
} pac_task;

/* After a failed automatic refresh, not before (as for the subscription). */
static long long pac_retry;

static int is_url(const wchar_t *s)
{ return !_wcsnicmp(s, L"https://", 8) || !_wcsnicmp(s, L"http://", 7); }

int pac_selected(void)
{ return ListView_GetNextItem(g_pac_list, -1, LVNI_SELECTED); }

void pac_reload(void)
{
    pac_store store;
    int i;
    g_pac_count = 0;
    ListView_DeleteAllItems(g_pac_list);
    if (!pacstore_load(&store)) return;
    for (i = 0; i < store.count; i++) {
        LVITEMW row;
        wchar_t state[64];
        ZeroMemory(&row, sizeof row);
        row.mask = LVIF_TEXT;
        row.iItem = i;
        row.pszText = store.items[i].enabled ? L"Да" : L"Нет";
        ListView_InsertItem(g_pac_list, &row);
        ListView_SetItemText(g_pac_list, i, 1, store.items[i].source);
        ListView_SetItemText(g_pac_list, i, 2, is_url(store.items[i].source) ? L"URL" : L"Файл");
        StringCchPrintfW(state, 64, L"Готов · %lu КБ",
                         (unsigned long)((strlen(store.items[i].text) + 1023) / 1024));
        ListView_SetItemText(g_pac_list, i, 3, state);
        if (store.items[i].enabled) g_pac_count++;
    }
    pacstore_free(&store);
}

static int pick_pac(HWND hwnd, wchar_t *path, size_t cap)
{
    IFileOpenDialog *dialog = NULL;
    IShellItem *item = NULL;
    PWSTR selected = NULL;
    COMDLG_FILTERSPEC types[] = { { L"PAC (*.pac; *.js)", L"*.pac;*.js" }, { L"Все файлы", L"*.*" } };
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dialog))) return 0;
    IFileOpenDialog_SetOptions(dialog, FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    IFileOpenDialog_SetFileTypes(dialog, 2, types);
    IFileOpenDialog_SetTitle(dialog, L"Добавить PAC");
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

static int read_source(const pac_task *task, char **text, size_t *length,
                       wchar_t *err, size_t cap)
{
    *text = NULL; *length = 0;
    if (is_url(task->source)) return net_fetch_pac(task->source, text, length, err, cap);
    *text = (char *)malloc(PAC_MAX + 1);
    if (!*text || file_read(task->source, *text, PAC_MAX + 1, length) != 1) {
        free(*text); *text = NULL;
        StringCchCopyW(err, cap, L"Не удалось прочитать PAC целиком (не более 4 МиБ)");
        return 0;
    }
    return 1;
}

/* Refreshes every enabled PAC added by URL, the way "Обновить" does, and
   keeps the stored copy of any that fails. A script via VPN is skipped (and
   retried later) while the VPN with PAC is not up. Saves only when a script
   actually changed. */
static void work_pac_auto(long_job *j, pac_task *task)
{
    pac_store store;
    int i;
    if (!pacstore_load(&store)) { task->failed = 1; return; }
    for (i = 0; i < store.count; i++) {
        pac_item *item = &store.items[i];
        pac_task one;
        pac_script *check;
        char *text = NULL;
        size_t length = 0;
        DWORD error = 0;
        wchar_t err[256] = L"";
        if (!item->enabled || !is_url(item->source)) continue;
        ZeroMemory(&one, sizeof one);
        StringCchCopyW(one.source, 2048, item->source);
        job_stage(j, L"Обновление PAC…");
        if (!read_source(&one, &text, &length, err, 256)) { task->failed++; continue; }
        check = pac_open(text, length, err, 256);
        if (!check || pac_query(check, L"https://example.com/", &error) < 0) {
            pac_close(check);
            free(text);
            task->failed++;
            continue;
        }
        pac_close(check);
        if (strlen(item->text) == length && !memcmp(item->text, text, length)) { free(text); continue; }
        free(item->text);
        item->text = text;
        task->changed++;
    }
    if (task->changed && !pacstore_save(&store)) { task->changed = 0; task->failed++; }
    if (task->changed && task->vpn_on) task->reloaded = pacproc_reload(&store);
    pacstore_free(&store);
}

static void work_pac(long_job *j)
{
    pac_task *task = (pac_task *)j->extra;
    pac_store store;
    pac_item replacement;
    pac_script *check = NULL;
    DWORD error = 0;
    size_t length = 0;
    int i;
    ZeroMemory(&replacement, sizeof replacement);
    if (task->op == PAC_OP_AUTO) {
        work_pac_auto(j, task);
        j->ok = 1;               /* the outcome is in task: never a message box */
        return;
    }
    if (!pacstore_load(&store)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось прочитать pac.json");
        return;
    }
    if (task->op != PAC_OP_ADD && (task->index < 0 || task->index >= store.count)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Выбранный PAC больше не существует");
        goto done;
    }
    if (task->op == PAC_OP_ADD) {
        /* The same source twice only doubles every evaluation. */
        for (i = 0; i < store.count; i++)
            if (!_wcsicmp(store.items[i].source, task->source)) {
                StringCchCopyW(j->msg, SB_MSG_MAX, L"Этот PAC уже добавлен");
                goto done;
            }
    }
    if (task->op == PAC_OP_ADD || task->op == PAC_OP_REFRESH) {
        if (task->op == PAC_OP_REFRESH) {
            StringCchCopyW(task->source, 2048, store.items[task->index].source);
        }
        job_stage(j, is_url(task->source) ? L"Загрузка PAC…" : L"Чтение PAC…");
        if (!read_source(task, &replacement.text, &length, j->msg, SB_MSG_MAX)) goto done;
        job_stage(j, L"Проверка PAC…");
        check = pac_open(replacement.text, length, j->msg, SB_MSG_MAX);
        if (!check || pac_query(check, L"https://example.com/", &error) < 0) {
            if (!j->msg[0]) StringCchPrintfW(j->msg, SB_MSG_MAX,
                L"PAC не прошёл FindProxyForURL (ошибка Windows %lu). Сохранённая копия не изменена.",
                (unsigned long)error);
            goto done;
        }
        replacement.enabled = 1;
        StringCchCopyW(replacement.source, 2048, task->source);
        if (task->op == PAC_OP_ADD) {
            if (store.count >= PAC_ITEMS_MAX) {
                StringCchPrintfW(j->msg, SB_MSG_MAX, L"Можно сохранить не более %d PAC", PAC_ITEMS_MAX);
                goto done;
            }
            store.items[store.count++] = replacement;
        } else {
            replacement.enabled = store.items[task->index].enabled;
            free(store.items[task->index].text);
            store.items[task->index] = replacement;
        }
        replacement.text = NULL;
    } else if (task->op == PAC_OP_TOGGLE) {
        store.items[task->index].enabled = !store.items[task->index].enabled;
    } else if (task->op == PAC_OP_DELETE) {
        free(store.items[task->index].text);
        for (i = task->index; i + 1 < store.count; i++) store.items[i] = store.items[i + 1];
        ZeroMemory(&store.items[--store.count], sizeof store.items[0]);
    }
    if (!pacstore_save(&store)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось сохранить настройки PAC");
        goto done;
    }
    /* A running helper takes a new non-empty set in place. From or to no
       enabled PAC the sing-box config itself changes: that needs a reconnect. */
    if (task->vpn_on) task->reloaded = pacproc_reload(&store);
    j->ok = 1;
done:
    pac_close(check);
    free(replacement.text);
    pacstore_free(&store);
}

static void done_pac(HWND hwnd, long_job *j)
{
    pac_task *task = (pac_task *)j->extra;
    if (task && task->op == PAC_OP_AUTO) {
        /* Like the subscription: success restarts the interval, a failure
           retries in 15 minutes; the stored copies keep working meanwhile. */
        if (task->failed) pac_retry = _time64(NULL) + 15 * 60;
        else {
            settings_load(&g_set);
            g_set.pac_last = _time64(NULL);
            settings_save(&g_set);
            pac_retry = 0;
        }
        if (task->changed) {
            pac_reload();
            if (g_vpn_on && !task->reloaded) vpn_restart(hwnd, g_prof.active);
        }
        return;
    }
    if (!j->ok) problem(hwnd, j->msg);
    else {
        pac_reload();
        if (g_vpn_on && task && !task->reloaded)
            vpn_restart(hwnd, g_prof.active);
    }
}

static void start_task(HWND hwnd, const pac_task *task)
{
    long_job *j = job_new(work_pac, done_pac);
    pac_task *copy = (pac_task *)malloc(sizeof *copy);
    if (!j || !copy) { free(copy); job_free(j); problem(hwnd, L"Не хватило памяти"); return; }
    *copy = *task;
    copy->vpn_on = g_vpn_on;
    j->extra = copy; j->extra_size = sizeof *copy;
    job_start(hwnd, L"Настройка PAC…", j);
}

/* Called with the subscription check: once a minute and at startup. Uses the
   subscription's interval. Nothing to do without an enabled PAC URL. */
void pac_auto_check(HWND hwnd)
{
    pac_store store;
    pac_task task;
    long long now = _time64(NULL);
    int i, urls = 0;
    if (g_busy || g_sub_busy || g_modal) return;               /* next tick */
    if (pac_retry && now < pac_retry) return;
    settings_load(&g_set);
    if (g_set.pac_last && now < g_set.pac_last + (long long)settings_sub_hours[g_set.sub_interval] * 3600) return;
    if (!pacstore_load(&store)) return;
    for (i = 0; i < store.count; i++)
        if (store.items[i].enabled && is_url(store.items[i].source)) urls++;
    pacstore_free(&store);
    if (!urls) return;
    ZeroMemory(&task, sizeof task);
    task.op = PAC_OP_AUTO;
    start_task(hwnd, &task);
}

void pac_open_page(HWND hwnd)
{
    pac_store store;
    if (!pacstore_load(&store)) {
        problem(hwnd, L"Не удалось прочитать pac.json: файл повреждён или создан более новой версией Utgard.");
        return;
    }
    pacstore_free(&store);
    g_page = PAGE_PAC;
    pac_reload();
    layout(hwnd);
}

void pac_back(HWND hwnd) { g_page = PAGE_UTGARD; layout(hwnd); }

void pac_add_file(HWND hwnd)
{
    pac_task task;
    ZeroMemory(&task, sizeof task);
    task.op = PAC_OP_ADD;
    if (pick_pac(hwnd, task.source, 2048)) start_task(hwnd, &task);
}

void pac_add_url(HWND hwnd)
{
    pac_task task;
    ZeroMemory(&task, sizeof task);
    task.op = PAC_OP_ADD;
    if (!ask_string(hwnd, L"PAC по URL", L"Адрес HTTP(S) файла PAC", L"", task.source, 2048)) return;
    if (_wcsnicmp(task.source, L"https://", 8)) {
        problem(hwnd, L"PAC по URL принимается только по HTTPS");
        return;
    }
    start_task(hwnd, &task);
}

void pac_action(HWND hwnd, int op)
{
    pac_task task;
    int selected = pac_selected();
    if (selected < 0) return;
    if (op == PAC_OP_DELETE && modal_box(hwnd, L"Удалить выбранный PAC?", L"PAC",
                                         MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    ZeroMemory(&task, sizeof task);
    task.op = op; task.index = selected;
    start_task(hwnd, &task);
}

/* The list header is drawn here: the system header stays light in the dark
   theme. Header notifications go to the header's parent, the list, hence
   the subclass. Items are painted fully (CDRF_SKIPDEFAULT); the strip right
   of the last column is painted over after the default pass. */
static LRESULT paint_header(NMCUSTOMDRAW *d)
{
    HWND header = d->hdr.hwndFrom;
    RECT r;
    switch (d->dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT: {
        wchar_t text[64] = L"";
        HDITEMW item;
        HGDIOBJ old;
        ZeroMemory(&item, sizeof item);
        item.mask = HDI_TEXT;
        item.pszText = text;
        item.cchTextMax = 64;
        Header_GetItem(header, (int)d->dwItemSpec, &item);
        FillRect(d->hdc, &d->rc, g_brush_bg);
        r = d->rc; r.left = r.right - 1;  FillRect(d->hdc, &r, g_brush_line);
        r = d->rc; r.top = r.bottom - 1;  FillRect(d->hdc, &r, g_brush_line);
        r = d->rc; r.left += S(6); r.right -= S(6);
        old = SelectObject(d->hdc, g_font);
        SetBkMode(d->hdc, TRANSPARENT);
        SetTextColor(d->hdc, CLR_MUTED);
        DrawTextW(d->hdc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(d->hdc, old);
        return CDRF_SKIPDEFAULT;
    }
    case CDDS_POSTPAINT: {
        RECT last;
        int count = Header_GetItemCount(header);
        GetClientRect(header, &r);
        if (count > 0 && Header_GetItemRect(header, count - 1, &last)) r.left = last.right;
        if (r.left < r.right) {
            FillRect(d->hdc, &r, g_brush_bg);
            r.top = r.bottom - 1;
            FillRect(d->hdc, &r, g_brush_line);
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

LRESULT CALLBACK pac_list_proc(HWND list, UINT msg, WPARAM wp, LPARAM lp,
                               UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == WM_NOTIFY) {
        NMHDR *n = (NMHDR *)lp;
        if (n && n->code == NM_CUSTOMDRAW && n->hwndFrom == ListView_GetHeader(list))
            return paint_header((NMCUSTOMDRAW *)lp);
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(list, pac_list_proc, id);
    return DefSubclassProc(list, msg, wp, lp);
}
