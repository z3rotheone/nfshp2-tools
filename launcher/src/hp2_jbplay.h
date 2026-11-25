/* hp2_jbplay - the front end's music: plays the jukebox's songs whose
 * play status includes the menus (Menu or On), one after another in random
 * order, at the Menu Music volume.
 *
 * It only produces samples (stereo int16 at the bank's rate); the caller
 * hands them to the sound card (fe_main.c queues them to SDL). It follows
 * the settings live: a volume change applies to the next samples. Switching
 * the playing song to Off in the Jukebox stops the music (hp2_jbplay_stop)
 * until a song is played again; songs set to Off are never picked. No SDL.
 */
#ifndef HP2_JBPLAY_H
#define HP2_JBPLAY_H

#include "hp2_jukebox.h"
#include "hp2_settings.h"
#include "hp2_vag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const hp2_jukebox  *jb;
    const hp2_settings *cfg;
    char                path[1024];     /* the bank: ZZDATA.BIN or an extracted file */
    long long           base;           /* where the bank starts in path */
    hp2_stream          st;
    int                 song;           /* playing, or -1 */
    int                 last;           /* the one before, not repeated if avoidable */
    int                 stopped;        /* silent until play, skip or resume */
    uint32_t            rng;
    int                 race;           /* 1: the race's music: songs set to On, Race Music volume */
} hp2_jbplay;

/* The bank's song table must outlive the player. seed picks the order. */
void hp2_jbplay_init(hp2_jbplay *p, const hp2_jukebox *jb, const hp2_settings *cfg,
                     const char *path, long long base, uint32_t seed);
void hp2_jbplay_close(hp2_jbplay *p);

/* A song's play status (as the Jukebox shows it) and whether it plays in
 * the menus. */
hp2_play hp2_jbplay_status(const hp2_jukebox *jb, const hp2_settings *cfg, int song);
int      hp2_jbplay_in_menus(hp2_play p);

/* Up to frames stereo frames into out, with the Menu Music volume applied.
 * Starts the next song when one ends. Returns 0 when no song may play in
 * the menus (out untouched). */
int hp2_jbplay_fill(hp2_jbplay *p, int16_t *out, int frames);

/* Race mode: only songs whose status is On play (Menu ones are for the
 * menus), at the Race Music volume. Set it before the first fill. */
void hp2_jbplay_set_race(hp2_jbplay *p, int race);

/* The song playing, or -1. */
int hp2_jbplay_now(const hp2_jbplay *p);

/* Start song now (whatever its status: the player asked for it). 1 if it
 * opened. The caller drops what it has queued for the sound card. */
int hp2_jbplay_play(hp2_jbplay *p, int song);

/* End the song playing; the next fill picks another (Next Song). */
void hp2_jbplay_skip(hp2_jbplay *p);

/* Stop the music: fill gives nothing until hp2_jbplay_play, _skip or
 * _resume. The caller drops what it has queued for the sound card. */
void hp2_jbplay_stop(hp2_jbplay *p);
void hp2_jbplay_resume(hp2_jbplay *p);
int  hp2_jbplay_stopped(const hp2_jbplay *p);

#ifdef __cplusplus
}
#endif
#endif
