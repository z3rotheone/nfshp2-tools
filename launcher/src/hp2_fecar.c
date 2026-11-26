#include "hp2_fecar.h"
#include "hp2_scene.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hp2_raster.h"

typedef struct { hp2_rvert v[3]; float z; int tex; int blend; int flip; } tri;

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
    for (i = 0; i < c->ntex; i++) if (c->tex[i].ok) hp2_rtex_free(&c->tex[i].t);
    free(c->tex);
    c->tex = NULL;
    c->ntex = 0;
}

void hp2_fecar_close(hp2_fecar *c)
{
    if (!c->ok) return;
    if (c->loaded) hp2_car_free(&c->car);
    drop_textures(c);
    if (c->ss) SDL_DestroyTexture(c->ss);
    hp2_rast_free(&c->rast);
    if (c->smoke_ok) hp2_rtex_free(&c->smoke);
    if (c->bgtex) SDL_DestroyTexture(c->bgtex);
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

/* The index of the car's texture for hash h (made as a mip-mapped RGBA texture; .ok is 0 when no pack has it). .blend
 * is set when it has translucent texels (the windows), which are drawn blended; the rest are solid whatever their alpha. */
static int texture_for(hp2_fecar *c, uint32_t h)
{
    int i;
    const hp2_tex *t;
    uint8_t *px;
    for (i = 0; i < c->ntex; i++) if (c->tex[i].hash == h) return i;
    if (c->ntex % 32 == 0) {
        void *p = realloc(c->tex, sizeof *c->tex * (size_t)(c->ntex + 32));
        if (!p) return -1;
        c->tex = p;
    }
    memset(&c->tex[c->ntex], 0, sizeof c->tex[c->ntex]);
    c->tex[c->ntex].hash = h;
    t = hp2_tex_find(&c->ts, h);
    if (t && t->bpp && (px = (uint8_t *)malloc((size_t)t->w * t->h * 4)) != NULL) {
        if (hp2_tex_decode(t, px) == 0) {
            long q, np = (long)t->w * t->h, mid = 0;
            for (q = 0; q < np; q++) {      /* the GS draws a texture at up to twice its colours (vertex colour 128 = 1x) */
                int k2;
                for (k2 = 0; k2 < 3; k2++) { int x = px[4 * q + k2] * 8 / 5; px[4 * q + k2] = (uint8_t)(x > 255 ? 255 : x); }
                if (px[4 * q + 3] < 200) mid++;
            }
            c->tex[c->ntex].blend = mid > np / 20;
            c->tex[c->ntex].ok = hp2_rtex_make(&c->tex[c->ntex].t, px, t->w, t->h) == 0;
        }
        free(px);
    }
    return c->ntex++;
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
                    const float sc[3], const float tr[3], tri **list, size_t *n, size_t *cap, int mirror, float gz)
{
    int b, i;
    for (b = 0; b < m->nb; b++) {
        const hp2_mbatch *mb = &m->b[b];
        uint32_t th = hp2_car_texture(car, &c->ts, mb->tex, skin);
        int ti = mb->tex ? texture_for(c, th) : -1;
        int tblend = ti >= 0 && c->tex[ti].blend;
        for (i = 0; i + 2 < mb->count; i += 3) {
            tri *t;
            float z = 0;
            int j, vblend = 0;
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
                float sx, sy, zv, h = p[2] - gz;
                if (mirror) q[2] = 2.0f * gz - p[2];                 /* the reflection in the floor */
                float al = mb->tex && v->c[3] ? v->c[3] * 2 / 255.0f : 1.0f;
                /* the model's vertex colours are the game's own dim lighting; the port lights it from above and
                 * the front: a soft key light on the (turned) normal, 0.45 ambient */
                float nx = v->n[0] / 127.0f, ny = v->n[1] / 127.0f, nz = v->n[2] / 127.0f, lit = 0.85f;
                if (v->n[0] || v->n[1] || v->n[2]) {
                    float wx = nx * k->yaw_c - ny * k->yaw_s, wy = nx * k->yaw_s + ny * k->yaw_c;
                    if (mirror) nz = -nz;
                    lit = 0.62f + 0.95f * fmaxf(0.0f, 0.55f * wx + 0.25f * wy + 0.8f * nz);
                    if (lit > 1.4f) lit = 1.4f;
                }
                if (al > 1.0f) al = 1.0f;
                if (mirror) {                                   /* faint, fading with height above the floor */
                    float f = 0.2f * (1.0f - h / 1.2f);
                    al = f > 0 ? f : 0;
                    lit *= 0.7f;
                    vblend = 1;
                }
                if (al < 0.98f) vblend = 1;
                zv = project(k, q, &sx, &sy);
                z += zv;
                t->v[j].x = sx; t->v[j].y = sy; t->v[j].z = zv;
                t->v[j].u = v->u; t->v[j].v = v->v;
                t->v[j].r = t->v[j].g = t->v[j].b = lit;
                if (mirror) { t->v[j].r = lit * c->paint[0]; t->v[j].g = lit * c->paint[1]; t->v[j].b = lit * c->paint[2]; }
                t->v[j].a = al;
            }
            t->z = z / 3.0f;
            t->tex = mirror ? -1 : ti;                 /* the reflection is flat paint colour: faint, and quick to draw */
            t->blend = mirror ? 2 : tblend || vblend;
            t->flip = sc[0] * sc[1] * sc[2] < 0;           /* a mirrored part (the left wheels) has its winding turned round */
            if (mirror && tblend) (*n)--;               /* no reflection of the windows */
        }
    }
    return 0;
}

