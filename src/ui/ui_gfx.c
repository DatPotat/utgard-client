/*
 * Utgard client - Smooth shapes: rounded rectangles, discs and the Lucide
 * icons, anti-aliased through GDI+.
 *
 * gdiplus.dll is not a KnownDLL, so an ordinary import would be looked for
 * next to utgard.exe first - the DLL planting noted in the security debts.
 * It is loaded from System32 only, by name, at start. Should that fail the
 * same shapes are drawn with plain GDI: rougher edges, nothing missing.
 */

#define UTGARD_ICON_PATHS          /* this file draws the outlines */
#include "ui.h"
#include <gdiplus.h>
#include "svgpath.h"

#define GP(fn) static __typeof__(fn) *p_##fn
GP(GdiplusStartup);        GP(GdiplusShutdown);
GP(GdipCreateFromHDC);     GP(GdipDeleteGraphics);
GP(GdipSetSmoothingMode);  GP(GdipSetPixelOffsetMode);
GP(GdipCreatePath);        GP(GdipDeletePath);
GP(GdipStartPathFigure);   GP(GdipClosePathFigure);
GP(GdipAddPathLine);       GP(GdipAddPathBezier);  GP(GdipAddPathArc);
GP(GdipCreateSolidFill);   GP(GdipDeleteBrush);    GP(GdipFillPath);
GP(GdipCreatePen1);        GP(GdipDeletePen);      GP(GdipDrawPath);
GP(GdipSetPenLineCap197819); GP(GdipSetPenLineJoin);
GP(GdipFillEllipse);       GP(GdipWidenPath);
#undef GP

static ULONG_PTR g_token;
static int       g_gp;
static HMODULE   g_gp_module;

