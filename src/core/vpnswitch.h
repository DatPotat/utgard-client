#ifndef UTGARD_VPNSWITCH_H
#define UTGARD_VPNSWITCH_H

#include <stddef.h>
#include <wchar.h>

/* Moving the running VPN from one profile to another. Only the order of the
   steps lives here - no Windows, no sing-box - so every branch is tested on
   its own with stand-in operations.

   Both profiles are prepared - configs built and checked - before anything
   running is touched: a profile that cannot start leaves the VPN as it was,
   and so does one we could not come back to. Only then the VPN goes down
   and comes up with the new profile, or, if that fails, with the old one. */

enum { VPN_NEW = 0, VPN_OLD = 1 };

typedef struct {
    void *ctx;
    int (*prepare)(void *ctx, int which, wchar_t *msg, size_t cap);
    int (*down)(void *ctx, wchar_t *msg, size_t cap);
    int (*up)(void *ctx, int which, wchar_t *msg, size_t cap);
} vpn_switch_ops;

/* 1 when the new profile runs. 0 otherwise; msg says what happened and
   whether the old profile runs again. */
int vpn_switch(const vpn_switch_ops *ops, wchar_t *msg, size_t cap);

#endif
