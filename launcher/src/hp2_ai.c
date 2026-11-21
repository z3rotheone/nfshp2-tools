/* hp2_ai - opponents and their driving line. See hp2_ai.h. */
#include "hp2_ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define G 9.81f
#define MAXNB 6

static float wrap_angle(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

/* ---- the driving line ---------------------------------------------------- */

#define CELL 2.0f               /* grid spacing, metres */
#define BAND 150.0f             /* how far from the route the grid reaches */
#define LAYERS 4                /* ground levels per cell (bridges, overhangs) */

typedef struct {
    int    w, h;
    float  x0, y0;
    int   *map;                 /* w x h x LAYERS: node or -1, highest first */
    int    n, cap;
    float *z;                   /* per node */
    int   *cell;
    int   *nb;                  /* 8 per node: the node a step away, or -1 */
    unsigned char *clear;       /* steps to the nearest edge of the road */
    unsigned char *big;         /* 1 on a stretch of ground of 2000 nodes or more and
                                   at least an eighth of the largest */
    float *off;                 /* distance from the main route */
} grid;

static const int DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

static void grid_free(grid *g)
{
    free(g->map);
    free(g->z);
    free(g->cell);
    free(g->nb);
    free(g->clear);
    free(g->big);
    free(g->off);
    memset(g, 0, sizeof *g);
}

static int add_node(grid *g, int cell, float z)
{
    if (g->n == g->cap) {
        int cap = g->cap ? 2 * g->cap : 4096;
        float *nz = realloc(g->z, sizeof(float) * cap);
        int *nc;
        if (!nz) return -1;
        g->z = nz;
        nc = realloc(g->cell, sizeof(int) * cap);
        if (!nc) return -1;
        g->cell = nc;
        g->cap = cap;
    }
    g->z[g->n] = z;
    g->cell[g->n] = cell;
    return g->n++;
}

static void node_xy(const grid *g, int v, float *x, float *y)
{
    *x = g->x0 + (g->cell[v] % g->w + 0.5f) * CELL;
    *y = g->y0 + (g->cell[v] / g->w + 0.5f) * CELL;
}

/* The ground near the main route as a grid of 2 m cells, up to four
 * levels per cell (a road under a bridge or an overhang), joined to the
 * cells around where the ground runs on without a step and no wall stands
 * between. */
static int grid_build(grid *g, const hp2_race *r, const hp2_coll *c)
{
    float len = r->loop ? r->len : r->route[3 * (r->n - 1) + 2], p, lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f }, xy[2];
    float *dist;
    int i, k, cells;
    memset(g, 0, sizeof *g);
    for (p = 0; p <= len; p += 10) {
        hp2_race_point(r, p, xy, NULL);
        for (k = 0; k < 2; k++) {
            if (xy[k] < lo[k]) lo[k] = xy[k];
            if (xy[k] > hi[k]) hi[k] = xy[k];
        }
    }
    g->x0 = lo[0] - BAND - CELL;
    g->y0 = lo[1] - BAND - CELL;
    g->w = (int)((hi[0] - lo[0] + 2 * BAND) / CELL) + 3;
    g->h = (int)((hi[1] - lo[1] + 2 * BAND) / CELL) + 3;
    cells = g->w * g->h;
    if (g->w <= 0 || g->h <= 0 || cells > 8 << 20) return -1;
    dist = malloc(sizeof(float) * cells);
    g->map = malloc(sizeof(int) * LAYERS * cells);
    if (!dist || !g->map) {
        free(dist);
        return -1;
    }
    for (i = 0; i < cells; i++) dist[i] = 1e30f;
    for (i = 0; i < LAYERS * cells; i++) g->map[i] = -1;
    for (p = 0; p <= len; p += CELL) {
        int cx, cy, x, y, rr = (int)(BAND / CELL);
        hp2_race_point(r, p, xy, NULL);
        cx = (int)((xy[0] - g->x0) / CELL);
        cy = (int)((xy[1] - g->y0) / CELL);
        for (y = cy - rr; y <= cy + rr; y++)
            for (x = cx - rr; x <= cx + rr; x++) {
                float dx, dy, d;
                if (x < 0 || y < 0 || x >= g->w || y >= g->h) continue;
                dx = g->x0 + (x + 0.5f) * CELL - xy[0];
                dy = g->y0 + (y + 0.5f) * CELL - xy[1];
                d = dx * dx + dy * dy;
                if (d < dist[y * g->w + x]) dist[y * g->w + x] = d;
            }
    }
    for (i = 0; i < cells; i++) {
        float x = g->x0 + (i % g->w + 0.5f) * CELL, y = g->y0 + (i / g->w + 0.5f) * CELL, zr = 1e4f, z;
        int sf, L;
        if (dist[i] > BAND * BAND) continue;
        for (L = 0; L < LAYERS; L++) {
            if (!hp2_coll_ground(c, x, y, zr, 0, &z, &sf, 0) || z > zr) break;
            g->map[LAYERS * i + L] = add_node(g, i, z);
            zr = z - 3;                 /* a car's height under it */
        }
    }
    g->nb = malloc(sizeof(int) * 8 * (g->n ? g->n : 1));
    g->clear = malloc(g->n ? g->n : 1);
    g->off = malloc(sizeof(float) * (g->n ? g->n : 1));
    if (!g->nb || !g->clear || !g->off || g->n < 2) {
        free(dist);
        grid_free(g);
        return -1;
    }
    for (i = 0; i < g->n; i++) g->off[i] = sqrtf(dist[g->cell[i]]);
    free(dist);
    for (i = 0; i < 8 * g->n; i++) g->nb[i] = -2;              /* -2: not worked out */
    for (i = 0; i < g->n; i++) {
        int cx = g->cell[i] % g->w, cy = g->cell[i] / g->w, d;
        float x, y;
        node_xy(g, i, &x, &y);
        for (d = 0; d < 8; d++) {
            int nx = cx + DX[d], ny = cy + DY[d], j, best = -1, L;
            float bd = (d & 1) ? 2.2f : 1.6f;                   /* the most a step may climb */
            if (g->nb[8 * i + d] != -2) continue;
            g->nb[8 * i + d] = -1;
            if (nx < 0 || ny < 0 || nx >= g->w || ny >= g->h) continue;
            for (L = 0; L < LAYERS; L++) {
                j = g->map[LAYERS * (ny * g->w + nx) + L];
                if (j >= 0 && fabsf(g->z[j] - g->z[i]) < bd) {
                    bd = fabsf(g->z[j] - g->z[i]);
                    best = j;
                }
            }
            if (best >= 0) {
                float x2, y2;
                node_xy(g, best, &x2, &y2);
                if (hp2_coll_blocked(c, x, y, x2, y2, g->z[i] < g->z[best] ? g->z[i] : g->z[best])) best = -1;
            }
            g->nb[8 * i + d] = best;
            if (best >= 0 && g->nb[8 * best + ((d + 4) & 7)] == -2) g->nb[8 * best + ((d + 4) & 7)] = i;
        }
    }
    /* how many steps each node is from the road's edge (a node short of a
     * neighbour), up to 15 */
    {
        int *q = malloc(sizeof(int) * g->n), qh = 0, qt = 0;
        if (!q) {
            grid_free(g);
            return -1;
        }
        for (i = 0; i < g->n; i++) {
            int d, edge = 0;
            for (d = 0; d < 8; d++) edge |= g->nb[8 * i + d] < 0;
            g->clear[i] = edge ? 0 : 255;
            if (edge) q[qt++] = i;
        }
        while (qh < qt) {
            int u = q[qh++], d;
            for (d = 0; d < 8; d += 2) {
                int v = g->nb[8 * u + d];
                if (v >= 0 && g->clear[v] == 255) {
                    g->clear[v] = (unsigned char)(g->clear[u] < 15 ? g->clear[u] + 1 : 15);
                    q[qt++] = v;
                }
            }
        }
        for (i = 0; i < g->n; i++)
            if (g->clear[i] == 255) g->clear[i] = 15;
        /* the stretches of ground joined together: the inside of a wall
         * round a pillar or a sign, or a shelf of terrain walled off from
         * the road, is not somewhere to drive to */
        g->big = calloc((size_t)g->n, 1);
        if (g->big) {
            int *mark = calloc((size_t)g->n, sizeof(int)), comp = 0, *size = NULL, most = 0;
            if (mark && (size = calloc((size_t)g->n + 1, sizeof(int)))) {
                for (i = 0; i < g->n; i++) {
                    int d;
                    if (mark[i]) continue;
                    comp++;
                    qh = qt = 0;
                    q[qt++] = i;
                    mark[i] = comp;
                    while (qh < qt) {
                        int u = q[qh++];
                        for (d = 0; d < 8; d++) {
                            int v = g->nb[8 * u + d];
                            if (v >= 0 && !mark[v]) {
                                mark[v] = comp;
                                q[qt++] = v;
                            }
                        }
                    }
                    size[comp] = qt;
                    if (qt > most) most = qt;
                }
                for (i = 0; i < g->n; i++) {
                    int sz = size[mark[i]];
                    g->big[i] = sz >= 2000 && sz >= most / 8;
                }
            }
            free(size);
            free(mark);
        }
        free(q);
    }
    return 0;
}

