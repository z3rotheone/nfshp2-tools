/* hp2_scene - placed track scenery. See hp2_scene.h and tools/hp2_scenery.py. */
#include "hp2_scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CH_SECTION  0x80034100u
#define CH_SEC_MOD  0x00034102u
#define CH_SEC_INST 0x00034103u
#define CH_SEC_HDR  0x00034101u
#define CH_ROUTES   0x80034147u
#define CH_ROUTE    0x00034148u
#define CH_RPOINT   0x00034149u
#define CH_FOG      0x00034250u

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static float rdf(const uint8_t *p)
{
    uint32_t w = rd32(p);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static const uint8_t *skip_fill(const uint8_t *p, const uint8_t *e)
{
    while (p + 4 <= e && rd32(p) == 0x11111111u) p += 4;
    return p;
}

/* The body of child `id` of a container, past filler; NULL if absent. */
static const uint8_t *child(const uint8_t *b, const uint8_t *e, uint32_t id, const uint8_t **end)
{
    while (b + 8 <= e) {
        uint32_t c = rd32(b), sz = rd32(b + 4);
        if (sz > (size_t)(e - b - 8)) return NULL;
        if (c == id) {
            *end = b + 8 + sz;
            return skip_fill(b + 8, *end);
        }
        b += 8 + sz;
    }
    return NULL;
}

/* ---- decoded models, cached by hash ------------------------------------ */

typedef struct {
    uint32_t hash;
    int      state;             /* 0 not tried, 1 decoded, 2 none */
    int      role;              /* ROLE_* from the part's name */
    hp2_mesh m;
} cmesh;

typedef struct {
    int    n, cap;
    cmesh *c;
} mcache;

enum { ROLE_SCENERY, ROLE_SKY, ROLE_ENVMAP };

/* SKYDOME and PANORAMA are the sky: drawn behind everything, without fog.
 * SKYDOME_ENVMAP is a second dome, probably the cars' reflection map; it
 * is not drawn. */
static int role_of(const char *name)
{
    if (!strcmp(name, "SKYDOME_ENVMAP")) return ROLE_ENVMAP;
    if (!strcmp(name, "SKYDOME") || !strcmp(name, "PANORAMA")) return ROLE_SKY;
    return ROLE_SCENERY;
}

static cmesh *cache_get(mcache *mc, const hp2_partset *ps, uint32_t h, int *bad)
{
    int i;
    for (i = 0; i < mc->n; i++)
        if (mc->c[i].hash == h) return mc->c[i].state == 1 ? &mc->c[i] : NULL;
    if (mc->n == mc->cap) {
        int cap = mc->cap ? mc->cap * 2 : 256;
        cmesh *n = (cmesh *)realloc(mc->c, (size_t)cap * sizeof *n);
        if (!n) return NULL;
        mc->c = n;
        mc->cap = cap;
    }
    {
        cmesh *c = &mc->c[mc->n++];
        const hp2_part *p = hp2_geom_find(ps, h);
        memset(c, 0, sizeof *c);
        c->hash = h;
        c->state = 2;
        c->role = p ? role_of(p->name) : ROLE_SCENERY;
        if (p && p->geom && hp2_geom_decode(p, &c->m) == 0 && c->m.nv) {
            c->state = 1;
            *bad += c->m.bad;
        }
        return c->state == 1 ? c : NULL;
    }
}

/* ---- building ---------------------------------------------------------- */

typedef struct {
    uint32_t  tex;
    int       nv, cap;
    hp2_vert *v;
} bucket;

static int bucket_add(bucket **bs, int *nb, int *cap, uint32_t tex, const hp2_vert *src, int n,
                      const float pos[3], const float m[9])
{
    bucket *b = NULL;
    int i;
    for (i = 0; i < *nb; i++)
        if ((*bs)[i].tex == tex) { b = &(*bs)[i]; break; }
    if (!b) {
        if (*nb == *cap) {
            int c = *cap ? *cap * 2 : 16;
            bucket *nn = (bucket *)realloc(*bs, (size_t)c * sizeof *nn);
            if (!nn) return -1;
            *bs = nn;
            *cap = c;
        }
        b = &(*bs)[(*nb)++];
        memset(b, 0, sizeof *b);
        b->tex = tex;
    }
    if (b->nv + n > b->cap) {
        int c = b->cap ? b->cap : 768;
        hp2_vert *nn;
        while (c < b->nv + n) c *= 2;
        nn = (hp2_vert *)realloc(b->v, (size_t)c * sizeof *nn);
        if (!nn) return -1;
        b->v = nn;
        b->cap = c;
    }
    for (i = 0; i < n; i++) {
        const hp2_vert *s = &src[i];
        hp2_vert *d = &b->v[b->nv++];
        *d = *s;
        d->x = pos[0] + s->x * m[0] + s->y * m[3] + s->z * m[6];
        d->y = pos[1] + s->x * m[1] + s->y * m[4] + s->z * m[7];
        d->z = pos[2] + s->x * m[2] + s->y * m[5] + s->z * m[8];
        if (s->n[0] || s->n[1] || s->n[2]) {
            float nx = s->n[0] * m[0] + s->n[1] * m[3] + s->n[2] * m[6];
            float ny = s->n[0] * m[1] + s->n[1] * m[4] + s->n[2] * m[7];
            float nz = s->n[0] * m[2] + s->n[1] * m[5] + s->n[2] * m[8];
            float l = (float)sqrt(nx * nx + ny * ny + nz * nz);
            if (l > 0) {
                d->n[0] = (int8_t)(nx / l * 127.0f);
                d->n[1] = (int8_t)(ny / l * 127.0f);
                d->n[2] = (int8_t)(nz / l * 127.0f);
            }
        }
    }
    return 0;
}

/* Instance records already placed (the same 48 bytes may sit in two
 * sections; hp2_scenery.py writes them once). An open-addressed set. */
typedef struct {
    const uint8_t **slot;
    size_t          size, used;
} rset;

static uint32_t rec_hash(const uint8_t *r)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 48; i++) h = (h ^ r[i]) * 16777619u;
    return h;
}

