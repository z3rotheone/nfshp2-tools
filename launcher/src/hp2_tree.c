/* Event trees: see hp2_tree.h. */
#include "hp2_tree.h"

#include <string.h>

#define CH_TREE   0x80034A00u
#define CH_NAME   0x00000001u
#define CH_LINKS  0x00034A03u
#define CH_EVENTS 0x00034A05u
#define REC       188
#define GRID_W    20
#define GRID_H    10

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The depth of event n: steps back along first links; -1 on a loop or a
 * missing event. */
static int depth_of(const hp2_tree *t, int n)
{
    int d = 0;
    while (n >= 1 && n <= t->count && t->ev[n - 1].number == n) {
        if (t->ev[n - 1].nlinks == 0) return d;
        n = t->ev[n - 1].links[0];
        if (++d > HP2_TREE_MAX_EVENTS) return -1;
    }
    return -1;
}

/* One 0x80034A00 chunk body; 1 if it gave a tree. */
static int parse_tree(const uint8_t *b, size_t sz, hp2_tree out[2])
{
    const uint8_t *links = NULL, *events = NULL;
    size_t nlinks = 0, nevents = 0, o = 0;
    int kind = -1, i;
    hp2_tree *t;
    while (o + 8 <= sz) {
        uint32_t id = rd32(b + o), csz = rd32(b + o + 4);
        if (csz > sz - o - 8) return 0;
        if (id == CH_NAME) {
            if (csz >= 12 && !memcmp(b + o + 8, "Championship", 12)) kind = 0;
            else if (csz >= 11 && !memcmp(b + o + 8, "Hot Pursuit", 11)) kind = 1;
        } else if (id == CH_LINKS) { links = b + o + 8; nlinks = csz / 8; }
        else if (id == CH_EVENTS) { events = b + o + 8; nevents = csz / REC; }
        o += 8 + csz;
    }
    if (kind < 0 || !events || out[kind].present) return 0;
    t = &out[kind];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    for (i = 0; (size_t)i < nevents; i++) {
        const uint8_t *r = events + (size_t)i * REC;
        int n = r[0x28], k, c = (signed char)r[0x3D];
        hp2_tree_event *e;
        if (n < 1 || n > HP2_TREE_MAX_EVENTS) continue;
        e = &t->ev[n - 1];
        if (e->number) continue;
        e->number = n;
        e->col = e->row = -1;
        if (c < 0) c = 0;
        if (c > HP2_TREE_MAX_LINKS) c = HP2_TREE_MAX_LINKS;
        if (0x7C + 2 * c > REC) c = (REC - 0x7C) / 2;
        for (k = 0; k < c; k++) {
            int li = (int16_t)rd16(r + 0x7C + 2 * k);
            if (li < 0 || (size_t)li >= nlinks) continue;
            e->links[e->nlinks++] = rd16(links + (size_t)li * 8 + 2);
        }
        if (n > t->count) t->count = n;
    }
    for (i = 0; i < t->count; i++)      /* events must run 1..count */
        if (t->ev[i].number != i + 1) { t->count = i; break; }
    for (i = 0; i < t->count; i++) t->ev[i].depth = depth_of(t, i + 1);
    t->present = t->count > 0;
    return t->present;
}

int hp2_tree_parse(const uint8_t *frontb, size_t len, hp2_tree t[2])
{
    size_t i;
    int found = 0;
    memset(t, 0, 2 * sizeof *t);
    if (!frontb) return 0;
    for (i = 0; i + 8 <= len; i += 4) {
        uint32_t sz;
        if (rd32(frontb + i) != CH_TREE) continue;
        sz = rd32(frontb + i + 4);
        if (sz < 8 || sz > len - i - 8) continue;
        if (parse_tree(frontb + i + 8, sz, t)) {
            found++;
            i += 8 + sz - 4;
        }
    }
    return found;
}

/* A 200-byte run of ' ' and 'x' with at least one of each. */
static int is_grid(const uint8_t *p)
{
    int k, x = 0;
    for (k = 0; k < GRID_W * GRID_H; k++) {
        if (p[k] == 'x') x++;
        else if (p[k] != ' ') return 0;
    }
    return x > 0 && x < GRID_W * GRID_H;
}