/* The node at (x, y) nearest height z, looking up to 3 cells round. */
static int grid_find(const grid *g, float x, float y, float z)
{
    int cx = (int)((x - g->x0) / CELL), cy = (int)((y - g->y0) / CELL), rr, dx, dy, L, best = -1;
    for (rr = 0; rr <= 3 && best < 0; rr++) {
        float bd = 1e30f;
        for (dy = -rr; dy <= rr; dy++)
            for (dx = -rr; dx <= rr; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= g->w || ny >= g->h) continue;
                for (L = 0; L < LAYERS; L++) {
                    int j = g->map[LAYERS * (ny * g->w + nx) + L];
                    float d;
                    if (j < 0 || (g->big && !g->big[j])) continue;
                    d = fabsf(g->z[j] - z) + (g->clear[j] ? 0 : 50) + (float)(dx * dx + dy * dy);
                    if (d < bd) {
                        bd = d;
                        best = j;
                    }
                }
            }
    }
    return best;
}

/* A small binary heap of (cost, node) for the search. */
typedef struct {
    int    n, cap;
    float *k;
    int   *v;
} heap;

static int hpush(heap *h, float k, int v)
{
    int i;
    if (h->n == h->cap) {
        int cap = h->cap ? 2 * h->cap : 4096;
        float *nk = realloc(h->k, sizeof(float) * cap);
        int *nv;
        if (!nk) return -1;
        h->k = nk;
        nv = realloc(h->v, sizeof(int) * cap);
        if (!nv) return -1;
        h->v = nv;
        h->cap = cap;
    }
    i = h->n++;
    while (i > 0 && h->k[(i - 1) / 2] > k) {
        h->k[i] = h->k[(i - 1) / 2];
        h->v[i] = h->v[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    h->k[i] = k;
    h->v[i] = v;
    return 0;
}

static int hpop(heap *h)
{
    int top = h->v[0], i = 0, v;
    float k;
    h->n--;
    k = h->k[h->n];
    v = h->v[h->n];
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && h->k[c + 1] < h->k[c]) c++;
        if (h->k[c] >= k) break;
        h->k[i] = h->k[c];
        h->v[i] = h->v[c];
        i = c;
    }
    if (h->n) {
        h->k[i] = k;
        h->v[i] = v;
    }
    return top;
}