/* 1 if r was already in the set (and adds it otherwise); -1 out of memory. */
static int rset_add(rset *s, const uint8_t *r)
{
    size_t i;
    if ((s->used + 1) * 2 > s->size) {
        size_t ns = s->size ? s->size * 2 : 4096, k;
        const uint8_t **nn = (const uint8_t **)calloc(ns, sizeof *nn);
        if (!nn) return -1;
        for (k = 0; k < s->size; k++)
            if (s->slot[k]) {
                size_t j = rec_hash(s->slot[k]) & (ns - 1);
                while (nn[j]) j = (j + 1) & (ns - 1);
                nn[j] = s->slot[k];
            }
        free(s->slot);
        s->slot = nn;
        s->size = ns;
    }
    i = rec_hash(r) & (s->size - 1);
    while (s->slot[i]) {
        if (!memcmp(s->slot[i], r, 48)) return 1;
        i = (i + 1) & (s->size - 1);
    }
    s->slot[i] = r;
    s->used++;
    return 0;
}

static void read_extras(hp2_scene *s, const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf, *e = buf + len;
    while (p + 8 <= e) {
        uint32_t id = rd32(p), sz = rd32(p + 4);
        const uint8_t *o = p + 8, *oe;
        if (sz > (size_t)(e - o)) break;
        oe = o + sz;
        if (id == CH_ROUTES && !s->has_start) {
            const uint8_t *re, *pe;
            const uint8_t *r = child(o, oe, CH_ROUTE, &re);
            const uint8_t *pt = child(o, oe, CH_RPOINT, &pe);
            if (r && pt && r + 40 <= re) {
                int first = rd16(r + 2), count = rd16(r + 6);
                const uint8_t *a = pt + 20 * first, *b = pt + 20 * (first + 1);
                if (count >= 2 && pt + 20 * (first + count) <= pe) {
                    int k;
                    s->route = (float *)malloc((size_t)count * 3 * sizeof(float));
                    if (s->route) {
                        s->route_n = count;
                        for (k = 0; k < count; k++) {
                            const uint8_t *q = pt + 20 * (first + k);
                            s->route[3 * k] = rdf(q);
                            s->route[3 * k + 1] = rdf(q + 4);
                            s->route[3 * k + 2] = rdf(q + 16);
                        }
                    }
                    s->route_loop = r[1] == 1;
                    s->route_len = rdf(r + 12);
                }
                if (count >= 2 && b + 20 <= pe) {
                    s->has_start = 1;
                    s->start[0] = rdf(a);
                    s->start[1] = rdf(a + 4);
                    s->ahead[0] = rdf(b);
                    s->ahead[1] = rdf(b + 4);
                }
            }
        } else if (id == CH_FOG && !s->has_fog) {
            /* 16-byte header (3, 3, 2, size), then 128-byte records; the
             * first is FOG<MAIN>: +0x10 name, +0x54 RGBA, +0x58 start,
             * +0x70 density (docs/formats.md 11, "Fog") */
            const uint8_t *r = skip_fill(o, oe) + 16;
            if (r + 128 <= oe && !memcmp(r + 0x10, "FOG<MAIN>", 9)) {
                s->has_fog = 1;
                memcpy(s->fog_rgba, r + 0x54, 4);
                s->fog_start = rdf(r + 0x58);
                s->fog_density = rdf(r + 0x70);
            }
        }
        p = oe;
    }
}

