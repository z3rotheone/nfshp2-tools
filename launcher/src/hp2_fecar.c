#include "hp2_fecar.h"
#include "hp2_scene.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { SDL_Vertex v[3]; float z; int tex; } tri;

int hp2_fecar_open(hp2_fecar *c, const char *disc, SDL_Renderer *r, const uint8_t *globalb, size_t globalb_len)
{
    memset(c, 0, sizeof *c);
    if (!disc || !globalb) return -1;
    if (hp2_zdir_open(&c->z, disc) != 0) return -1;
    c->r = r;
    c->globalb = (uint8_t *)globalb;
    c->globalb_len = globalb_len;
    c->nrecs = hp2_cardb_parse(globalb, globalb_len, c->recs, HP2_CARDB_MAX);
    if (c->nrecs <= 0) { hp2_zdir_close(&c->z); return -1; }
    c->cartex = hp2_zdir_load(&c->z, "CARS\\TEXTURES.BIN", &c->cartex_len);
    if (c->cartex) hp2_tex_scan(&c->ts, c->cartex, c->cartex_len);
    hp2_tex_scan(&c->ts, globalb, globalb_len);
    c->ok = 1;
    return 0;
}

static void drop_textures(hp2_fecar *c)
{
    int i;
    for (i = 0; i < c->ntex; i++) if (c->tex[i].t) SDL_DestroyTexture(c->tex[i].t);
    free(c->tex);
    c->tex = NULL;
    c->ntex = 0;
}

void hp2_fecar_close(hp2_fecar *c)
{
    if (!c->ok) return;
    if (c->loaded) hp2_car_free(&c->car);
    drop_textures(c);
    hp2_tex_free(&c->ts);
    free(c->cartex);
    free(c->work);
    hp2_zdir_close(&c->z);
    c->ok = 0;
}

static uint8_t *decompress(uint8_t *b, size_t *len)
{
    if (b && *len >= 4 && !memcmp(b, "COMP", 4)) {
        size_t n;
        uint8_t *d = hp2_comp_decode(b, *len, &n);
        free(b);
        *len = n;
        return d;
    }
    return b;
}

static int load_car(hp2_fecar *c, const char *name)
{
    const hp2_carrec *rec = hp2_cardb_find(c->recs, c->nrecs, name);
    size_t len = 0;
    uint8_t *g;
    if (c->loaded) { hp2_car_free(&c->car); c->loaded = 0; }
    c->name[0] = 0;
    if (!rec) return -1;
    g = hp2_zdir_load(&c->z, rec->path, &len);
    if (!g) { len = 0; g = decompress(hp2_zdir_load(&c->z, rec->path_lzc, &len), &len); }
    if (!g) return -1;
    if (hp2_car_build(&c->car, g, len, rec) != 0) { free(g); return -1; }
    free(g);
    c->loaded = 1;
    snprintf(c->name, sizeof c->name, "%s", name);
    c->skin = 0;
    drop_textures(c);        /* a skin or another car has its own */
    return 0;
}

static SDL_Texture *texture_for(hp2_fecar *c, uint32_t h)
{
    int i;
    const hp2_tex *t;
    uint8_t *px;
    SDL_Texture *st = NULL;
    for (i = 0; i < c->ntex; i++) if (c->tex[i].hash == h) return c->tex[i].t;
    if (c->ntex % 32 == 0) {
        void *p = realloc(c->tex, sizeof *c->tex * (size_t)(c->ntex + 32));
        if (!p) return NULL;
        c->tex = p;
    }
    t = hp2_tex_find(&c->ts, h);
    if (t && t->bpp && (px = (uint8_t *)malloc((size_t)t->w * t->h * 4)) != NULL) {
        if (hp2_tex_decode(t, px) == 0) {
            long q, np = (long)t->w * t->h;
            for (q = 0; q < np; q++) {      /* the GS draws a texture at up to twice its colours (vertex colour 128 = 1x) */
                int k2;
                for (k2 = 0; k2 < 3; k2++) { int x = px[4 * q + k2] * 8 / 5; px[4 * q + k2] = (uint8_t)(x > 255 ? 255 : x); }
            }
            st = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t->w, t->h);
            if (st) {
                SDL_UpdateTexture(st, NULL, px, t->w * 4);
                SDL_SetTextureBlendMode(st, SDL_BLENDMODE_BLEND);
            }
        }
        free(px);
    }
    c->tex[c->ntex].hash = h;
    c->tex[c->ntex].t = st;
    c->ntex++;
    return st;
}

