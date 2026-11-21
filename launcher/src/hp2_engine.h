/* hp2_engine - a car's engine sound from its bank: SOUND\NEWE<NAME>.MSH
 * (the sample headers) and .MSB (the samples, PS-ADPCM), as the car
 * database names it (docs/formats.md 12).
 *
 * The .MSH: u32 size, u32 16, u32 count, then count records of 52 bytes:
 * char[32] name, u8 0, u8 1, u16 sample rate (22,050), u16 u16 (SPU ADSR
 * values), u32 offset of the sample in the .MSB, 8 bytes 0. Each sample
 * runs to the next one's offset: a silent block, then a loop (block flags
 * 6 at the start, 3 at the end).
 *
 * An engine bank holds 13: <prefix> + C (coasting, off the throttle) or L
 * (under load) + E (engine) or X (exhaust) + id (idle), Lo, Md or Hi; and
 * <prefix>Horn. The rpm each was recorded at is not in the file; this
 * module assumes idle for "id" and 30%, 55% and 80% of the redline for Lo,
 * Md and Hi, plays the two nearest at the pitch the engine speed asks for
 * and crossfades between coasting and load. No SDL, plain C99.
 */
#ifndef HP2_ENGINE_H
#define HP2_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_ENGINE_MAX 16

typedef struct {
    char     name[33];
    int      rate;
    int16_t *pcm;               /* the loop */
    int      n;
    double   phase;
} hp2_esample;

typedef struct {
    int         count;
    hp2_esample s[HP2_ENGINE_MAX];
    int         pick[2][2][3];  /* [coast, load][engine, exhaust][low, mid, high] -> sample, -1 none */
    float       at[2][3];       /* the rpm each of those stands for */
    int         horn;           /* -1 if none */
    float       rpm_last, gain_last[HP2_ENGINE_MAX];
} hp2_engine;

/* Reads a bank. 0, or -1 when it is not one or memory runs out. */
int  hp2_engine_load(hp2_engine *e, const uint8_t *msh, size_t msh_len, const uint8_t *msb, size_t msb_len,
                     float idle_rpm, float redline_rpm);
void hp2_engine_free(hp2_engine *e);

/* Mixes frames of stereo int16 at out_rate, the engine at rpm with load
 * 0 (coasting) .. 1 (full throttle), volume 0..1. Adds to nothing: out is
 * overwritten. The rpm glides from the last call's to this one's. */
void hp2_engine_mix(hp2_engine *e, float rpm, float load, float volume, int16_t *out, int frames, int out_rate);

#ifdef __cplusplus
}
#endif
#endif