typedef struct {
    int    n, cap;
    float *p;                   /* x, y, z, half */
} poly;

static void add(poly *p, float x, float y, float z, float half)
{
    if (p->n == p->cap) {
        int cap = p->cap ? 2 * p->cap : 256;
        float *t = realloc(p->p, sizeof(float) * 4 * cap);
        if (!t) return;
        p->p = t;
        p->cap = cap;
    }
    p->p[4 * p->n] = x;
    p->p[4 * p->n + 1] = y;
    p->p[4 * p->n + 2] = z;
    p->p[4 * p->n + 3] = half;
    p->n++;
}

/* The way from node a to node b: A*, each step costing its length times
 * (1 + (distance from the main route past 30 m / 20 m)^2) (so it keeps to
 * the route's road, not a shortcut) and more near the road's edge (so it
 * keeps to the middle); never through a node at the edge (a gap narrower
 * than three cells is no way for a car). Adds the nodes after a, up to
 * and including b, to out. */
static int way(const grid *g, int a, int b, float *cost, int *prev, int *stamp, int tag, heap *h, poly *out)
{
    float bx, by;
    int u, m = 0, i, *tmp;
    node_xy(g, b, &bx, &by);
    h->n = 0;
    stamp[a] = tag;
    cost[a] = 0;
    prev[a] = -1;
    {
        float ax, ay;
        node_xy(g, a, &ax, &ay);
        if (hpush(h, sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by)), a)) return -1;
    }
    while (h->n) {
        int d;
        float ux, uy;
        u = hpop(h);
        if (u == b) break;
        node_xy(g, u, &ux, &uy);
        for (d = 0; d < 8; d++) {
            int v = g->nb[8 * u + d];
            float f, c, vx, vy;
            if (v < 0 || (!g->clear[v] && v != b)) continue;     /* a car's width off the edge */
            f = g->off[v] > 30 ? (g->off[v] - 30) / 20 : 0;
            c = cost[u] + ((d & 1) ? 1.4142f : 1.0f) * CELL * (1 + f * f) * (1 + 4.0f / (1 + g->clear[v]));
            if (stamp[v] == tag && c >= cost[v]) continue;
            stamp[v] = tag;
            cost[v] = c;
            prev[v] = u;
            node_xy(g, v, &vx, &vy);
            if (hpush(h, c + sqrtf((vx - bx) * (vx - bx) + (vy - by) * (vy - by)), v)) return -1;
        }
    }
    if (stamp[b] != tag) return -1;
    for (i = b; i != a; i = prev[i]) m++;
    tmp = malloc(sizeof(int) * (m ? m : 1));
    if (!tmp) return -1;
    for (i = b, u = m; i != a; i = prev[i]) tmp[--u] = i;
    for (i = 0; i < m; i++) {
        float x, y;
        node_xy(g, tmp[i], &x, &y);
        add(out, x, y, g->z[tmp[i]], (g->clear[tmp[i]] + 0.5f) * CELL);
    }
    free(tmp);
    return m;
}