typedef struct { float right[3], up[3], fwd[3], cam[3], focal, cx, cy; float yaw_c, yaw_s; } cam_t;

/* A model point (after `m` places it) to the screen; returns its depth. */
static float project(const cam_t *k, const float p[3], float *sx, float *sy)
{
    float d[3] = { p[0] - k->cam[0], p[1] - k->cam[1], p[2] - k->cam[2] };
    float xv = d[0] * k->right[0] + d[1] * k->right[1] + d[2] * k->right[2];
    float yv = d[0] * k->up[0] + d[1] * k->up[1] + d[2] * k->up[2];
    float zv = d[0] * k->fwd[0] + d[1] * k->fwd[1] + d[2] * k->fwd[2];
    if (zv < 0.1f) zv = 0.1f;
    *sx = k->cx + xv / zv * k->focal;
    *sy = k->cy - yv / zv * k->focal;
    return zv;
}

static int cmp_far(const void *a, const void *b)
{
    float za = ((const tri *)a)->z, zb = ((const tri *)b)->z;
    return za < zb ? 1 : za > zb ? -1 : 0;
}

/* Adds a mesh to the list: each vertex scaled (sx, sy, sz), moved to (tx, ty, tz), turned by the yaw. */
static int add_mesh(hp2_fecar *c, const hp2_car *car, const hp2_mesh *m, const cam_t *k, int skin,
                    const float sc[3], const float tr[3], tri **list, size_t *n, size_t *cap)
{
    int b, i;
    for (b = 0; b < m->nb; b++) {
        const hp2_mbatch *mb = &m->b[b];
        uint32_t th = hp2_car_texture(car, &c->ts, mb->tex, skin);
        SDL_Texture *st = mb->tex ? texture_for(c, th) : NULL;
        int ti = -1;
        for (i = 0; i < c->ntex; i++) if (c->tex[i].hash == th) ti = i;
        (void)st;
        for (i = 0; i + 2 < mb->count; i += 3) {
            tri *t;
            float z = 0;
            int j;
            if (*n >= *cap) {
                size_t nc = *cap ? *cap * 2 : 8192;
                tri *p = realloc(*list, nc * sizeof **list);
                if (!p) return -1;
                *list = p;
                *cap = nc;
            }
            t = &(*list)[(*n)++];
            for (j = 0; j < 3; j++) {
                const hp2_vert *v = &m->v[mb->first + i + j];
                float p[3] = { v->x * sc[0] + tr[0], v->y * sc[1] + tr[1], v->z * sc[2] + tr[2] };
                float q[3] = { p[0] * k->yaw_c - p[1] * k->yaw_s, p[0] * k->yaw_s + p[1] * k->yaw_c, p[2] };
                float sx, sy;
                int al = v->c[3] * 2, r, g, bl;
                /* the model's vertex colours are the game's own dim lighting; the port lights it from above and
                 * the front: a soft key light on the (turned) normal, 0.45 ambient */
                float nx = v->n[0] / 127.0f, ny = v->n[1] / 127.0f, nz = v->n[2] / 127.0f, lit = 0.85f;
                if (v->n[0] || v->n[1] || v->n[2]) {
                    float wx = nx * k->yaw_c - ny * k->yaw_s, wy = nx * k->yaw_s + ny * k->yaw_c;
                    lit = 0.45f + 0.75f * fmaxf(0.0f, 0.55f * wx + 0.25f * wy + 0.8f * nz);
                    if (lit > 1.0f) lit = 1.0f;
                }
                r = g = bl = (int)(255.0f * lit);
                z += project(k, q, &sx, &sy);
                t->v[j].position.x = sx;
                t->v[j].position.y = sy;
                t->v[j].tex_coord.x = v->u;
                t->v[j].tex_coord.y = v->v;
                t->v[j].color.r = (Uint8)(r > 255 ? 255 : r);
                t->v[j].color.g = (Uint8)(g > 255 ? 255 : g);
                t->v[j].color.b = (Uint8)(bl > 255 ? 255 : bl);
                t->v[j].color.a = mb->tex && v->c[3] ? (Uint8)(al > 255 ? 255 : al) : 255;
            }
            t->z = z / 3.0f;
            t->tex = ti;
            if (ti < 0 && mb->tex) t->tex = -2;   /* a texture we do not have: drawn flat */
        }
    }
    return 0;
}