static int fill(hp2_tree *t, const uint8_t *g)
{
    int cell, n = 0;
    for (cell = 0; cell < GRID_W * GRID_H; cell++) {
        if (g[cell] != 'x') continue;
        if (n >= t->count) return 0;
        t->ev[n].col = cell % GRID_W;
        t->ev[n].row = cell / GRID_W;
        if (t->ev[n].row != t->ev[n].depth) return 0;
        n++;
    }
    return n == t->count;
}

int hp2_tree_grid(hp2_tree t[2], const uint8_t *exe, size_t len)
{
    size_t i;
    int k;
    if (!exe || !t[0].present || !t[1].present) return 0;
    for (i = 0; i + 208 + GRID_W * GRID_H <= len; i += 4) {
        if (!is_grid(exe + i) || !is_grid(exe + i + 208)) continue;
        if (fill(&t[0], exe + i) && fill(&t[1], exe + i + 208)) {
            t[0].has_grid = t[1].has_grid = 1;
            return 1;
        }
    }
    for (k = 0; k < 2; k++) {
        int n;
        for (n = 0; n < HP2_TREE_MAX_EVENTS; n++) t[k].ev[n].col = t[k].ev[n].row = -1;
        t[k].has_grid = 0;
    }
    return 0;
}

int hp2_tree_pos(const hp2_tree *t, int n, float *x, float *y)
{
    if (!t || !t->has_grid || n < 1 || n > t->count) return 0;
    *x = HP2_TREE_COL_UNITS * (float)t->ev[n - 1].col;
    *y = HP2_TREE_ROW_UNITS * (float)t->ev[n - 1].row;
    return 1;
}

/* Groups: a link joins its event (as a child) with the linked event (as a
 * parent). Slot n-1 is event n as a parent, slot MAX + n-1 as a child. */
static int find(int *p, int a) { while (p[a] != a) a = p[a] = p[p[a]]; return a; }

int hp2_tree_segments(const hp2_tree *t, hp2_tree_seg *out, int max)
{
    int parent[2 * HP2_TREE_MAX_EVENTS], used[2 * HP2_TREE_MAX_EVENTS];
    int i, k, r, n = 0;
    if (!t || !t->has_grid) return 0;
    for (i = 0; i < 2 * HP2_TREE_MAX_EVENTS; i++) { parent[i] = i; used[i] = 0; }
    for (i = 0; i < t->count; i++)
        for (k = 0; k < t->ev[i].nlinks; k++) {
            int p = t->ev[i].links[k] - 1, a, b;
            if (p < 0 || p >= t->count) continue;
            a = find(parent, p); b = find(parent, HP2_TREE_MAX_EVENTS + i);
            parent[a] = b;
            used[p] = used[HP2_TREE_MAX_EVENTS + i] = 1;
        }
    for (r = 0; r < 2 * HP2_TREE_MAX_EVENTS; r++) {
        float lo = 1e9f, hi = -1e9f, yp = 0.0f, yc = 0.0f, bar;
        int np = 0, nc = 0;
        if (find(parent, r) != r) continue;
        for (i = 0; i < 2 * HP2_TREE_MAX_EVENTS; i++) {
            const hp2_tree_event *e;
            float x;
            if (!used[i] || find(parent, i) != r) continue;
            e = &t->ev[i % HP2_TREE_MAX_EVENTS];
            x = HP2_TREE_COL_UNITS * (float)e->col;
            if (x < lo) lo = x;
            if (x > hi) hi = x;
            if (i < HP2_TREE_MAX_EVENTS) { yp = HP2_TREE_ROW_UNITS * (float)e->row; np++; }
            else { yc = HP2_TREE_ROW_UNITS * (float)e->row; nc++; }
        }
        if (!np || !nc) continue;
        bar = 0.5f * (yp + yc);
        if (hi > lo && n < max) { out[n].x0 = lo; out[n].y0 = bar; out[n].x1 = hi; out[n].y1 = bar; n++; }
        for (i = 0; i < 2 * HP2_TREE_MAX_EVENTS; i++) {
            const hp2_tree_event *e;
            float x, y;
            if (!used[i] || find(parent, i) != r) continue;
            e = &t->ev[i % HP2_TREE_MAX_EVENTS];
            x = HP2_TREE_COL_UNITS * (float)e->col;
            y = HP2_TREE_ROW_UNITS * (float)e->row;
            if (n < max) { out[n].x0 = x; out[n].y0 = y < bar ? y : bar; out[n].x1 = x; out[n].y1 = y < bar ? bar : y; n++; }
        }
    }
    return n;
}
