/* svgpath.c: the icon outlines, checked by their points. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "svgpath.h"
#define UTGARD_ICON_PATHS
#include "../src/ui/icons_lucide.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { float pts[64][2]; int n, moves, closes; float minx, maxx, miny, maxy; } rec;

static void add(rec *r, float x, float y)
{
    if (r->n < 64) { r->pts[r->n][0] = x; r->pts[r->n][1] = y; r->n++; }
    if (x < r->minx) r->minx = x;
    if (x > r->maxx) r->maxx = x;
    if (y < r->miny) r->miny = y;
    if (y > r->maxy) r->maxy = y;
}
static void mv(void *c, float x, float y) { ((rec *)c)->moves++; add(c, x, y); }
static void ln(void *c, float x, float y) { add(c, x, y); }
static void cu(void *c, float a, float b, float d, float e, float x, float y)
{
    /* a point on the curve at t = 0.5 keeps the box honest */
    rec *r = c;
    float px = r->n ? r->pts[r->n - 1][0] : 0, py = r->n ? r->pts[r->n - 1][1] : 0;
    add(c, (px + 3 * a + 3 * d + x) / 8, (py + 3 * b + 3 * e + y) / 8);
    add(c, x, y);
}
static void cl(void *c) { ((rec *)c)->closes++; }
static const svgpath_sink SINK = { mv, ln, cu, cl };

static int walk(const char *d, rec *r)
{
    memset(r, 0, sizeof *r);
    r->minx = r->miny = 1e9f; r->maxx = r->maxy = -1e9f;
    return svgpath_walk(d, &SINK, r);
}

#define NEAR(a, b) (fabsf((a) - (b)) < 0.02f)

int main(void)
{
    rec r;
    int i;

    CHECK(walk(ICON_PATH[ICON_CHECK], &r));                 /* M20 6 9 17l-5-5 */
    CHECK(r.n == 3 && NEAR(r.pts[1][0], 9) && NEAR(r.pts[1][1], 17) &&
          NEAR(r.pts[2][0], 4) && NEAR(r.pts[2][1], 12));

    CHECK(walk(ICON_PATH[ICON_CHEVRON_RIGHT], &r));         /* was m9 18 6-6-6-6 */
    CHECK(r.n == 3 && NEAR(r.pts[1][0], 15) && NEAR(r.pts[1][1], 12) &&
          NEAR(r.pts[2][0], 9) && NEAR(r.pts[2][1], 6));

    CHECK(walk(ICON_PATH[ICON_X], &r));                     /* two strokes, crossing */
    CHECK(r.moves == 2 && NEAR(r.pts[1][0], 6) && NEAR(r.pts[1][1], 18) &&
          NEAR(r.pts[2][0], 6) && NEAR(r.pts[2][1], 6) && NEAR(r.pts[3][0], 18) && NEAR(r.pts[3][1], 18));

    CHECK(walk(ICON_PATH[ICON_CIRCLE], &r));                /* r = 10 around (12, 12) */
    CHECK(r.closes == 1 && fabsf(r.minx - 2) < 0.3f && fabsf(r.maxx - 22) < 0.3f &&
          fabsf(r.miny - 2) < 0.3f && fabsf(r.maxy - 22) < 0.3f);

    for (i = 0; i < ICON_COUNT; i++) {                      /* every icon parses, inside the grid */
        CHECK(walk(ICON_PATH[i], &r));
        CHECK(r.minx > -0.5f && r.miny > -0.5f && r.maxx < 24.5f && r.maxy < 24.5f);
    }
    CHECK(!walk("M0 0 Q 1 1 2 2", &r));                     /* unsupported: refused */
    CHECK(walk("M1 1a1 1 0 011 1", &r) && NEAR(r.pts[r.n - 1][0], 2) && NEAR(r.pts[r.n - 1][1], 2));

    if (fails) { printf("svgpath: %d FAILED\n", fails); return 1; }
    printf("svgpath: ok\n");
    return 0;
}
