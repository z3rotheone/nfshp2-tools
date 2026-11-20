/* hp2_font - the bitmap fonts of the front end (chunk 0x00015010 in
 * FRONTB.BUN and GLOBALB.BUN; docs/formats.md section 18), and the inline
 * codes of the game's strings.
 *
 * A font is a list of glyph rectangles in a texture whose name hash is the
 * font's own (FONT_IMPACTSMALL draws from the texture FONT_IMPACTSMALL),
 * plus kerning pairs. Characters are Latin-1. No SDL, plain C99; fonts
 * point into the caller's buffer, which must outlive the set.
 */
#ifndef HP2_FONT_H
#define HP2_FONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t ch;            /* Latin-1 code */
    uint16_t kern_count;
    float    x, y, w, h;    /* rectangle in the font texture, pixels */
    float    advance;       /* pen step; can be narrower than w */
    float    xoff;          /* added to the pen when drawing */
    uint32_t kern_first;
} hp2_glyph;

typedef struct {
    uint32_t       hash;            /* font name hash = its texture's hash */
    int            count;
    int            kern_count;
    float          line_height;     /* tallest glyph */
    const uint8_t *glyphs;          /* count x 32 bytes */
    const uint8_t *kern;            /* kern_count x 8 bytes, or NULL */
    int16_t        index[256];      /* character -> glyph number, -1 = none */
} hp2_font;

typedef struct {
    int       count;
    hp2_font *font;
} hp2_fontset;

/* Adds every font chunk of a bundle to the set. Returns the number added or
 * -1 when out of memory. Reads never run past len. */
int  hp2_font_scan(hp2_fontset *set, const uint8_t *buf, size_t len);
void hp2_font_free(hp2_fontset *set);
const hp2_font *hp2_font_find(const hp2_fontset *set, uint32_t hash);

/* The glyph for a character, or 0 with *g untouched when the font lacks it. */
int hp2_font_glyph(const hp2_font *f, unsigned char c, hp2_glyph *g);

/* Extra spacing between prev and cur (usually negative). The pairs are
 * stored with the second character: cur's list names the characters that
 * may come before it (T lists L, for "LT"). */
float hp2_font_kern(const hp2_font *f, unsigned char prev, unsigned char cur);

/* ---- strings ------------------------------------------------------------
 * Strings carry codes: $RGB(r,g,b) sets the colour, $FONT(NAME) the font,
 * '@' breaks the line. Anything else, including a '$' that starts no code
 * ($TARGET_TIME is filled in by the game first), is text. */
typedef enum { HP2_TOK_END, HP2_TOK_CHAR, HP2_TOK_NEWLINE, HP2_TOK_RGB, HP2_TOK_FONT } hp2_tok_kind;

typedef struct {
    hp2_tok_kind kind;
    unsigned char ch;           /* CHAR */
    uint8_t  rgb[3];            /* RGB */
    uint32_t font;              /* FONT: hash of the name */
} hp2_tok;

/* Reads one token at *p and advances it. */
hp2_tok hp2_text_next(const char **p);

/* Width in font pixels of the first line of s, with kerning, codes skipped
 * (a $FONT change is not followed; the width is in f). */
float hp2_text_width(const hp2_font *f, const char *s);

#ifdef __cplusplus
}
#endif
#endif
