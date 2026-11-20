/* hp2_fe - the front-end screen layouts of FRONTB.BUN, and where they land
 * on a window of any shape.
 *
 * The original menus are data: 41 screens on a 640 x 480 canvas (chunk
 * 0x80010200 of FRONTEND\FRONTB.BUN, see docs/formats.md section 17). This
 * module parses that chunk from a buffer the caller read from the user's own
 * disc image; nothing of the game is compiled in. It does not draw. A
 * renderer takes the widgets and the viewport and does the drawing.
 *
 * No SDL, plain C99. hp2_fe_parse allocates; hp2_fe_free releases.
 */
#ifndef HP2_FE_H
#define HP2_FE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HP2_FE_TEXT = 1, HP2_FE_IMAGE = 2 } hp2_fe_kind;

/* Text alignment word. Rows use LEFT (x is the left edge); the value column
 * uses CENTRE (x is the middle, between the two arrows). y is the top of the
 * line in both. */
#define HP2_FE_ALIGN_LEFT    2u
#define HP2_FE_ALIGN_CENTRE  4u
#define HP2_FE_ALIGN_RIGHT   8u    /* x is the right edge (added by the port, not in the layouts) */

typedef struct {
    hp2_fe_kind kind;
    char     name[25];       /* "MENUITEM_1", "MI_1_MASK", "NONAME" */
    uint32_t name_hash;
    float    x, y;           /* canvas pixels */
    float    sx, sy;         /* scale: an image is its texture's size times this */
    uint8_t  visible;        /* +0x29: 0 = hidden until the screen's code shows it */
    uint8_t  layer;          /* +0x2A: draw order, lower is in front */
    /* text */
    uint32_t font;           /* 0x2D213565 = FONT_IMPACTBIG, 0x5F3A4A3C = FONT_IMPACTNORMAL */
    uint32_t capacity;
    uint8_t  rgba[4];
    uint32_t string_id;      /* key into FE_<LANGUAGE>.BIN; 0 = none */
    char     text[64];       /* the layout's own placeholder text */
    uint32_t align;          /* +0x58: HP2_FE_ALIGN_* bits */
    /* image */
    uint32_t texture;        /* name hash of a texture (FE or global pack) */
    uint8_t  vcol[4][4];     /* corner colours TL, TR, BR, BL; PS2 scale,
                                0x80 = 1.0 (alpha 0x80 = opaque) */
    float    uv[4][2];       /* texture coordinates, same corner order */
} hp2_fe_widget;

typedef struct {
    uint32_t       id;       /* hp2_fe_hash of the upper-case screen name */
    int            width, height;   /* the canvas, 640 x 480 */
    int            count;
    hp2_fe_widget *widgets;
} hp2_fe_screen;

typedef struct {
    int            count;
    hp2_fe_screen *screens;
} hp2_fe_layout;

/* One thing to draw: a widget copy (moved, recoloured, hidden as needed),
 * optionally with other text and a clip rectangle. hp2_femenu builds lists
 * of these; hp2_fedraw draws them. */
typedef struct {
    hp2_fe_widget w;        /* a copy: move it, recolour it, hide it */
    const char   *str;      /* text to draw instead of w.text, or NULL */
    int           clip;     /* 1: clip to clip_rect (canvas units) */
    float         clip_rect[4];   /* x, y, w, h */
    int           extend;   /* 1: tile this image sideways into the margins
                               (full-width backgrounds on wide windows) */
} hp2_fe_item;

/* djb2 variant used for every name in the front end: seed 0xFFFFFFFF, x33. */
uint32_t hp2_fe_hash(const char *name);

/* Parses a whole FRONTB.BUN. Returns 0 on success, -1 on bad data (nothing
 * is left allocated then). Reads never run past len. */
int  hp2_fe_parse(const uint8_t *buf, size_t len, hp2_fe_layout *out);
void hp2_fe_free(hp2_fe_layout *l);

/* Finds a screen by its name ("OPTIONS") or id; NULL if absent. */
const hp2_fe_screen *hp2_fe_find(const hp2_fe_layout *l, const char *name);
const hp2_fe_screen *hp2_fe_find_id(const hp2_fe_layout *l, uint32_t id);

/* Widgets named MENUITEM_<n> (text, the row label) in row order, n from 1.
 * Returns the widget or NULL when the screen has no row n. */
const hp2_fe_widget *hp2_fe_row(const hp2_fe_screen *s, int n);
int hp2_fe_row_count(const hp2_fe_screen *s);

/* ---- viewport ----------------------------------------------------------
 * How the 640 x 480 canvas sits on a window.
 *   FIT      4:3 kept, bars at the sides (what the PS2 showed on a 16:9 TV
 *            with 4:3 selected)
 *   STRETCH  fills the window, circles become ovals
 *   EXPAND   4:3 content kept at the height of the window and centred, with
 *            the extra width left for a background to fill; the menu is
 *            not distorted and not tiny
 */
typedef enum { HP2_FE_FIT, HP2_FE_STRETCH, HP2_FE_EXPAND } hp2_fe_fit;

typedef struct {
    float sx, sy;       /* window pixels per canvas pixel, per axis */
    float ox, oy;       /* window position of canvas (0, 0) */
    float margin_l, margin_r;   /* canvas units of extra room left and right
                                   of the 640 wide canvas (EXPAND, FIT) */
} hp2_fe_view;

void hp2_fe_view_make(hp2_fe_view *v, int win_w, int win_h, int canvas_w, int canvas_h,
                      hp2_fe_fit fit);
void hp2_fe_to_window(const hp2_fe_view *v, float cx, float cy, float *wx, float *wy);

#ifdef __cplusplus
}
#endif

#endif
