/* hp2_hud - the in-race display drawn with OpenGL from the game's own
 * layout: the race screen of GLOBAL\INGAMEB.BUN (a front-end layout like
 * the menus', docs/formats.md 17), its textures (InGameTextures.tpk in
 * INGAMEB.BUN, the gauge faces in GLOBAL\HUDTEX.BIN) and fonts (GLOBALB.BUN,
 * INGAMEB.BUN).
 *
 * The layout places widgets on a 640 x 480 canvas; here the canvas keeps
 * the window's height and widgets on its left half stick to the window's
 * left edge, the others to the right, so a wide window keeps the gauges in
 * the corners. Needs SDL_opengl.h; draws with the fixed-function pipeline.
 */
#ifndef HP2_HUD_H
#define HP2_HUD_H

#include "hp2_fe.h"
#include "hp2_font.h"
#include "hp2_tex.h"

#include <SDL_opengl.h>

#define HP2_HUD_SCREEN    0xF1F1F8E5u   /* the race screen's id */
#define HP2_PRERACE_SCREEN 0x936ACE42u  /* 3, 2, 1, GO */

typedef struct {
    uint32_t hash;
    GLuint   id;
    int      w, h;
} hp2_hudtex;

typedef struct {
    const hp2_texset  *ts;
    const hp2_fontset *fonts;
    hp2_hudtex         cache[96];
    int                n;
    hp2_fe_view        view;
    int                missing;
} hp2_hud;

void hp2_hud_init(hp2_hud *h, const hp2_texset *ts, const hp2_fontset *fonts);
/* A widget of a screen by name, or NULL. */
const hp2_fe_widget *hp2_hud_widget(const hp2_fe_screen *s, const char *name);

/* Sets up a 2D projection over a W x H window; restore 3D state after. */
void hp2_hud_begin(hp2_hud *h, int W, int H);
void hp2_hud_end(void);
/* An image widget; tex overrides its texture when non-zero. It turns by
 * angle degrees (clockwise) about pivot (in texture pixels) when angle is
 * non-zero, the pivot staying where the widget puts it. alpha 0..1 fades it. */
void hp2_hud_image(hp2_hud *h, const hp2_fe_widget *w, uint32_t tex, float angle, const float *pivot, float alpha);
/* A text widget showing s (its own text when NULL). */
void hp2_hud_text(hp2_hud *h, const hp2_fe_widget *w, const char *s);

#endif
