/* hp2_fedraw - SDL2 drawing of front-end widgets. See hp2_fedraw.h. */
#include "hp2_fedraw.h"

#include <stdlib.h>
#include <string.h>

void hp2_fedraw_init(hp2_fedraw *d, SDL_Renderer *r, const hp2_texset *tex, const hp2_fontset *fonts)
{
    memset(d, 0, sizeof *d);
    d->r = r;
    d->tex = tex;
    d->fonts = fonts;
}

void hp2_fedraw_free(hp2_fedraw *d)
{
    int i;
    for (i = 0; i < d->count; i++)
        if (d->cache[i].tex) SDL_DestroyTexture(d->cache[i].tex);
    free(d->cache);
    memset(d, 0, sizeof *d);
}

/* The SDL texture for a name hash, decoded on first use. NULL (cached as
 * such) when the packs do not have it. */
static const hp2_fedraw_cached *get_tex(hp2_fedraw *d, uint32_t hash)
{
    hp2_fedraw_cached c;
    const hp2_tex *t;
    int i;
    for (i = 0; i < d->count; i++)
        if (d->cache[i].hash == hash) return &d->cache[i];
    memset(&c, 0, sizeof c);
    c.hash = hash;
    if (hash == HP2_FEDRAW_SOLID) {      /* a flat white 16 x 16, for the corner colours to tint */
        static const uint8_t white[4] = { 255, 255, 255, 255 };
        c.tex = SDL_CreateTexture(d->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1);
        if (c.tex) {
            SDL_UpdateTexture(c.tex, NULL, white, 4);
            SDL_SetTextureBlendMode(c.tex, SDL_BLENDMODE_BLEND);
            c.w = c.h = 16;
        }
        t = NULL;
    } else {
        t = hp2_tex_find(d->tex, hash);
    }
    if (t && t->bpp) {
        uint8_t *px = (uint8_t *)malloc((size_t)t->w * t->h * 4);
        if (px && hp2_tex_decode(t, px) == 0) {
            c.tex = SDL_CreateTexture(d->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t->w, t->h);
            if (c.tex) {
                SDL_UpdateTexture(c.tex, NULL, px, t->w * 4);
                SDL_SetTextureBlendMode(c.tex, SDL_BLENDMODE_BLEND);
                c.w = t->w;
                c.h = t->h;
            }
        }
        free(px);
    }
    if (!c.tex) d->missing++;
    if (d->count == d->cap) {
        int nc = d->cap ? d->cap * 2 : 64;
        hp2_fedraw_cached *n = (hp2_fedraw_cached *)realloc(d->cache, (size_t)nc * sizeof *n);
        if (!n) return NULL;
        d->cache = n;
        d->cap = nc;
    }
    d->cache[d->count] = c;
    return &d->cache[d->count++];
}

/* PS2 colour: 0x80 is full intensity, more is brighter still; SDL modulates
 * with 255 at most. */
static Uint8 ps2c(uint8_t c) { return c >= 128 ? 255 : (Uint8)(c * 2); }

static void vert(SDL_Vertex *v, const hp2_fe_view *view, float cx, float cy, const uint8_t *rgba, float u, float t)
{
    hp2_fe_to_window(view, cx, cy, &v->position.x, &v->position.y);
    v->color.r = ps2c(rgba[0]);
    v->color.g = ps2c(rgba[1]);
    v->color.b = ps2c(rgba[2]);
    v->color.a = ps2c(rgba[3]);
    v->tex_coord.x = u;
    v->tex_coord.y = t;
}

static void quad(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex v[4])
{
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, t, v, 4, idx, 6);
}

static void draw_image(hp2_fedraw *d, const hp2_fe_view *view, const hp2_fe_item *it)
{
    const hp2_fe_widget *w = &it->w;
    const hp2_fedraw_cached *c = get_tex(d, w->texture);
    float tw = c && c->tex ? (float)c->w : 16.0f, th = c && c->tex ? (float)c->h : 16.0f;
    float W = tw * w->sx, H = th * w->sy;
    float x0 = w->x, x1 = w->x;
    /* only a background spanning the whole canvas is tiled into the margins */
    if (it->extend && W > 0.0f && w->x <= 0.5f && w->x + W >= 639.5f) {
        while (x0 > -view->margin_l) x0 -= W;
        while (x1 + W < 640.0f + view->margin_r) x1 += W;
    }
    for (; x0 <= x1 + 0.5f; x0 += W) {
        SDL_Vertex v[4];
        vert(&v[0], view, x0, w->y, w->vcol[0], w->uv[0][0], w->uv[0][1]);
        vert(&v[1], view, x0 + W, w->y, w->vcol[1], w->uv[1][0], w->uv[1][1]);
        vert(&v[2], view, x0 + W, w->y + H, w->vcol[2], w->uv[2][0], w->uv[2][1]);
        vert(&v[3], view, x0, w->y + H, w->vcol[3], w->uv[3][0], w->uv[3][1]);
        quad(d->r, c ? c->tex : NULL, v);
        if (W <= 0.0f) break;
    }
}

