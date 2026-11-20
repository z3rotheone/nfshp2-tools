/* hp2_fedraw - draws front-end widgets with SDL2 (2.0.18+, for
 * SDL_RenderGeometry): images with their corner colours and UVs, text in the
 * game's own bitmap fonts, all placed through an hp2_fe_view.
 *
 * It draws a list of items, back to front by layer (higher layer first;
 * equal layers keep list order). What goes in the list (which widgets, where
 * the rows are, what the text says) is decided elsewhere (hp2_femenu), so
 * this file holds no menu logic.
 */
#ifndef HP2_FEDRAW_H
#define HP2_FEDRAW_H

#include "hp2_fe.h"
#include "hp2_font.h"
#include "hp2_tex.h"

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t     hash;
    SDL_Texture *tex;
    int          w, h;
} hp2_fedraw_cached;

typedef struct {
    SDL_Renderer      *r;
    const hp2_texset  *tex;
    const hp2_fontset *fonts;
    int                count, cap;
    hp2_fedraw_cached *cache;
    int                missing;     /* textures or fonts asked for but absent */
    int                clip_canvas; /* 1: nothing outside the 640x480 canvas
                                       (FIT / STRETCH: as the PS2 screen edge
                                       cut it); 0: margins open (EXPAND) */
} hp2_fedraw;

void hp2_fedraw_init(hp2_fedraw *d, SDL_Renderer *r, const hp2_texset *tex, const hp2_fontset *fonts);
void hp2_fedraw_free(hp2_fedraw *d);

/* Draws the items. The list is reordered (stable sort by layer). */
void hp2_fedraw_items(hp2_fedraw *d, const hp2_fe_view *v, hp2_fe_item *items, int n);

#ifdef __cplusplus
}
#endif
#endif
