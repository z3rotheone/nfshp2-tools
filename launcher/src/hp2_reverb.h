/* hp2_reverb - the track's echo regions (top-level chunk 0x80038000, chunk
 * 0x38010: "Cave_21", "Cavern_21", "Tunnel_22", "Mine01_22", "Overpass01_21",
 * "Hangar_22", "WaterfallVerb_21"): a name[32], u32 1, u32 n, n ground points
 * (x, y) and a triangulation. The record holds no reverb setting, so how a
 * region sounds (a four-comb echo with a two-stage allpass, its length and
 * level chosen by the region's kind from its name) is the port's own. The
 * effect is applied to the race's effects, not to the music or speech.
 * docs/formats.md, "Ambient sound zones". No SDL, plain C99. */
#ifndef HP2_REVERB_H
#define HP2_REVERB_H

#include <stddef.h>
#include <stdint.h>

#define HP2_REVERB_COMBS 4
#define HP2_REVERB_MAXBUF 4096       /* frames per comb line, at 44,100 Hz (93 ms) */

typedef struct {
    char   name[33];
    int    n;
    float *pt;                /* x y x y ... */
    float  lo[2], hi[2];
    float  wet, decay;        /* port's own: level 0..1 and feedback */
} hp2_reverb_zone;

typedef struct {
    int              nz;
    hp2_reverb_zone *z;
    float            level, feedback;    /* the fade towards the current zone's settings */
    int              comb_len[HP2_REVERB_COMBS], pos[HP2_REVERB_COMBS];
    float            line[HP2_REVERB_COMBS][HP2_REVERB_MAXBUF];   /* mono */
    float            lp;
} hp2_reverb;                   /* hp2_reverb_init before use */

void hp2_reverb_init(hp2_reverb *r);
/* Reads the regions of a decompressed track file; returns how many (-1 on memory failure). */
int  hp2_reverb_parse(hp2_reverb *r, const uint8_t *buf, size_t len);
void hp2_reverb_free(hp2_reverb *r);
/* The kind's wet level and feedback from a region's name (Cave, Cavern, Tunnel, Mine, Hangar,
 * Overpass, Waterfall; anything else a small room). */
void hp2_reverb_kind(const char *name, float *wet, float *decay);
/* Adds the echo of the frames in out (stereo int16, in place) for a listener at (x, y). */
void hp2_reverb_apply(hp2_reverb *r, float x, float y, int16_t *out, int frames);

#endif