/* The line over the grid: from the route every 150 m to the next. */
static int along_ground(poly *out, const hp2_race *r, const hp2_coll *c)
{
    grid g;
    heap h = { 0, 0, NULL, NULL };
    float len = r->loop ? r->len : r->route[3 * (r->n - 1) + 2], p, z = 0, *cost = NULL;
    int *prev = NULL, *stamp = NULL, *wp = NULL, nwp = 0, i, ok = 0;
    if (grid_build(&g, r, c)) return 0;
    cost = malloc(sizeof(float) * g.n);
    prev = malloc(sizeof(int) * g.n);
    stamp = calloc((size_t)g.n, sizeof(int));
    wp = malloc(sizeof(int) * ((size_t)(len / 150) + 3));
    if (!cost || !prev || !stamp || !wp) goto done;
    {
        float xy[2];
        int sf;
        hp2_race_point(r, 0, xy, NULL);
        hp2_coll_ground(c, xy[0], xy[1], 1e4f, 0, &z, &sf, 0);
    }
    /* a waypoint where the route has ground under it for 150 m either way
     * (it runs off the collision ground where it cuts a bend or crosses
     * between roads, and a waypoint at the edge of that would pull the line
     * the wrong way at a fork) */
    for (p = 0; p < len; p += 150) {
        float xy[2], q;
        int v, ok2 = 1;
        for (q = p - 150; q <= p + 150 && ok2; q += 4) {
            int cx, cy, L;
            if (!r->loop && (q < 0 || q > len)) continue;
            hp2_race_point(r, q, xy, NULL);
            cx = (int)((xy[0] - g.x0) / CELL);
            cy = (int)((xy[1] - g.y0) / CELL);
            for (ok2 = 0, L = 0; L < LAYERS && !ok2; L++) ok2 = g.map[LAYERS * (cy * g.w + cx) + L] >= 0;
        }
        if (!ok2) continue;
        hp2_race_point(r, p, xy, NULL);
        v = grid_find(&g, xy[0], xy[1], z);
        if (v < 0) continue;
        z = g.z[v];
        if (!nwp || wp[nwp - 1] != v) wp[nwp++] = v;
    }
    if (!r->loop) {
        float xy[2];
        int v;
        hp2_race_point(r, len, xy, NULL);
        v = grid_find(&g, xy[0], xy[1], z);
        if (v >= 0 && wp[nwp - 1] != v) wp[nwp++] = v;
    }
    if (nwp < 2) goto done;
    if (r->loop) wp[nwp++] = wp[0];
    {
        float x, y;
        node_xy(&g, wp[0], &x, &y);
        add(out, x, y, g.z[wp[0]], (g.clear[wp[0]] + 0.5f) * CELL);
    }
    {
        int from = wp[0], tag = 0;
        for (i = 1; i < nwp; i++) {
            int to = wp[i], got = way(&g, from, to, cost, prev, stamp, ++tag, &h, out), L;
            /* not joined: try the cell's other levels (the waypoint may sit
             * on terrain over the road), then the next waypoint */
            for (L = 0; got < 0 && L < LAYERS; L++) {
                int j = g.map[LAYERS * g.cell[wp[i]] + L];
                if (j < 0 || j == wp[i] || (g.big && !g.big[j])) continue;
                if ((got = way(&g, from, j, cost, prev, stamp, ++tag, &h, out)) >= 0) to = j;
            }
            if (got < 0 && i + 1 < nwp) continue;
            if (got < 0) {
                /* no way through at all: the line jumps (to the start, on a circuit) */
                float x, y;
                node_xy(&g, to, &x, &y);
                add(out, x, y, g.z[to], CELL);
            }
            from = to;
        }
    }
    if (r->loop && out->n > 1) out->n--;     /* the last is the first again */
    ok = out->n >= 2;
done:
    free(cost);
    free(prev);
    free(stamp);
    free(wp);
    free(h.k);
    free(h.v);
    grid_free(&g);
    return ok;
}

