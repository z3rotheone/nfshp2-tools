/* hp2_fe - front-end screen layouts. See hp2_fe.h. */
#include "hp2_fe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH_SCREENS 0x80010200u
#define CH_SCREEN  0x80010201u
#define CH_HEADER  0x00010203u
#define CH_IMAGE   0x00010210u
#define CH_TEXT    0x00010212u

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

uint32_t hp2_fe_hash(const char *s)
{
    uint32_t x = 0xFFFFFFFFu;
    for (; *s; s++) x = x * 33u + (uint8_t)*s;
    return x;
}

/* Next chunk header at *o inside [0,end): id and body size; 0 when it does
 * not fit, so a damaged file stops the walk instead of running off. */
static int chunk_at(const uint8_t *b, size_t o, size_t end, uint32_t *id, size_t *sz)
{
    if (o + 8 > end) return 0;
    *id = rd32(b + o);
    *sz = rd32(b + o + 4);
    return *sz <= end - o - 8;
}

static int parse_widget(const uint8_t *b, size_t sz, int text, hp2_fe_widget *w)
{
    size_t n;
    memset(w, 0, sizeof *w);
    if (sz < 0x58) return -1;
    w->kind = text ? HP2_FE_TEXT : HP2_FE_IMAGE;
    memcpy(w->name, b + 16, 24);
    w->name[24] = 0;
    w->name_hash = rd32(b + 8);
    w->x = rdf(b + 0x30);
    w->y = rdf(b + 0x34);
    w->sx = rdf(b + 0x38);
    w->sy = rdf(b + 0x3C);
    w->visible = b[0x29];
    w->layer = b[0x2A];
    if (text) {
        if (sz < 0x64) return -1;
        w->font = rd32(b + 0x50);
        w->capacity = rd32(b + 0x54);
        w->align = rd32(b + 0x58);
        memcpy(w->rgba, b + 0x5C, 4);
        w->string_id = rd32(b + 0x60);
        for (n = 0; n < sizeof w->text - 1 && 0x64 + n < sz && b[0x64 + n]; n++)
            w->text[n] = (char)b[0x64 + n];
    } else {
        static const float unit[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        int i;
        w->texture = rd32(b + 0x50);
        /* Every image in the shipped file is 0x90 bytes; a shorter one gets
         * white corners and the whole texture. */
        if (sz >= 0x90) {
            memcpy(w->vcol, b + 0x58, 16);
            for (i = 0; i < 4; i++) {
                w->uv[i][0] = rdf(b + 0x70 + 8 * i);
                w->uv[i][1] = rdf(b + 0x74 + 8 * i);
            }
        } else {
            memset(w->vcol, 0x80, sizeof w->vcol);
            memcpy(w->uv, unit, sizeof w->uv);
        }
    }
    return 0;
}

/* One 80010201 screen: a 00010203 header and its widgets. The header's u16
 * count is only a hint; the walk decides how many widgets there are. On
 * failure nothing is left allocated. */
static int parse_screen(const uint8_t *b, size_t sz, hp2_fe_screen *s)
{
    size_t o = 0, csz, cap = 0, fill = 0;
    uint32_t id;
    int have_header = 0;
    memset(s, 0, sizeof *s);
    while (chunk_at(b, o, sz, &id, &csz)) {
        const uint8_t *body = b + o + 8;
        if (id == CH_HEADER) {
            if (csz < 0x20) goto bad;
            s->id = rd32(body + 8);
            s->width = (int)rd16(body + 0x18);
            s->height = (int)rd16(body + 0x1A);
            have_header = 1;
        } else if (id == CH_TEXT || id == CH_IMAGE) {
            if (fill == cap) {
                hp2_fe_widget *nw;
                size_t ncap = cap ? cap * 2 : 16;
                nw = (hp2_fe_widget *)realloc(s->widgets, ncap * sizeof *nw);
                if (!nw) goto bad;
                s->widgets = nw;
                cap = ncap;
            }
            if (parse_widget(body, csz, id == CH_TEXT, &s->widgets[fill]) < 0) goto bad;
            fill++;
        }
        o += 8 + csz;
    }
    if (!have_header) goto bad;
    s->count = (int)fill;
    return 0;
bad:
    free(s->widgets);
    memset(s, 0, sizeof *s);
    return -1;
}

/* Collects the 80010201 screens inside one 80010200 body. */
static int parse_screens(const uint8_t *b, size_t sz, hp2_fe_layout *l)
{
    size_t o = 0, csz, cap = (size_t)l->count;
    uint32_t id;
    while (chunk_at(b, o, sz, &id, &csz)) {
        if (id == CH_SCREEN) {
            if ((size_t)l->count == cap) {
                hp2_fe_screen *ns;
                size_t ncap = cap ? cap * 2 : 64;
                ns = (hp2_fe_screen *)realloc(l->screens, ncap * sizeof *ns);
                if (!ns) return -1;
                l->screens = ns;
                cap = ncap;
            }
            if (parse_screen(b + o + 8, csz, &l->screens[l->count]) < 0) return -1;
            l->count++;
        }
        o += 8 + csz;
    }
    return 0;
}

/* Walks a chunk list looking for 80010200, descending into other containers
 * (high bit set) so it is found wherever the bundle keeps it. depth bounds
 * the recursion on damaged data. Returns 1 when found and parsed, 0 when not
 * here, -1 on bad data. */
static int find_screens(const uint8_t *b, size_t sz, hp2_fe_layout *l, int depth)
{
    size_t o = 0, csz;
    uint32_t id;
    if (depth > 8) return 0;
    while (chunk_at(b, o, sz, &id, &csz)) {
        const uint8_t *body = b + o + 8;
        if (id == CH_SCREENS) return parse_screens(body, csz, l) < 0 ? -1 : 1;
        if (id & 0x80000000u) {
            int r = find_screens(body, csz, l, depth + 1);
            if (r) return r;
        }
        o += 8 + csz;
    }
    return 0;
}

int hp2_fe_parse(const uint8_t *buf, size_t len, hp2_fe_layout *out)
{
    int r;
    memset(out, 0, sizeof *out);
    if (!buf) return -1;
    r = find_screens(buf, len, out, 0);
    if (r <= 0 || out->count == 0) {
        hp2_fe_free(out);
        return -1;
    }
    return 0;
}

void hp2_fe_free(hp2_fe_layout *l)
{
    int i;
    if (!l) return;
    for (i = 0; i < l->count; i++) free(l->screens[i].widgets);
    free(l->screens);
    l->screens = NULL;
    l->count = 0;
}

const hp2_fe_screen *hp2_fe_find_id(const hp2_fe_layout *l, uint32_t id)
{
    int i;
    if (!l) return NULL;
    for (i = 0; i < l->count; i++)
        if (l->screens[i].id == id) return &l->screens[i];
    return NULL;
}

/* Screen ids hash the upper-case name; accept any case. */
const hp2_fe_screen *hp2_fe_find(const hp2_fe_layout *l, const char *name)
{
    char up[64];
    size_t n;
    if (!name) return NULL;
    for (n = 0; name[n]; n++) {
        if (n == sizeof up - 1) return NULL;
        up[n] = (name[n] >= 'a' && name[n] <= 'z') ? (char)(name[n] - 32) : name[n];
    }
    up[n] = 0;
    return hp2_fe_find_id(l, hp2_fe_hash(up));
}

const hp2_fe_widget *hp2_fe_row(const hp2_fe_screen *s, int n)
{
    char want[25];
    int i;
    if (!s || n < 1 || n > 9999) return NULL;
    snprintf(want, sizeof want, "MENUITEM_%d", n);
    for (i = 0; i < s->count; i++)
        if (s->widgets[i].kind == HP2_FE_TEXT && strcmp(s->widgets[i].name, want) == 0)
            return &s->widgets[i];
    return NULL;
}

int hp2_fe_row_count(const hp2_fe_screen *s)
{
    int n = 0;
    while (hp2_fe_row(s, n + 1)) n++;
    return n;
}

/* ---- viewport ---------------------------------------------------------- */

void hp2_fe_view_make(hp2_fe_view *v, int win_w, int win_h, int canvas_w, int canvas_h,
                      hp2_fe_fit fit)
{
    float W, H, cw, ch, s;
    memset(v, 0, sizeof *v);
    v->sx = v->sy = 1.0f;
    if (win_w <= 0 || win_h <= 0 || canvas_w <= 0 || canvas_h <= 0) return;
    W = (float)win_w; H = (float)win_h; cw = (float)canvas_w; ch = (float)canvas_h;

    if (fit == HP2_FE_STRETCH) {
        v->sx = W / cw;
        v->sy = H / ch;
        return;
    }
    /* EXPAND scales to the window height. When the window is narrower than
     * the canvas shape that would cut the sides off, so it falls back to FIT
     * (bars above and below). On a wide window FIT and EXPAND place the
     * canvas the same way; they differ in what the renderer puts in the
     * margins (black bars or a background). */
    s = H / ch;
    if (fit == HP2_FE_FIT || cw * s > W) {
        float sw = W / cw;
        if (sw < s) s = sw;
    }
    v->sx = v->sy = s;
    v->ox = (W - cw * s) * 0.5f;
    v->oy = (H - ch * s) * 0.5f;
    v->margin_l = v->margin_r = v->ox > 0 ? v->ox / s : 0.0f;
}

void hp2_fe_to_window(const hp2_fe_view *v, float cx, float cy, float *wx, float *wy)
{
    if (wx) *wx = v->ox + cx * v->sx;
    if (wy) *wy = v->oy + cy * v->sy;
}
