/* hp2_tex - textures from the texture packs (.tpk) inside the game's bundles
 * (FRONTB.BUN, GLOBALB.BUN, ...), decoded to RGBA.
 *
 * The read side of tools/hp2_tpk.py in C: 4-bit and 8-bit palette textures
 * (linear or stored pre-swizzled for the GS) and 32-bit RGBA, level 0 only.
 * PS2 alpha 0..0x80 becomes 0..255. See docs/formats.md section 6.
 *
 * hp2_tex_scan only indexes: entries point into the caller's buffer, which
 * must outlive the set. hp2_tex_decode does the work on demand. No SDL,
 * plain C99.
 */
#ifndef HP2_TEX_H
#define HP2_TEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t       hash;        /* name hash (hp2_fe_hash of the name) */
    char           name[25];
    int            w, h;
    int            bpp;         /* 4, 8 or 32; 0 = a layout not handled */
    const uint8_t *pix;         /* level 0 pixels in the caller's buffer */
    const uint8_t *pal;         /* palette (NULL for 32-bit) */
} hp2_tex;

typedef struct {
    int      count, cap;
    hp2_tex *tex;
} hp2_texset;                   /* zero it before the first scan */

/* Indexes every pack found in a bundle (or a bare .tpk) and adds its
 * textures to the set; call once per bundle. A hash already in the set
 * keeps its first texture. Returns the number of textures added, or -1 when
 * out of memory. Reads never run past len. */
int  hp2_tex_scan(hp2_texset *set, const uint8_t *buf, size_t len);
void hp2_tex_free(hp2_texset *set);

const hp2_tex *hp2_tex_find(const hp2_texset *set, uint32_t hash);

/* Writes w * h * 4 bytes of RGBA (rows top to bottom). Returns 0, or -1 for
 * a texture whose layout is not handled (bpp 0). */
int hp2_tex_decode(const hp2_tex *t, uint8_t *rgba);

#ifdef __cplusplus
}
#endif
#endif
