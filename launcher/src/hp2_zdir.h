/* hp2_zdir - read game files out of the disc's archive: ZDIR.BIN (index,
 * 12-byte records: id, sector, size) + ZZDATA.BIN (docs/formats.md
 * section 1). The id is the name hash of the upper-case path with
 * backslashes, so files are found by name: "FRONTEND\\FRONTB.BUN".
 *
 * Point it at the NFSHP2 folder of your own disc copy. Plain C99 stdio;
 * reads ZZDATA.BIN on demand (it is 3 GB), no SDL.
 */
#ifndef HP2_ZDIR_H
#define HP2_ZDIR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      count;
    uint8_t *index;     /* the whole ZDIR.BIN */
    char     data[1024];/* path of ZZDATA.BIN */
} hp2_zdir;

/* dir holds ZDIR.BIN and ZZDATA.BIN. 0 on success, -1 otherwise. */
int  hp2_zdir_open(hp2_zdir *z, const char *dir);
void hp2_zdir_close(hp2_zdir *z);

/* Name hash of a path as the archive keys it: upper case, '/' read as '\'. */
uint32_t hp2_zdir_hash(const char *path);

/* Reads one file into a new buffer (free() it). NULL if absent or on a read
 * error; *len gets the size. */
uint8_t *hp2_zdir_load(const hp2_zdir *z, const char *path, size_t *len);
uint8_t *hp2_zdir_load_id(const hp2_zdir *z, uint32_t id, size_t *len);

/* The first max bytes of a file (all of it if smaller): the music bank's
 * song table without its 193 MB of audio. */
uint8_t *hp2_zdir_load_part(const hp2_zdir *z, const char *path, size_t max, size_t *len);

/* Reads a whole loose file (an extracted entry). */
uint8_t *hp2_read_file(const char *path, size_t *len);
uint8_t *hp2_read_file_part(const char *path, size_t max, size_t *len);

#ifdef __cplusplus
}
#endif
#endif
