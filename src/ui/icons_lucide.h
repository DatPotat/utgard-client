/*
 * Utgard client - Icon outlines from Lucide (https://lucide.dev), taken from
 * github.com/lucide-icons/lucide, branch main, icons/<name>.svg, on 2026-09-28.
 * ISC License, Copyright (c) 2026 Lucide Icons and Contributors; icons
 * derived from Feather are also MIT, Copyright (c) 2013-present Cole Bemis
 * (licenses/THIRD-PARTY-NOTICES.txt). Each icon is one SVG path on a 24x24
 * grid, drawn with a 2-unit round stroke. When this file was made, circle,
 * line and rect elements were rewritten as path commands, and a path that
 * opened with "m" (an absolute first pair, relative lines after it) was
 * written as "M x y l ..." so the paths could be joined.
 */

#ifndef UTGARD_ICONS_LUCIDE_H
#define UTGARD_ICONS_LUCIDE_H

enum {
    ICON_POWER,
    ICON_SERVER,
    ICON_ROUTE,
    ICON_SHIELD,
    ICON_SETTINGS,
    ICON_CHEVRON_RIGHT,
    ICON_CHEVRON_DOWN,
    ICON_TRASH,
    ICON_X,
    ICON_FOLDER_OPEN,
    ICON_PLUS,
    ICON_CHECK,
    ICON_CIRCLE,
    ICON_CIRCLE_DASHED,
    ICON_CIRCLE_ALERT,
    ICON_SEARCH,
    ICON_COUNT
};

/* The outlines themselves only where they are drawn (ui_gfx.c) and tested:
   an unused static array elsewhere is a warning, and warnings are errors. */
#ifdef UTGARD_ICON_PATHS
static const char *const ICON_PATH[ICON_COUNT] = {
    "M12 2v10 M18.4 6.6a9 9 0 1 1-12.77.04",
    "M4 2H20A2 2 0 0 1 22 4V8A2 2 0 0 1 20 10H4A2 2 0 0 1 2 8V4A2 2 0 0 1 4 2z M4 14H20A2 2 0 0 1 22 16V20A2 2 0 0 1 20 22H4A2 2 0 0 1 2 20V16A2 2 0 0 1 4 14z M6 6L6.01 6 M6 18L6.01 18",
    "M3 19a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z M9 19h8.5a3.5 3.5 0 0 0 0-7h-11a3.5 3.5 0 0 1 0-7H15 M15 5a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z",
    "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z",
    "M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915 M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z",
    "M9 18l6-6-6-6",
    "M6 9l6 6 6-6",
    "M10 11v6 M14 11v6 M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6 M3 6h18 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2",
    "M18 6 6 18 M6 6l12 12",
    "M6 14l1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2",
    "M5 12h14 M12 5v14",
    "M20 6 9 17l-5-5",
    "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0z",
    "M10.1 2.182a10 10 0 0 1 3.8 0 M13.9 21.818a10 10 0 0 1-3.8 0 M17.609 3.721a10 10 0 0 1 2.69 2.7 M2.182 13.9a10 10 0 0 1 0-3.8 M20.279 17.609a10 10 0 0 1-2.7 2.69 M21.818 10.1a10 10 0 0 1 0 3.8 M3.721 6.391a10 10 0 0 1 2.7-2.69 M6.391 20.279a10 10 0 0 1-2.69-2.7",
    "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0z M12 8L12 12 M12 16L12.01 16",
    "M21 21l-4.34-4.34 M3 11a8 8 0 1 0 16 0a8 8 0 1 0 -16 0z"
};
#endif

#endif
