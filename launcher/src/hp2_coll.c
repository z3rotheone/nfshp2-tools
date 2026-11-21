/* hp2_coll - collision mesh. See hp2_coll.h and tools/hp2_scenery.py. */
#include "hp2_coll.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CH_CELLS 0x00034131u
#define CH_POLYS 0x00034132u
#define CH_INDEX 0x00034133u
#define EMPTY    0xFFFFFFFFu

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p)
{
    uint32_t w = rd32(p);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static const uint8_t *body(const uint8_t *buf, size_t len, uint32_t id, const uint8_t **end)
{
    size_t o = 0;
    while (o + 8 <= len) {
        uint32_t c = rd32(buf + o), sz = rd32(buf + o + 4);
        if (sz > len - o - 8) return NULL;
        if (c == id) {
            const uint8_t *b = buf + o + 8;
            *end = b + sz;
            while (b + 4 <= *end && rd32(b) == 0x11111111u) b += 4;
            return b;
        }
        o += 8 + sz;
    }
    return NULL;
}

static uint32_t key_of(int cx, int cy) { return (uint32_t)(cx + 32768) << 16 | (uint32_t)(cy + 32768); }
static uint32_t slot_of(uint32_t k, int size) { return (k * 2654435761u) & (uint32_t)(size - 1); }
static int cell_coord(float v) { return (int)floorf(v / 32.0f); }

static int find_cell(const hp2_coll *c, int cx, int cy)
{
    uint32_t k = key_of(cx, cy), s;
    if (!c->cell_size) return -1;
    s = slot_of(k, c->cell_size);
    while (c->cell_key[s] != EMPTY) {
        if (c->cell_key[s] == k) return (int)s;
        s = (s + 1) & (uint32_t)(c->cell_size - 1);
    }
    return -1;
}

int hp2_coll_parse(hp2_coll *c, const uint8_t *buf, size_t len)
{
    const uint8_t *pe, *ie, *ce, *p, *ix, *cl;
    int i, k;
    memset(c, 0, sizeof *c);
    p = body(buf, len, CH_POLYS, &pe);
    ix = body(buf, len, CH_INDEX, &ie);
    cl = body(buf, len, CH_CELLS, &ce);
    if (!p || !ix || !cl) return -1;
    c->npoly = (int)((pe - p) / 32);
    c->poly = (hp2_cpoly *)calloc((size_t)(c->npoly ? c->npoly : 1), sizeof *c->poly);
    c->nindex = (int)((ie - ix) / 2);
    c->index = (uint16_t *)malloc((size_t)(c->nindex ? c->nindex : 1) * 2);
    if (!c->poly || !c->index) goto fail;
    for (i = 0; i < c->nindex; i++) c->index[i] = rd16(ix + 2 * i);
    for (i = 0; i < c->npoly; i++) {
        const uint8_t *r = p + 32 * i;
        hp2_cpoly *q = &c->poly[i];
        int base = (int16_t)rd16(r + 4);
        float ax, ay, az, bx, by, bz, nx, ny, nz, l;
        q->flags = rd16(r + 2);
        for (k = 0; k < 4; k++) {
            q->x[k] = (int16_t)rd16(r + 8 + 2 * k) / 8.0f;
            q->y[k] = (int16_t)rd16(r + 16 + 2 * k) / 8.0f;
            q->z[k] = base + (int16_t)rd16(r + 24 + 2 * k) / 256.0f;
        }
        q->nv = (q->x[3] == q->x[2] && q->y[3] == q->y[2] && q->z[3] == q->z[2]) ? 3 : 4;
        for (k = 0; k < 3; k++) { q->lo[k] = 1e30f; q->hi[k] = -1e30f; }
        for (k = 0; k < q->nv; k++) {
            const float v[3] = { q->x[k], q->y[k], q->z[k] };
            int j;
            for (j = 0; j < 3; j++) {
                if (v[j] < q->lo[j]) q->lo[j] = v[j];
                if (v[j] > q->hi[j]) q->hi[j] = v[j];
            }
        }
        /* normal from the diagonals (Newell would do the same for planar quads) */
        ax = q->x[2] - q->x[0]; ay = q->y[2] - q->y[0]; az = q->z[2] - q->z[0];
        bx = q->x[q->nv == 4 ? 3 : 1] - q->x[q->nv == 4 ? 1 : 0];
        by = q->y[q->nv == 4 ? 3 : 1] - q->y[q->nv == 4 ? 1 : 0];
        bz = q->z[q->nv == 4 ? 3 : 1] - q->z[q->nv == 4 ? 1 : 0];
        nx = ay * bz - az * by;
        ny = az * bx - ax * bz;
        nz = ax * by - ay * bx;
        l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 0) { nx /= l; ny /= l; nz /= l; }
        if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }
        q->n[0] = nx; q->n[1] = ny; q->n[2] = nz;
        q->wall = (q->flags & 0xFF00) == 0x1600 || (q->flags & 0xFF00) == 0x1200 || nz < 0.3f;
    }
    c->ncell = (int)((ce - cl - 12) / 12);
    if (c->ncell < 0) c->ncell = 0;
    c->cell_size = 64;
    while (c->cell_size < c->ncell * 2) c->cell_size *= 2;
    c->cell_key = (uint32_t *)malloc((size_t)c->cell_size * 4);
    c->cell_start = (uint16_t *)malloc((size_t)c->cell_size * 2);
    c->cell_count = (uint16_t *)malloc((size_t)c->cell_size * 2);
    if (!c->cell_key || !c->cell_start || !c->cell_count) goto fail;
    memset(c->cell_key, 0xFF, (size_t)c->cell_size * 4);
    for (i = 0; i < c->ncell; i++) {
        const uint8_t *r = cl + 12 + 12 * i;
        uint32_t k2 = key_of(cell_coord(rdf(r)), cell_coord(rdf(r + 4)));
        uint32_t s = slot_of(k2, c->cell_size);
        while (c->cell_key[s] != EMPTY && c->cell_key[s] != k2) s = (s + 1) & (uint32_t)(c->cell_size - 1);
        c->cell_key[s] = k2;
        c->cell_start[s] = rd16(r + 8);
        c->cell_count[s] = rd16(r + 10);
    }
    return 0;
