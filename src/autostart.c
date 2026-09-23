#define COBJMACROS
#include "autostart.h"

#include <oleauto.h>
#include <sddl.h>
#include <strsafe.h>
#include <taskschd.h>

#define XML_MAX 4096

/* The task names the user by SID: unique per account, and nothing in it needs
   escaping - Task Scheduler accepts a SID wherever it takes a user name. */
static int user_sid(wchar_t *out, size_t cap)
{
    HANDLE  tok;
    union { TOKEN_USER user; BYTE raw[256]; } buf;
    DWORD   len = 0;
    wchar_t *text = NULL;
    int     ok = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
    if (GetTokenInformation(tok, TokenUser, &buf, sizeof buf, &len) &&
        ConvertSidToStringSidW(buf.user.User.Sid, &text)) {
        ok = SUCCEEDED(StringCchCopyW(out, cap, text));
        LocalFree(text);
    }
    CloseHandle(tok);
    return ok;
}

static int exe_path(wchar_t *out, DWORD cap)
{
    DWORD n = GetModuleFileNameW(NULL, out, cap);
    return n > 0 && n < cap;
}

/* Element text in XML: only & and < are unsafe there; > is escaped too for
   symmetry with what the scheduler writes back. */
static int xml_escape(const wchar_t *in, wchar_t *out, size_t cap)
{
    size_t used = 0;

    for (; *in; in++) {
        const wchar_t *rep = NULL;
        wchar_t        one[2] = { *in, 0 };
        size_t         len;

        if (*in == L'&') rep = L"&amp;";
        else if (*in == L'<') rep = L"&lt;";
        else if (*in == L'>') rep = L"&gt;";
        else rep = one;
        len = wcslen(rep);
        if (used + len >= cap) return 0;
        memcpy(out + used, rep, len * sizeof *out);
        used += len;
    }
    out[used] = 0;
    return 1;
}

typedef struct {
    ITaskService *svc;
    ITaskFolder  *root;
    BSTR          name;
} sched;

static void sched_close(sched *s)
{
    if (s->name) SysFreeString(s->name);
    if (s->root) ITaskFolder_Release(s->root);
    if (s->svc)  ITaskService_Release(s->svc);
}

static HRESULT sched_open(sched *s, const wchar_t *sid)
{
    VARIANT none;
    BSTR    slash;
    wchar_t name[128];
    HRESULT hr;

    ZeroMemory(s, sizeof *s);
    VariantInit(&none);

    if (FAILED(StringCchPrintfW(name, 128, L"Utgard (%s)", sid))) return E_FAIL;
    s->name = SysAllocString(name);
    if (!s->name) return E_OUTOFMEMORY;

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskService, (void **)&s->svc);
    if (FAILED(hr)) return hr;
    hr = ITaskService_Connect(s->svc, none, none, none, none);
    if (FAILED(hr)) return hr;

    slash = SysAllocString(L"\\");
    if (!slash) return E_OUTOFMEMORY;
    hr = ITaskService_GetFolder(s->svc, slash, &s->root);
    SysFreeString(slash);
    return hr;
}

/* The command of the task's first action, quotes stripped. */
static HRESULT task_command(sched *s, wchar_t *out, size_t cap)
{
    IRegisteredTask   *reg  = NULL;
    ITaskDefinition   *def  = NULL;
    IActionCollection *acts = NULL;
    IAction           *act  = NULL;
    IExecAction       *ex   = NULL;
    BSTR               path = NULL;
    HRESULT            hr;

    hr = ITaskFolder_GetTask(s->root, s->name, &reg);
    if (SUCCEEDED(hr)) hr = IRegisteredTask_get_Definition(reg, &def);
    if (SUCCEEDED(hr)) hr = ITaskDefinition_get_Actions(def, &acts);
    if (SUCCEEDED(hr)) hr = IActionCollection_get_Item(acts, 1, &act);
    if (SUCCEEDED(hr)) hr = IAction_QueryInterface(act, &IID_IExecAction, (void **)&ex);
    if (SUCCEEDED(hr)) hr = IExecAction_get_Path(ex, &path);
    if (SUCCEEDED(hr)) {
        const wchar_t *p = path ? path : L"";
        size_t         n;
        if (*p == L'"') p++;
        if (FAILED(StringCchCopyW(out, cap, p))) hr = E_FAIL;
        n = wcslen(out);
        if (n && out[n - 1] == L'"') out[n - 1] = 0;
    }

    if (path) SysFreeString(path);
    if (ex)   IExecAction_Release(ex);
    if (act)  IAction_Release(act);
    if (acts) IActionCollection_Release(acts);
    if (def)  ITaskDefinition_Release(def);
    if (reg)  IRegisteredTask_Release(reg);
    return hr;
}

