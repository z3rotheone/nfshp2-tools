/* hp2_coll - a track's collision mesh: the ground under a point and the
 * walls around it. The read side of the collision part of
 * tools/hp2_scenery.py in C; docs/formats.md section 11, "Collision".
 *
 * 0x34132 holds polygons (3 or 4 vertices, heights in 1/256 units over a
 * base), 0x34131 a 32-unit grid of cells listing them through 0x34133.
 * A polygon whose surface word is 0x12xx / 0x16xx is an invisible wall;
 * one that spans the encoding's whole range (-127..127, most of them)
 * stands at every height. Any polygon steeper than about 73 degrees
 * (normal z < 0.3) is treated as a wall too. No SDL, plain C99.
 */
#ifndef HP2_COLL_H
#define HP2_COLL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    x[4], y[4], z[4];
    int      nv;                /* 3 or 4 */
    uint16_t flags;             /* the surface word; low nibble = surface */
    uint8_t  wall;
    float    n[3];              /* unit normal, z >= 0 for ground */
    float    lo[3], hi[3];
} hp2_cpoly;

typedef struct {
    int        npoly;
    hp2_cpoly *poly;
    int        ncell;
    uint32_t  *cell_key;        /* open-addressed: (cx, cy) packed, or 0xFFFFFFFF */
    uint16_t  *cell_start, *cell_count;
    int        cell_size;       /* slots, a power of two */
    int        nindex;
    uint16_t  *index;
} hp2_coll;

/* 0, or -1 when the chunks are not there or memory runs out. */
int  hp2_coll_parse(hp2_coll *c, const uint8_t *buf, size_t len);
void hp2_coll_free(hp2_coll *c);

/* The ground under (x, y): of the walkable polygons there, the highest
 * whose height is at most zref + step (or the lowest above, if none).
 * 1 and *z, *surface (the surface word), n (the normal) when found; 0 when
 * no ground is under the point. */
int hp2_coll_ground(const hp2_coll *c, float x, float y, float zref, float step,
                    float *z, int *surface, float n[3]);

/* Pushes a circle (centre x, y, radius r, at height z) out of every wall it
 * overlaps. Returns 1 if it moved, with the last wall's outward normal in
 * n (x, y). */
int hp2_coll_push(const hp2_coll *c, float *x, float *y, float z, float r, float n[2]);

/* 1 if the straight line from (ax, ay) to (bx, by) at height z crosses a
 * wall (for opponents looking for a way past). */
int hp2_coll_blocked(const hp2_coll *c, float ax, float ay, float bx, float by, float z);

#ifdef __cplusplus
}
#endif
#endif