/* Height of the drawn ground under (x, y): the highest triangle of a
 * world-space part (road, terrain) containing the point, or -1e9. */
static float ground(const hp2_scene *s, float x, float y)
{
    float best = -1e9f;
    int i, j, k;
    for (i = 0; i < s->nsec; i++) {
        const hp2_ssection *sec = &s->sec[i];
        if (x < sec->box[0] || x > sec->box[3] || y < sec->box[1] || y > sec->box[4]) continue;
        for (j = 0; j < sec->ng; j++)
            for (k = 0; k + 2 < sec->g[j].nv; k += 3) {
                const hp2_vert *a = &sec->g[j].v[k], *b = a + 1, *c = a + 2;
                float d = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
                float u, v, z;
                if (fabsf(d) < 1e-6f) continue;
                u = ((x - a->x) * (c->y - a->y) - (c->x - a->x) * (y - a->y)) / d;
                v = ((b->x - a->x) * (y - a->y) - (x - a->x) * (b->y - a->y)) / d;
                if (u < 0 || v < 0 || u + v > 1) continue;
                z = a->z + u * (b->z - a->z) + v * (c->z - a->z);
                if (z > best && z < 1000) best = z;
            }
    }
    return best;
}

int hp2_scene_build(hp2_scene *s, const uint8_t *buf, size_t len, int flags)
{
    hp2_partset ps;
    mcache mc;
    rset seen;
    const uint8_t *p = buf, *e = buf + len;
    bucket *bs = NULL, *sky = NULL;
    int nb = 0, bcap = 0, nsky = 0, skycap = 0, ret = -1, i;

    memset(s, 0, sizeof *s);
    memset(&ps, 0, sizeof ps);
    memset(&mc, 0, sizeof mc);
    memset(&seen, 0, sizeof seen);
    for (i = 0; i < 3; i++) { s->box[i] = 1e30f; s->box[i + 3] = -1e30f; }
    if (hp2_geom_scan(&ps, buf, len) < 0) goto done;

    while (p + 8 <= e) {
        uint32_t id = rd32(p), sz = rd32(p + 4);
        const uint8_t *o = p + 8, *oe;
        if (sz > (size_t)(e - o)) break;
        oe = o + sz;
        if (id == CH_SECTION) {
            const uint8_t *he, *me, *ie;
            const uint8_t *hd = child(o, oe, CH_SEC_HDR, &he);
            const uint8_t *mods = child(o, oe, CH_SEC_MOD, &me);
            const uint8_t *ins = child(o, oe, CH_SEC_INST, &ie);
            int nmod, k;
            hp2_ssection *sec;
            if (!hd || !mods || !ins) { p = oe; continue; }
            nmod = (int)((me - mods) / 40);
            if (s->nsec % 16 == 0) {
                hp2_ssection *nn = (hp2_ssection *)realloc(s->sec, (size_t)(s->nsec + 16) * sizeof *nn);
                if (!nn) goto done;
                s->sec = nn;
            }
            sec = &s->sec[s->nsec++];
            memset(sec, 0, sizeof *sec);
            sec->num = (int)rd32(hd + 8);
            for (k = 0; k < 3; k++) { sec->box[k] = 1e30f; sec->box[k + 3] = -1e30f; }
            nb = 0;
            for (; ins + 48 <= ie; ins += 48) {
                int model = rd16(ins + 12), kind = ins[15], j;
                float pos[3], m[9];
                cmesh *cm;
                s->inst++;
                if (kind == 1 && (flags & HP2_SCENE_NO_SHADOWS)) continue;
                j = rset_add(&seen, ins);
                if (j < 0) goto done;
                if (j) { s->dup++; continue; }
                if (model >= nmod) { s->missing++; continue; }
                cm = cache_get(&mc, &ps, rd32(mods + 40 * model), &s->bad);
                if (!cm) { s->missing++; continue; }
                for (j = 0; j < 3; j++) pos[j] = rdf(ins + 16 + 4 * j);
                for (j = 0; j < 9; j++) m[j] = rds16(ins + 28 + 2 * j) / 16384.0f;
                s->placed++;
                sec->placed++;
                if (kind == 1) s->shadows++;
                if (cm->role == ROLE_ENVMAP) continue;
                for (j = 0; j < cm->m.nb; j++) {
                    const hp2_mbatch *mb = &cm->m.b[j];
                    if (cm->role == ROLE_SKY) {
                        if (bucket_add(&sky, &nsky, &skycap, mb->tex, cm->m.v + mb->first, mb->count, pos, m) < 0)
                            goto done;
                        s->sky_tris += mb->count / 3;
                    } else if (bucket_add(&bs, &nb, &bcap, mb->tex, cm->m.v + mb->first, mb->count, pos, m) < 0) {
                        goto done;
                    }
                    s->tris += mb->count / 3;
                }
            }
            /* hand the buckets to the section */
            if (nb) {
                sec->g = (hp2_sgroup *)calloc((size_t)nb, sizeof *sec->g);
                if (!sec->g) goto done;
            }
            for (k = 0; k < nb; k++) {
                int v;
                sec->g[k].tex = bs[k].tex;
                sec->g[k].nv = bs[k].nv;
                sec->g[k].v = bs[k].v;
                for (v = 0; v < bs[k].nv; v++) {
                    const hp2_vert *q = &bs[k].v[v];
                    if (q->x < sec->box[0]) sec->box[0] = q->x;
                    if (q->y < sec->box[1]) sec->box[1] = q->y;
                    if (q->z < sec->box[2]) sec->box[2] = q->z;
                    if (q->x > sec->box[3]) sec->box[3] = q->x;
                    if (q->y > sec->box[4]) sec->box[4] = q->y;
                    if (q->z > sec->box[5]) sec->box[5] = q->z;
                }
                bs[k].v = NULL;
            }
            sec->ng = nb;
            nb = 0;
            for (k = 0; k < 3; k++) {
                if (sec->box[k] < s->box[k]) s->box[k] = sec->box[k];
                if (sec->box[k + 3] > s->box[k + 3]) s->box[k + 3] = sec->box[k + 3];
            }
        }
        p = oe;
    }
    if (!s->nsec) goto done;
    if (nsky) {
        s->sky.g = (hp2_sgroup *)calloc((size_t)nsky, sizeof *s->sky.g);
        if (!s->sky.g) goto done;
        for (i = 0; i < nsky; i++) {
            s->sky.g[i].tex = sky[i].tex;
            s->sky.g[i].nv = sky[i].nv;
            s->sky.g[i].v = sky[i].v;
            sky[i].v = NULL;
        }
        s->sky.ng = nsky;
    }
    read_extras(s, buf, len);
    if (s->has_start) {
        float z = ground(s, s->start[0], s->start[1]);
        float z2 = ground(s, s->ahead[0], s->ahead[1]);
        s->start[2] = z > -1e8f ? z : 0;
        s->ahead[2] = z2 > -1e8f ? z2 : s->start[2];
    }
    ret = 0;
done:
    for (i = 0; i < nb; i++) free(bs[i].v);
    free(bs);
    for (i = 0; i < nsky; i++) free(sky[i].v);
    free(sky);
    for (i = 0; i < mc.n; i++) hp2_mesh_free(&mc.c[i].m);
    free(mc.c);
    free(seen.slot);
    hp2_geom_free(&ps);
    if (ret < 0) hp2_scene_free(s);
    return ret;
}

