/* hp2_lang - menu text from LANGUAGES\FE_<LANGUAGE>.BIN (chunk 0x00039000,
 * docs/formats.md section 14): strings by id (the layouts' string ids) or by
 * key (name hash of the English wording). Strings are Latin-1 and may carry
 * $RGB / $FONT codes and '@' line breaks (see hp2_font.h).
 *
 * Points into the caller's buffer, which must outlive it. No allocation.
 */
#ifndef HP2_LANG_H
#define HP2_LANG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int            count;
    const uint8_t *keys;    /* count x (u32 key, u32 offset), sorted signed */
    const uint8_t *ids;     /* count x (u32 id, u32 offset), sorted */
    const char    *text;
    size_t         text_size;
} hp2_lang;

/* 0 on success, -1 when buf is not a language file. */
int hp2_lang_open(hp2_lang *l, const uint8_t *buf, size_t len);

/* The string, or NULL when the id or key is not in the file. */
const char *hp2_lang_by_id(const hp2_lang *l, uint32_t id);
const char *hp2_lang_by_key(const hp2_lang *l, uint32_t key);

#ifdef __cplusplus
}
#endif
#endif
