/* hp2_tires - see hp2_tires.h. */
#include "hp2_tires.h"

#include <math.h>
#include <string.h>

static const char *const NAMES[HP2_TV_COUNT] = {
    "RoadNoise", "GravelNoise", "LongitudinalSkid", "SquallSkid", "GravelSkid", "BurnOutPeel"
};

void hp2_tires_bind(hp2_tires *t, const hp2_sfx *bank)
{
    int i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < HP2_TV_COUNT; i++) t->v[i].smp = bank ? hp2_sfx_find(bank, NAMES[i]) : NULL;
}

int hp2_tires_loose(int surface)
{
    switch (surface & 0xF) {
    case 2: case 3: case 6: case 0xB: return 1;     /* dirt, gravel, grass, leaves */
    default: return 0;
    }
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int16_t sat(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void hp2_tires_mix(hp2_tires *t, const hp2_tires_in *in, float volume, int16_t *out, int frames)
{
    float target[HP2_TV_COUNT], sp = fabsf(in->speed), sn = clampf(sp / 45.0f, 0.0f, 1.0f), step;
    int loose = hp2_tires_loose(in->surface), slide = in->on_ground && in->sliding > 0 && sp > 4.0f, i, f;
    if (volume <= 0) return;
    if (volume > 1) volume = 1;
    volume *= 0.5f;                                          /* the port's own headroom */
    memset(target, 0, sizeof target);
    if (in->on_ground && sp > 0.5f) {
        if (loose) target[HP2_TV_GRAVEL] = 0.2f + 0.8f * sn;
        else target[HP2_TV_ROAD] = 0.15f + 0.6f * sn;
    }
    if (slide) {
        if (loose) target[HP2_TV_GSKID] = 1.0f;
        else if (fabsf(in->steer) >= 0.1f) target[HP2_TV_SQUALL] = 1.0f;
        else target[HP2_TV_LSKID] = 1.0f;
    }
    if (in->on_ground && !loose && in->throttle > 0.7f && sp < 12.0f && in->sliding > 0) target[HP2_TV_BURN] = 1.0f;
    step = (float)frames / (0.1f * HP2_SFX_RATE);            /* a tenth of a second to change */
    for (i = 0; i < HP2_TV_COUNT; i++) {
        hp2_tvoice *v = &t->v[i];
        if (!v->smp || v->smp->frames <= 0) continue;
        if (v->level < target[i]) { v->level += step; if (v->level > target[i]) v->level = target[i]; }
        else if (v->level > target[i]) { v->level -= step; if (v->level < target[i]) v->level = target[i]; }
        if (v->level <= 0) { v->pos = 0; continue; }
        for (f = 0; f < frames; f++) {
            float g = v->level * volume;
            out[2 * f] = sat(out[2 * f] + (int)(v->smp->pcm[2 * v->pos] * g));
            out[2 * f + 1] = sat(out[2 * f + 1] + (int)(v->smp->pcm[2 * v->pos + 1] * g));
            if (++v->pos >= v->smp->frames) v->pos = 0;
        }
    }
}
