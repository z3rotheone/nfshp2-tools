/* hp2_car - car database and car assembly. See hp2_car.h, tools/hp2_carinfo.py
 * and tools/hp2_car.py. */
#include "hp2_car.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH_CARDB 0x00034600u
#define RECORD   0x560

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p)
{
    uint32_t w = rd32(p);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static void text(char *dst, const uint8_t *src)
{
    int i;
    for (i = 0; i < 32 && src[i]; i++) dst[i] = (char)src[i];
    dst[i] = 0;
}

/* Start and record count of the table: a top-level chunk, else found by
 * searching (as hp2_carinfo.py find_table). */
static const uint8_t *find_table(const uint8_t *d, size_t len, int *n)
{
    size_t o = 0;
    while (o + 8 <= len) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        if (id == CH_CARDB && sz <= len - o - 8) {
            size_t b = (o + 8 + 15) & ~(size_t)15;
            *n = (int)((o + 8 + sz - b) / RECORD);
            return d + b;
        }
        if (sz > len - o - 8) break;
        o += 8 + sz;
    }
    for (o = 0; o + 8 <= len; o += 4) {
        uint32_t sz;
        if (rd32(d + o) != CH_CARDB) continue;
        sz = rd32(d + o + 4);
        if (sz > RECORD * 8 && sz < len - o) {
            size_t b = (o + 8 + 15) & ~(size_t)15;
            *n = (int)((o + 8 + sz - b) / RECORD);
            return d + b;
        }
    }
    return NULL;
}

int hp2_cardb_parse(const uint8_t *globalb, size_t len, hp2_carrec *out, int max)
{
    int n = 0, i, k, got = 0;
    const uint8_t *t = find_table(globalb, len, &n);
    if (!t) return -1;
    for (i = 0; i < n && got < max; i++) {
        const uint8_t *r = t + (size_t)RECORD * i;
        hp2_carrec *c = &out[got];
        if (!r[0]) continue;
        memset(c, 0, sizeof *c);
        c->slot = i;
        text(c->name, r);
        text(c->model, r + 0x20);
        text(c->path, r + 0x40);
        text(c->path_lzc, r + 0x60);
        text(c->make, r + 0xC0);
        for (k = 0; k < 4; k++) {
            const uint8_t *w = r + 0x120 + 32 * k;
            c->wheel[k].x = rdf(w);
            c->wheel[k].y = rdf(w + 4);
            c->wheel[k].z = rdf(w + 8);
            c->wheel[k].radius = rdf(w + 12);
            c->wheel[k].width = rdf(w + 16);
        }
        c->length = rdf(r + 0x1E4);
        c->width = rdf(r + 0x1E8);
        c->height = rdf(r + 0x1EC);
        c->final_drive = rdf(r + 0x278);
        c->gears = (int)rd32(r + 0x288);
        if (c->gears > 6) c->gears = 6;
        if (c->gears < 0) c->gears = 0;
        c->reverse = rdf(r + 0x290);
        for (k = 0; k < c->gears; k++) c->ratio[k] = rdf(r + 0x298 + 4 * k);
        c->idle_rpm = rdf(r + 0x2B0);
        c->peak_rpm = rdf(r + 0x2B4);
        c->redline_rpm = rdf(r + 0x2B8);
        c->top_speed = rdf(r + 0x320);
        got++;
    }
    return got;
}

const hp2_carrec *hp2_cardb_find(const hp2_carrec *recs, int n, const char *name)
{
    int i, k;
    for (i = 0; i < n; i++) {
        for (k = 0; name[k] && recs[i].name[k]; k++)
            if (toupper((unsigned char)name[k]) != toupper((unsigned char)recs[i].name[k])) break;
        if (!name[k] && !recs[i].name[k]) return &recs[i];
    }
    return NULL;
}

/* ---- assembly ------------------------------------------------------------ */

static uint32_t hashf(const char *fmt, const char *a, const char *b)
{
    char s[96];
    snprintf(s, sizeof s, fmt, a, b);
    return hp2_geom_hash(s);
}

static int append(hp2_mesh *dst, const hp2_mesh *src)
{
    hp2_vert *v;
    hp2_mbatch *b;
    int i;
    if (!src->nv) return 0;
    v = (hp2_vert *)realloc(dst->v, (size_t)(dst->nv + src->nv) * sizeof *v);
    if (!v) return -1;
    dst->v = v;
    b = (hp2_mbatch *)realloc(dst->b, (size_t)(dst->nb + src->nb) * sizeof *b);
    if (!b) return -1;
    dst->b = b;
    memcpy(dst->v + dst->nv, src->v, (size_t)src->nv * sizeof *v);
    for (i = 0; i < src->nb; i++) {
        dst->b[dst->nb + i] = src->b[i];
        dst->b[dst->nb + i].first += dst->nv;
    }
    dst->nv += src->nv;
    dst->nb += src->nb;
    dst->bad += src->bad;
    dst->wrong += src->wrong;
    return 0;
}

