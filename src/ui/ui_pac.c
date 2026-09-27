#include "ui.h"
#include "pacstore.h"
#include "pacproc.h"

enum { PAC_OP_ADD = 1, PAC_OP_REFRESH, PAC_OP_TOGGLE, PAC_OP_DELETE, PAC_OP_ROUTE };

typedef struct {
    int op, index, via_vpn;
    wchar_t source[2048];
} pac_task;

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
        ListView_SetItemText(g_pac_list, i, 3,
            is_url(store.items[i].source)
                ? (store.items[i].via_vpn ? L"Через VPN" : L"Напрямую")
                : L"—");
        StringCchPrintfW(state, 64, L"Готов · %lu КБ",
                         (unsigned long)((strlen(store.items[i].text) + 1023) / 1024));
        ListView_SetItemText(g_pac_list, i, 4, state);
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
    int ok;
    *text = NULL; *length = 0;
    if (is_url(task->source)) {
        unsigned short port = 0;
        char password[65] = { 0 };
        if (task->via_vpn && !singbox_running()) {
            StringCchCopyW(err, cap, L"Чтобы скачать PAC через VPN, сначала включите VPN");
            return 0;
        }
        if (task->via_vpn && !pacproc_proxy(&port, password)) {
            StringCchCopyW(err, cap,
                L"Для загрузки PAC через VPN переподключите VPN с включённым PAC");
            return 0;
        }
        ok = net_fetch_pac(task->source, port, port ? password : NULL,
                           text, length, err, cap);
        SecureZeroMemory(password, sizeof password);
        return ok;
    }
    *text = (char *)malloc(PAC_MAX + 1);
    if (!*text || file_read(task->source, *text, PAC_MAX + 1, length) != 1) {
        free(*text); *text = NULL;
        StringCchCopyW(err, cap, L"Не удалось прочитать PAC целиком (не более 4 МиБ)");
        return 0;
    }
    return 1;
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
    if (!pacstore_load(&store)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось прочитать pac.json");
        return;
    }
    if (task->op != PAC_OP_ADD && (task->index < 0 || task->index >= store.count)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Выбранный PAC больше не существует");
        goto done;
    }
    if (task->op == PAC_OP_ADD || task->op == PAC_OP_REFRESH) {
        if (task->op == PAC_OP_REFRESH) {
            StringCchCopyW(task->source, 2048, store.items[task->index].source);
            task->via_vpn = store.items[task->index].via_vpn;
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
        replacement.via_vpn = task->via_vpn;
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
    } else if (task->op == PAC_OP_ROUTE) {
        if (!is_url(store.items[task->index].source)) {
            StringCchCopyW(j->msg, SB_MSG_MAX, L"Для локального файла способ загрузки не применяется");
            goto done;
        }
        store.items[task->index].via_vpn = !store.items[task->index].via_vpn;
    } else if (task->op == PAC_OP_DELETE) {
        free(store.items[task->index].text);
        for (i = task->index; i + 1 < store.count; i++) store.items[i] = store.items[i + 1];
        ZeroMemory(&store.items[--store.count], sizeof store.items[0]);
    }
    if (!pacstore_save(&store)) {
        StringCchCopyW(j->msg, SB_MSG_MAX, L"Не удалось сохранить настройки PAC");
        goto done;
    }
    j->ok = 1;
done:
    pac_close(check);
    free(replacement.text);
    pacstore_free(&store);
}

static void done_pac(HWND hwnd, long_job *j)
{
    pac_task *task = (pac_task *)j->extra;
    if (!j->ok) problem(hwnd, j->msg);
    else {
        pac_reload();
        if (g_vpn_on && (!task || task->op != PAC_OP_ROUTE))
            vpn_restart(hwnd, g_prof.active);
    }
}

static void start_task(HWND hwnd, const pac_task *task)
{
    long_job *j = job_new(work_pac, done_pac);
    pac_task *copy = (pac_task *)malloc(sizeof *copy);
    if (!j || !copy) { free(copy); job_free(j); problem(hwnd, L"Не хватило памяти"); return; }
    *copy = *task;
    j->extra = copy; j->extra_size = sizeof *copy;
    job_start(hwnd, L"Настройка PAC…", j);
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
    int route;
    ZeroMemory(&task, sizeof task);
    task.op = PAC_OP_ADD;
    if (!ask_string(hwnd, L"PAC по URL", L"Адрес HTTP(S) файла PAC", L"", task.source, 2048)) return;
    if (_wcsnicmp(task.source, L"https://", 8)) {
        problem(hwnd, L"PAC по URL принимается только по HTTPS");
        return;
    }
    route = modal_box(hwnd,
        L"Как скачать PAC?\n\n«Да» — через активный VPN-профиль\n«Нет» — напрямую",
        L"Загрузка PAC", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (route == IDCANCEL) return;
    task.via_vpn = route == IDYES;
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

void pac_show_help(HWND hwnd)
{
    modal_box(hwnd, L"Активные PAC проверяются вместе: если хотя бы один возвращает PROXY, HTTP, SOCKS или SOCKS5, соединение идёт через выбранный профиль Utgard. DIRECT действует, только если все PAC вернули DIRECT. Адреса прокси из файлов не используются.\n\n"
        L"Для URL можно выбрать прямую загрузку или загрузку через уже активный VPN. Списки сайтов и приложений имеют приоритет. TUN передаёт домен/IP, порт и схему, но не путь HTTPS-страницы. Если приложение скрывает домен собственным DNS, неизвестный веб-адрес направляется через VPN, чтобы доменное правило PAC не обходилось. Ошибка любого активного PAC блокирует соединение. Счётчик показывает уникальные сайты и TCP-приложения с момента подключения. Изменения правил применяются с переподключением VPN.",
        L"Как работает PAC", MB_OK | MB_ICONINFORMATION);
}
