/* vpnswitch.c: the order of a profile switch, every branch, with stand-in
   operations. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include "vpnswitch.h"
static int prep_new, prep_old, down_ok, up_new, up_old, fails;
static char calls[128];
static int say(int ok, wchar_t *m, size_t cap, const wchar_t *why) { if (m && cap) { wcsncpy(m, ok ? L"" : why, cap - 1); m[cap - 1] = 0; } return ok; }
static int prepare(void *c, int w, wchar_t *m, size_t cap) { (void)c; strcat(calls, w == VPN_NEW ? "prep-new;" : "prep-old;"); return say(w == VPN_NEW ? prep_new : prep_old, m, cap, L"[prep]"); }
static int down(void *c, wchar_t *m, size_t cap) { (void)c; strcat(calls, "down;"); return say(down_ok, m, cap, L"[down]"); }
static int up(void *c, int w, wchar_t *m, size_t cap) { (void)c; strcat(calls, w == VPN_NEW ? "up-new;" : "up-old;"); return say(w == VPN_NEW ? up_new : up_old, m, cap, w == VPN_NEW ? L"[up-new]" : L"[up-old]"); }
static void run(const char *name, int want_ok, const char *want_calls, const wchar_t *want_text)
{
    vpn_switch_ops ops = { NULL, prepare, down, up };
    wchar_t msg[600]; int r;
    calls[0] = 0;
    r = vpn_switch(&ops, msg, 600);
    int good = r == want_ok && !strcmp(calls, want_calls) && (!want_text || wcsstr(msg, want_text));
    printf("%-26s %s calls=%s msg=%ls\n", name, good ? "ok " : "BAD", calls, msg);
    if (!good) fails++;
    prep_new = prep_old = down_ok = up_new = up_old = 1;
}
int main(void)
{
    setlocale(LC_ALL, "C.UTF-8");
    prep_new = prep_old = down_ok = up_new = up_old = 1;
    run("switches", 1, "prep-new;prep-old;down;up-new;", NULL);
    prep_new = 0; run("new does not prepare", 0, "prep-new;", L"как прежде");
    prep_old = 0; run("old does not prepare", 0, "prep-new;prep-old;", L"возврат");
    down_ok = 0;  run("cannot stop", 0, "prep-new;prep-old;down;", L"[down]");
    up_new = 0;   run("new fails, old back", 0, "prep-new;prep-old;down;up-new;up-old;", L"возвращён прежний");
    up_new = 0; up_old = 0; run("both fail", 0, "prep-new;prep-old;down;up-new;up-old;", L"[up-old]");
    { vpn_switch_ops ops = { NULL, prepare, down, up }; prep_new = 0; calls[0] = 0; vpn_switch(&ops, NULL, 0); printf("%-26s %s\n", "NULL message buffer", "ok "); prep_new = 1; }
    printf("vpnswitch: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
