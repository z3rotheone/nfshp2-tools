/* hp2_raster - a small software triangle rasteriser with a depth buffer, for the 3D car of the front end.
 *
 * SDL's 2D renderer has no depth buffer, so the car's triangles had to be sorted back to front, which fails for a
 * car (interior seen through the bodywork, panels over panels). This draws them with a z-buffer instead:
 * perspective-correct textures with mip maps, Gouraud colour, opaque triangles first (depth test and write), then
 * blended ones (test only). The result is premultiplied RGBA. No SDL, plain C99. */
#ifndef HP2_RASTER_H
#define HP2_RASTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_RTEX_LEVELS 12

typedef struct {
    int      w, h, n;                       /* level 0 size, levels made */
    int      lw[HP2_RTEX_LEVELS], lh[HP2_RTEX_LEVELS];
    uint8_t *lev[HP2_RTEX_LEVELS];          /* RGBA, row 0 on top */
} hp2_rtex;

/* Copies rgba (w x h) and builds its mip chain (2x2 box filter, premultiplied so clear texels do not bleed).
 * 0, or -1 when out of memory. */
int  hp2_rtex_make(hp2_rtex *t, const uint8_t *rgba, int w, int h);
void hp2_rtex_free(hp2_rtex *t);

typedef struct {
    int      w, h;
    uint8_t *px;                            /* w * h * 4, premultiplied RGBA, row 0 on top */
    float   *z;                             /* 1 / depth; larger is nearer, 0 = nothing yet */
} hp2_rast;

int  hp2_rast_init(hp2_rast *r, int w, int h);      /* 0, or -1 */
void hp2_rast_free(hp2_rast *r);
void hp2_rast_clear(hp2_rast *r);                   /* transparent black, depth cleared */

typedef struct {
    float x, y;                             /* pixels, y down */
    float z;                                /* view depth, > 0 */
    float u, v;                             /* 0..1 across the texture, wrapping; v 0 = top row */
    float r, g, b, a;                       /* 0..1 (colour multiplies the texture) */
} hp2_rvert;

void hp2_rast_clear_z(hp2_rast *r);                 /* forget the depths (the colours stay) */
/* A soft elliptical glow, premultiplied over what is there: colour (cr, cg, cb) at strength a0 in the middle,
 * falling to nothing at the rim (alpha a0 * (1 - d^2) for d the distance in radii). */
void hp2_rast_glow(hp2_rast *r, float cx, float cy, float rx, float ry, float cr, float cg, float cb, float a0);

/* One triangle, both windings. tex may be NULL (flat colour). blend 0: opaque (the texel's alpha is ignored);
 * 1: alpha-blended over what is there, depth-tested but not written; 2: blended, and the depth written too
 * (a translucent object whose own triangles must not stack). */
void hp2_rast_tri(hp2_rast *r, const hp2_rvert v[3], const hp2_rtex *tex, int blend);

#ifdef __cplusplus
}
#endif
#endif
