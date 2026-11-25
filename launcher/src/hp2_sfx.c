#include "hp2_sfx.h"
#include "hp2_vag.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }

/* One channel: every block of [d, d + len), as 16-bit mono. */
static int16_t *decode(const uint8_t *d, size_t len, int *n)
{
    size_t blocks = len / 16, i;
    int16_t *out = (int16_t *)malloc((blocks ? blocks : 1) * 28 * sizeof *out);
    hp2_adpcm st = { 0, 0 };
    int total = 0;
    if (!out) return NULL;
    for (i = 0; i < blocks; i++) {
        uint8_t blk[16];
        memcpy(blk, d + 16 * i, 16);
        if ((blk[1] & 7) == 7) blk[1] = 0;     /* an end marker still carries samples here */
        total += hp2_adpcm_block(&st, blk, out + total);
    }
    *n = total;
    return out;
}

int hp2_sfx_load(hp2_sfx *x, const uint8_t *msh, size_t msh_len, const uint8_t *msb, size_t msb_len)
{
    uint32_t count, k;
    int added = 0;
    if (!msh || !msb || msh_len < 12 || rd32(msh + 4) != 16) return -1;
    count = rd32(msh + 8);
    if (count == 0 || (size_t)12 + 52 * (size_t)count > msh_len) return -1;
    for (k = 0; k < count; k++) {
        const uint8_t *r = msh + 12 + 52 * k;
        uint32_t off = rd32(r + 40), end = k + 1 < count ? rd32(r + 52 + 40) : (uint32_t)msb_len;
        unsigned rate = rd16(r + 34);
        int pair = k + 1 < count && !memcmp(r, r + 52, 32);      /* the next one is the right channel */
        int nl = 0, nr = 0, n, i;
        int16_t *l, *rr = NULL, *o;
        hp2_sfx_sample *s;
        if (k > 0 && !memcmp(r, r - 52, 32)) continue;           /* a right channel, done with its left */
        if (end > msb_len || off >= end || rate < 4000 || rate > 48000) continue;
        l = decode(msb + off, end - off, &nl);
        if (!l) return -1;
        if (pair) {
            uint32_t off2 = rd32(r + 52 + 40), end2 = k + 2 < count ? rd32(r + 104 + 40) : (uint32_t)msb_len;
            if (end2 <= msb_len && off2 < end2) rr = decode(msb + off2, end2 - off2, &nr);
        }
        n = nl;
        if (rr && nr < n) n = nr;
        if (n <= 0) { free(l); free(rr); continue; }
        {   int mult = rate * 2 <= HP2_SFX_RATE + 1000 ? 2 : 1;       /* 22,050 -> 44,100 */
            o = (int16_t *)malloc((size_t)n * mult * 2 * sizeof *o);
            if (!o) { free(l); free(rr); return -1; }
            for (i = 0; i < n; i++) {
                int a = l[i], b = rr ? rr[i] : l[i];
                int a2 = i + 1 < n ? l[i + 1] : a, b2 = i + 1 < n ? (rr ? rr[i + 1] : l[i + 1]) : b;
                if (mult == 2) {
                    o[4 * i] = (int16_t)a; o[4 * i + 1] = (int16_t)b;
                    o[4 * i + 2] = (int16_t)((a + a2) / 2); o[4 * i + 3] = (int16_t)((b + b2) / 2);
                } else { o[2 * i] = (int16_t)a; o[2 * i + 1] = (int16_t)b; }
            }
            n *= mult;
        }
        free(l); free(rr);
        if (x->count >= x->cap) {
            int nc = x->cap ? x->cap * 2 : 32;
            hp2_sfx_sample *p = (hp2_sfx_sample *)realloc(x->s, (size_t)nc * sizeof *p);
            if (!p) { free(o); return -1; }
            x->s = p;
            x->cap = nc;
        }
        s = &x->s[x->count++];
        memcpy(s->name, r, 32);
        s->name[32] = 0;
        s->pcm = o;
        s->frames = n;
        added++;
    }
    return added;
}

void hp2_sfx_free(hp2_sfx *x)
{
    int i;
    for (i = 0; i < x->count; i++) free(x->s[i].pcm);
    free(x->s);
    memset(x, 0, sizeof *x);
}

const hp2_sfx_sample *hp2_sfx_find(const hp2_sfx *x, const char *name)
{
    int i;
    for (i = 0; i < x->count; i++) if (!strcmp(x->s[i].name, name)) return &x->s[i];
    return NULL;
}
