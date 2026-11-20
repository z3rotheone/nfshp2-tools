/* hp2_jukebox - the song list of a music bank (SOUND\JUKEBOXM.JBC, archive
 * entry 322, which the game plays; docs/formats.md section 6).
 *
 * The bank opens with its own table:
 *     +0x00  u32 0x10, u32 2, u32 sample rate (32000), u32 1, u32 0x1000
 *     +0x20  u32 track count (23)
 *     +0x24  char[] bank name ("InGame")
 *     +0x60  count x 0x60-byte records: u32 offset of the track's VAG
 *            header, u32 size, u32 0, then text "Artist-Title-Album-
 *            Genre-N" (NUL ended; album may be empty) and filler
 * N is the song's default play status. Only the table is read: pass the
 * first few kilobytes of the bank (hp2_jukebox_header_size says how many),
 * not its 193 MB of audio. No SDL, no allocation.
 */
#ifndef HP2_JUKEBOX_H
#define HP2_JUKEBOX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_JB_MAX 64           /* songs; the shipped bank has 23 */

typedef struct {
    char artist[48];
    char title[64];
    char album[48];
    char genre[24];
    int  bank_status;           /* N from the bank, as stored */
} hp2_jb_track;

typedef struct {
    int          count;
    char         name[32];      /* "InGame" */
    hp2_jb_track track[HP2_JB_MAX];
    int          genres;
    char         genre[16][24]; /* distinct genres, in order of first use */
} hp2_jukebox;

/* Bytes of the bank's start that hold the table (always enough: 0x60 for
 * the header + 0x60 per song, for HP2_JB_MAX songs). */
size_t hp2_jukebox_header_size(void);

/* 0 on success, -1 if buf does not start with a song table. */
int hp2_jukebox_parse(hp2_jukebox *jb, const uint8_t *buf, size_t len);

/* Songs of genre g in bank order: fills idx (up to cap), returns the count. */
int hp2_jukebox_songs(const hp2_jukebox *jb, int g, int *idx, int cap);

#ifdef __cplusplus
}
#endif
#endif
