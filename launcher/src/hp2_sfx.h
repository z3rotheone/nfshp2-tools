/* hp2_sfx - the menu sounds: the sample banks SOUND\COMMON.MSH/.MSB (menu
 * moves and selects) and SOUND\FRONTEND.MSH/.MSB (car select, event tree,
 * name entry). Each sound is stored twice under one name in a row: the left
 * and the right channel. Decoded here to stereo 44,100 Hz int16 (22,050 Hz
 * samples doubled with a linear interpolation). docs/formats.md, "Sample
 * banks". No SDL, plain C99. */
#ifndef HP2_SFX_H
#define HP2_SFX_H

#include <stddef.h>
#include <stdint.h>

#define HP2_SFX_RATE 44100

typedef struct {
    char     name[33];
    int16_t *pcm;           /* frames * 2, left and right */
    int      frames;
} hp2_sfx_sample;

typedef struct {
    int             count, cap;
    hp2_sfx_sample *s;
} hp2_sfx;                  /* zero it before the first load */

/* Adds a bank's sounds. Returns how many were added, or -1 when it is not a
 * bank or memory runs out. */
int  hp2_sfx_load(hp2_sfx *x, const uint8_t *msh, size_t msh_len, const uint8_t *msb, size_t msb_len);
void hp2_sfx_free(hp2_sfx *x);
/* The sound called name, or NULL. */
const hp2_sfx_sample *hp2_sfx_find(const hp2_sfx *x, const char *name);

#endif
