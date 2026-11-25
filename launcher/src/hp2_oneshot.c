#include "hp2_oneshot.h"

#include <string.h>

void hp2_oneshot_bind(hp2_oneshot *o, const hp2_sfx *bank)
{
    memset(o, 0, sizeof *o);
    o->bank = bank;
}

int hp2_oneshot_play(hp2_oneshot *o, const char *name, float gain)
{
    const hp2_sfx_sample *s = o->bank ? hp2_sfx_find(o->bank, name) : NULL;
    int i, v = 0;
    if (!s || s->frames <= 0) return 0;
    for (i = 0; i < HP2_ONESHOT_VOICES; i++) {
        if (!o->smp[i]) { v = i; break; }
        if (o->pos[i] > o->pos[v]) v = i;
    }
    o->smp[v] = s; o->pos[v] = 0; o->gain[v] = gain;
    return 1;
}

void hp2_oneshot_mix(hp2_oneshot *o, float volume, int16_t *out, int frames)
{
    int v, i;
    if (volume <= 0) return;
    if (volume > 1) volume = 1;
    for (v = 0; v < HP2_ONESHOT_VOICES; v++) {
        const hp2_sfx_sample *s = o->smp[v];
        float g = volume * o->gain[v] * 0.5f;       /* the port's own headroom */
        if (!s) continue;
        for (i = 0; i < frames && o->pos[v] < s->frames; i++, o->pos[v]++) {
            int a = out[2 * i] + (int)(s->pcm[2 * o->pos[v]] * g), b = out[2 * i + 1] + (int)(s->pcm[2 * o->pos[v] + 1] * g);
            out[2 * i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
            out[2 * i + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
        }
        if (o->pos[v] >= s->frames) o->smp[v] = NULL;
    }
}