static void smooth(hp2_ai_line *l, int passes)
{
    int j, k;
    float *t = malloc(sizeof(float) * 2 * l->n);
    if (!t) return;
    for (j = 0; j < passes; j++) {
        for (k = 0; k < l->n; k++) {
            int a = k - 1, b = k + 1;
            if (l->loop) {
                a = (a + l->n) % l->n;
                b %= l->n;
            } else {
                if (a < 0) a = 0;
                if (b >= l->n) b = l->n - 1;
            }
            t[2 * k] = 0.25f * l->pt[4 * a] + 0.5f * l->pt[4 * k] + 0.25f * l->pt[4 * b];
            t[2 * k + 1] = 0.25f * l->pt[4 * a + 1] + 0.5f * l->pt[4 * k + 1] + 0.25f * l->pt[4 * b + 1];
        }
        for (k = 0; k < l->n; k++) {
            l->pt[4 * k] = t[2 * k];
            l->pt[4 * k + 1] = t[2 * k + 1];
        }
    }
    free(t);
}

int hp2_ai_line_build(hp2_ai_line *l, const hp2_race *r, const hp2_coll *c)
{
    poly p = { 0, 0, NULL };
    float total, *cum;
    int i, k, j;
    memset(l, 0, sizeof *l);
    if (r->n < 2) return -1;
    if (!c || !along_ground(&p, r, c)) {
        float len = r->loop ? r->len : r->route[3 * (r->n - 1) + 2], xy[2], s;
        p.n = 0;
        for (s = 0; s < len; s += HP2_AI_STEP) {
            hp2_race_point(r, s, xy, NULL);
            add(&p, xy[0], xy[1], 0, 6);
        }
    }
    if (p.n < 2) {
        free(p.p);
        return -1;
    }
    /* resample every HP2_AI_STEP metres */
    cum = malloc(sizeof(float) * (p.n + 1));
    if (!cum) {
        free(p.p);
        return -1;
    }
    cum[0] = 0;
    for (i = 1; i <= p.n; i++) {
        int a = i - 1, b = i % p.n;
        float dx = p.p[4 * b] - p.p[4 * a], dy = p.p[4 * b + 1] - p.p[4 * a + 1];
        cum[i] = cum[i - 1] + sqrtf(dx * dx + dy * dy);
    }
    total = r->loop ? cum[p.n] : cum[p.n - 1];
    l->loop = r->loop;
    l->n = (int)(total / HP2_AI_STEP) + (r->loop ? 0 : 1);
    if (l->n < 2) l->n = 2;
    l->len = r->loop ? l->n * HP2_AI_STEP : (l->n - 1) * HP2_AI_STEP;
    l->pt = malloc(sizeof(float) * 4 * l->n);
    if (!l->pt) {
        free(cum);
        free(p.p);
        l->n = 0;
        return -1;
    }
    for (k = 0, i = 0; k < l->n; k++) {
        float at = total * k / (r->loop ? l->n : (l->n - 1)), f;
        int a, b, last = r->loop ? p.n : p.n - 1;
        while (i + 1 < last && cum[i + 1] < at) i++;
        a = i;
        b = (i + 1) % p.n;
        f = cum[i + 1] > cum[i] ? (at - cum[i]) / (cum[i + 1] - cum[i]) : 0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        for (j = 0; j < 4; j++) l->pt[4 * k + j] = p.p[4 * a + j] + (p.p[4 * b + j] - p.p[4 * a + j]) * f;
    }
    free(cum);
    free(p.p);
    smooth(l, 4);               /* the grid's stair steps */
    return 0;
}