void hp2_fecar_set_textures(hp2_fecar *c, const hp2_texset *fets) { c->fets = fets; }

/* The backdrop of the car select screen, which the game draws in code: soft blue smoke drifting about, and the
 * spotlit floor. The look is the port's own (read off a PCSX2 screenshot): the smoke is the game's SMOKEB_CS sprite
 * sheet (4 x 4 puffs) tinted blue. */
static void backdrop(hp2_fecar *c, hp2_rast *r, float t, float fcx, float fcy, float fr)
{
    int i;
    if (!c->smoke_ok && c->fets) {
        const hp2_tex *st = hp2_tex_find(c->fets, hp2_geom_hash("SMOKEB_CS"));
        if (st && st->bpp) {
            uint8_t *px = (uint8_t *)malloc((size_t)st->w * st->h * 4);
            if (px && hp2_tex_decode(st, px) == 0) c->smoke_ok = hp2_rtex_make(&c->smoke, px, st->w, st->h) == 0;
            free(px);
        }
    }
    if (c->smoke_ok) {
        for (i = 0; i < 14; i++) {
            float ph = (float)i * 2.399f, sz = (0.28f + 0.22f * (float)((i * 7) % 5) / 4.0f) * (float)r->w;
            float x = ((float)((i * 37) % 100) / 100.0f) * (float)r->w + sinf(t * 0.7f + ph) * 0.03f * (float)r->w;
            float y = (0.04f + 0.5f * (float)((i * 53) % 100) / 100.0f) * (float)r->h + cosf(t * 0.5f + ph) * 0.02f * (float)r->h;
            int cell = (i * 5 + (int)(t * 0.4f)) % 16;
            float u0 = (float)(cell % 4) * 0.25f, v0 = (float)(cell / 4) * 0.25f;
            hp2_rvert q[4];
            int k;
            for (k = 0; k < 4; k++) {
                q[k].x = x + ((k == 0 || k == 3) ? -sz : sz) * 0.5f;
                q[k].y = y + ((k < 2) ? -sz : sz) * 0.5f;
                q[k].z = 500.0f;
                q[k].u = u0 + ((k == 0 || k == 3) ? 0.002f : 0.248f);
                q[k].v = v0 + ((k < 2) ? 0.002f : 0.248f);
                q[k].r = 0.13f; q[k].g = 0.25f; q[k].b = 0.62f; q[k].a = 0.8f;
            }
            hp2_rast_tri(r, q, &c->smoke, 1);
            { hp2_rvert tri2[3]; tri2[0] = q[0]; tri2[1] = q[2]; tri2[2] = q[3]; hp2_rast_tri(r, tri2, &c->smoke, 1); }
        }
    }
    /* the floor: a wide pool of light, brighter near the car, with a soft rim */
    hp2_rast_glow(r, fcx, fcy, fr * 1.5f, fr * 0.36f, 0.5f, 0.53f, 0.6f, 0.55f);
    hp2_rast_glow(r, fcx, fcy, fr * 0.85f, fr * 0.2f, 0.68f, 0.7f, 0.76f, 0.35f);
}

