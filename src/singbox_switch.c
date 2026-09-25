#include "singbox.h"
#include <stdio.h>

int singbox_switch(const char *config, const char *fallback, wchar_t *msg, size_t cap)
{
    wchar_t failure[SB_MSG_MAX] = {0}, restore[SB_MSG_MAX] = {0};
    int restored;

    /* A bad new profile must leave the running tunnel untouched. Validate
       rollback too, before giving up the current connection. */
    if (!singbox_check(config, msg, cap) || !singbox_check(fallback, msg, cap))
        return 0;
    if (!singbox_stop(msg, cap)) return 0;
    if (singbox_start(config, failure, SB_MSG_MAX)) {
        if (msg && cap) msg[0] = L'\0';
        return 1;
    }

    restored = singbox_start(fallback, restore, SB_MSG_MAX);
    if (msg && cap) {
        if (restored)
            swprintf(msg, cap, L"Не удалось переключить профиль. Прежний профиль восстановлен. %.300ls", failure);
        else
            swprintf(msg, cap, L"Не удалось переключить профиль и восстановить VPN. %.180ls %.180ls", failure, restore);
        msg[cap - 1] = L'\0';
    }
    return 0;
}
