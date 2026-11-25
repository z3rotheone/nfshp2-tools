/* hp2_oneshot - one-shot race sounds from a sample bank (crashes, horn,
 * countdown beeps, the finish line): up to eight at once, each played once
 * from the start. Which sound goes with which event, and the volume, are
 * the port's own. No SDL, plain C99. */
#ifndef HP2_ONESHOT_H
#define HP2_ONESHOT_H

#include "hp2_sfx.h"

#define HP2_ONESHOT_VOICES 8

typedef struct {
    const hp2_sfx_sample *smp[HP2_ONESHOT_VOICES];
    int   pos[HP2_ONESHOT_VOICES];
    float gain[HP2_ONESHOT_VOICES];
    const hp2_sfx *bank;
} hp2_oneshot;

void hp2_oneshot_bind(hp2_oneshot *o, const hp2_sfx *bank);
/* Starts name (at gain 0..1; a free voice or the one furthest along). 1 if found. */
int  hp2_oneshot_play(hp2_oneshot *o, const char *name, float gain);
/* Adds the playing sounds at volume 0..1 into out (stereo int16). */
void hp2_oneshot_mix(hp2_oneshot *o, float volume, int16_t *out, int frames);

#endif
