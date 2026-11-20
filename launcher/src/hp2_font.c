/* hp2_font - front-end fonts and string codes. See hp2_font.h. */
#include "hp2_font.h"
#include "hp2_fe.h"

#include <stdlib.h>
#include <string.h>

#define CH_FONT 0x00015010u

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static void glyph_at(const uint8_t *r, hp2_glyph *g)
{
    g->ch = (uint16_t)rd16(r);
    g->kern_count = (uint16_t)rd16(r + 2);
    g->x = rdf(r + 4);
    g->y = rdf(r + 8);
    g->w = rdf(r + 12);
    g->h = rdf(r + 16);
    g->advance = rdf(r + 20);
    g->xoff = rdf(r + 24);
    g->kern_first = rd32(r + 28);
}

/* Chunk = header (20 bytes: hash, glyph count, kerning count, then the
 * offsets of the glyphs and the kerning table counted from the chunk's own
 * id word), 32-byte glyphs, 8-byte kerning pairs. */
static int parse_font(const uint8_t *chunk, size_t total, hp2_font *f)
{
    uint32_t n, k, go, ko;
    int i;
    memset(f, 0, sizeof *f);
    memset(f->index, 0xFF, sizeof f->index);
    if (total < 8 + 20) return -1;
    f->hash = rd32(chunk + 8);
    n = rd32(chunk + 12);
    k = rd32(chunk + 16);
    go = rd32(chunk + 20);
    ko = rd32(chunk + 24);
    if (n == 0 || n > 256 || go < 28 || go > total || (total - go) / 32 < n) return -1;
    f->count = (int)n;
    f->glyphs = chunk + go;
    /* A table that does not fit is dropped. (FONT_EVENTNUMBERS has 50
     * pairs of 0xCD filler; they fit, but no glyph points into them.) */
    if (k && ko >= go + 32 * n && ko <= total && (total - ko) / 8 >= k) {
        f->kern = chunk + ko;
        f->kern_count = (int)k;
    }
    for (i = 0; i < f->count; i++) {
        hp2_glyph g;
        glyph_at(f->glyphs + 32 * i, &g);
        if (g.ch < 256 && f->index[g.ch] < 0) f->index[g.ch] = (int16_t)i;
        if (g.h > f->line_height) f->line_height = g.h;
    }
    return 0;
}

static int walk(hp2_fontset *set, int *cap, const uint8_t *b, size_t len, int depth)
{
    size_t o = 0;
    int added = 0;
    while (o + 8 <= len) {
        uint32_t id = rd32(b + o), sz = rd32(b + o + 4);
        if (sz > len - o - 8) break;
        if (id == CH_FONT) {
            hp2_font f;
            if (parse_font(b + o, 8 + (size_t)sz, &f) == 0 && !hp2_font_find(set, f.hash)) {
                if (set->count == *cap) {
                    int nc = *cap ? *cap * 2 : 8;
                    hp2_font *nf = (hp2_font *)realloc(set->font, (size_t)nc * sizeof *nf);
                    if (!nf) return -1;
                    set->font = nf;
                    *cap = nc;
                }
                set->font[set->count++] = f;
                added++;
            }
        } else if ((id & 0x80000000u) && depth < 4) {
            int r = walk(set, cap, b + o + 8, sz, depth + 1);
            if (r < 0) return -1;
            added += r;
        }
        o += 8 + (size_t)sz;
    }
    return added;
}

int hp2_font_scan(hp2_fontset *set, const uint8_t *buf, size_t len)
{
    int cap = set->count;   /* realloc to exactly count is always safe */
    if (cap) {
        hp2_font *nf = (hp2_font *)realloc(set->font, (size_t)cap * sizeof *nf);
        if (!nf) return -1;
        set->font = nf;
    }
    return walk(set, &cap, buf, len, 0);
}

void hp2_font_free(hp2_fontset *set)
{
    if (!set) return;
    free(set->font);
    set->font = NULL;
    set->count = 0;
}

const hp2_font *hp2_font_find(const hp2_fontset *set, uint32_t hash)
{
    int i;
    if (!set) return NULL;
    for (i = 0; i < set->count; i++)
        if (set->font[i].hash == hash) return &set->font[i];
    return NULL;
}

int hp2_font_glyph(const hp2_font *f, unsigned char c, hp2_glyph *g)
{
    if (!f || f->index[c] < 0) return 0;
    glyph_at(f->glyphs + 32 * f->index[c], g);
    return 1;
}

float hp2_font_kern(const hp2_font *f, unsigned char prev, unsigned char cur)
{
    hp2_glyph g;
    uint32_t i;
    if (!f->kern || !hp2_font_glyph(f, cur, &g)) return 0.0f;
    for (i = 0; i < g.kern_count; i++) {
        uint32_t k = g.kern_first + i;
        if (k >= (uint32_t)f->kern_count) break;
        if (rd32(f->kern + 8 * k) == prev) return rdf(f->kern + 8 * k + 4);
    }
    return 0.0f;
}

/* ---- string codes -------------------------------------------------------- */

static int starts(const char *s, const char *w) { return strncmp(s, w, strlen(w)) == 0; }

hp2_tok hp2_text_next(const char **p)
{
    hp2_tok t;
    const char *s = *p;
    memset(&t, 0, sizeof t);
    if (!s || !*s) { t.kind = HP2_TOK_END; return t; }
    if (*s == '@') { t.kind = HP2_TOK_NEWLINE; *p = s + 1; return t; }
    if (*s == '$' && starts(s, "$RGB(")) {
        int v[3] = { 0, 0, 0 }, n = 0;
        const char *q = s + 5;
        while (n < 3) {
            if (*q < '0' || *q > '9') break;
            while (*q >= '0' && *q <= '9') { v[n] = v[n] * 10 + (*q - '0'); if (v[n] > 255) v[n] = 255; q++; }
            n++;
            if (*q == ',') q++;
        }
        if (n == 3 && *q == ')') {
            t.kind = HP2_TOK_RGB;
            t.rgb[0] = (uint8_t)v[0]; t.rgb[1] = (uint8_t)v[1]; t.rgb[2] = (uint8_t)v[2];
            *p = q + 1;
            return t;
        }
    }
    if (*s == '$' && starts(s, "$FONT(")) {
        const char *q = s + 6, *e = strchr(q, ')');
        if (e && e - q < 32) {
            char name[32];
            memcpy(name, q, (size_t)(e - q));
            name[e - q] = 0;
            t.kind = HP2_TOK_FONT;
            t.font = hp2_fe_hash(name);
            *p = e + 1;
            return t;
        }
    }
    t.kind = HP2_TOK_CHAR;
    t.ch = (unsigned char)*s;
    *p = s + 1;
    return t;
}

float hp2_text_width(const hp2_font *f, const char *s)
{
    float w = 0.0f;
    unsigned char prev = 0;
    for (;;) {
        hp2_tok t = hp2_text_next(&s);
        hp2_glyph g;
        if (t.kind == HP2_TOK_END || t.kind == HP2_TOK_NEWLINE) break;
        if (t.kind != HP2_TOK_CHAR) continue;
        if (prev) w += hp2_font_kern(f, prev, t.ch);
        if (hp2_font_glyph(f, t.ch, &g)) w += g.advance;
        prev = t.ch;
    }
    return w;
}