void hp2_scene_drop_vertices(hp2_scene *s)
{
    int i, j;
    for (i = 0; i < s->nsec; i++)
        for (j = 0; j < s->sec[i].ng; j++) { free(s->sec[i].g[j].v); s->sec[i].g[j].v = NULL; }
    for (j = 0; j < s->sky.ng; j++) { free(s->sky.g[j].v); s->sky.g[j].v = NULL; }
}

void hp2_scene_free(hp2_scene *s)
{
    int i, j;
    for (i = 0; i < s->nsec; i++) {
        for (j = 0; j < s->sec[i].ng; j++) free(s->sec[i].g[j].v);
        free(s->sec[i].g);
    }
    free(s->sec);
    for (j = 0; j < s->sky.ng; j++) free(s->sky.g[j].v);
    free(s->sky.g);
    free(s->route);
    memset(s, 0, sizeof *s);
}

/* ---- COMP ---------------------------------------------------------------- */

uint8_t *hp2_comp_decode(const uint8_t *in, size_t len, size_t *out_len)
{
    uint32_t us, cs;
    int method;
    uint8_t *out;
    size_t i = 16, o = 0;
    *out_len = 0;
    if (len < 16 || memcmp(in, "COMP", 4) || in[5] != 16) return NULL;
    method = rd16(in + 6);
    us = rd32(in + 8);
    cs = rd32(in + 12);
    if (cs != len) return NULL;
    out = (uint8_t *)malloc(us ? us : 1);
    if (!out) return NULL;
    if (method == 1) {
        if (len - 16 < us) { free(out); return NULL; }
        memcpy(out, in + 16, us);
        *out_len = us;
        return out;
    }
    if (method != 0) { free(out); return NULL; }
    while (o < us) {
        unsigned flags;
        int b;
        if (i + 2 > len) goto bad;
        flags = in[i] | in[i + 1] << 8;
        i += 2;
        for (b = 0; b < 16 && o < us; b++) {
            if (flags & (1u << b)) {
                size_t ln, d, k;
                if (i + 2 > len) goto bad;
                ln = (size_t)(in[i] & 0x0F) + 3;
                d = (size_t)(in[i] >> 4) << 8 | in[i + 1];
                i += 2;
                if (d == 0 || d > o) goto bad;
                for (k = 0; k < ln && o < us; k++, o++) out[o] = out[o - d];
            } else {
                if (i >= len) goto bad;
                out[o++] = in[i++];
            }
        }
    }
    *out_len = us;
    return out;
bad:
    free(out);
    return NULL;
}

