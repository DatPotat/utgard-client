/*
 * Utgard client - Popup menus drawn in the palette: the owner-drawn items
 * take the theme's font, colours and hover, instead of the system look.
 */

#include "ui.h"

HMENU menu_create(void)
{
    HMENU    m = CreatePopupMenu();
    MENUINFO mi;
    if (!m) return NULL;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize  = sizeof mi;
    mi.fMask   = MIM_BACKGROUND;
    mi.hbrBack = g_brush_surface;
    SetMenuInfo(m, &mi);
    return m;
}

/* text must outlive the menu: it is the item's data, not a copy. */
void menu_add(HMENU m, UINT id, const wchar_t *text, int grayed)
{
    AppendMenuW(m, MF_OWNERDRAW | (grayed ? MF_GRAYED : 0), id, (LPCWSTR)text);
}

void menu_separator(HMENU m)
{
    AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, NULL);
}

int menu_measure(MEASUREITEMSTRUCT *mi)
{
    const wchar_t *text;
    HDC     dc;
    HGDIOBJ old;
    SIZE    sz = { 0, 0 };

    if (mi->CtlType != ODT_MENU) return 0;
    text = (const wchar_t *)mi->itemData;
    if (!text) { mi->itemWidth = (UINT)scaled(40); mi->itemHeight = (UINT)scaled(9); return 1; }
    dc = GetDC(NULL);
    old = SelectObject(dc, g_font);
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    mi->itemWidth  = (UINT)(sz.cx + scaled(40));
    mi->itemHeight = (UINT)scaled(36);
    return 1;
}

int menu_draw(const DRAWITEMSTRUCT *d)
{
    const wchar_t *text;
    RECT     r = d->rcItem;
    int      gray, sel;
    HBRUSH   back;

    if (d->CtlType != ODT_MENU) return 0;
    text = (const wchar_t *)d->itemData;
    gray = (d->itemState & ODS_GRAYED) != 0;
    sel  = (d->itemState & ODS_SELECTED) && !gray;
    back = CreateSolidBrush(CLR_SURFACE);
    FillRect(d->hDC, &r, back);
    DeleteObject(back);
    if (!text) {
        fill(d->hDC, r.left + scaled(8), (r.top + r.bottom) / 2, r.right - r.left - scaled(16), scaled(1), g_brush_line);
        return 1;
    }
    if (sel) {
        RECT h = r;
        InflateRect(&h, -scaled(4), -scaled(2));
        rounded_r(d->hDC, &h, CLR_TINT, CLR_TINT, scaled(6));
    }
    text_at(d->hDC, r.left + scaled(16), r.top, r.right - r.left - scaled(24), r.bottom - r.top, text,
            gray ? CLR_MUTED : CLR_TEXT, g_font, DT_LEFT | DT_END_ELLIPSIS);
    return 1;
}