static void draw_text(hp2_fedraw *d, const hp2_fe_view *view, const hp2_fe_item *it)
{
    const hp2_fe_widget *w = &it->w;
    const hp2_font *f = hp2_font_find(d->fonts, w->font);
    const char *s = it->str ? it->str : w->text;
    const hp2_fedraw_cached *c;
    uint8_t col[4];
    float pen, y = w->y;
    unsigned char prev = 0;
    if (!f) { d->missing++; return; }
    c = get_tex(d, f->hash);
    if (!c || !c->tex) return;
    memcpy(col, w->rgba, 4);
    pen = w->x;
    if (w->align & HP2_FE_ALIGN_CENTRE) pen -= hp2_text_width(f, s) * w->sx * 0.5f;
    else if (w->align & HP2_FE_ALIGN_RIGHT) pen -= hp2_text_width(f, s) * w->sx;
    for (;;) {
        hp2_tok t = hp2_text_next(&s);
        hp2_glyph g;
        if (t.kind == HP2_TOK_END) break;
        if (t.kind == HP2_TOK_NEWLINE) {
            y += f->line_height * w->sy;
            pen = w->x;
            if (w->align & HP2_FE_ALIGN_CENTRE) pen -= hp2_text_width(f, s) * w->sx * 0.5f;
            prev = 0;
            continue;
        }
        if (t.kind == HP2_TOK_RGB) {
            /* $RGB values are taken as 0..255; not yet checked in game */
            col[0] = (uint8_t)(t.rgb[0] / 2);
            col[1] = (uint8_t)(t.rgb[1] / 2);
            col[2] = (uint8_t)(t.rgb[2] / 2);
            continue;
        }
        if (t.kind == HP2_TOK_FONT) {
            const hp2_font *nf = hp2_font_find(d->fonts, t.font);
            const hp2_fedraw_cached *nc = nf ? get_tex(d, nf->hash) : NULL;
            if (nf && nc && nc->tex) { f = nf; c = nc; }
            continue;
        }
        if (prev) pen += hp2_font_kern(f, prev, t.ch) * w->sx;
        prev = t.ch;
        if (!hp2_font_glyph(f, t.ch, &g)) continue;
        if (g.w > 0 && g.h > 0) {
            SDL_Vertex v[4];
            float gx = pen + g.xoff * w->sx, gw = g.w * w->sx, gh = g.h * w->sy;
            float u0 = g.x / c->w, v0 = g.y / c->h, u1 = (g.x + g.w) / c->w, v1 = (g.y + g.h) / c->h;
            vert(&v[0], view, gx, y, col, u0, v0);
            vert(&v[1], view, gx + gw, y, col, u1, v0);
            vert(&v[2], view, gx + gw, y + gh, col, u1, v1);
            vert(&v[3], view, gx, y + gh, col, u0, v1);
            quad(d->r, c->tex, v);
        }
        pen += g.advance * w->sx;
    }
}

static int by_layer(const void *a, const void *b)
{
    const hp2_fe_item *x = (const hp2_fe_item *)a, *y = (const hp2_fe_item *)b;
    if (x->w.layer != y->w.layer) return x->w.layer > y->w.layer ? -1 : 1;
    return 0;
}

void hp2_fedraw_items(hp2_fedraw *d, const hp2_fe_view *v, hp2_fe_item *items, int n)
{
    int i, j;
    /* stable: insertion sort (lists are short) */
    for (i = 1; i < n; i++) {
        hp2_fe_item t = items[i];
        for (j = i; j > 0 && by_layer(&items[j - 1], &t) > 0; j--) items[j] = items[j - 1];
        items[j] = t;
    }
    for (i = 0; i < n; i++) {
        const hp2_fe_item *it = &items[i];
        float c[4] = { 0, 0, 0, 0 };
        int clipped = 0;
        if (!it->w.visible) continue;
        if (d->clip_canvas) { c[2] = 640.0f; c[3] = 480.0f; clipped = 1; }
        if (it->clip) {
            if (clipped) {   /* intersect with the canvas */
                float x0 = it->clip_rect[0] > 0 ? it->clip_rect[0] : 0, y0 = it->clip_rect[1] > 0 ? it->clip_rect[1] : 0;
                float x1 = it->clip_rect[0] + it->clip_rect[2], y1 = it->clip_rect[1] + it->clip_rect[3];
                if (x1 > 640.0f) x1 = 640.0f;
                if (y1 > 480.0f) y1 = 480.0f;
                c[0] = x0; c[1] = y0; c[2] = x1 - x0; c[3] = y1 - y0;
            } else {
                memcpy(c, it->clip_rect, sizeof c);
            }
            clipped = 1;
        }
        if (clipped) {
            float x0, y0, x1, y1;
            SDL_Rect r;
            if (c[2] <= 0 || c[3] <= 0) continue;
            hp2_fe_to_window(v, c[0], c[1], &x0, &y0);
            hp2_fe_to_window(v, c[0] + c[2], c[1] + c[3], &x1, &y1);
            r.x = (int)(x0 + 0.5f); r.y = (int)(y0 + 0.5f);
            r.w = (int)(x1 + 0.5f) - r.x; r.h = (int)(y1 + 0.5f) - r.y;
            SDL_RenderSetClipRect(d->r, &r);
        }
        if (it->w.kind == HP2_FE_IMAGE) draw_image(d, v, it);
        else draw_text(d, v, it);
        if (clipped) SDL_RenderSetClipRect(d->r, NULL);
    }
}
