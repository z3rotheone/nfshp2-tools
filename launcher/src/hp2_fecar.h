/* hp2_fecar - the 3D car of the car select screen, drawn through SDL's 2D
 * renderer: the car's model is turned, projected and its triangles drawn far
 * to near with their textures (SDL_RenderGeometry), so it needs no OpenGL and
 * works with the software renderer the screenshots use. Cars come from the
 * disc (CARS\<model>\GEOMETRY.BIN); without a disc nothing is drawn. */
#ifndef HP2_FECAR_H
#define HP2_FECAR_H

#include <SDL.h>
#include "hp2_car.h"
#include "hp2_zdir.h"

typedef struct {
    int           ok;                 /* a disc and the car database are there */
    hp2_zdir      z;
    uint8_t      *globalb, *cartex;   /* globalb is the caller's */
    size_t        globalb_len, cartex_len;
    hp2_carrec    recs[HP2_CARDB_MAX];
    int           nrecs;
    hp2_texset    ts;
    SDL_Renderer *r;
    /* the car in hand */
    char          name[40];
    int           skin;
    int           loaded;
    hp2_car       car;
    /* textures made so far, by hash */
    struct { uint32_t hash; SDL_Texture *t; } *tex;
    int           ntex;
    void         *work;               /* triangle scratch */
    size_t        work_cap;
} hp2_fecar;

/* globalb stays the caller's. Returns 0 when cars can be shown. */
int  hp2_fecar_open(hp2_fecar *c, const char *disc, SDL_Renderer *r, const uint8_t *globalb, size_t globalb_len);
void hp2_fecar_close(hp2_fecar *c);
/* Draws the car named in the database (CORVETTE, 911TURBO ...) with skin
 * 1..8, turned `yaw` degrees, into the window rectangle. */
/* The colour of skin 1..8 of a car (the paint of its skin texture), for the
 * swatch column. 1 when found, 0 when not. */
int  hp2_fecar_skin_rgb(hp2_fecar *c, const char *name, int skin, uint8_t rgb[3]);
void hp2_fecar_draw(hp2_fecar *c, const char *name, int skin, float yaw, const SDL_Rect *area);

#endif
