/*
 * Utgard client - Placement of every control on every page.
 */

#include "ui.h"

void layout(HWND hwnd)
{
    HWND c_hwnd = hwnd;
    RECT c;
    GetClientRect(hwnd, &c);

    MoveWindow(g_tab_utgard, S(14),  S(6), S(96), S(28), TRUE);
    MoveWindow(g_tab_zapret, S(112), S(6), S(96), S(28), TRUE);
    MoveWindow(g_set_open, c.right - PAD - S(110), S(6), S(110), S(28), TRUE);
    MoveWindow(g_ping_now, c.right - PAD - S(110) - S(8) - S(140), S(6), S(140), S(28), TRUE);
    ShowWindow(g_ping_now, g_page == PAGE_UTGARD ? SW_SHOW : SW_HIDE);
    EnableWindow(g_ping_now, !g_ping_busy && g_prof.count > 0);

    {
        int sp = (g_page == PAGE_SETTINGS) ? SW_SHOW : SW_HIDE;

        int cw = c.right - PAD * 2 - S(130) - S(84);   /* room left of "Что это?" */

        MoveWindow(g_set_mtu,   PAD + S(130), TABS_H + S(94),  S(100), S(28), TRUE);
        MoveWindow(g_set_log,   PAD + S(130), TABS_H + S(136), c.right - PAD * 2 - S(130),
                   S(300), TRUE);
        MoveWindow(g_set_stack, PAD + S(130), TABS_H + S(178), cw, S(300), TRUE);
        MoveWindow(g_set_dns,   PAD + S(130), TABS_H + S(220), cw, S(300), TRUE);
        MoveWindow(g_set_sub,   PAD + S(130), TABS_H + S(262), cw, S(300), TRUE);
        MoveWindow(g_set_tray,  PAD, TABS_H + S(318), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_auto,  PAD, TABS_H + S(352), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_upd,   PAD, TABS_H + S(386), c.right - PAD * 2, S(28), TRUE);
        MoveWindow(g_set_upd_now, PAD, TABS_H + S(424), S(200), S(30), TRUE);
        ShowWindow(g_set_sub,   sp);
        ShowWindow(g_set_stack, sp);
        ShowWindow(g_set_dns,   sp);
        ShowWindow(g_set_tray,  sp);
        ShowWindow(g_set_auto,  sp);
        ShowWindow(g_set_upd,   sp);
        ShowWindow(g_set_upd_now, sp);
        MoveWindow(g_set_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_set_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_set_mtu,  sp);
        ShowWindow(g_set_log,  sp);
        ShowWindow(g_set_back, sp);
        ShowWindow(g_set_save, sp);

        if (g_tip) {
            TTTOOLINFOW ti;
            ZeroMemory(&ti, sizeof ti);
            ti.cbSize = sizeof ti;
            ti.hwnd   = hwnd;
            ti.uId    = 4;
            if (g_page == PAGE_SETTINGS) {
                ti.rect.left   = PAD + S(238);
                ti.rect.top    = TABS_H + S(98);
                ti.rect.right  = ti.rect.left + S(80);
                ti.rect.bottom = ti.rect.top + S(20);
            }                                    /* else an empty rect: no tip */
            SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);

            {
                static const int ys[2] = { 178, 220 };
                int k;
                for (k = 0; k < 2; k++) {
                    ti.uId = (UINT_PTR)(5 + k);
                    SetRectEmpty(&ti.rect);
                    if (g_page == PAGE_SETTINGS) {
                        ti.rect.left   = c.right - PAD - S(76);
                        ti.rect.top    = TABS_H + S(ys[k]) + S(4);
                        ti.rect.right  = ti.rect.left + S(76);
                        ti.rect.bottom = ti.rect.top + S(20);
                    }
                    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                }
            }
        }
    }

    MoveWindow(g_toggle, c.right - PAD - S(124), TABS_H + S(26), S(124), S(32), TRUE);
    SetWindowTextW(g_toggle, g_vpn_on ? L"Выключить" : L"Включить");
    EnableWindow(g_toggle, g_vpn_on ||
                 (!g_installing && g_prof.count > 0 && g_prof.active >= 0));

    {
        int list_top = TABS_H + S(190);
        int list_h   = c.bottom - FOOTER_H - S(12) - list_top;
        int on       = (g_page == PAGE_UTGARD) ? SW_SHOW : SW_HIDE;

        if (list_h < S(60)) list_h = S(60);
        MoveWindow(g_plist, PAD, list_top, c.right - PAD * 2, list_h, TRUE);
        MoveWindow(g_btn_hosts, PAD, TABS_H + S(112), S(150), S(30), TRUE);
        MoveWindow(g_btn_apps,  PAD + S(158), TABS_H + S(112), S(170), S(30), TRUE);
        ShowWindow(g_btn_hosts, on);
        ShowWindow(g_btn_apps,  on);
        MoveWindow(g_prof_add, PAD, c.bottom - FOOTER_H + S(4), S(176), S(30), TRUE);
        MoveWindow(g_prof_del, PAD + S(184), c.bottom - FOOTER_H + S(4), S(110), S(30), TRUE);
        MoveWindow(g_prof_sub, PAD + S(302), c.bottom - FOOTER_H + S(4), S(116), S(30), TRUE);

        ShowWindow(g_plist,    on);
        ShowWindow(g_prof_add, on);
        ShowWindow(g_prof_del, on);
        ShowWindow(g_prof_sub, on);
        EnableWindow(g_prof_del, !g_sub_busy && profile_selected() >= 0);
        EnableWindow(g_prof_add, !g_sub_busy);
        EnableWindow(g_prof_sub, !g_sub_busy);
    }

    if (g_zap.valid) {
        int list_top = TABS_H + S(448);
        int list_h   = c.bottom - FOOTER_H - S(12) - list_top;

        SetWindowTextW(g_pick_path, L"Изменить путь");
        MoveWindow(g_pick_path, c.right - PAD - S(130), TABS_H + S(24), S(130), S(30), TRUE);
        if (list_h < S(60)) list_h = S(60);
        MoveWindow(g_list, PAD, list_top, c.right - PAD * 2, list_h, TRUE);

        MoveWindow(g_zap_stop, c.right - PAD - S(104), TABS_H + S(84), S(104), S(30), TRUE);
        MoveWindow(g_zap_restart, c.right - PAD - S(104) - S(8) - S(124),
                   TABS_H + S(84), S(124), S(30), TRUE);
        MoveWindow(g_zap_start, PAD, c.bottom - FOOTER_H + S(4), S(140), S(30), TRUE);
        MoveWindow(g_zap_list, PAD, TABS_H + S(354), S(250), S(30), TRUE);
        ShowWindow(g_zap_list, (g_page == PAGE_ZAPRET && g_zap.valid) ? SW_SHOW : SW_HIDE);
        {
            static const int rows[4] = { 210, 246, 282, 318 };
            HWND ctl[4];
            int  i;
            TTTOOLINFOW ti;

            ctl[0] = g_zap_game; ctl[1] = g_zap_ipset;
            ctl[2] = g_zap_ipupd; ctl[3] = g_zap_hosts;

            ZeroMemory(&ti, sizeof ti);
            ti.cbSize = sizeof ti;
            ti.hwnd   = c_hwnd;

            for (i = 0; i < 4; i++) {
                MoveWindow(ctl[i], PAD, TABS_H + S(rows[i]), S(250), S(30), TRUE);
                ShowWindow(ctl[i], (g_page == PAGE_ZAPRET && g_zap.valid)
                                       ? SW_SHOW : SW_HIDE);
                if (g_tip) {
                    ti.uId = (UINT_PTR)i;
                    /* Only on the zapret tab; elsewhere the same spot would
                       pop up a zapret tip over unrelated controls. */
                    if (g_page != PAGE_ZAPRET) {
                        SetRectEmpty(&ti.rect);
                        SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                        continue;
                    }
                    ti.rect.left   = PAD + S(258);
                    ti.rect.top    = TABS_H + S(rows[i]) + S(6);
                    ti.rect.right  = ti.rect.left + S(78);
                    ti.rect.bottom = ti.rect.top + S(18);
                    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
                }
            }
        }

        {
            int done = g_exc_known > 0 && g_exc_present >= g_exc_known;
            SetWindowTextW(g_zap_fix, done ? L"Адреса VPN добавлены в исключения"
                                           : L"Исправить конфликт VPN и zapret");
            MoveWindow(g_zap_fix, c.right - PAD - S(252), TABS_H + S(154),
                       S(252), S(30), TRUE);
            EnableWindow(g_zap_fix, !done && g_prof.count > 0);
        }

        {
            const wchar_t *sel = selected_strategy();
            /* Starting the strategy that already runs would be a no-op, so the
               button only wakes up on a different choice. */
            int same = sel && g_status.mode == ZAPRET_SERVICE &&
                       _wcsicmp(sel, g_status.strategy) == 0;

            EnableWindow(g_zap_stop,    g_status.mode != ZAPRET_OFF);
            EnableWindow(g_zap_restart, g_status.mode != ZAPRET_OFF);
            EnableWindow(g_zap_start,   sel != NULL && !same);
        }
    } else {
        SetWindowTextW(g_pick_path, L"Указать папку…");
        MoveWindow(g_pick_path, PAD, TABS_H + S(100), S(168), S(32), TRUE);
    }

    {
        int  ep = (g_page == PAGE_EDIT);
        int  field_w = c.right - PAD * 2 - S(84);
        int  i, y;

        for (i = 0; i < ED_ROWS; i++) {
            int showN = ep && i < g_ed_ncount;
            int showP = ep && i < g_ed_pcount;

            y = TABS_H + S(92) + i * S(36);
            MoveWindow(g_ed_name[i],   PAD, y, field_w, S(28), TRUE);
            MoveWindow(g_ed_nminus[i], PAD + field_w + S(6),  y, S(36), S(28), TRUE);
            MoveWindow(g_ed_nplus[i],  PAD + field_w + S(46), y, S(36), S(28), TRUE);
            ShowWindow(g_ed_name[i],   showN ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_nplus[i],  showN ? SW_SHOW : SW_HIDE);
            /* The first row of a section has no minus: a section is never empty. */
            ShowWindow(g_ed_nminus[i], showN && i > 0 ? SW_SHOW : SW_HIDE);

            y = ed_path_top() + i * S(36);
            MoveWindow(g_ed_path[i],   PAD, y, field_w, S(28), TRUE);
            MoveWindow(g_ed_pminus[i], PAD + field_w + S(6),  y, S(36), S(28), TRUE);
            MoveWindow(g_ed_pplus[i],  PAD + field_w + S(46), y, S(36), S(28), TRUE);
            ShowWindow(g_ed_path[i],   showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pplus[i],  showP ? SW_SHOW : SW_HIDE);
            ShowWindow(g_ed_pminus[i], showP && i > 0 ? SW_SHOW : SW_HIDE);
        }

        MoveWindow(g_ed_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_ed_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_ed_back, ep ? SW_SHOW : SW_HIDE);
        ShowWindow(g_ed_save, ep ? SW_SHOW : SW_HIDE);
    }

    {
        int pp = (g_page == PAGE_PICK) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_pk_search, PAD + S(62), TABS_H + S(66),
                   c.right - PAD * 2 - S(62), S(24), TRUE);
        MoveWindow(g_pk_list, PAD, TABS_H + S(100), c.right - PAD * 2,
                   c.bottom - FOOTER_H - S(12) - (TABS_H + S(100)), TRUE);
        MoveWindow(g_pk_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_pk_save, c.right - PAD - S(210), c.bottom - FOOTER_H + S(4),
                   S(210), S(30), TRUE);
        ShowWindow(g_pk_search, pp);
        ShowWindow(g_pk_list,   pp);
        ShowWindow(g_pk_back,   pp);
        ShowWindow(g_pk_save,   pp);
        EnableWindow(g_pk_save, g_pk_checked_n > 0);
    }

    {
        int hp = (g_page == PAGE_HOSTS) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_hedit, PAD + S(2), TABS_H + S(98), c.right - PAD * 2 - S(4),
                   c.bottom - FOOTER_H - S(14) - (TABS_H + S(98)), TRUE);
        MoveWindow(g_h_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_h_tidy, PAD + S(108), c.bottom - FOOTER_H + S(4),
                   S(150), S(30), TRUE);
        MoveWindow(g_h_save, c.right - PAD - S(130), c.bottom - FOOTER_H + S(4),
                   S(130), S(30), TRUE);
        ShowWindow(g_hedit,  hp);
        ShowWindow(g_h_back, hp);
        ShowWindow(g_h_tidy, hp);
        ShowWindow(g_h_save, hp);
    }

    {
        int ap = (g_page == PAGE_APPS) ? SW_SHOW : SW_HIDE;

        MoveWindow(g_alist, PAD, TABS_H + S(80), c.right - PAD * 2,
                   c.bottom - FOOTER_H - S(12) - (TABS_H + S(80)), TRUE);
        MoveWindow(g_app_back, PAD, c.bottom - FOOTER_H + S(4), S(100), S(30), TRUE);
        MoveWindow(g_app_pick, PAD + S(108), c.bottom - FOOTER_H + S(4),
                   S(210), S(30), TRUE);
        MoveWindow(g_app_manual, PAD + S(326), c.bottom - FOOTER_H + S(4),
                   S(160), S(30), TRUE);
        ShowWindow(g_alist,      ap);
        ShowWindow(g_app_back,   ap);
        ShowWindow(g_app_pick,   ap);
        ShowWindow(g_app_manual, ap);
    }

    {
        int on = (g_page == PAGE_ZAPRET && g_zap.valid) ? SW_SHOW : SW_HIDE;
        ShowWindow(g_list,        on);
        ShowWindow(g_zap_fix,     on);
        ShowWindow(g_zap_stop,    on);
        ShowWindow(g_zap_restart, on);
        ShowWindow(g_zap_start,   on);
    }

    ShowWindow(g_toggle,    g_page == PAGE_UTGARD ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pick_path, g_page == PAGE_ZAPRET ? SW_SHOW : SW_HIDE);

    /* While a job runs, every button that could start another is greyed.
       Done last so it overrides the ordinary rules above. */
    if (g_busy) {
        HWND busy_off[] = {
            g_toggle, g_zap_start, g_zap_stop, g_zap_restart, g_zap_fix,
            g_zap_game, g_zap_ipset, g_zap_ipupd, g_zap_hosts, g_pick_path, g_zap_list,
            g_h_save, g_h_tidy, g_h_back
        };
        size_t k;
        for (k = 0; k < sizeof busy_off / sizeof busy_off[0]; k++)
            if (busy_off[k]) EnableWindow(busy_off[k], FALSE);
    } else {
        EnableWindow(g_h_save, TRUE);
        EnableWindow(g_h_tidy, TRUE);
        EnableWindow(g_h_back, TRUE);
        EnableWindow(g_zap_game, TRUE);
        EnableWindow(g_zap_ipset, TRUE);
        EnableWindow(g_zap_ipupd, TRUE);
        EnableWindow(g_zap_hosts, TRUE);
        EnableWindow(g_pick_path, TRUE);
        EnableWindow(g_zap_list, TRUE);
    }

    /* children are not repainted by invalidating the parent */
    InvalidateRect(g_tab_utgard, NULL, TRUE);
    InvalidateRect(g_tab_zapret, NULL, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}