void hp2_scene_reverse_route(hp2_scene *s)
{
    int n = s->route_n, k;
    float *r, last;
    if (n < 2 || !(r = (float *)malloc(sizeof(float) * 3 * n))) return;
    last = s->route[3 * (n - 1) + 2];
    for (k = 0; k < n; k++) {
        /* a circuit keeps point 0 (the line) first: 0, n-1, n-2, ... 1 */
        int j = s->route_loop ? (n - k) % n : n - 1 - k;
        r[3 * k] = s->route[3 * j];
        r[3 * k + 1] = s->route[3 * j + 1];
        r[3 * k + 2] = s->route_loop ? (k ? s->route_len - s->route[3 * j + 2] : 0) : last - s->route[3 * j + 2];
    }
    memcpy(s->route, r, sizeof(float) * 3 * n);
    free(r);
    if (s->has_start) {
        float zs = s->start[2], za = s->ahead[2];
        if (!s->route_loop) {           /* the old finish */
            s->start[0] = s->route[0];
            s->start[1] = s->route[1];
            s->ahead[0] = s->route[3];
            s->ahead[1] = s->route[4];
            s->start[2] = ground(s, s->start[0], s->start[1]);
            s->ahead[2] = ground(s, s->ahead[0], s->ahead[1]);
            if (s->start[2] < -1e8f) s->start[2] = zs;
            if (s->ahead[2] < -1e8f) s->ahead[2] = s->start[2];
        } else {                        /* the same line, facing the other way */
            s->ahead[0] = s->route[3];
            s->ahead[1] = s->route[4];
            s->ahead[2] = ground(s, s->ahead[0], s->ahead[1]);
            if (s->ahead[2] < -1e8f) s->ahead[2] = za;
        }
    }
}
