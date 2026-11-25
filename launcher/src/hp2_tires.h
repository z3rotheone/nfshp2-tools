/* hp2_tires - the car's tyre sounds in a race, from SOUND\INGAME's samples:
 * RoadNoise and GravelNoise (a loop whose level follows the speed on that
 * surface), LongitudinalSkid and SquallSkid (a slide on the road, braking or
 * in a corner), GravelSkid (a slide on loose ground) and BurnOutPeel (wheels
 * spinning at a standstill). The samples are the game's; when each plays and
 * at what level is the port's own, the game's rules not being decoded.
 * No SDL, plain C99. */
#ifndef HP2_TIRES_H
#define HP2_TIRES_H

#include "hp2_sfx.h"

enum { HP2_TV_ROAD, HP2_TV_GRAVEL, HP2_TV_LSKID, HP2_TV_SQUALL, HP2_TV_GSKID, HP2_TV_BURN, HP2_TV_COUNT };

typedef struct {
    const hp2_sfx_sample *smp;
    long                  pos;
    float                 level;     /* 0..1, slewed */
} hp2_tvoice;

typedef struct {
    hp2_tvoice v[HP2_TV_COUNT];
} hp2_tires;

/* What the car is doing: its forward speed (m/s), the surface under it (the
 * collision polygon's flags, low nibble), whether it is on the ground, how
 * many of its four tyres slide, the front wheels' steer angle (radians) and
 * the throttle 0..1. */
typedef struct {
    float speed;
    int   surface, on_ground, sliding;
    float steer, throttle;
} hp2_tires_in;

void hp2_tires_bind(hp2_tires *t, const hp2_sfx *bank);
/* Whether a surface (collision flags) is loose ground: dirt, gravel, grass, leaves. */
int  hp2_tires_loose(int surface);
/* Adds `frames` stereo frames of tyre sound into out at volume 0..1, saturating. */
void hp2_tires_mix(hp2_tires *t, const hp2_tires_in *in, float volume, int16_t *out, int frames);

#endif