fail:
    hp2_coll_free(c);
    return -1;
}

void hp2_coll_free(hp2_coll *c)
{
    free(c->poly);
    free(c->index);
    free(c->cell_key);
    free(c->cell_start);
    free(c->cell_count);
    memset(c, 0, sizeof *c);
}

/* Height of triangle (a, b, c) of q at (x, y), if the point is inside. */
static int tri_height(const hp2_cpoly *q, int a, int b, int cc, float x, float y, float *z)
{
    float d = (q->x[b] - q->x[a]) * (q->y[cc] - q->y[a]) - (q->x[cc] - q->x[a]) * (q->y[b] - q->y[a]);
    float u, v, e = 1e-4f;
    if (fabsf(d) < 1e-6f) return 0;
    u = ((x - q->x[a]) * (q->y[cc] - q->y[a]) - (q->x[cc] - q->x[a]) * (y - q->y[a])) / d;
    v = ((q->x[b] - q->x[a]) * (y - q->y[a]) - (x - q->x[a]) * (q->y[b] - q->y[a])) / d;
    if (u < -e || v < -e || u + v > 1 + e) return 0;
    *z = q->z[a] + u * (q->z[b] - q->z[a]) + v * (q->z[cc] - q->z[a]);
    return 1;
}

int hp2_coll_ground(const hp2_coll *c, float x, float y, float zref, float step,
                    float *z, int *surface, float n[3])
{
    int s = find_cell(c, cell_coord(x), cell_coord(y)), i, best = -1, above = -1;
    float bz = -1e30f, az = 1e30f, h;
    if (s < 0) return 0;
    for (i = 0; i < c->cell_count[s]; i++) {
        int k = c->cell_start[s] + i, pi;
        const hp2_cpoly *q;
        if (k >= c->nindex) break;
        pi = c->index[k];
        if (pi >= c->npoly) continue;
        q = &c->poly[pi];
        if (q->wall || x < q->lo[0] || x > q->hi[0] || y < q->lo[1] || y > q->hi[1]) continue;
        if (!tri_height(q, 0, 1, 2, x, y, &h) && !(q->nv == 4 && tri_height(q, 0, 2, 3, x, y, &h))) continue;
        if (h <= zref + step) {
            if (h > bz) { bz = h; best = pi; }
        } else if (h < az) {
            az = h;
            above = pi;
        }
    }
    if (best < 0) { best = above; bz = az; }
    if (best < 0) return 0;
    *z = bz;
    if (surface) *surface = c->poly[best].flags;
    if (n) memcpy(n, c->poly[best].n, sizeof c->poly[best].n);
    return 1;
}

