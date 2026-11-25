/* hp2_ambient - see hp2_ambient.h. */
#include "hp2_ambient.h"

#include <stdlib.h>
#include <string.h>

#define CH_ZONES  0x80038000u
#define CH_BED    0x00038020u
#define CH_ONCE   0x00038030u

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p)
{
    uint32_t w = rd32(p);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static int add_zone(hp2_ambient *a, const uint8_t *r, size_t sz, int bed)
{
    /* bed:  name[32] sample[32] gain f32, f32, u32, f32 x3, u32 n, n * (x y), triangles
     * once: name[32] sample[32] period, spread, 3 f32, u32 n, n * (x y), triangles */
    size_t head = bed ? 88 : 84;
    uint32_t n;
    hp2_amb_zone *z;
    int i;
    if (sz < head + 4) return 0;
    n = rd32(r + head);
    if (n < 3 || n > 4096 || head + 4 + 8 * (size_t)n > sz) return 0;
    {   hp2_amb_zone *nz = (hp2_amb_zone *)realloc(a->z, (size_t)(a->nz + 1) * sizeof *nz);
        if (!nz) return -1;
        a->z = nz;
    }
    z = &a->z[a->nz];
    memset(z, 0, sizeof *z);
    z->pt = (float *)malloc(2 * (size_t)n * sizeof(float));
    if (!z->pt) return -1;
    a->nz++;
    z->bed = bed;
    memcpy(z->sample, r + 32, 32);
    z->sample[32] = 0;
    if (bed) z->gain = rdf(r + 64);
    else { z->period = rdf(r + 64); z->spread = rdf(r + 68); }
    if (!(z->gain >= 0)) z->gain = 0;
    if (z->gain > 1.5f) z->gain = 1.5f;
    if (!(z->period >= 0) || z->period > 600) z->period = 15;
    if (!(z->spread >= 0) || z->spread > 600) z->spread = 0;
    z->n = (int)n;
    z->lo[0] = z->lo[1] = 1e30f;
    z->hi[0] = z->hi[1] = -1e30f;
    for (i = 0; i < (int)n; i++) {
        int k;
        z->pt[2 * i] = rdf(r + head + 4 + 8 * i);
        z->pt[2 * i + 1] = rdf(r + head + 8 + 8 * i);
        for (k = 0; k < 2; k++) {
            if (z->pt[2 * i + k] < z->lo[k]) z->lo[k] = z->pt[2 * i + k];
            if (z->pt[2 * i + k] > z->hi[k]) z->hi[k] = z->pt[2 * i + k];
        }
    }
    z->pos = -1;
    return 1;
}

int hp2_ambient_parse(hp2_ambient *a, const uint8_t *buf, size_t len)
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
                if (cid == CH_BED || cid == CH_ONCE)
                    if (add_zone(a, q + 8, csz, cid == CH_BED) < 0) return -1;
                q += 8 + csz;
            }
        }
        p = o + sz;
    }
    return a->nz;
}

void hp2_ambient_free(hp2_ambient *a)
{
    int i;
    for (i = 0; i < a->nz; i++) free(a->z[i].pt);
    free(a->z);
    memset(a, 0, sizeof *a);
}

const char *hp2_ambient_bank_name(int track)
{
    switch (track / 10) {
    case 1: return "PARKLAND";
    case 2: return "DESERT";
    case 3: return "GREECE";
    case 4: return "ALPINE";
    case 6: return "TROPICS";
    default: return NULL;
    }
}

static uint32_t rnd(hp2_ambient *a)
{
    a->rng ^= a->rng << 13;
    a->rng ^= a->rng >> 17;
    a->rng ^= a->rng << 5;
    return a->rng;
}

static long wait_frames(hp2_ambient *a, const hp2_amb_zone *z)
{
    double s = z->period + z->spread * (rnd(a) % 1000) / 1000.0;
    return (long)(s * HP2_SFX_RATE);
}

void hp2_ambient_bind(hp2_ambient *a, const hp2_sfx *bank, uint32_t seed)
{
    int i;
    a->rng = seed ? seed : 1;
    for (i = 0; i < a->nz; i++) {
        hp2_amb_zone *z = &a->z[i];
        z->smp = bank ? hp2_sfx_find(bank, z->sample) : NULL;
        z->level = 0;
        z->pos = -1;
        z->wait = z->bed ? 0 : wait_frames(a, z) / 3;   /* the first one comes sooner */
    }
}

int hp2_amb_inside(const hp2_amb_zone *z, float x, float y)
{
    int i, j, in = 0;
    if (x < z->lo[0] || x > z->hi[0] || y < z->lo[1] || y > z->hi[1]) return 0;
    for (i = 0, j = z->n - 1; i < z->n; j = i++) {
        float xi = z->pt[2 * i], yi = z->pt[2 * i + 1], xj = z->pt[2 * j], yj = z->pt[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

static int16_t sat(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void hp2_ambient_mix(hp2_ambient *a, float x, float y, float volume, int16_t *out, int frames)
{
    int i;
    if (volume <= 0) return;
    if (volume > 1) volume = 1;
    volume *= 0.5f;                     /* the port's own headroom: the samples are mastered loud */
    for (i = 0; i < a->nz; i++) {
        hp2_amb_zone *z = &a->z[i];
        int inside, f;
        if (!z->smp || z->smp->frames <= 0) continue;
        inside = hp2_amb_inside(z, x, y);
        if (z->bed && inside) {         /* neighbouring zones of one sound (five waterfalls) play once, not summed */
            int j;
            for (j = 0; j < i; j++)
                if (a->z[j].bed && a->z[j].smp == z->smp && hp2_amb_inside(&a->z[j], x, y)) { inside = 0; break; }
        }
        if (z->bed) {
            float step = (float)frames / (0.5f * HP2_SFX_RATE);
            z->level += inside ? step : -step;
            if (z->level > 1) z->level = 1;
            if (z->level < 0) z->level = 0;
            if (z->level <= 0) { z->pos = -1; continue; }
            if (z->pos < 0) z->pos = 0;
            for (f = 0; f < frames; f++) {
                float g = z->level * z->gain * volume;
                out[2 * f] = sat(out[2 * f] + (int)(z->smp->pcm[2 * z->pos] * g));
                out[2 * f + 1] = sat(out[2 * f + 1] + (int)(z->smp->pcm[2 * z->pos + 1] * g));
                if (++z->pos >= z->smp->frames) z->pos = 0;
            }
        } else {
            if (z->pos < 0) {
                if (!inside) continue;
                z->wait -= frames;
                if (z->wait > 0) continue;
                z->pos = 0;
            }
            for (f = 0; f < frames && z->pos >= 0; f++) {
                out[2 * f] = sat(out[2 * f] + (int)(z->smp->pcm[2 * z->pos] * volume * 0.8f));
                out[2 * f + 1] = sat(out[2 * f + 1] + (int)(z->smp->pcm[2 * z->pos + 1] * volume * 0.8f));
                if (++z->pos >= z->smp->frames) { z->pos = -1; z->wait = wait_frames(a, z); }
            }
        }
    }
}
