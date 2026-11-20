/* hp2_vag - PS-ADPCM (Sony VAG) decoding and streaming of the music bank's
 * tracks (docs/formats.md sections 4 and 6). The C side of
 * tools/hp2_audio.py's decoder.
 *
 * A music track is a 48-byte big-endian VAG header followed by stereo
 * ADPCM whose two channels alternate in fixed chunks: L, R, L, R. The chunk
 * is a property of the bank: 32,768 bytes in SOUND\JUKEBOXM.JBC (entry 322,
 * the bank the game plays). hp2_stream reads one chunk pair at a time from
 * the file, so a 193 MB bank is never loaded. No SDL; plain C99 stdio.
 */
#ifndef HP2_VAG_H
#define HP2_VAG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_VAG_HEADER   48
#define HP2_MUSIC_CHUNK  32768      /* bank 322's stereo interleave, measured */

typedef struct { int h1, h2; } hp2_adpcm;   /* one channel's history; zero it to start */

/* Decodes one 16-byte block into 28 samples. Returns 28, or 0 when the
 * block's flag byte is 7 (the end of the sound; nothing written). */
int hp2_adpcm_block(hp2_adpcm *st, const uint8_t *blk, int16_t *out);

/* Data size and sample rate of a VAG header; 0 on success, -1 if p is not
 * one ("VAGp", rate 2000..48000, size at least one block). */
int hp2_vag_header(const uint8_t *p, size_t len, uint32_t *size, uint32_t *rate);

typedef struct {
    FILE     *f;
    long long data;                 /* file offset of the first ADPCM byte */
    uint32_t  size, pos;            /* ADPCM bytes in all; bytes used so far */
    uint32_t  rate;
    uint8_t   buf[2 * HP2_MUSIC_CHUNK];
    int       lblocks, rblocks;     /* blocks of L and R in buf */
    int       next;                 /* next block pair to decode */
    int16_t   pend[28 * 2];         /* decoded frames not handed out yet */
    int       npend, ipend;
    hp2_adpcm st[2];
    int       done;
} hp2_stream;

/* Opens the track whose VAG header is at file offset vag in path. 0 on
 * success, -1 otherwise (nothing left open). */
int  hp2_stream_open(hp2_stream *s, const char *path, long long vag);
void hp2_stream_close(hp2_stream *s);

/* Up to frames stereo frames (L, R interleaved int16) into out. Returns how
 * many; fewer than asked only at the end of the track (then 0 after). */
int hp2_stream_read(hp2_stream *s, int16_t *out, int frames);

#ifdef __cplusplus
}
#endif
#endif
