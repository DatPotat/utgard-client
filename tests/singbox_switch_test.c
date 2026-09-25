#include "singbox.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int check_new, check_old, stop_ok, start_new, start_old;
static char calls[64];

static int answer(int ok, wchar_t *msg, size_t cap, const wchar_t *reason)
{
    if (msg && cap) {
        swprintf(msg, cap, L"%ls", ok ? L"" : reason);
        msg[cap - 1] = 0;
    }
    return ok;
}

int singbox_check(const char *config, wchar_t *msg, size_t cap)
{
    int fresh = strcmp(config, "new") == 0;
    strcat(calls, fresh ? "check-new;" : "check-old;");
    return answer(fresh ? check_new : check_old, msg, cap, L"invalid config");
}

int singbox_stop(wchar_t *msg, size_t cap)
{
    strcat(calls, "stop;");
    return answer(stop_ok, msg, cap, L"stop failed");
}

int singbox_start(const char *config, wchar_t *msg, size_t cap)
{
    int fresh = strcmp(config, "new") == 0;
    strcat(calls, fresh ? "start-new;" : "start-old;");
    return answer(fresh ? start_new : start_old, msg, cap, L"start failed");
}

static void reset(void)
{
    calls[0] = 0;
    check_new = check_old = stop_ok = start_new = start_old = 1;
}

int main(void)
{
    wchar_t msg[SB_MSG_MAX];
    reset();
    assert(singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(!msg[0]);
    assert(!strcmp(calls, "check-new;check-old;stop;start-new;"));

    reset(); check_new = 0;
    assert(!singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(!strcmp(calls, "check-new;")); /* Existing VPN is untouched. */

    reset(); check_old = 0;
    assert(!singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(!strcmp(calls, "check-new;check-old;"));

    reset(); stop_ok = 0;
    assert(!singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(!strcmp(calls, "check-new;check-old;stop;"));

    reset(); start_new = 0;
    assert(!singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(!strcmp(calls, "check-new;check-old;stop;start-new;start-old;"));
    assert(wcsstr(msg, L"Прежний профиль восстановлен"));

    reset(); start_new = start_old = 0;
    assert(!singbox_switch("new", "old", msg, SB_MSG_MAX));
    assert(wcsstr(msg, L"и восстановить VPN"));
    assert(wcsstr(msg, L"start failed"));
    puts("VPN profile switch: validation, restart and rollback: OK");
    return 0;
}
