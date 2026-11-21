/* hp2_engine - engine sound banks. See hp2_engine.h. */
#include "hp2_engine.h"
#include "hp2_vag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The loop of one sample: from the block flagged 6 (or the first) to the
 * block flagged 3 (or the end). */
static int decode(hp2_esample *s, const uint8_t *b, size_t len)
{
    size_t nb = len / 16, k, first = 0, last = nb;
    hp2_adpcm st = { 0, 0 };
    int16_t tmp[28];
    for (k = 0; k < nb; k++) {
        uint8_t f = b[16 * k + 1];
        if (f == 6 && !first) first = k;
        if ((f == 3 || f == 7) && k >= first) { last = f == 3 ? k + 1 : k; break; }
    }
    if (last <= first) return -1;
    s->pcm = (int16_t *)malloc((last - first) * 28 * sizeof(int16_t));
    if (!s->pcm) return -1;
    for (k = 0; k < last; k++) {
        int16_t *o = k >= first ? s->pcm + (k - first) * 28 : tmp;
        uint8_t blk[16];
        memcpy(blk, b + 16 * k, 16);
        if (blk[1] == 7) blk[1] = 0;
        hp2_adpcm_block(&st, blk, o);
    }
    s->n = (int)(last - first) * 28;
    return 0;
}

static int suffix(const char *name, const char *suf)
{
    size_t a = strlen(name), b = strlen(suf);
    return a >= b && !strcmp(name + a - b, suf);
}

int hp2_engine_load(hp2_engine *e, const uint8_t *msh, size_t msh_len, const uint8_t *msb, size_t msb_len,
                    float idle_rpm, float redline_rpm)
{
    static const char *level[2][3] = { { "id", "Md", "Hi" }, { "Lo", "Md", "Hi" } };
    static const float frac[2][3] = { { 0, 0.55f, 0.80f }, { 0.30f, 0.55f, 0.80f } };
    int n, i, a, l, k;
    memset(e, 0, sizeof *e);
    memset(e->pick, 0xFF, sizeof e->pick);
    e->horn = -1;
    if (msh_len < 12) return -1;
    n = (int)rd32(msh + 8);
    if (n < 1 || n > HP2_ENGINE_MAX || 12 + 52 * (size_t)n > msh_len) return -1;
    for (i = 0; i < n; i++) {
        const uint8_t *r = msh + 12 + 52 * i;
        uint32_t off = rd32(r + 40), end = i + 1 < n ? rd32(r + 52 + 40) : (uint32_t)msb_len;
        hp2_esample *s = &e->s[i];
        memcpy(s->name, r, 32);
        s->name[32] = 0;
        s->rate = rd16(r + 34);
        if (s->rate < 4000 || s->rate > 48000) s->rate = 22050;
        if (end > msb_len || off >= end || decode(s, msb + off, end - off) < 0) { hp2_engine_free(e); return -1; }
        e->count++;
        if (suffix(s->name, "Horn")) e->horn = i;
    }
    for (a = 0; a < 2; a++)
        for (l = 0; l < 2; l++)
            for (k = 0; k < 3; k++) {
                char suf[8];
                suf[0] = a ? 'L' : 'C';
                suf[1] = l ? 'X' : 'E';
                strcpy(suf + 2, level[a][k]);
                for (i = 0; i < n; i++)
                    if (suffix(e->s[i].name, suf)) e->pick[a][l][k] = i;
            }
    for (a = 0; a < 2; a++)
        for (k = 0; k < 3; k++) e->at[a][k] = frac[a][k] > 0 ? frac[a][k] * redline_rpm : idle_rpm;
    e->rpm_last = idle_rpm;
    return e->count ? 0 : -1;
}

void hp2_engine_free(hp2_engine *e)
{
    int i;
    for (i = 0; i < e->count; i++) free(e->s[i].pcm);
    memset(e, 0, sizeof *e);
}

/* The weight of each sample at an rpm and load. */
static void weights(const hp2_engine *e, float rpm, float load, float *g)
{
    int a, l, k;
    memset(g, 0, HP2_ENGINE_MAX * sizeof *g);
    for (a = 0; a < 2; a++) {
        float wa = a ? load : 1 - load;
        int lo = 0, hi = 0;
        float t = 0;
        if (rpm <= e->at[a][0]) { lo = hi = 0; }
        else if (rpm >= e->at[a][2]) { lo = hi = 2; }
        else {
            lo = rpm < e->at[a][1] ? 0 : 1;
            hi = lo + 1;
            t = (rpm - e->at[a][lo]) / (e->at[a][hi] - e->at[a][lo]);
        }
        for (l = 0; l < 2; l++) {
            int s0 = e->pick[a][l][lo], s1 = e->pick[a][l][hi];
            if (s0 >= 0) g[s0] += wa * (1 - t) * 0.5f;
            if (s1 >= 0) g[s1] += wa * t * 0.5f;
        }
        (void)k;
    }
}

/* The rpm a sample stands for. */
static float sample_rpm(const hp2_engine *e, int i)
{
    int a, l, k;
    for (a = 0; a < 2; a++)
        for (l = 0; l < 2; l++)
            for (k = 0; k < 3; k++)
                if (e->pick[a][l][k] == i) return e->at[a][k];
    return 0;
}

void hp2_engine_mix(hp2_engine *e, float rpm, float load, float volume, int16_t *out, int frames, int out_rate)
{
    float g0[HP2_ENGINE_MAX], g1[HP2_ENGINE_MAX], at[HP2_ENGINE_MAX];
    int f, i;
    if (load < 0) load = 0;
    if (load > 1) load = 1;
    weights(e, rpm, load, g1);
    memcpy(g0, e->gain_last, sizeof g0);
    for (i = 0; i < e->count; i++) at[i] = sample_rpm(e, i);
    for (f = 0; f < frames; f++) {
        float t = (float)(f + 1) / frames, r = e->rpm_last + (rpm - e->rpm_last) * t, mix = 0;
        for (i = 0; i < e->count; i++) {
            hp2_esample *s = &e->s[i];
            float g = g0[i] + (g1[i] - g0[i]) * t, pitch, v;
            int j, j2;
            double fr;
            if (g <= 0.0001f || at[i] <= 0 || !s->n) continue;
            pitch = r / at[i];
            if (pitch < 0.3f) pitch = 0.3f;
            if (pitch > 3.0f) pitch = 3.0f;
            j = (int)s->phase;
            fr = s->phase - j;
            j2 = j + 1 < s->n ? j + 1 : 0;
            v = (float)(s->pcm[j] * (1 - fr) + s->pcm[j2] * fr);
            mix += v * g;
            s->phase += pitch * s->rate / out_rate;
            while (s->phase >= s->n) s->phase -= s->n;
        }
        mix *= volume * (0.7f + 0.3f * load);
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out[2 * f] = out[2 * f + 1] = (int16_t)mix;
    }
    /* samples that fell silent keep their place, so they come back smoothly */
    memcpy(e->gain_last, g1, sizeof g1);
    e->rpm_last = rpm;
}
