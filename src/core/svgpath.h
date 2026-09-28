#ifndef UTGARD_SVGPATH_H
#define UTGARD_SVGPATH_H

/* SVG path data (the "d" attribute) walked into moves, lines and cubic
   curves. Arcs become cubics, so any 2-D drawing API can take the result;
   the Windows window hands it to GDI+ for anti-aliased icons. No Windows
   headers: tested on the host (tests/test_svgpath.c).
   Handles M L H V C A Z in both cases and implicit repeats - the commands
   the Lucide icons in the window use. S, Q and T are refused. */

typedef struct {
    void (*move)(void *ctx, float x, float y);
    void (*line)(void *ctx, float x, float y);
    void (*cubic)(void *ctx, float x1, float y1, float x2, float y2, float x, float y);
    void (*close)(void *ctx);
} svgpath_sink;

/* 1 when the whole string was understood, 0 at the first thing it was not. */
int svgpath_walk(const char *d, const svgpath_sink *sink, void *ctx);

#endif
