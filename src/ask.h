#ifndef UTGARD_ASK_H
#define UTGARD_ASK_H

#include <windows.h>

/* Button kinds the caller's painter understands; stored in GWLP_USERDATA of
   each button exactly as the main window does it. */
#define ASK_BTN_PRIMARY   1
#define ASK_BTN_SECONDARY 2

typedef void (*ask_draw_button_fn)(const DRAWITEMSTRUCT *d);
typedef int  (*ask_metric_fn)(int logical_pixels);

/* Hand the prompt the main window's painter, scaling and theme objects, so
   there is one place that knows what a button looks like. */
void ask_configure(ask_draw_button_fn draw, ask_metric_fn scale,
                   HFONT font, HFONT small_font,
                   HBRUSH bg, HBRUSH surface, HBRUSH line,
                   COLORREF text, COLORREF muted, COLORREF surface_color);

/* The theme, shared with other modal windows so there is one place that
   knows what the client looks like. */
enum { ASK_BG = 0, ASK_SURFACE, ASK_LINE, ASK_TEXT, ASK_MUTED,
       ASK_OK, ASK_WARN, ASK_ACCENT, ASK_COLOR_COUNT };

void     ask_configure_colors(const COLORREF *colors);   /* ASK_COLOR_COUNT of them */
COLORREF ask_color(int which);
HFONT    ask_font(int small_one);
HBRUSH   ask_brush(int which);                           /* ASK_BG/SURFACE/LINE */

/* Hover for owner-drawn buttons. Windows gives an owner-drawn button no hot
   state of its own, so each one is subclassed: the pointer entering and
   leaving is tracked, and the painter asks whether it is over the button. */
void ask_hover_attach(HWND button);
int  ask_is_hot(HWND button);

/* Vertically centre the text of a single-line edit. The control keeps its
   full rectangle; its client area is shrunk top and bottom by half the spare
   height and the margin painted in the field colour. */
void ask_edit_center(HWND edit);


/* Modal single-field prompt. Returns 1 when the user accepted a non-empty
   value, 0 on cancel or an empty field. */
int ask_string(HWND owner, const wchar_t *title, const wchar_t *hint,
               const wchar_t *initial, wchar_t *out, size_t cap);

#endif
