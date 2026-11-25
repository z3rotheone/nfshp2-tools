/* hp2_speech - the police radio: SOUND\SPEECH\ENGLISHBAN.PCK (index) and
 * ENGLISHDAT.PCK (one 24,000 Hz mono VAG per clip, 300 MB, read a clip at a
 * time), named by the line vocabulary of entry 367 (docs/formats.md section
 * 16). Lines are named "<cop><Event>": 19, 27, 31 and 46 are the four cop
 * voices, e.g. "27Arrest". WHICH line the game plays for which event lives in
 * EVENTS.EVT, which is not decoded: the port picks lines by name for its own
 * triggers. One line plays at a time. No SDL, plain C99. */
#ifndef HP2_SPEECH_H
#define HP2_SPEECH_H

#include <stddef.h>
#include <stdint.h>

#define HP2_SPEECH_MAXLINES 1024

typedef struct {
    uint32_t line;          /* line number */
    uint32_t dat;           /* offset of the clip's VAG in DAT.PCK */
    int      variant;       /* -1 for the main clip */
} hp2_speech_clip;

typedef struct {
    int              nclips, nnames;
    hp2_speech_clip *clip;
    uint32_t        *name_line;     /* line number of vocabulary entry i */
    char           (*name)[32];
    char             dat[1024];     /* the file holding DAT.PCK */
    long long        dat_base;      /* where DAT.PCK starts in it */
    int16_t         *pcm;           /* the line playing: stereo 44,100 Hz */
    int              frames, pos;
    unsigned         seed;
} hp2_speech;

/* ban and vocab (entry 367) are read at once and kept apart from the
 * buffers; dat names the file and the offset of DAT.PCK. 0 on success. */
int  hp2_speech_open(hp2_speech *s, const uint8_t *ban, size_t ban_len, const uint8_t *vocab, size_t vocab_len,
                     const char *dat, long long dat_base);
void hp2_speech_close(hp2_speech *s);
/* Whether a line named <cop><suffix> exists for some cop voice. */
int  hp2_speech_has(const hp2_speech *s, const char *suffix);
/* Starts a line: suffix such as "Arrest" with one of the four cop voices
 * picked at random. Returns 1 when it started, 0 when the line is absent or
 * another is still playing (unless urgent, which cuts it off). */
int  hp2_speech_say(hp2_speech *s, const char *suffix, int urgent);
int  hp2_speech_busy(const hp2_speech *s);
/* Adds the playing line, at volume 0..1, into out (stereo int16). */
void hp2_speech_mix(hp2_speech *s, float volume, int16_t *out, int frames);

#endif