void hp2_ai_line_free(hp2_ai_line *l)
{
    free(l->pt);
    memset(l, 0, sizeof *l);
}

static int idx(const hp2_ai_line *l, int k)
{
    if (l->loop) return ((k % l->n) + l->n) % l->n;
    return k < 0 ? 0 : k >= l->n ? l->n - 1 : k;
}

void hp2_ai_line_point(const hp2_ai_line *l, float s, float xy[2], float dir[2], float *half)
{
    float f, dx, dy, d;
    int a, b, c0, c1;
    if (l->loop) {
        s = fmodf(s, l->len);
        if (s < 0) s += l->len;
    } else if (s < 0) s = 0;
    f = s / HP2_AI_STEP;
    a = (int)floorf(f);
    f -= a;
    if (!l->loop && a >= l->n - 1) {
        a = l->n - 1;
        f = 0;
    }
    a = idx(l, a);
    b = idx(l, a + 1);
    xy[0] = l->pt[4 * a] + (l->pt[4 * b] - l->pt[4 * a]) * f;
    xy[1] = l->pt[4 * a + 1] + (l->pt[4 * b + 1] - l->pt[4 * a + 1]) * f;
    if (half) *half = l->pt[4 * a + 3] + (l->pt[4 * b + 3] - l->pt[4 * a + 3]) * f;
    if (dir) {
        c0 = idx(l, a - 1);
        c1 = idx(l, a + 2);
        dx = l->pt[4 * c1] - l->pt[4 * c0];
        dy = l->pt[4 * c1 + 1] - l->pt[4 * c0 + 1];
        d = sqrtf(dx * dx + dy * dy);
        dir[0] = d > 0 ? dx / d : 1;
        dir[1] = d > 0 ? dy / d : 0;
    }
}