int hp2_coll_push(const hp2_coll *c, float *x, float *y, float z, float r, float n[2])
{
    int moved = 0, dx, dy, i;
    int cx = cell_coord(*x), cy = cell_coord(*y);
    for (dx = -1; dx <= 1; dx++)
        for (dy = -1; dy <= 1; dy++) {
            int s = find_cell(c, cx + dx, cy + dy);
            if (s < 0) continue;
            for (i = 0; i < c->cell_count[s]; i++) {
                int k = c->cell_start[s] + i, a, b, j, m;
                const hp2_cpoly *q;
                float best = -1, sx, sy, ex, ey, lx, ly, t, px, py, d, l2;
                if (k >= c->nindex || c->index[k] >= c->npoly) continue;
                q = &c->poly[c->index[k]];
                if (!q->wall) continue;
                /* most invisible walls run from -127 to +127, all the encoding
                 * allows: they stand at any height (track 44 is at z = 290) */
                if (q->hi[2] - q->lo[2] < 250 && (z + 0.5f > q->hi[2] || z + 2.0f < q->lo[2])) continue;
                if (*x + r < q->lo[0] || *x - r > q->hi[0] || *y + r < q->lo[1] || *y - r > q->hi[1]) continue;
                /* the wall's footprint: its two furthest vertices in x, y */
                a = 0;
                b = 1;
                for (j = 0; j < q->nv; j++)
                    for (m = j + 1; m < q->nv; m++) {
                        float e = (q->x[j] - q->x[m]) * (q->x[j] - q->x[m]) + (q->y[j] - q->y[m]) * (q->y[j] - q->y[m]);
                        if (e > best) { best = e; a = j; b = m; }
                    }
                sx = q->x[a]; sy = q->y[a]; ex = q->x[b]; ey = q->y[b];
                lx = ex - sx; ly = ey - sy;
                l2 = lx * lx + ly * ly;
                if (l2 < 1e-6f) continue;
                t = ((*x - sx) * lx + (*y - sy) * ly) / l2;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                px = sx + t * lx;
                py = sy + t * ly;
                d = sqrtf((*x - px) * (*x - px) + (*y - py) * (*y - py));
                if (d >= r) continue;
                if (d > 1e-4f) {
                    n[0] = (*x - px) / d;
                    n[1] = (*y - py) / d;
                } else {
                    float ll = sqrtf(l2);
                    n[0] = -ly / ll;
                    n[1] = lx / ll;
                }
                *x = px + n[0] * r;
                *y = py + n[1] * r;
                moved = 1;
            }
        }
    return moved;
}

static int crosses(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    float d1 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), d2 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
    float d3 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx), d4 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
    return d1 * d2 < 0 && d3 * d4 < 0;
}

int hp2_coll_blocked(const hp2_coll *c, float ax, float ay, float bx, float by, float z)
{
    float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay)), t;
    int lastx = 1 << 30, lasty = 1 << 30;
    for (t = 0; t <= len + 16; t += 16) {
        float f = len > 0 ? (t > len ? 1 : t / len) : 0;
        int cx = cell_coord(ax + (bx - ax) * f), cy = cell_coord(ay + (by - ay) * f), dx, dy, i;
        if (cx == lastx && cy == lasty) continue;
        lastx = cx;
        lasty = cy;
        for (dx = -1; dx <= 1; dx++)
            for (dy = -1; dy <= 1; dy++) {
                int s = find_cell(c, cx + dx, cy + dy);
                if (s < 0) continue;
                for (i = 0; i < c->cell_count[s]; i++) {
                    int k = c->cell_start[s] + i, a = 0, b = 1, j, m;
                    const hp2_cpoly *q;
                    float best = -1;
                    if (k >= c->nindex || c->index[k] >= c->npoly) continue;
                    q = &c->poly[c->index[k]];
                    if (!q->wall) continue;
                    if (q->hi[2] - q->lo[2] < 250 && (z + 0.5f > q->hi[2] || z + 2.0f < q->lo[2])) continue;
                    for (j = 0; j < q->nv; j++)
                        for (m = j + 1; m < q->nv; m++) {
                            float e = (q->x[j] - q->x[m]) * (q->x[j] - q->x[m]) + (q->y[j] - q->y[m]) * (q->y[j] - q->y[m]);
                            if (e > best) { best = e; a = j; b = m; }
                        }
                    if (crosses(ax, ay, bx, by, q->x[a], q->y[a], q->x[b], q->y[b])) return 1;
                }
            }
    }
    return 0;
}
