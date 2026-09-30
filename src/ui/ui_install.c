/*
 * Utgard client - Core downloads (sing-box, AmneziaWG) and the first-run questions.
 */

#include "awgcore.h"
#include "coredir.h"
#include "ui.h"

/* State owned by this file (declared in ui.h). */
int g_installing;
int g_awg_after_singbox; /* AmneziaWG said yes at start while sing-box downloads */

static DWORD WINAPI install_thread(LPVOID param)
{
    install_job *job = (install_job *)param;

    job->ok = job->awg ? awgcore_install(job->msg, SB_MSG_MAX)
                       : singbox_install(job->msg, SB_MSG_MAX);
    PostMessageW(job->hwnd, WM_APP_INSTALL, 0, (LPARAM)job);
    return 0;
}

/* Asks first. The download is 21 MB from GitHub, and a client that reaches out
   on its own the first time it starts is not something to do silently. */
/* The question to ask about sing-box; 0 when none is needed (installed and
   right, or a problem already reported). */
static int singbox_question(HWND hwnd, wchar_t *question, size_t cap)
{
    if (g_installing) return 0;

    /* Present but not the pinned release - an older version left over, or a
       modified file. Launching is refused either way, so offer the fix here
       instead of leaving only an error at the moment of switching on. */
    if (singbox_present()) {
        wchar_t why[400];
        int     reinstall = 0;
        if (singbox_verify(why, 400, &reinstall)) return 0;
        /* The folder itself is wrong - its permissions, a link, a stray
           file: a fresh download changes none of that, so say what it is. */
        if (!reinstall) {
            problem(hwnd, why);
            return 0;
        }
        if (singbox_running()) {
            problem(hwnd, L"Установленный sing-box не совпадает с нужной версией. "
                          L"Выключите VPN, и программа предложит скачать правильную.");
            return 0;
        }
        StringCchPrintfW(question, cap,
            L"Установленный sing-box не совпадает с версией %s — это старая "
            L"версия или изменённый файл. Запускать его программа не будет.\n\n"
            L"Скачать правильную версию сейчас?", singbox_version());
        return 1;
    }

    StringCchPrintfW(question, cap,
        L"Не найден sing-box — без него VPN работать не может.\n\n"
        L"Скачать его сейчас?\n\n"
        L"Если не хотите скачивать автоматически — загрузите с GitHub архив %s "
        L"и положите его файлы в папку core\\sing-box рядом с программой. "
        L"Другие версии и сборки программа не запустит.",
        singbox_archive());
    return 1;
}

static int singbox_install_start(HWND hwnd)
{
    install_job *job;
    HANDLE       th;

    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return 0; }
    job->hwnd = hwnd;

    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return 0; }
    CloseHandle(th);

    g_installing = 1;
    layout(hwnd);
    return 1;
}

int offer_install(HWND hwnd)
{
    wchar_t  question[900];
    ask_step step = { L"sing-box", question, L"Скачать", L"Не сейчас", 0 };
    if (!singbox_question(hwnd, question, 900)) return 0;
    ask_steps(hwnd, L"sing-box", &step, 1);
    return step.answer ? singbox_install_start(hwnd) : 0;
}

/* The AmneziaWG core, asked for when a profile first needs it. On success
   the switch-on the user asked for carries on by itself. */
static const wchar_t *awg_question(int resume)
{
    wchar_t dir[MAX_PATH * 2];
    if (g_installing || g_awg_ready) return NULL;
    /* On FAT32 it can never run; asking at start would only nag. */
    if (!resume && (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2) || !coredir_volume_has_acl(dir)))
        return NULL;
    return resume
            ? L"Для сервера AmneziaWG нужно ядро AmneziaWG — официальный пакет "
              L"amneziawg-windows-client с GitHub.\n\n"
              L"Программа проверит его контрольную сумму и возьмёт из него два файла "
              L"в папку core\\amneziawg рядом с собой. В систему ничего не устанавливается; "
              L"во время работы туннеля программа создаёт службу и сетевой адаптер "
              L"и удаляет их при выключении.\n\nСкачать сейчас?"
            : L"Не найдено ядро AmneziaWG — без него не работают серверы AmneziaWG.\n\n"
              L"Скачать его сейчас? Это официальный пакет amneziawg-windows-client с "
              L"GitHub: программа проверит его контрольную сумму и возьмёт из него два "
              L"файла в папку core\\amneziawg. В систему ничего не устанавливается.\n\n"
              L"Если серверы AmneziaWG не нужны, можно отказаться — программа спросит "
              L"снова, когда такой сервер понадобится.";
}