float hp2_ai_line_find(const hp2_ai_line *l, float x, float y, float z, float from)
{
    int k0, k1, k, best = 0;
    float bd = 1e30f;
    if (!l->n) return 0;
    if (from < 0) {
        k0 = 0;
        k1 = l->n - 1;
    } else {
        k0 = (int)((from - 40) / HP2_AI_STEP);
        k1 = (int)((from + 80) / HP2_AI_STEP);
    }
    for (k = k0; k <= k1; k++) {
        const float *p = l->pt + 4 * idx(l, k);
        float dx = p[0] - x, dy = p[1] - y, dz = (p[2] - z) * 2, d = dx * dx + dy * dy + dz * dz;
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    if (l->loop) return (float)(((best % l->n) + l->n) % l->n) * HP2_AI_STEP;
    return (float)idx(l, best) * HP2_AI_STEP;
}

/* ---- the driver ----------------------------------------------------------- */

/* The fastest the line allows from s to `ahead` metres on: v = sqrt(mu g R)
 * from how much it turns over each 12 m, and what braking from there would
 * need (v^2 = vb^2 + 2 a d). */
static float speed_limit(const hp2_ai_line *l, float s, float ahead, float grip)
{
    float best = 200, t, xy[2], d0[2], d1[2];
    for (t = 8; t < ahead; t += 12) {
        float turn, radius, v, here;
        hp2_ai_line_point(l, s + t, xy, d0, NULL);
        hp2_ai_line_point(l, s + t + 12, xy, d1, NULL);
        turn = fabsf(wrap_angle(atan2f(d1[1], d1[0]) - atan2f(d0[1], d0[0])));
        radius = turn > 1e-3f ? 12 / turn : 1e4f;
        v = sqrtf(grip * G * radius);
        here = sqrtf(v * v + 2 * 0.8f * grip * G * t);       /* what braking from here reaches */
        if (here < best) best = here;
    }
    return best;
}

void hp2_ai_drive(hp2_ai *ai, const hp2_race *r, const hp2_ai_line *l, const hp2_drive *d, float dt, hp2_drive_in *in)
{
    float v = d->speed, look = 8 + 0.45f * fabsf(v), xy[2], dir[2], tx, ty, want, err, limit, half, lane;
    memset(in, 0, sizeof *in);
    if (!l || !l->n) return;
    ai->at = hp2_ai_line_find(l, d->pos[0], d->pos[1], d->pos[2], ai->at);
    if (r->state != HP2_RACE_RUNNING) {
        in->brake = 1;
        return;
    }
    /* stuck against something: back out for a moment */
    if (ai->reverse > 0) {
        ai->reverse -= dt;
        in->brake = 1;
        hp2_ai_line_point(l, ai->at + look, xy, dir, NULL);
        err = wrap_angle(atan2f(xy[1] - d->pos[1], xy[0] - d->pos[0]) - d->heading);
        in->steer = err > 0 ? 1.0f : -1.0f;          /* backing up turns the other way */
        return;
    }
    if (fabsf(v) < 1.0f) ai->stuck += dt;
    else ai->stuck = 0;
    if (ai->stuck > 1.5f) {
        ai->stuck = 0;
        ai->reverse = 1.2f;
    }
    hp2_ai_line_point(l, ai->at + look, xy, dir, &half);
    lane = ai->lane;
    if (lane > half - 1.5f) lane = half - 1.5f;     /* keep a car's width off the edge */
    if (lane < -(half - 1.5f)) lane = -(half - 1.5f);
    if (half < 1.5f) lane = 0;
    tx = xy[0] - dir[1] * lane;               /* left of the line is (-dy, dx) */
    ty = xy[1] + dir[0] * lane;
    want = atan2f(ty - d->pos[1], tx - d->pos[0]);
    err = wrap_angle(want - d->heading);
    in->steer = -err * 2.5f;
    if (in->steer > 1) in->steer = 1;
    if (in->steer < -1) in->steer = -1;
    {
        /* the grip it can count on: the weaker axle's sliding friction
         * (0.85 of it; 1.25 for the 911's 1.47), 1.25 without a car */
        float mu = d->tyre[0][4] < d->tyre[1][4] ? d->tyre[0][4] : d->tyre[1][4];
        if (d->arcade || mu <= 0) mu = 1.47f;
        limit = speed_limit(l, ai->at, 40 + v * 3, 0.85f * mu * ai->skill);
    }
    if (fabsf(err) > 0.6f && limit > 20) limit = 20;                 /* well off line: steady */
    if (v < limit - 1) in->throttle = 1;
    else if (v > limit + 2) in->brake = v > limit + 6 ? 1.0f : 0.5f;
    else in->throttle = 0.3f;
    /* sliding: ease off (a driver's traction control) */
    if (fabsf(v) > 5) {
        float slip = fabsf(wrap_angle(atan2f(d->vel[1], d->vel[0]) - d->heading));
        if (slip > 0.12f && in->throttle > 0.2f) in->throttle = slip > 0.25f ? 0 : 0.2f;
    }
}
