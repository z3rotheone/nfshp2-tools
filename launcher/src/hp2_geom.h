/* hp2_geom - the game's models (cars and track scenery) decoded to
 * triangles. The read side of tools/hp2_geom.py in C; docs/formats.md
 * section 7.
 *
 * A model file (GEOMETRY.BIN, or a track's eLabScenery bundle) holds
 * 0x80034000 containers of parts. Each part is a VIF display list in
 * batches of at most 30 vertices, drawn as triangle strips. hp2_geom_scan
 * only indexes: parts point into the caller's buffer, which must outlive
 * the set. hp2_geom_decode does the work on demand. No SDL, plain C99.
 */
#ifndef HP2_GEOM_H
#define HP2_GEOM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t       hash;        /* name hash of the part (full name, see 7) */
    char           name[48];    /* as stored, may be cut short */
    int            ntex, nmat;
    const uint8_t *tex;         /* ntex entries of 8 bytes: u32 hash, u32 0 */
    const uint8_t *mat;         /* nmat entries, the same */
    const uint8_t *geom;        /* the VIF stream (0x34005 after its 8-byte lead) */
    size_t         geom_len;
    const uint8_t *rec;         /* the 64-byte batch records (0x34004) */
    int            nrec;
} hp2_part;

typedef struct {
    int       count, cap;
    hp2_part *part;
    int      *order;            /* part indices sorted by hash, for find */
} hp2_partset;                  /* zero it before the first scan */

/* Indexes every part of every 0x80034000 container at the top level of
 * buf. Returns the number added, or -1 when out of memory. */
int  hp2_geom_scan(hp2_partset *set, const uint8_t *buf, size_t len);
void hp2_geom_free(hp2_partset *set);
/* The first part with this hash, or NULL. */
const hp2_part *hp2_geom_find(const hp2_partset *set, uint32_t hash);

/* One vertex. Colours are the PS2's: 128 is full brightness, so a texture
 * is drawn at twice the vertex colour. */
typedef struct {
    float   x, y, z, u, v;
    uint8_t c[4];
    int8_t  n[3];               /* normal x 127; 0,0,0 where a batch has none */
    uint8_t pad;
} hp2_vert;

typedef struct {
    uint32_t tex;               /* texture name hash, 0 if none */
    uint32_t mat;               /* material name hash, 0 if none */
    int      first, count;      /* vertices, three per triangle */
} hp2_mbatch;

typedef struct {
    int         nv, nb;
    hp2_vert   *v;              /* a plain triangle list */
    hp2_mbatch *b;
    int         bad;            /* batches that failed to decode */
    int         wrong;          /* decoded, but not as their record says */
} hp2_mesh;

/* Decodes a part. 0 on success (m->bad may still count failed batches),
 * -1 when out of memory. */
int  hp2_geom_decode(const hp2_part *p, hp2_mesh *m);
void hp2_mesh_free(hp2_mesh *m);

/* The game's name hash (h = h * 33 + c from 0xFFFFFFFF). */
uint32_t hp2_geom_hash(const char *s);

#ifdef __cplusplus
}
#endif
#endif
