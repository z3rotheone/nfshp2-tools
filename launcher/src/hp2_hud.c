/* hp2_hud - the race display. See hp2_hud.h. */
#include "hp2_hud.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

void hp2_hud_init(hp2_hud *h, const hp2_texset *ts, const hp2_fontset *fonts)
{
    memset(h, 0, sizeof *h);
    h->ts = ts;
    h->fonts = fonts;
}

const hp2_fe_widget *hp2_hud_widget(const hp2_fe_screen *s, const char *name)
{
    int i;
    if (!s) return NULL;
    for (i = 0; i < s->count; i++)
        if (!strcmp(s->widgets[i].name, name)) return &s->widgets[i];
    return NULL;
}

static const hp2_hudtex *get(hp2_hud *h, uint32_t hash)
{
    hp2_hudtex *c;
    const hp2_tex *t;
    uint8_t *px;
    int i;
    for (i = 0; i < h->n; i++)
        if (h->cache[i].hash == hash) return h->cache[i].id ? &h->cache[i] : NULL;
    if (h->n == (int)(sizeof h->cache / sizeof h->cache[0])) return NULL;
    c = &h->cache[h->n++];
    memset(c, 0, sizeof *c);
    c->hash = hash;
    t = hp2_tex_find(h->ts, hash);
    if (!t || !t->bpp || !(px = (uint8_t *)malloc((size_t)t->w * t->h * 4))) { h->missing++; return NULL; }
    if (hp2_tex_decode(t, px) == 0) {
        glGenTextures(1, &c->id);
        glBindTexture(GL_TEXTURE_2D, c->id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        c->w = t->w;
        c->h = t->h;
    }
    free(px);
    return c->id ? c : NULL;
}

void hp2_hud_begin(hp2_hud *h, int W, int H)
{
    hp2_fe_view_make(&h->view, W, H, 640, 480, HP2_FE_EXPAND);
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void hp2_hud_end(void)
{
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

/* Canvas to window, with the left half held to the window's left edge and
 * the right half to its right edge. */
static void place(const hp2_hud *h, float anchor_x, float cx, float cy, float *wx, float *wy)
{
    float shift = anchor_x < 320 ? -h->view.margin_l : h->view.margin_r;
    hp2_fe_to_window(&h->view, cx + shift, cy, wx, wy);
}

static GLubyte ps2c(unsigned c) { return c >= 128 ? 255 : (GLubyte)(c * 2); }

void hp2_hud_image(hp2_hud *h, const hp2_fe_widget *w, uint32_t tex, float angle, const float *pivot, float alpha)
{
    const hp2_hudtex *c = get(h, tex ? tex : w->texture);
    float W, H, px, py, ca, sa, cx[4], cy[4];
    int k;
    if (!c) return;
    W = c->w * w->sx;
    H = c->h * w->sy;
    cx[0] = 0; cy[0] = 0; cx[1] = W; cy[1] = 0; cx[2] = W; cy[2] = H; cx[3] = 0; cy[3] = H;
    px = pivot ? pivot[0] * w->sx : 0;
    py = pivot ? pivot[1] * w->sy : 0;
    ca = cosf(angle * 3.14159265f / 180);
    sa = sinf(angle * 3.14159265f / 180);
    glBindTexture(GL_TEXTURE_2D, c->id);
    glBegin(GL_QUADS);
    for (k = 0; k < 4; k++) {
        float x = cx[k] - px, y = cy[k] - py, rx = x * ca - y * sa, ry = x * sa + y * ca, wx, wy;
        const uint8_t *col = w->vcol[k];
        place(h, w->x, w->x + px + rx, w->y + py + ry, &wx, &wy);
        glColor4ub(ps2c(col[0]), ps2c(col[1]), ps2c(col[2]), (GLubyte)(ps2c(col[3]) * alpha));
        glTexCoord2f(w->uv[k][0], w->uv[k][1]);
        glVertex2f(wx, wy);
    }
    glEnd();
}

void hp2_hud_text(hp2_hud *h, const hp2_fe_widget *w, const char *s)
{
    const hp2_font *f = hp2_font_find(h->fonts, w->font);
    const hp2_hudtex *c;
    float pen, y = w->y;
    unsigned char prev = 0;
    if (!s) s = w->text;
    if (!f || !(c = get(h, f->hash))) { h->missing++; return; }
    pen = w->x;
    /* bit 0 is on the race screen's time and lap texts, whose x sits at the
     * right end of their panel: read as right-aligned (inferred) */
    if (w->align & HP2_FE_ALIGN_CENTRE) pen -= hp2_text_width(f, s) * w->sx * 0.5f;
    else if (w->align & (HP2_FE_ALIGN_RIGHT | 1u)) pen -= hp2_text_width(f, s) * w->sx;
    glBindTexture(GL_TEXTURE_2D, c->id);
    glColor4ub(ps2c(w->rgba[0]), ps2c(w->rgba[1]), ps2c(w->rgba[2]), ps2c(w->rgba[3]));
    glBegin(GL_QUADS);
    for (;;) {
        hp2_tok t = hp2_text_next(&s);
        hp2_glyph g;
        if (t.kind == HP2_TOK_END || t.kind == HP2_TOK_NEWLINE) break;
        if (t.kind != HP2_TOK_CHAR) continue;
        if (prev) pen += hp2_font_kern(f, prev, t.ch) * w->sx;
        prev = t.ch;
        if (!hp2_font_glyph(f, t.ch, &g)) continue;
        if (g.w > 0 && g.h > 0) {
            float gx = pen + g.xoff * w->sx, gw = g.w * w->sx, gh = g.h * w->sy, x0, y0, x1, y1;
            float u0 = g.x / c->w, v0 = g.y / c->h, u1 = (g.x + g.w) / c->w, v1 = (g.y + g.h) / c->h;
            place(h, w->x, gx, y, &x0, &y0);
            place(h, w->x, gx + gw, y + gh, &x1, &y1);
            glTexCoord2f(u0, v0); glVertex2f(x0, y0);
            glTexCoord2f(u1, v0); glVertex2f(x1, y0);
            glTexCoord2f(u1, v1); glVertex2f(x1, y1);
            glTexCoord2f(u0, v1); glVertex2f(x0, y1);
        }
        pen += g.advance * w->sx;
    }
    glEnd();
}