int autostart_get(void)
{
    wchar_t sid[128], exe[MAX_PATH], cmd[MAX_PATH + 8];
    sched   s;
    int     on = 0;

    if (!user_sid(sid, 128) || !exe_path(exe, MAX_PATH)) return 0;
    if (SUCCEEDED(sched_open(&s, sid)) &&
        SUCCEEDED(task_command(&s, cmd, MAX_PATH + 8)))
        on = CompareStringOrdinal(cmd, -1, exe, -1, TRUE) == CSTR_EQUAL;
    sched_close(&s);
    return on;
}

static void say(wchar_t *err, size_t cap, const wchar_t *what, HRESULT hr)
{
    if (err && cap) StringCchPrintfW(err, cap, L"%s (код 0x%08lX)", what, (unsigned long)hr);
}

int autostart_set(int on, wchar_t *err, size_t errcap)
{
    /* Defaults the schema would otherwise apply, each one wrong for a tray
       app that owns a tunnel: no start on battery, kill after 72 hours,
       background priority 7 inherited by sing-box. Priority 4 is what a
       double-click in Explorer gives: normal CPU and memory priority. */
    static const wchar_t TEMPLATE[] =
        L"<Task version=\"1.3\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Description>Utgard: start with Windows</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><UserId>%s</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"Author\"><UserId>%s</UserId>"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
        L"</Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority></Settings>"
        L"<Actions Context=\"Author\"><Exec><Command>\"%s\"</Command>"
        L"<Arguments>--minimized</Arguments></Exec></Actions></Task>";

    wchar_t sid[128], exe[MAX_PATH], exe_xml[MAX_PATH * 5], xml[XML_MAX];
    sched   s;
    HRESULT hr;
    int     ok = 0;

    if (!user_sid(sid, 128)) {
        say(err, errcap, L"Не удалось определить учётную запись", HRESULT_FROM_WIN32(GetLastError()));
        return 0;
    }

    hr = sched_open(&s, sid);
    if (FAILED(hr)) {
        say(err, errcap, L"Планировщик заданий недоступен", hr);
        sched_close(&s);
        return 0;
    }

    if (!on) {
        hr = ITaskFolder_DeleteTask(s.root, s.name, 0);
        ok = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (!ok) say(err, errcap, L"Не удалось убрать из автозапуска", hr);
        sched_close(&s);
        return ok;
    }

    /* The schema caps Command at 260 characters, quotes included. */
    if (!exe_path(exe, MAX_PATH) || wcslen(exe) + 2 > 260 ||
        !xml_escape(exe, exe_xml, sizeof exe_xml / sizeof exe_xml[0]) ||
        FAILED(StringCchPrintfW(xml, XML_MAX, TEMPLATE, sid, sid, exe_xml))) {
        if (err && errcap)
            StringCchCopyW(err, errcap, L"Слишком длинный путь к программе для автозапуска");
        sched_close(&s);
        return 0;
    }

    {
        BSTR              bxml = SysAllocString(xml);
        IRegisteredTask  *reg  = NULL;
        VARIANT           none;

        VariantInit(&none);
        hr = bxml ? ITaskFolder_RegisterTask(s.root, s.name, bxml, TASK_CREATE_OR_UPDATE,
                                             none, none, TASK_LOGON_INTERACTIVE_TOKEN,
                                             none, &reg)
                  : E_OUTOFMEMORY;
        if (reg) IRegisteredTask_Release(reg);
        if (bxml) SysFreeString(bxml);
    }
    ok = SUCCEEDED(hr);
    if (!ok) say(err, errcap, L"Не удалось добавить в автозапуск", hr);
    sched_close(&s);
    return ok;
}
