#include "vpnswitch.h"

#include <stdio.h>

#define REASON_MAX 400

static void explain(wchar_t *msg, size_t cap, const wchar_t *what, const wchar_t *a, const wchar_t *b)
{
    if (!msg || !cap) return;
    swprintf(msg, cap, L"%ls%ls%.*ls%ls%.*ls", what,
             a[0] ? L" " : L"", REASON_MAX, a,
             b[0] ? L" " : L"", REASON_MAX, b);
    msg[cap - 1] = L'\0';
}

int vpn_switch(const vpn_switch_ops *ops, wchar_t *msg, size_t cap)
{
    wchar_t why[REASON_MAX] = L"", back[REASON_MAX] = L"";

    if (msg && cap) msg[0] = L'\0';

    if (!ops->prepare(ops->ctx, VPN_NEW, why, REASON_MAX)) {
        explain(msg, cap, L"Профиль не переключён, VPN работает как прежде.", why, L"");
        return 0;
    }
    if (!ops->prepare(ops->ctx, VPN_OLD, why, REASON_MAX)) {
        explain(msg, cap, L"Профиль не переключён, VPN работает как прежде: не удалось подготовить "
                       L"возврат к текущему профилю.", why, L"");
        return 0;
    }
    if (!ops->down(ops->ctx, why, REASON_MAX)) {
        explain(msg, cap, L"Профиль не переключён: не удалось остановить VPN.", why, L"");
        return 0;
    }
    if (ops->up(ops->ctx, VPN_NEW, why, REASON_MAX)) return 1;

    if (ops->up(ops->ctx, VPN_OLD, back, REASON_MAX))
        explain(msg, cap, L"Новый профиль не запустился — возвращён прежний.", why, L"");
    else
        explain(msg, cap, L"Новый профиль не запустился, и вернуть прежний не удалось — VPN выключен.",
             why, back);
    return 0;
}
