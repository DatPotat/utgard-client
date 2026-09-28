/*
 * Utgard client - SVG path data into moves, lines and cubics. See svgpath.h.
 */

#include "svgpath.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *skip(const char *p)
{
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int number(const char **pp, float *out)
{
    char *end;
    const char *p = skip(*pp);
    double v = strtod(p, &end);
    if (end == p) return 0;
    *out = (float)v;
    *pp = end;
    return 1;
}

/* Arc flags are single characters and may run together ("a2 2 0 011 4"). */
static int flag(const char **pp, int *out)
{
    const char *p = skip(*pp);
    if (*p != '0' && *p != '1') return 0;
    *out = *p - '0';
    *pp = p + 1;
    return 1;
}

static int starts_number(const char *p)
{
    p = skip(p);
    return (*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.';
}

/* SVG 1.1 appendix F.6.5 (endpoint to centre parameterisation), then each
   piece of at most 90 degrees becomes one cubic. */
static void arc(const svgpath_sink *s, void *ctx, float x1, float y1, float rx, float ry,
                float phi_deg, int large, int sweep, float x2, float y2)
{
    double phi = phi_deg * M_PI / 180.0, cp = cos(phi), sp = sin(phi);
    double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
    double x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    double lam, num, den, coef, cxp, cyp, cx, cy, t1, dt, ux, uy, vx, vy;
    int    n, i;

    rx = fabsf(rx); ry = fabsf(ry);
    if (rx == 0 || ry == 0 || (x1 == x2 && y1 == y2)) { s->line(ctx, x2, y2); return; }
    lam = (x1p * x1p) / ((double)rx * rx) + (y1p * y1p) / ((double)ry * ry);
    if (lam > 1) { rx *= (float)sqrt(lam); ry *= (float)sqrt(lam); }
    num = (double)rx * rx * ry * ry - (double)rx * rx * y1p * y1p - (double)ry * ry * x1p * x1p;
    den = (double)rx * rx * y1p * y1p + (double)ry * ry * x1p * x1p;
    coef = den > 0 && num > 0 ? sqrt(num / den) : 0;
    if (large == sweep) coef = -coef;
    cxp = coef * rx * y1p / ry;
    cyp = -coef * ry * x1p / rx;
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0;
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0;
    ux = (x1p - cxp) / rx; uy = (y1p - cyp) / ry;
    vx = (-x1p - cxp) / rx; vy = (-y1p - cyp) / ry;
    t1 = atan2(uy, ux);
    dt = atan2(vy, vx) - t1;
    if (sweep && dt < 0) dt += 2 * M_PI;
    if (!sweep && dt > 0) dt -= 2 * M_PI;

    n = (int)ceil(fabs(dt) / (M_PI / 2) - 1e-9);
    if (n < 1) n = 1;
    for (i = 0; i < n; i++) {
        double a0 = t1 + dt * i / n, a1 = t1 + dt * (i + 1) / n, h = 4.0 / 3.0 * tan((a1 - a0) / 4);
        double c0 = cos(a0), s0 = sin(a0), c1 = cos(a1), s1 = sin(a1);
        double p1x = c0 - h * s0, p1y = s0 + h * c0, p2x = c1 + h * s1, p2y = s1 - h * c1;
#define TX(X, Y) (float)(cx + cp * rx * (X) - sp * ry * (Y))
#define TY(X, Y) (float)(cy + sp * rx * (X) + cp * ry * (Y))
        s->cubic(ctx, TX(p1x, p1y), TY(p1x, p1y), TX(p2x, p2y), TY(p2x, p2y),
                 i == n - 1 ? x2 : TX(c1, s1), i == n - 1 ? y2 : TY(c1, s1));
#undef TX
#undef TY
    }
}

int svgpath_walk(const char *d, const svgpath_sink *s, void *ctx)
{
    const char *p = d;
    float x = 0, y = 0, sx = 0, sy = 0;
    char  cmd = 0;

    for (;;) {
        int   rel;
        p = skip(p);
        if (!*p) return 1;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
        else if (!cmd || !starts_number(p)) return 0;
        rel = cmd >= 'a';
        switch (cmd | 0x20) {
        case 'm': {
            float a, b;
            if (!number(&p, &a) || !number(&p, &b)) return 0;
            x = rel ? x + a : a; y = rel ? y + b : b;
            sx = x; sy = y;
            s->move(ctx, x, y);
            cmd = rel ? 'l' : 'L';          /* pairs after a move are lines */
            break;
        }
        case 'l': {
            float a, b;
            if (!number(&p, &a) || !number(&p, &b)) return 0;
            x = rel ? x + a : a; y = rel ? y + b : b;
            s->line(ctx, x, y);
            break;
        }
        case 'h': {
            float a;
            if (!number(&p, &a)) return 0;
            x = rel ? x + a : a;
            s->line(ctx, x, y);
            break;
        }
        case 'v': {
            float a;
            if (!number(&p, &a)) return 0;
            y = rel ? y + a : a;
            s->line(ctx, x, y);
            break;
        }
        case 'c': {
            float v[6];
            int   k;
            for (k = 0; k < 6; k++) if (!number(&p, &v[k])) return 0;
            if (rel) for (k = 0; k < 6; k += 2) { v[k] += x; v[k + 1] += y; }
            s->cubic(ctx, v[0], v[1], v[2], v[3], v[4], v[5]);
            x = v[4]; y = v[5];
            break;
        }
        case 'a': {
            float rx, ry, rot, ex, ey;
            int   large, sweep;
            if (!number(&p, &rx) || !number(&p, &ry) || !number(&p, &rot) ||
                !flag(&p, &large) || !flag(&p, &sweep) || !number(&p, &ex) || !number(&p, &ey))
                return 0;
            if (rel) { ex += x; ey += y; }
            arc(s, ctx, x, y, rx, ry, rot, large, sweep, ex, ey);
            x = ex; y = ey;
            break;
        }
        case 'z':
            s->close(ctx);
            x = sx; y = sy;
            cmd = 0;
            break;
        default:
            return 0;
        }
    }
}