/* Decodes part <model><suffix> and adds it to dst. 1 if found, 0 if not,
 * -1 out of memory. */
static int add_part(hp2_mesh *dst, const hp2_partset *ps, const char *model, const char *suffix)
{
    const hp2_part *p = hp2_geom_find(ps, hashf("%s%s", model, suffix));
    hp2_mesh m;
    int r;
    if (!p || !p->geom) return 0;
    if (hp2_geom_decode(p, &m) < 0) return -1;
    r = append(dst, &m) < 0 ? -1 : 1;
    hp2_mesh_free(&m);
    return r;
}

int hp2_car_build(hp2_car *c, const uint8_t *geom, size_t len, const hp2_carrec *rec)
{
    hp2_partset ps;
    int r, i;
    memset(c, 0, sizeof *c);
    memset(&ps, 0, sizeof ps);
    c->rec = *rec;
    if (hp2_geom_scan(&ps, geom, len) < 0) goto fail;
    r = add_part(&c->body, &ps, rec->model, "_A");
    if (r <= 0) goto fail;
    if (add_part(&c->body, &ps, rec->model, "_SIDE_MIRROR_LEFT") < 0
        || add_part(&c->body, &ps, rec->model, "_SIDE_MIRROR_RIGHT") < 0
        || add_part(&c->body, &ps, rec->model, "_LICENSE_PLATE_REAR") < 0
        || add_part(&c->tyre, &ps, rec->model, "_TIRE_FRONT_A") < 0
        || add_part(&c->brake[0], &ps, rec->model, "_BRAKE_FRONT") < 0
        || add_part(&c->brake[1], &ps, rec->model, "_BRAKE_REAR") < 0)
        goto fail;
    if (c->tyre.nv) {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (i = 0; i < c->tyre.nv; i++) {
            const float p[3] = { c->tyre.v[i].x, c->tyre.v[i].y, c->tyre.v[i].z };
            int k;
            for (k = 0; k < 3; k++) {
                if (p[k] < lo[k]) lo[k] = p[k];
                if (p[k] > hi[k]) hi[k] = p[k];
            }
        }
        c->tyre_radius = ((hi[0] - lo[0]) > (hi[2] - lo[2]) ? hi[0] - lo[0] : hi[2] - lo[2]) / 2;
        c->tyre_width = hi[1] - lo[1];
    }
    hp2_geom_free(&ps);
    return 0;
fail:
    hp2_geom_free(&ps);
    hp2_car_free(c);
    return -1;
}

void hp2_car_free(hp2_car *c)
{
    hp2_mesh_free(&c->body);
    hp2_mesh_free(&c->tyre);
    hp2_mesh_free(&c->brake[0]);
    hp2_mesh_free(&c->brake[1]);
}

uint32_t hp2_car_texture(const hp2_car *c, const hp2_texset *set, uint32_t h, int skin)
{
    static const char *lights[] = { "HEADLIGHT", "BRAKELIGHT" };
    static const char *windows[] = { "WINDOW_FRONT", "WINDOW_REAR", "WINDOW_LEFT_FRONT", "WINDOW_RIGHT_FRONT",
                                     "WINDOW_LEFT_REAR", "WINDOW_RIGHT_REAR" };
    const char *m = c->rec.model, *r = c->rec.name;
    uint32_t cand[4];
    char buf[64];
    int n = 0, i, k;
    if (h == hashf("%s%s", m, "_SKIN1")) {
        snprintf(buf, sizeof buf, "%s_SKIN%d", m, skin);
        cand[n++] = hp2_geom_hash(buf);
    }
    for (i = 0; i < 2; i++)
        for (k = 0; k < 2; k++) {
            snprintf(buf, sizeof buf, "%s_%s_%s", m, lights[i], k ? "RIGHT" : "LEFT");
            if (h == hp2_geom_hash(buf)) {
                cand[n++] = h;                       /* a few cars have their own */
                snprintf(buf, sizeof buf, "%s_%s", m, lights[i]);
                cand[n++] = hp2_geom_hash(buf);
            }
        }
    if (h == hp2_geom_hash("TIRE") || h == hashf("%s%s", m, "_TIRE")) {
        cand[n++] = hashf("%s%s", r, "_TIRE");
        cand[n++] = hashf("%s%s", m, "_TIRE");
    }
    for (i = 0; i < 6; i++)
        if (h == hp2_geom_hash(windows[i])) cand[n++] = hp2_geom_hash("WINDOW");
    for (i = 0; i < n; i++)
        if (hp2_tex_find(set, cand[i])) return cand[i];
    return h;
}
