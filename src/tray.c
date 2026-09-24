#include "tray.h"

#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>

static NOTIFYICONDATAW g_nid;
static HICON           g_icon_on, g_icon_off;
static int             g_state = -1;

/* Resource ids from res/utgard.rc. Icon 1 stays the program icon: Explorer
   shows the lowest-numbered one for the file. */
#define IDI_TRAY_OFF 2
#define IDI_TRAY_ON  3

/* LIM_SMALL is the small-icon metric for the current DPI - the size the
   notification area draws - picked from the sizes inside the .ico. */
static HICON load_icon(int id)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    HICON     icon = NULL;

    if (FAILED(LoadIconMetric(inst, MAKEINTRESOURCEW(id), LIM_SMALL, &icon)))
        LoadIconMetric(inst, MAKEINTRESOURCEW(1), LIM_SMALL, &icon);
    return icon;
}

int tray_init(HWND owner, UINT callback_message)
{
    g_icon_on  = load_icon(IDI_TRAY_ON);
    g_icon_off = load_icon(IDI_TRAY_OFF);

    ZeroMemory(&g_nid, sizeof g_nid);
    g_nid.cbSize           = sizeof g_nid;
    g_nid.hWnd             = owner;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = callback_message;
    g_nid.hIcon            = g_icon_off;
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"Utgard — VPN выключен");

    g_state = 0;
    return Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0;
}

void tray_set_state(int vpn_on)
{
    if (vpn_on == g_state) return;
    g_state = vpn_on;
    g_nid.hIcon  = vpn_on ? g_icon_on : g_icon_off;
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
                   vpn_on ? L"Utgard — VPN включён" : L"Utgard — VPN выключен");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void tray_readd(void)
{
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_icon_on)  DestroyIcon(g_icon_on);
    if (g_icon_off) DestroyIcon(g_icon_off);
    g_icon_on = g_icon_off = NULL;
}
