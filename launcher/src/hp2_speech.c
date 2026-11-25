#include "hp2_speech.h"
#include "hp2_vag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int hp2_speech_open(hp2_speech *s, const uint8_t *ban, size_t ban_len, const uint8_t *vocab, size_t vocab_len,
                    const char *dat, long long dat_base)
{
    uint32_t n, ln_off, i, o;
    memset(s, 0, sizeof *s);
    if (ban_len < 8 || vocab_len < 0x20 || strlen(dat) >= sizeof s->dat) return -1;
    n = rd32(ban);
    if (n > 20000 || 8 + 16 * (size_t)n > ban_len) return -1;
    ln_off = rd32(vocab + 0x18);
    if (ln_off >= vocab_len) return -1;
    s->name_line = (uint32_t *)calloc(HP2_SPEECH_MAXLINES, sizeof *s->name_line);
    s->name = calloc(HP2_SPEECH_MAXLINES, 32);
    s->clip = (hp2_speech_clip *)calloc(n ? n : 1, sizeof *s->clip);
    if (!s->name_line || !s->name || !s->clip) { hp2_speech_close(s); return -1; }
    for (o = ln_off; o + 72 <= vocab_len && s->nnames < HP2_SPEECH_MAXLINES; o += 72) {
        s->name_line[s->nnames] = rd32(vocab + o);
        memcpy(s->name[s->nnames], vocab + o + 40, 31);
        s->nnames++;
    }
    for (i = 0; i < n; i++) {
        uint32_t d = rd32(ban + 8 + 16 * i + 4), desc = rd32(ban + 8 + 16 * i + 12);
        unsigned num, sub;
        if (desc + 8 > ban_len) continue;
        num = (unsigned)(ban[desc] | ban[desc + 1] << 8);
        sub = (unsigned)(ban[desc + 2] | ban[desc + 3] << 8);
        {   int known = 0, k;
            for (k = 0; k < s->nnames; k++) if (s->name_line[k] == num) { known = 1; break; }
            if (!known) continue;       /* the closing record is not a line */
        }
        s->clip[s->nclips].line = num;
        s->clip[s->nclips].dat = d;
        s->clip[s->nclips].variant = sub == 0xFFFF ? -1 : (int)sub;
        s->nclips++;
    }
    strcpy(s->dat, dat);
    s->dat_base = dat_base;
    s->seed = 12345u;
    return s->nclips > 0 ? 0 : (hp2_speech_close(s), -1);
}

void hp2_speech_close(hp2_speech *s)
{
    free(s->clip); free(s->name_line); free(s->name); free(s->pcm);
    memset(s, 0, sizeof *s);
}

static unsigned rnd(hp2_speech *s) { s->seed = s->seed * 1664525u + 1013904223u; return s->seed >> 8; }

/* Line numbers named <cop><suffix>; cop = 0 for any voice. Returns how many (up to max). */
static int find_lines(const hp2_speech *s, const char *suffix, int cop, uint32_t *out, int max)
{
    int i, n = 0;
    size_t sl = strlen(suffix);
    for (i = 0; i < s->nnames && n < max; i++) {
        const char *nm = s->name[i];
        size_t l = strlen(nm);
        if (l != sl + 2 || strcmp(nm + 2, suffix)) continue;
        if (cop && (nm[0] - '0') * 10 + (nm[1] - '0') != cop) continue;
        out[n++] = s->name_line[i];
    }
    return n;
}

int hp2_speech_has(const hp2_speech *s, const char *suffix)
{
    uint32_t l[8];
    return find_lines(s, suffix, 0, l, 8) > 0;
}

int hp2_speech_busy(const hp2_speech *s) { return s->pcm && s->pos < s->frames; }

int hp2_speech_say(hp2_speech *s, const char *suffix, int urgent)
{
    static const int cops[4] = { 19, 27, 31, 46 };
    uint32_t lines[8], line;
    int nl, i, nc = 0, pick;
    hp2_speech_clip *c[64];
    FILE *f;
    uint8_t hdr[HP2_VAG_HEADER], *raw;
    uint32_t size, rate;
    int16_t *mono, *o;
    int nm, m, k;
    hp2_adpcm st;
    if (hp2_speech_busy(s) && !urgent) return 0;
    nl = find_lines(s, suffix, cops[rnd(s) & 3], lines, 8);
    if (!nl) nl = find_lines(s, suffix, 0, lines, 8);
    if (!nl) return 0;
    line = lines[rnd(s) % (unsigned)nl];
    for (i = 0; i < s->nclips && nc < 64; i++) if (s->clip[i].line == line) c[nc++] = &s->clip[i];
    if (!nc) return 0;
    pick = (int)(rnd(s) % (unsigned)nc);
    f = fopen(s->dat, "rb");
    if (!f) return 0;
    if (fseek(f, (long)(s->dat_base + c[pick]->dat), SEEK_SET) || fread(hdr, 1, sizeof hdr, f) != sizeof hdr
        || hp2_vag_header(hdr, sizeof hdr, &size, &rate) || size > (1u << 22)) { fclose(f); return 0; }
    raw = (uint8_t *)malloc(size);
    if (!raw || fread(raw, 1, size, f) != size) { free(raw); fclose(f); return 0; }
    fclose(f);
    nm = (int)(size / 16) * 28;
    mono = (int16_t *)malloc((size_t)nm * sizeof *mono);
    if (!mono) { free(raw); return 0; }
    memset(&st, 0, sizeof st);
    for (m = 0, k = 0; (uint32_t)(k + 1) * 16 <= size; k++) {
        int got = hp2_adpcm_block(&st, raw + 16 * k, mono + m);
        if (!got) break;
        m += got;
    }
    free(raw);
    nm = m;
    {   int out_n = (int)((long long)nm * 44100 / (long long)rate), j;
        o = (int16_t *)malloc((size_t)out_n * 2 * sizeof *o);
        if (!o || out_n <= 0) { free(o); free(mono); return 0; }
        for (j = 0; j < out_n; j++) {
            double p = (double)j * rate / 44100.0;
            int i0 = (int)p, i1 = i0 + 1 < nm ? i0 + 1 : nm - 1;
            double a = mono[i0] + (mono[i1] - mono[i0]) * (p - i0);
            o[2 * j] = o[2 * j + 1] = (int16_t)a;
        }
        free(s->pcm);
        s->pcm = o; s->frames = out_n; s->pos = 0;
    }
    free(mono);
    return 1;
}

void hp2_speech_mix(hp2_speech *s, float volume, int16_t *out, int frames)
{
    int i;
    if (!hp2_speech_busy(s) || volume <= 0) return;
    if (volume > 1) volume = 1;
    volume *= 0.7f;                     /* the port's own headroom: the clips are mastered near full scale */
    for (i = 0; i < frames && s->pos < s->frames; i++, s->pos++) {
        int a = out[2 * i] + (int)(s->pcm[2 * s->pos] * volume), b = out[2 * i + 1] + (int)(s->pcm[2 * s->pos + 1] * volume);
        out[2 * i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        out[2 * i + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
}
