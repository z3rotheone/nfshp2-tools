#include "hp2_reverb.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CH_ZONES 0x80038000u
#define CH_VERB  0x00038010u

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t w = rd32(p); float f; memcpy(&f, &w, 4); return f; }

void hp2_reverb_init(hp2_reverb *r)
{
    static const int len[HP2_REVERB_COMBS] = { 1307, 1637, 1811, 1927 };    /* 29.6 .. 43.7 ms */
    int i;
    memset(r, 0, sizeof *r);
    for (i = 0; i < HP2_REVERB_COMBS; i++) r->comb_len[i] = len[i];
}

void hp2_reverb_kind(const char *name, float *wet, float *decay)
{
    struct { const char *key; float wet, decay; } k[] = {
        { "Cavern", 0.45f, 0.86f }, { "Cave", 0.40f, 0.82f }, { "Tunnel", 0.40f, 0.80f }, { "Mine", 0.38f, 0.80f },
        { "Hangar", 0.40f, 0.82f }, { "Overpass", 0.22f, 0.65f }, { "Waterfall", 0.28f, 0.72f },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof *k; i++)
        if (!strncasecmp(name, k[i].key, strlen(k[i].key))) { *wet = k[i].wet; *decay = k[i].decay; return; }
    *wet = 0.2f; *decay = 0.6f;
}

static int add_zone(hp2_reverb *r, const uint8_t *q, size_t sz)
{
    uint32_t n;
    hp2_reverb_zone *z;
    int i, k;
    if (sz < 32 + 8) return 0;
    n = rd32(q + 36);
    if (n < 3 || n > 4096 || 40 + 8 * (size_t)n > sz) return 0;
    {   hp2_reverb_zone *nz = (hp2_reverb_zone *)realloc(r->z, (size_t)(r->nz + 1) * sizeof *nz);
        if (!nz) return -1;
        r->z = nz;
    }
    z = &r->z[r->nz];
    memset(z, 0, sizeof *z);
    z->pt = (float *)malloc(2 * (size_t)n * sizeof(float));
    if (!z->pt) return -1;
    r->nz++;
    memcpy(z->name, q, 32);
    z->name[32] = 0;
    z->n = (int)n;
    z->lo[0] = z->lo[1] = 1e30f; z->hi[0] = z->hi[1] = -1e30f;
    for (i = 0; i < (int)n; i++)
        for (k = 0; k < 2; k++) {
            float v = rdf(q + 40 + 8 * i + 4 * k);
            z->pt[2 * i + k] = v;
            if (v < z->lo[k]) z->lo[k] = v;
            if (v > z->hi[k]) z->hi[k] = v;
        }
    hp2_reverb_kind(z->name, &z->wet, &z->decay);
    return 1;
}

int hp2_reverb_parse(hp2_reverb *r, const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf, *e = buf + len;
    while (p + 8 <= e) {
        uint32_t id = rd32(p), sz = rd32(p + 4);
        const uint8_t *o = p + 8;
        if (sz > (size_t)(e - o)) break;
        if (id == CH_ZONES) {
            const uint8_t *q = o, *qe = o + sz;
            while (q + 8 <= qe) {
                uint32_t cid = rd32(q), csz = rd32(q + 4);
                if (csz > (size_t)(qe - q - 8)) break;
                if (cid == CH_VERB && add_zone(r, q + 8, csz) < 0) return -1;
                q += 8 + csz;
            }
        }
        p = o + sz;
    }
    return r->nz;
}

void hp2_reverb_free(hp2_reverb *r)
{
    int i;
    for (i = 0; i < r->nz; i++) free(r->z[i].pt);
    free(r->z);
    r->z = NULL; r->nz = 0;
}

static int inside(const hp2_reverb_zone *z, float x, float y)
{
    int i, j, in = 0;
    if (x < z->lo[0] || x > z->hi[0] || y < z->lo[1] || y > z->hi[1]) return 0;
    for (i = 0, j = z->n - 1; i < z->n; j = i++) {
        float xi = z->pt[2 * i], yi = z->pt[2 * i + 1], xj = z->pt[2 * j], yj = z->pt[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

void hp2_reverb_apply(hp2_reverb *r, float x, float y, int16_t *out, int frames)
{
    float wet = 0, decay = 0.6f;
    int i, c;
    for (i = 0; i < r->nz; i++)
        if (inside(&r->z[i], x, y) && r->z[i].wet > wet) { wet = r->z[i].wet; decay = r->z[i].decay; }
    for (i = 0; i < frames; i++) {
        float in, sum = 0, o;
        r->level += (wet - r->level) * (1.0f / 11025.0f);       /* about a quarter second to follow */
        r->feedback += (decay - r->feedback) * (1.0f / 11025.0f);
        if (r->level < 0.001f && wet == 0) {                    /* dry and decayed: keep the lines emptying */
            r->level = 0;
        }
        in = 0.5f * (out[2 * i] + out[2 * i + 1]);
        for (c = 0; c < HP2_REVERB_COMBS; c++) {
            float *l = r->line[c];
            float d = l[r->pos[c]];
            sum += d;
            l[r->pos[c]] = in * 0.5f + d * r->feedback;
            if (++r->pos[c] >= r->comb_len[c]) r->pos[c] = 0;
        }
        r->lp += (sum * 0.25f - r->lp) * 0.5f;                  /* darken the tail */
        o = r->lp * r->level * 2.0f;
        {   int a = out[2 * i] + (int)o, b = out[2 * i + 1] + (int)o;
            out[2 * i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
            out[2 * i + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
        }
    }
}
