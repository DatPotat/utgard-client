#ifndef UTGARD_TRAY_H
#define UTGARD_TRAY_H

#include <windows.h>

/* The notification-area icon. Its colour follows the VPN state, so the user
   can tell at a glance without opening the window. */

int  tray_init(HWND owner, UINT callback_message, COLORREF on, COLORREF off);
void tray_set_state(int vpn_on);
void tray_readd(void);      /* Explorer restarted and dropped every icon */
void tray_remove(void);

#endif
