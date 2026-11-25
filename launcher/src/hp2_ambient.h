/* hp2_ambient - a course's ambient sounds: the sound zones of the track file
 * (top-level chunk 0x80038000: 0x38020 steady beds such as Crickets, Stream,
 * Sandstorm; 0x38030 now and then sounds such as birds, coyotes, thunder;
 * each a name, a sample of the location's SOUND\<LOCATION> bank and a polygon
 * on the ground) played from the car's place. docs/formats.md, "Ambient sound
 * zones". Which zone is where is the game's; how they are mixed (a bed fades
 * in over half a second inside its polygon, a one-shot comes every period plus
 * up to spread seconds while the car is inside, one volume) is the port's own:
 * the game's emitter rules are not decoded. No SDL, plain C99. */
#ifndef HP2_AMBIENT_H
#define HP2_AMBIENT_H

#include "hp2_sfx.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int            bed;            /* 1 a steady loop, 0 an occasional sound */
    char           sample[33];     /* the bank's name for it */
    float          gain;           /* bed: the file's level, 0..1.5 */
    float          period, spread; /* one-shot: seconds between sounds, plus up to this */
    int            n;
    float         *pt;             /* n ground points, x y x y... */
    float          lo[2], hi[2];   /* the polygon's box */
    const hp2_sfx_sample *smp;     /* bound by hp2_ambient_bind */
    float          level;          /* bed: 0..1 fade */
    long           pos;            /* frame being played, -1 = none */
    long           wait;           /* one-shot: frames to the next */
} hp2_amb_zone;

typedef struct {
    int           nz;
    hp2_amb_zone *z;
    uint32_t      rng;
} hp2_ambient;                     /* zero it before the first parse */

/* Reads the zones of a decompressed track file. Returns how many, or -1 on a
 * memory failure; a file without the chunk gives 0. */
int  hp2_ambient_parse(hp2_ambient *a, const uint8_t *buf, size_t len);
void hp2_ambient_free(hp2_ambient *a);
/* The location's sample bank for a course number (11 -> "PARKLAND"...), or NULL. */
const char *hp2_ambient_bank_name(int track);
/* Finds each zone's sample in the bank; zones without one are dropped from play. */
void hp2_ambient_bind(hp2_ambient *a, const hp2_sfx *bank, uint32_t seed);
/* Adds `frames` stereo frames (44,100 Hz) of the zones around (x, y) into out,
 * at volume 0..1 (the Ambient Sound Effects setting), saturating. */
void hp2_ambient_mix(hp2_ambient *a, float x, float y, float volume, int16_t *out, int frames);
/* Whether the ground point is inside the zone's polygon. */
int  hp2_amb_inside(const hp2_amb_zone *z, float x, float y);

#endif