/* The camera for a buffer whose top left is window pixel (ox, oy) and that holds sf buffer pixels per window pixel. */
static void make_cam(cam_t *k, float L, float el, float yaw_rad, float sf, int ox, int oy, const SDL_Rect *car)
{
    float D = L * 2.3f;
    k->cam[0] = D * cosf(el); k->cam[1] = 0; k->cam[2] = D * sinf(el) + 0.5f;
    k->fwd[0] = -cosf(el); k->fwd[1] = 0; k->fwd[2] = -sinf(el);
    k->right[0] = 0; k->right[1] = 1; k->right[2] = 0;
    k->up[0] = -sinf(el); k->up[1] = 0; k->up[2] = cosf(el);
    /* the car's size and place come from its own rectangle */
    k->focal = (float)car->w * sf * 0.5f * D / (L * 0.64f);
    k->cx = ((float)(car->x - ox) + (float)car->w * 0.5f) * sf;
    k->cy = ((float)(car->y - oy) + (float)car->h * 0.55f) * sf;
    k->yaw_c = cosf(yaw_rad); k->yaw_s = sinf(yaw_rad);
}

void hp2_fecar_draw(hp2_fecar *c, const char *name, int skin, float yaw, const SDL_Rect *area, const SDL_Rect *car)
{
    static const float one[3] = { 1, 1, 1 }, zero[3] = { 0, 0, 0 };
    tri *list = (tri *)c->work;
    size_t n = 0, cap = c->work_cap, i, nm;
    cam_t k;
    float L, el = 0.22f, a = yaw * 3.14159265f / 180.0f, sf, gz = 1e9f;
    int w, bw, bh, pass, lx0, ly0, lx1, ly1;
    SDL_Rect layer;
    SDL_BlendMode pm;
    if (!c->ok || !name || area->w < 8 || area->h < 8) return;
    if (!c->loaded || strcmp(c->name, name)) { if (load_car(c, name) != 0) return; }
    if (c->skin != skin) { drop_textures(c); c->skin = skin; }
    {   char key[48];
        snprintf(key, sizeof key, "%s/%d", name, skin);
        if (strcmp(key, c->paint_key)) {
            uint8_t rgb[3];
            c->paint[0] = c->paint[1] = c->paint[2] = 0.3f;
            if (hp2_fecar_skin_rgb(c, name, skin, rgb)) { c->paint[0] = rgb[0] / 255.0f; c->paint[1] = rgb[1] / 255.0f; c->paint[2] = rgb[2] / 255.0f; }
            snprintf(c->paint_key, sizeof c->paint_key, "%s", key);
        }
    }
    pm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    /* 1. the backdrop: made once per window size, at about a million pixels (it is all soft), and left to the renderer
     * to stretch. The floor sits where a car of any length stands (the camera scales with the car). */
    if (!c->bgtex || c->bgw != area->w || c->bgh != area->h) {
        float bs = sqrtf(0.9e6f / ((float)area->w * (float)area->h)), fx, fy;
        hp2_rast br;
        cam_t bk;
        const float p0[3] = { 0, 0, 0 };
        bs = bs > 1.2f ? 1.2f : bs < 0.4f ? 0.4f : bs;
        if (c->bgtex) { SDL_DestroyTexture(c->bgtex); c->bgtex = NULL; }
        if (hp2_rast_init(&br, (int)((float)area->w * bs), (int)((float)area->h * bs)) == 0) {
            make_cam(&bk, 4.5f, el, 0.0f, bs, area->x, area->y, car);
            project(&bk, p0, &fx, &fy);
            backdrop(c, &br, 0.0f, fx, fy, (float)car->w * bs * 0.5f);
            c->bgtex = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, br.w, br.h);
            if (c->bgtex) {
                SDL_UpdateTexture(c->bgtex, NULL, br.px, br.w * 4);
                SDL_SetTextureScaleMode(c->bgtex, SDL_ScaleModeLinear);
                if (SDL_SetTextureBlendMode(c->bgtex, pm) != 0) SDL_SetTextureBlendMode(c->bgtex, SDL_BLENDMODE_BLEND);
                c->bgw = area->w; c->bgh = area->h;
            }
            hp2_rast_free(&br);
        }
    }
    if (c->bgtex) SDL_RenderCopy(c->r, c->bgtex, NULL, area);
    /* 2. the car and its reflection: rasterised here with a depth buffer, in a layer just big enough for them
     * (the car's rectangle and room below it for the reflection), at up to 2.2x the window's pixels (about 1.3
     * million at most), and scaled down by the renderer, which smooths the edges */
    lx0 = car->x - car->w / 8; ly0 = car->y - car->h / 20; lx1 = car->x + car->w + car->w / 8; ly1 = car->y + car->h + car->h * 45 / 100;
    if (lx0 < area->x) lx0 = area->x;
    if (ly0 < area->y) ly0 = area->y;
    if (lx1 > area->x + area->w) lx1 = area->x + area->w;
    if (ly1 > area->y + area->h) ly1 = area->y + area->h;
    layer.x = lx0; layer.y = ly0; layer.w = lx1 - lx0; layer.h = ly1 - ly0;
    if (layer.w < 8 || layer.h < 8) return;
    sf = sqrtf(1.3e6f / ((float)layer.w * (float)layer.h));
    sf = sf > 2.2f ? 2.2f : sf < 0.75f ? 0.75f : sf;
    bw = (int)((float)layer.w * sf); bh = (int)((float)layer.h * sf);
    if (c->rast.w != bw || c->rast.h != bh) {
        hp2_rast_free(&c->rast);
        if (hp2_rast_init(&c->rast, bw, bh) != 0) return;
    }
    hp2_rast_clear(&c->rast);
    L = c->car.rec.length > 1.0f ? c->car.rec.length : 4.5f;
    make_cam(&k, L, el, a, sf, layer.x, layer.y, car);
    for (w = 0; w < 4; w++) {
        float z0 = c->car.rec.wheel[w].z - c->car.rec.wheel[w].radius;
        if (z0 < gz) gz = z0;
    }
    if (gz > 1e8f) gz = 0;
    for (pass = 0; pass < 2; pass++) {      /* 0: the reflection in the floor, 1: the car */
        int mir = pass == 0;
        float mgz = mir ? gz : 0;
        n = 0;
        if (add_mesh(c, &c->car, &c->car.body, &k, skin, one, zero, &list, &n, &cap, mir, mgz) != 0) goto done;
        for (w = 0; w < 4; w++) {
            const hp2_wheel *wh = &c->car.rec.wheel[w];
            int front = w == HP2_WHEEL_FL || w == HP2_WHEEL_FR;
            float sy = wh->y > 0 ? -1.0f : 1.0f;
            float tr0, tw0;
            const hp2_mesh *tm = hp2_car_tyre(&c->car, w, &tr0, &tw0);
            float sr = tr0 > 0 ? wh->radius / tr0 : 1;
            float sw = tw0 > 0 ? wh->width / tw0 : 1;
            float s2[3] = { sr, sy * sw, sr }, sb[3] = { 1, sy, 1 }, tr[3] = { wh->x, wh->y, wh->z };
            if (add_mesh(c, &c->car, &c->car.brake[front ? 0 : 1], &k, skin, sb, tr, &list, &n, &cap, mir, mgz) != 0) goto done;
            if (add_mesh(c, &c->car, tm, &k, skin, s2, tr, &list, &n, &cap, mir, mgz) != 0) goto done;
        }
        if (mir) {                          /* the reflection: translucent, its own triangles do not stack */
            for (i = 0; i < n; i++) hp2_rast_tri(&c->rast, list[i].v, NULL, 2);
            hp2_rast_clear_z(&c->rast);
            continue;
        }
        for (i = 0; i < n; i++)             /* solid triangles in any order: the depth buffer sorts them out */
            if (!list[i].blend)
                hp2_rast_tri(&c->rast, list[i].v, list[i].tex >= 0 && c->tex[list[i].tex].ok ? &c->tex[list[i].tex].t : NULL,
                             list[i].flip ? HP2_RAST_CULL_NEG : HP2_RAST_CULL_POS);   /* solid parts are closed shells: the far side is never seen */
        nm = 0;                             /* then the blended ones (windows) far to near */
        for (i = 0; i < n; i++) if (list[i].blend) { tri t = list[i]; list[nm++] = t; }
        qsort(list, nm, sizeof *list, cmp_far);
        for (i = 0; i < nm; i++)
            hp2_rast_tri(&c->rast, list[i].v, list[i].tex >= 0 && c->tex[list[i].tex].ok ? &c->tex[list[i].tex].t : NULL, 1);
    }
    if (!c->ss || c->ssw != bw || c->ssh != bh) {
        if (c->ss) SDL_DestroyTexture(c->ss);
        c->ssw = bw; c->ssh = bh;
        c->ss = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, bw, bh);
        if (c->ss) SDL_SetTextureScaleMode(c->ss, SDL_ScaleModeLinear);
    }
    if (c->ss) {
        SDL_UpdateTexture(c->ss, NULL, c->rast.px, bw * 4);
        /* the buffer holds premultiplied colour */
        if (SDL_SetTextureBlendMode(c->ss, pm) != 0) SDL_SetTextureBlendMode(c->ss, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(c->r, c->ss, NULL, &layer);
    }
done:
    c->work = list;
    c->work_cap = cap;
}