void gfx_startup(void)
{
    HMODULE             m = LoadLibraryExW(L"gdiplus.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    GdiplusStartupInput in = { 1, NULL, FALSE, FALSE };

    if (!m) return;
    g_gp_module = m;
#define LOAD(fn) if (!(p_##fn = (__typeof__(p_##fn))(void *)GetProcAddress(m, #fn))) { gfx_shutdown(); return; }
    LOAD(GdiplusStartup);       LOAD(GdiplusShutdown);
    LOAD(GdipCreateFromHDC);    LOAD(GdipDeleteGraphics);
    LOAD(GdipSetSmoothingMode); LOAD(GdipSetPixelOffsetMode);
    LOAD(GdipCreatePath);       LOAD(GdipDeletePath);
    LOAD(GdipStartPathFigure);  LOAD(GdipClosePathFigure);
    LOAD(GdipAddPathLine);      LOAD(GdipAddPathBezier);  LOAD(GdipAddPathArc);
    LOAD(GdipCreateSolidFill);  LOAD(GdipDeleteBrush);    LOAD(GdipFillPath);
    LOAD(GdipCreatePen1);       LOAD(GdipDeletePen);      LOAD(GdipDrawPath);
    LOAD(GdipSetPenLineCap197819); LOAD(GdipSetPenLineJoin);
    LOAD(GdipFillEllipse);      LOAD(GdipWidenPath);
#undef LOAD
    g_gp = p_GdiplusStartup(&g_token, &in, NULL) == Ok;
    if (!g_gp) gfx_shutdown();
}

void gfx_shutdown(void)
{
    if (g_gp) p_GdiplusShutdown(g_token);
    g_gp = 0;
    if (g_gp_module) FreeLibrary(g_gp_module);
    g_gp_module = NULL;
}

static ARGB argb(COLORREF c)
{
    return 0xFF000000u | ((ARGB)GetRValue(c) << 16) | ((ARGB)GetGValue(c) << 8) | GetBValue(c);
}

/* The pages paint with the viewport origin moved past the sidebar. Whether
   GDI+ honours that origin is not something to lean on: it is taken out of
   the DC for the time GDI+ draws and applied as a translation instead. */
typedef struct { GpGraphics *g; POINT org; float ox, oy; } gp_ctx;

static int begin(HDC dc, gp_ctx *c)
{
    GetViewportOrgEx(dc, &c->org);
    SetViewportOrgEx(dc, 0, 0, NULL);
    c->ox = (float)c->org.x;
    c->oy = (float)c->org.y;
    if (p_GdipCreateFromHDC(dc, &c->g) != Ok) {
        SetViewportOrgEx(dc, c->org.x, c->org.y, NULL);
        return 0;
    }
    p_GdipSetSmoothingMode(c->g, SmoothingModeAntiAlias);
    p_GdipSetPixelOffsetMode(c->g, PixelOffsetModeHalf);   /* pixel i covers [i, i+1] */
    return 1;
}

static void end(HDC dc, gp_ctx *c)
{
    p_GdipDeleteGraphics(c->g);
    SetViewportOrgEx(dc, c->org.x, c->org.y, NULL);
}

/* Strokes are widened into outlines and filled rather than drawn with the
   pen: a fill is anti-aliased everywhere GDI+ runs, while pen drawing was
   seen coming out stepped under Wine. On Windows both go through the same
   rasteriser, so the result there is the same either way. */
static void stroke_path(gp_ctx *c, GpPath *p, GpPen *pen, COLORREF color)
{
    GpSolidFill *b = NULL;
    if (p_GdipWidenPath(p, pen, NULL, 0.1f) != Ok) { p_GdipDrawPath(c->g, pen, p); return; }
    p_GdipCreateSolidFill(argb(color), &b);
    p_GdipFillPath(c->g, (GpBrush *)b, p);
    p_GdipDeleteBrush((GpBrush *)b);
}

static void add_round(GpPath *p, float x, float y, float w, float h, float r)
{
    float d;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    d = r * 2;
    p_GdipStartPathFigure(p);
    if (d <= 0) {
        p_GdipAddPathLine(p, x, y, x + w, y);
        p_GdipAddPathLine(p, x + w, y, x + w, y + h);
        p_GdipAddPathLine(p, x + w, y + h, x, y + h);
    } else {
        p_GdipAddPathArc(p, x, y, d, d, 180, 90);
        p_GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
        p_GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
        p_GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    }
    p_GdipClosePathFigure(p);
}

/* radius is the corner radius, in device pixels. */
void rounded_r(HDC dc, const RECT *r, COLORREF fillc, COLORREF border, int radius)
{
    gp_ctx c;
    float  x = (float)r->left, y = (float)r->top;
    float  w = (float)(r->right - r->left), h = (float)(r->bottom - r->top);

    if (w <= 0 || h <= 0) return;
    if (g_gp && begin(dc, &c)) {
        GpPath      *p = NULL;
        GpSolidFill *b = NULL;
        x += c.ox; y += c.oy;
        p_GdipCreatePath(FillModeAlternate, &p);
        add_round(p, x, y, w, h, (float)radius);
        p_GdipCreateSolidFill(argb(fillc), &b);
        p_GdipFillPath(c.g, (GpBrush *)b, p);
        p_GdipDeleteBrush((GpBrush *)b);
        p_GdipDeletePath(p);
        if (border != fillc) {
            float  lw = (float)S(1);
            GpPen *pen = NULL;
            p_GdipCreatePath(FillModeAlternate, &p);
            add_round(p, x + lw / 2, y + lw / 2, w - lw, h - lw, (float)radius - lw / 2);
            p_GdipCreatePen1(argb(border), lw, UnitPixel, &pen);
            stroke_path(&c, p, pen, border);
            p_GdipDeletePen(pen);
            p_GdipDeletePath(p);
        }
        end(dc, &c);
        return;
    }
    {
        HBRUSH  br  = CreateSolidBrush(fillc);
        HPEN    pen = CreatePen(PS_SOLID, S(1), border);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        RoundRect(dc, r->left, r->top, r->right, r->bottom, radius * 2, radius * 2);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(br); DeleteObject(pen);
    }
}

void dot(HDC dc, int cx, int cy, int r, COLORREF color)
{
    gp_ctx c;
    if (g_gp && begin(dc, &c)) {
        GpSolidFill *b = NULL;
        p_GdipCreateSolidFill(argb(color), &b);
        p_GdipFillEllipse(c.g, (GpBrush *)b, cx - r + c.ox, cy - r + c.oy, (float)(2 * r), (float)(2 * r));
        p_GdipDeleteBrush((GpBrush *)b);
        end(dc, &c);
        return;
    }
    {
        HBRUSH  br  = CreateSolidBrush(color);
        HPEN    pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(br); DeleteObject(pen);
    }
}

/* ---- icons ------------------------------------------------------------- */

typedef struct { GpPath *p; HDC dc; float x0, y0, s, cx, cy; } sink_ctx;

static void gp_move(void *v, float x, float y)
{
    sink_ctx *k = v;
    p_GdipStartPathFigure(k->p);
    k->cx = k->x0 + x * k->s; k->cy = k->y0 + y * k->s;
}
static void gp_line(void *v, float x, float y)
{
    sink_ctx *k = v;
    float nx = k->x0 + x * k->s, ny = k->y0 + y * k->s;
    p_GdipAddPathLine(k->p, k->cx, k->cy, nx, ny);
    k->cx = nx; k->cy = ny;
}
static void gp_cubic(void *v, float a, float b, float c, float d, float x, float y)
{
    sink_ctx *k = v;
    float nx = k->x0 + x * k->s, ny = k->y0 + y * k->s;
    p_GdipAddPathBezier(k->p, k->cx, k->cy, k->x0 + a * k->s, k->y0 + b * k->s,
                        k->x0 + c * k->s, k->y0 + d * k->s, nx, ny);
    k->cx = nx; k->cy = ny;
}
static void gp_close(void *v) { p_GdipClosePathFigure(((sink_ctx *)v)->p); }

static void gdi_move(void *v, float x, float y)
{
    sink_ctx *k = v;
    MoveToEx(k->dc, (int)(k->x0 + x * k->s + 0.5f), (int)(k->y0 + y * k->s + 0.5f), NULL);
}
static void gdi_line(void *v, float x, float y)
{
    sink_ctx *k = v;
    LineTo(k->dc, (int)(k->x0 + x * k->s + 0.5f), (int)(k->y0 + y * k->s + 0.5f));
}
static void gdi_cubic(void *v, float a, float b, float c, float d, float x, float y)
{
    sink_ctx *k = v;
    POINT p[3];
    p[0].x = (LONG)(k->x0 + a * k->s + 0.5f); p[0].y = (LONG)(k->y0 + b * k->s + 0.5f);
    p[1].x = (LONG)(k->x0 + c * k->s + 0.5f); p[1].y = (LONG)(k->y0 + d * k->s + 0.5f);
    p[2].x = (LONG)(k->x0 + x * k->s + 0.5f); p[2].y = (LONG)(k->y0 + y * k->s + 0.5f);
    PolyBezierTo(k->dc, p, 3);
}
static void gdi_close(void *v) { CloseFigure(((sink_ctx *)v)->dc); }

/* A Lucide icon in a size x size box at (x, y): the 24-unit grid scaled to
   the box, the 2-unit stroke scaled with it, round ends and joins. */
void gfx_icon(HDC dc, int icon, int x, int y, int size, COLORREF color)
{
    sink_ctx k;
    float    stroke;
    gp_ctx   c;

    if (icon < 0 || icon >= ICON_COUNT || size <= 0) return;
    k.s = size / 24.0f;
    stroke = 2.0f * k.s;
    if (g_gp && begin(dc, &c)) {
        static const svgpath_sink SINK = { gp_move, gp_line, gp_cubic, gp_close };
        GpPen *pen = NULL;
        k.x0 = x + c.ox; k.y0 = y + c.oy; k.dc = dc;
        p_GdipCreatePath(FillModeAlternate, &k.p);
        svgpath_walk(ICON_PATH[icon], &SINK, &k);
        p_GdipCreatePen1(argb(color), stroke, UnitPixel, &pen);
        p_GdipSetPenLineCap197819(pen, LineCapRound, LineCapRound, DashCapRound);
        p_GdipSetPenLineJoin(pen, LineJoinRound);
        stroke_path(&c, k.p, pen, color);
        p_GdipDeletePen(pen);
        p_GdipDeletePath(k.p);
        end(dc, &c);
        return;
    }
    {
        static const svgpath_sink SINK = { gdi_move, gdi_line, gdi_cubic, gdi_close };
        LOGBRUSH lb = { BS_SOLID, color, 0 };
        HPEN     pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                                    (DWORD)(stroke + 0.5f), &lb, 0, NULL);
        HGDIOBJ  op = SelectObject(dc, pen);
        k.x0 = (float)x; k.y0 = (float)y; k.dc = dc;
        BeginPath(dc);
        svgpath_walk(ICON_PATH[icon], &SINK, &k);
        EndPath(dc);
        StrokePath(dc);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
}
