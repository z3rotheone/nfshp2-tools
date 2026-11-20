/* hp2_jukebox - a music bank's song table. See hp2_jukebox.h. */
#include "hp2_jukebox.h"

#include <stdlib.h>
#include <string.h>

#define HEAD 0x60
#define REC  0x60
#define TEXT 0x0C               /* text offset inside a record */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

size_t hp2_jukebox_header_size(void) { return HEAD + (size_t)REC * HP2_JB_MAX; }

static size_t nlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static void copy(char *dst, size_t cap, const char *s, size_t n)
{
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
    while (n && dst[n - 1] == ' ') dst[--n] = 0;      /* "...-Rock-2 " */
}

/* "Artist-Title-Album-Genre-N": the last three fields are split from the
 * right, the artist up to the first '-', and the title is what is left (so a
 * title may contain '-'). */
static int split(const char *s, hp2_jb_track *t)
{
    const char *end = s + strlen(s), *p, *d[3], *first;
    int k = 0;
    while (end > s && end[-1] == ' ') end--;
    for (p = end; p > s && k < 3; p--)
        if (p[-1] == '-') d[k++] = p - 1;               /* d[0] before N, d[1] before genre, d[2] before album */
    if (k < 3) return -1;
    first = strchr(s, '-');
    if (!first || first >= d[2]) return -1;
    copy(t->artist, sizeof t->artist, s, (size_t)(first - s));
    copy(t->title, sizeof t->title, first + 1, (size_t)(d[2] - first - 1));
    copy(t->album, sizeof t->album, d[2] + 1, (size_t)(d[1] - d[2] - 1));
    copy(t->genre, sizeof t->genre, d[1] + 1, (size_t)(d[0] - d[1] - 1));
    if (d[0] + 1 >= end || d[0][1] < '0' || d[0][1] > '9') return -1;
    t->bank_status = atoi(d[0] + 1);
    return t->artist[0] && t->title[0] && t->genre[0] ? 0 : -1;
}

int hp2_jukebox_parse(hp2_jukebox *jb, const uint8_t *buf, size_t len)
{
    uint32_t n, i;
    int g;
    memset(jb, 0, sizeof *jb);
    if (!buf || len < HEAD || rd32(buf) != 0x10) return -1;
    n = rd32(buf + 0x20);
    if (n == 0 || n > HP2_JB_MAX || (size_t)HEAD + (size_t)REC * n > len) return -1;
    copy(jb->name, sizeof jb->name, (const char *)buf + 0x24, nlen((const char *)buf + 0x24, 0x3C));
    for (i = 0; i < n; i++) {
        const char *txt = (const char *)buf + HEAD + REC * i + TEXT;
        char line[REC - TEXT + 1];
        size_t L = nlen(txt, REC - TEXT);
        memcpy(line, txt, L);
        line[L] = 0;
        if (split(line, &jb->track[i]) != 0) { memset(jb, 0, sizeof *jb); return -1; }
    }
    jb->count = (int)n;
    for (i = 0; i < n; i++) {
        for (g = 0; g < jb->genres; g++)
            if (!strcmp(jb->genre[g], jb->track[i].genre)) break;
        if (g == jb->genres && jb->genres < 16)
            copy(jb->genre[jb->genres++], sizeof jb->genre[0], jb->track[i].genre, strlen(jb->track[i].genre));
    }
    return 0;
}

int hp2_jukebox_songs(const hp2_jukebox *jb, int g, int *idx, int cap)
{
    int i, n = 0;
    if (!jb || g < 0 || g >= jb->genres) return 0;
    for (i = 0; i < jb->count; i++)
        if (!strcmp(jb->track[i].genre, jb->genre[g])) {
            if (n < cap) idx[n] = i;
            n++;
        }
    return n < cap ? n : cap;
}
