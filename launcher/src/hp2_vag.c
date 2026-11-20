/* hp2_vag - PS-ADPCM and music streams. See hp2_vag.h. */
#if !defined(_WIN32)
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "hp2_vag.h"

#include <string.h>

static const int F0[5] = { 0, 60, 115, 98, 122 };
static const int F1[5] = { 0, 0, -52, -55, -60 };

/* x >> n rounding down, also for negative x (where C leaves >> to the
 * compiler); tools/hp2_audio.py relies on Python's floor shift. */
static int asr(int x, int n) { return x >= 0 ? x >> n : -((-x + (1 << n) - 1) >> n); }

int hp2_adpcm_block(hp2_adpcm *st, const uint8_t *b, int16_t *out)
{
    int shift = b[0] & 0x0F, pred = (b[0] >> 4) & 0x0F, i;
    int h1 = st->h1, h2 = st->h2;
    if (b[1] == 7) return 0;
    if (pred > 4) pred = 0;          /* as the hardware does */
    if (shift > 12) shift = 9;
    for (i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0x0F;   /* low nibble first */
        int s = nib > 7 ? nib - 16 : nib;
        /* (s << 12) >> shift, with a multiply: C may not shift a negative left */
        int v = asr(s * 4096, shift) + asr(F0[pred] * h1 + F1[pred] * h2 + 32, 6);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
        h2 = h1;
        h1 = v;
    }
    st->h1 = h1;
    st->h2 = h2;
    return 28;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int hp2_vag_header(const uint8_t *p, size_t len, uint32_t *size, uint32_t *rate)
{
    if (len < HP2_VAG_HEADER || memcmp(p, "VAGp", 4) != 0) return -1;
    *size = be32(p + 12);
    *rate = be32(p + 16);
    if (*rate < 2000 || *rate > 48000 || *size < 16) return -1;
    return 0;
}

static int seek(FILE *f, long long off)
{
#if defined(_WIN32)
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

int hp2_stream_open(hp2_stream *s, const char *path, long long vag)
{
    uint8_t h[HP2_VAG_HEADER];
    memset(s, 0, sizeof *s);
    s->f = fopen(path, "rb");
    if (!s->f) return -1;
    if (seek(s->f, vag) != 0 || fread(h, 1, sizeof h, s->f) != sizeof h
        || hp2_vag_header(h, sizeof h, &s->size, &s->rate) != 0) {
        hp2_stream_close(s);
        return -1;
    }
    s->data = vag + HP2_VAG_HEADER;
    return 0;
}

void hp2_stream_close(hp2_stream *s)
{
    if (s->f) fclose(s->f);
    s->f = NULL;
    s->done = 1;
}

/* The next chunk pair: up to 32 KB of L, then the rest of the pair for R
 * (the last pair may be short), as tools/hp2_audio.py splits it. */
static int refill(hp2_stream *s)
{
    uint32_t n = s->size - s->pos, l, r;
    if (s->done || n < 32) return 0;
    if (n > 2 * HP2_MUSIC_CHUNK) n = 2 * HP2_MUSIC_CHUNK;
    if (seek(s->f, s->data + s->pos) != 0 || fread(s->buf, 1, n, s->f) != n) return 0;
    s->pos += n;
    l = n < HP2_MUSIC_CHUNK ? n : HP2_MUSIC_CHUNK;
    r = n - l;
    s->lblocks = (int)(l / 16);
    s->rblocks = (int)(r / 16);
    s->next = 0;
    return s->lblocks > 0 && s->rblocks > 0;
}

int hp2_stream_read(hp2_stream *s, int16_t *out, int frames)
{
    int got = 0;
    while (got < frames) {
        if (s->ipend < s->npend) {
            int k = s->npend - s->ipend;
            if (k > frames - got) k = frames - got;
            memcpy(out + 2 * got, s->pend + 2 * s->ipend, (size_t)k * 2 * sizeof(int16_t));
            s->ipend += k;
            got += k;
            continue;
        }
        if (s->done) break;
        if (s->next >= s->lblocks || s->next >= s->rblocks) {
            if (!refill(s)) { s->done = 1; break; }
        }
        {
            int16_t L[28], R[28];
            int i;
            if (!hp2_adpcm_block(&s->st[0], s->buf + 16 * s->next, L)
                || !hp2_adpcm_block(&s->st[1], s->buf + HP2_MUSIC_CHUNK + 16 * s->next, R)) {
                s->done = 1;
                break;
            }
            s->next++;
            for (i = 0; i < 28; i++) { s->pend[2 * i] = L[i]; s->pend[2 * i + 1] = R[i]; }
            s->npend = 28;
            s->ipend = 0;
        }
    }
    return got;
}
