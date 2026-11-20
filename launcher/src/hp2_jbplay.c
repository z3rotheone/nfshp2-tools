/* hp2_jbplay - menu music from the jukebox. See hp2_jbplay.h. */
#include "hp2_jbplay.h"

#include <stdio.h>
#include <string.h>

hp2_play hp2_jbplay_status(const hp2_jukebox *jb, const hp2_settings *cfg, int song)
{
    if (!jb || song < 0 || song >= jb->count) return HP2_PLAY_OFF;
    if (song < HP2_SONGS && cfg->song[song] >= 0) return (hp2_play)cfg->song[song];
    return hp2_play_from_bank(jb->track[song].bank_status);
}

int hp2_jbplay_in_menus(hp2_play p)
{
    return p == HP2_PLAY_MENU || p == HP2_PLAY_ON;
}

void hp2_jbplay_init(hp2_jbplay *p, const hp2_jukebox *jb, const hp2_settings *cfg,
                     const char *path, long long base, uint32_t seed)
{
    memset(p, 0, sizeof *p);
    p->jb = jb;
    p->cfg = cfg;
    snprintf(p->path, sizeof p->path, "%s", path ? path : "");
    p->base = base;
    p->song = p->last = -1;
    p->rng = seed ? seed : 0x9E3779B9u;
    p->st.done = 1;
}

void hp2_jbplay_close(hp2_jbplay *p)
{
    hp2_stream_close(&p->st);
    p->song = -1;
}

static uint32_t next_rand(hp2_jbplay *p)
{
    p->rng ^= p->rng << 13;
    p->rng ^= p->rng >> 17;
    p->rng ^= p->rng << 5;
    return p->rng;
}

/* A random song that may play in the menus, other than the last one when
 * there is a choice; opens its stream. 0 if none can play. */
static int start_next(hp2_jbplay *p)
{
    int cand[HP2_JB_MAX], n = 0, i, tries;
    hp2_stream_close(&p->st);
    if (p->song >= 0) p->last = p->song;
    p->song = -1;
    for (i = 0; i < p->jb->count; i++)
        if (hp2_jbplay_in_menus(hp2_jbplay_status(p->jb, p->cfg, i))) cand[n++] = i;
    for (tries = 0; tries < n; tries++) {
        int k = cand[next_rand(p) % (uint32_t)n];
        if (k == p->last && n > 1) continue;
        if (hp2_stream_open(&p->st, p->path, p->base + p->jb->track[k].offset) == 0) {
            p->song = k;
            return 1;
        }
    }
    return 0;
}

int hp2_jbplay_fill(hp2_jbplay *p, int16_t *out, int frames)
{
    int got = 0, vol, i, restarts = 0;
    if (!p->jb || !p->path[0] || p->stopped) return 0;
    while (got < frames) {
        int k;
        if (p->st.done) {
            if (restarts++ > p->jb->count || !start_next(p)) break;
        }
        k = hp2_stream_read(&p->st, out + 2 * got, frames - got);
        got += k;
    }
    vol = p->cfg->volume[HP2_VOL_MENU_MUSIC];
    if (vol < 100)
        for (i = 0; i < 2 * got; i++) out[i] = (int16_t)(out[i] * vol / 100);
    return got;
}

int hp2_jbplay_now(const hp2_jbplay *p) { return p->st.done ? -1 : p->song; }

int hp2_jbplay_play(hp2_jbplay *p, int song)
{
    if (!p->jb || song < 0 || song >= p->jb->count) return 0;
    hp2_stream_close(&p->st);
    if (p->song >= 0) p->last = p->song;
    p->song = -1;
    p->stopped = 0;
    if (hp2_stream_open(&p->st, p->path, p->base + p->jb->track[song].offset) != 0) return 0;
    p->song = song;
    return 1;
}

void hp2_jbplay_skip(hp2_jbplay *p)
{
    hp2_stream_close(&p->st);      /* the next fill starts another song */
    p->stopped = 0;
}

void hp2_jbplay_stop(hp2_jbplay *p)
{
    hp2_stream_close(&p->st);
    if (p->song >= 0) p->last = p->song;
    p->song = -1;
    p->stopped = 1;
}

void hp2_jbplay_resume(hp2_jbplay *p) { p->stopped = 0; }

int hp2_jbplay_stopped(const hp2_jbplay *p) { return p->stopped; }