int hp2_fecar_skin_rgb(hp2_fecar *c, const char *name, int skin, uint8_t rgb[3])
{
    const hp2_carrec *rec;
    const hp2_tex *t;
    char buf[64];
    uint8_t *px;
    long i, np;
    double sum[3] = { 0, 0, 0 }, wsum = 0;
    if (!c->ok || !name || skin < 1 || skin > 9) return 0;
    rec = hp2_cardb_find(c->recs, c->nrecs, name);
    if (!rec) return 0;
    snprintf(buf, sizeof buf, "%s_SKIN%d", rec->model, skin);
    t = hp2_tex_find(&c->ts, hp2_geom_hash(buf));
    if (!t || !t->bpp) return 0;
    px = (uint8_t *)malloc((size_t)t->w * t->h * 4);
    if (!px) return 0;
    if (hp2_tex_decode(t, px) != 0) { free(px); return 0; }
    np = (long)t->w * t->h;
    for (i = 0; i < np; i += 3) {      /* every third pixel: paint dominates, coloured ones count more */
        int r = px[4 * i], g = px[4 * i + 1], b = px[4 * i + 2], mx = r > g ? (r > b ? r : b) : (g > b ? g : b),
            mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        double w;
        if (px[4 * i + 3] < 128 || mx < 24) continue;
        w = 8.0 + (double)(mx - mn);
        sum[0] += w * r; sum[1] += w * g; sum[2] += w * b; wsum += w;
    }
    free(px);
    if (wsum <= 0) return 0;
    for (i = 0; i < 3; i++) { int v = (int)(sum[i] / wsum * 1.7); rgb[i] = (uint8_t)(v > 255 ? 255 : v); }
    return 1;
}
