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

typedef struct {
    hp2_fe_kind kind;
    char     name[25];       /* "MENUITEM_1", "MI_1_MASK", "NONAME" */
    uint32_t name_hash;
    float    x, y;           /* canvas pixels */
    float    sx, sy;         /* scale */
    /* text */
    uint32_t font;           /* 0x2D213565 = FONT_IMPACTBIG, 0x5F3A4A3C = FONT_IMPACTNORMAL */
    uint32_t capacity;
    uint8_t  rgba[4];
    uint32_t string_id;      /* key into FE_<LANGUAGE>.BIN; 0 = none */
    char     text[64];       /* the layout's own placeholder text */
    /* image */
    uint32_t texture;        /* name hash of a texture in the FE pack */
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