void hp2_fecar_draw(hp2_fecar *c, const char *name, int skin, float yaw, const SDL_Rect *area)
{
    static const float one[3] = { 1, 1, 1 }, zero[3] = { 0, 0, 0 };
    tri *list = (tri *)c->work;
    size_t n = 0, cap = c->work_cap, i;
    cam_t k;
    float L, D, el = 0.22f, a = yaw * 3.14159265f / 180.0f;
    int w;
    if (!c->ok || !name) return;
    if (!c->loaded || strcmp(c->name, name)) { if (load_car(c, name) != 0) return; }
    if (c->skin != skin) { drop_textures(c); c->skin = skin; }
    L = c->car.rec.length > 1.0f ? c->car.rec.length : 4.5f;
    D = L * 2.3f;
    k.cam[0] = D * cosf(el); k.cam[1] = 0; k.cam[2] = D * sinf(el) + 0.5f;
    k.fwd[0] = -cosf(el); k.fwd[1] = 0; k.fwd[2] = -sinf(el);
    k.right[0] = 0; k.right[1] = 1; k.right[2] = 0;
    k.up[0] = -sinf(el); k.up[1] = 0; k.up[2] = cosf(el);
    k.focal = (float)area->w * 0.5f * D / (L * 0.64f);
    k.cx = (float)area->x + (float)area->w * 0.5f;
    k.cy = (float)area->y + (float)area->h * 0.55f;
    k.yaw_c = cosf(a); k.yaw_s = sinf(a);
    if (add_mesh(c, &c->car, &c->car.body, &k, skin, one, zero, &list, &n, &cap) != 0) goto done;
    for (w = 0; w < 4; w++) {
        const hp2_wheel *wh = &c->car.rec.wheel[w];
        int front = w == HP2_WHEEL_FL || w == HP2_WHEEL_FR;
        float sy = wh->y > 0 ? -1.0f : 1.0f;
        float sr = c->car.tyre_radius > 0 ? wh->radius / c->car.tyre_radius : 1;
        float sw = c->car.tyre_width > 0 ? wh->width / c->car.tyre_width : 1;
        float s2[3] = { sr, sy * sw, sr }, sb[3] = { 1, sy, 1 }, tr[3] = { wh->x, wh->y, wh->z };
        if (add_mesh(c, &c->car, &c->car.brake[front ? 0 : 1], &k, skin, sb, tr, &list, &n, &cap) != 0) goto done;
        if (add_mesh(c, &c->car, &c->car.tyre, &k, skin, s2, tr, &list, &n, &cap) != 0) goto done;
    }
    qsort(list, n, sizeof *list, cmp_far);
    SDL_RenderSetClipRect(c->r, area);
    for (i = 0; i < n;) {
        size_t j = i;
        SDL_Vertex vv[3 * 256];
        int m = 0;
        int ti = list[i].tex;
        while (j < n && list[j].tex == ti && m < 256) { memcpy(vv + 3 * m, list[j].v, sizeof list[j].v); m++; j++; }
        SDL_RenderGeometry(c->r, ti >= 0 ? c->tex[ti].t : NULL, vv, 3 * m, NULL, 0);
        i = j;
    }
    SDL_RenderSetClipRect(c->r, NULL);
done:
    c->work = list;
    c->work_cap = cap;
}
