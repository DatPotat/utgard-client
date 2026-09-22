#include "tray.h"

#include <shellapi.h>
#include <strsafe.h>

static NOTIFYICONDATAW g_nid;
static HICON           g_icon_on, g_icon_off;
static int             g_state = -1;

/* A filled disc in the given colour. Drawn at runtime: the executable carries
   no icon resource, and two plain states are all the tray needs. */
static HICON make_disc(COLORREF color)
{
    int      size = GetSystemMetrics(SM_CXSMICON);
    HDC      screen = GetDC(NULL);
    HDC      dc = CreateCompatibleDC(screen);
    HBITMAP  color_bmp = CreateCompatibleBitmap(screen, size, size);
    HBITMAP  mask_bmp  = CreateBitmap(size, size, 1, 1, NULL);
    HGDIOBJ  old;
    HBRUSH   br;
    HPEN     pen;
    ICONINFO ii;
    HICON    icon;
    RECT     all = { 0, 0, size, size };
    int      m = size / 8;

    /* Colour plane: black outside, the disc inside. */
    old = SelectObject(dc, color_bmp);
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    br  = CreateSolidBrush(color);
    pen = CreatePen(PS_SOLID, 1, color);
    SelectObject(dc, br);
    SelectObject(dc, pen);
    Ellipse(dc, m, m, size - m, size - m);
    SelectObject(dc, old);
    DeleteObject(br);
    DeleteObject(pen);

    /* Mask plane: white is transparent, black is where the colour shows. */
    old = SelectObject(dc, mask_bmp);
    FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    SelectObject(dc, GetStockObject(BLACK_PEN));
    Ellipse(dc, m, m, size - m, size - m);
    SelectObject(dc, old);

    ZeroMemory(&ii, sizeof ii);
    ii.fIcon    = TRUE;
    ii.hbmColor = color_bmp;
    ii.hbmMask  = mask_bmp;
    icon = CreateIconIndirect(&ii);

    DeleteObject(color_bmp);
    DeleteObject(mask_bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return icon;
}

int tray_init(HWND owner, UINT callback_message, COLORREF on, COLORREF off)
{
    g_icon_on  = make_disc(on);
    g_icon_off = make_disc(off);

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