static void awg_install_start(HWND hwnd, int resume)
{
    install_job *job;
    HANDLE       th;

    job = (install_job *)calloc(1, sizeof *job);
    if (!job) { problem(hwnd, L"Не хватило памяти"); return; }
    job->hwnd   = hwnd;
    job->awg    = 1;
    job->resume = resume;
    th = CreateThread(NULL, 0, install_thread, job, 0, NULL);
    if (!th) { free(job); problem(hwnd, L"Не удалось запустить загрузку"); return; }
    CloseHandle(th);
    g_installing = 2;
    layout(hwnd);
}

/* Already answered yes (at start): download without asking again. */
void offer_awg_download(HWND hwnd)
{
    if (!g_installing && !g_awg_ready) awg_install_start(hwnd, 0);
}

void offer_awg_install(HWND hwnd, int resume)
{
    const wchar_t *text = awg_question(resume);
    ask_step step = { L"AmneziaWG", NULL, L"Скачать", L"Не сейчас", 0 };
    if (!text) return;
    step.text = text;
    ask_steps(hwnd, L"AmneziaWG", &step, 1);
    if (step.answer) awg_install_start(hwnd, resume);
}

/* The first-run questions - a newer release, sing-box, AmneziaWG - in one
   window, one tab each, in that order. Held back until the start-up update
   check answers (or 6 s pass), and until the window is shown. */
static int notice_wait, notice_pending;

int startup_notice_waiting(void) { return notice_wait; }

void startup_notice_begin(HWND hwnd)
{
    if (upd_running()) {
        notice_wait = 1;
        SetTimer(hwnd, TIMER_NOTICE, 6000, NULL);
    } else startup_notice(hwnd);
}

void startup_notice_timer(HWND hwnd)
{
    KillTimer(hwnd, TIMER_NOTICE);
    if (notice_wait) startup_notice(hwnd);
}

void startup_notice_shown(HWND hwnd)
{
    if (notice_pending) startup_notice(hwnd);
    else if (g_upd_pending) { g_upd_pending = 0; upd_prompt(hwnd); }
}

void startup_notice(HWND hwnd)
{
    wchar_t        update_text[256], singbox_text[900];
    const wchar_t *awg_text;
    ask_step       steps[3];
    int            count = 0, update = -1, singbox = -1, awg = -1, installing = 0;

    notice_wait = 0;
    KillTimer(hwnd, TIMER_NOTICE);
    if (!IsWindowVisible(hwnd)) { notice_pending = 1; return; }
    notice_pending = 0;
    g_upd_pending = 0;

    if (upd_question(update_text, 256)) {
        steps[count].tab = L"Обновление"; steps[count].text = update_text;
        steps[count].yes = L"Открыть страницу"; steps[count].no = L"Позже";
        update = count++;
    }
    if (singbox_question(hwnd, singbox_text, 900)) {
        steps[count].tab = L"sing-box"; steps[count].text = singbox_text;
        steps[count].yes = L"Скачать"; steps[count].no = L"Не сейчас";
        singbox = count++;
    }
    if ((awg_text = awg_question(0)) != NULL) {
        steps[count].tab = L"AmneziaWG"; steps[count].text = awg_text;
        steps[count].yes = L"Скачать"; steps[count].no = L"Не сейчас";
        awg = count++;
    }
    if (!count) return;
    ask_steps(hwnd, L"Utgard", steps, count);

    if (update >= 0 && steps[update].answer) upd_open_page(hwnd);
    if (singbox >= 0 && steps[singbox].answer) installing = singbox_install_start(hwnd);
    /* One download at a time: AmneziaWG follows sing-box when both are wanted. */
    if (awg >= 0 && steps[awg].answer) {
        if (installing) g_awg_after_singbox = 1;
        else awg_install_start(hwnd, 0);
    }
}
