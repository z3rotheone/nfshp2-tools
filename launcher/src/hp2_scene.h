/* hp2_scene - a track's scenery placed in the world, ready to draw.
 * The read side of tools/hp2_scenery.py in C; docs/formats.md section 11.
 *
 * Every 0x80034100 section lists models and instances; each instance puts
 * a model at   world = position + v * M.   hp2_scene_build decodes each
 * model once, places every instance and gathers the triangles of each
 * section by texture, so a renderer draws one group per (section, texture)
 * and skips whole sections that are out of view.
 *
 * The sky (SKYDOME, PANORAMA) is kept apart, to draw first and unfogged;
 * SKYDOME_ENVMAP is counted but not kept.
 *
 * Also read: the main route (for a start position) and the main fog.
 * No SDL, plain C99.
 */
#ifndef HP2_SCENE_H
#define HP2_SCENE_H

#include "hp2_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t  tex;              /* texture name hash, 0 = untextured */
    int       nv;               /* vertices, three per triangle */
    hp2_vert *v;                /* world space, z up */
} hp2_sgroup;

typedef struct {
    int         num;            /* the section number (41, 42 ...) */
    int         placed;         /* instances drawn from this section */
    float       box[6];         /* min xyz, max xyz of what it draws */
    int         ng;
    hp2_sgroup *g;
} hp2_ssection;

typedef struct {
    int           nsec;
    hp2_ssection *sec;
    float         box[6];       /* the whole track, sky left out */
    hp2_ssection  sky;          /* SKYDOME and PANORAMA (num 0, no box) */
    /* counts, for checks */
    int           inst;         /* instance records read */
    int           placed;       /* instances drawn */
    int           dup;          /* records repeated in another section */
    int           missing;      /* instances of models not in the file */
    int           shadows;      /* kind 1 instances (drawn) */
    long          tris;         /* all drawn, sky included */
    long          sky_tris;
    int           bad;          /* batches that failed to decode */
    /* the main route's first two points, for a start position */
    int           has_start;
    float         start[3], ahead[3];
    /* the main route (0x34148 record 0): points x, y, progress */
    int           route_n;
    float        *route;            /* route_n x 3 floats */
    int           route_loop;       /* 1 = a circuit */
    float         route_len;        /* its length: a lap, or start to finish */
    /* the main fog (FOG<MAIN>), if found */
    int           has_fog;
    uint8_t       fog_rgba[4];
    float         fog_start, fog_density;
} hp2_scene;

/* flags for hp2_scene_build */
#define HP2_SCENE_NO_SHADOWS 1

/* Builds the scene from a scenery file (decompressed). Returns 0, or -1 if
 * the file holds no sections or memory runs out. The scene keeps no
 * pointers into buf. */
int  hp2_scene_build(hp2_scene *s, const uint8_t *buf, size_t len, int flags);
void hp2_scene_free(hp2_scene *s);
/* Frees the vertices once a renderer has its own copy; the sections, their
 * boxes and group counts stay (every group's v becomes NULL, nv stays). */
void hp2_scene_drop_vertices(hp2_scene *s);

/* Decompresses a COMP file (.LZC entries; docs/formats.md section 2).
 * A new buffer (free() it), or NULL if the data is not valid COMP. */
uint8_t *hp2_comp_decode(const uint8_t *in, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
