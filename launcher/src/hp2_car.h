/* hp2_car - the car database and a whole car put together from its model.
 * The read side of tools/hp2_carinfo.py and tools/hp2_car.py in C;
 * docs/formats.md sections 7 and 12.
 *
 * The database (GLOBAL\GLOBALB.BUN chunk 0x34600) gives each car its model
 * name, wheel hubs, size, gearing and top speed. A car's GEOMETRY.BIN holds
 * the body (already in place) and one right-hand tyre and the brake discs at
 * the origin; hp2_car_build gathers them so a renderer can put a tyre on
 * each hub (mirrored for the left side) and turn it.
 *
 * Model axes: +x front, +y left, +z up; units are metres. No SDL, plain C99.
 */
#ifndef HP2_CAR_H
#define HP2_CAR_H

#include "hp2_geom.h"
#include "hp2_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;              /* hub, model axes */
    float radius, width;
} hp2_wheel;

enum { HP2_WHEEL_FL, HP2_WHEEL_FR, HP2_WHEEL_RR, HP2_WHEEL_RL };

typedef struct {
    int       slot;             /* record index in the table */
    char      name[33];         /* record: 911TURBO, 911TURBOHP, BMWM5COP ... */
    char      model[33];        /* the GEOMETRY.BIN and texture prefix */
    char      path[33];         /* CARS\<model>\GEOMETRY.BIN */
    char      path_lzc[33];
    char      make[33];
    hp2_wheel wheel[4];         /* front left, front right, rear right, rear left */
    float     length, width, height;
    float     final_drive;
    int       gears;
    float     reverse, ratio[6];
    float     idle_rpm, peak_rpm, redline_rpm;
    float     top_speed;        /* mph */
} hp2_carrec;

#define HP2_CARDB_MAX 64

/* Reads the records with a name. Returns how many (at most max), or -1 if
 * the table is not there. */
int hp2_cardb_parse(const uint8_t *globalb, size_t len, hp2_carrec *out, int max);
/* The record named name (case-insensitive), or NULL. */
const hp2_carrec *hp2_cardb_find(const hp2_carrec *recs, int n, const char *name);

typedef struct {
    hp2_carrec rec;
    hp2_mesh   body;            /* body (detail level A), mirrors, rear plate: in place */
    hp2_mesh   tyre;            /* a right-hand tyre, hub at the origin */
    float      tyre_radius;     /* of that model; scale it to each wheel's own */
    float      tyre_width;
    hp2_mesh   brake[2];        /* front, rear disc at the origin (may be empty) */
} hp2_car;

/* Puts a car together. 0, or -1 when the body is not in the file or memory
 * runs out. The car keeps no pointers into geom. */
int  hp2_car_build(hp2_car *c, const uint8_t *geom, size_t len, const hp2_carrec *rec);
void hp2_car_free(hp2_car *c);

/* The texture to draw for hash h in a car's batch: the model names slots,
 * which the game fills at run time (docs/formats.md 7, "Car textures"):
 * <CAR>_SKIN1 -> <CAR>_SKIN<skin>, <CAR>_HEADLIGHT_LEFT -> <CAR>_HEADLIGHT,
 * TIRE -> <record>_TIRE, the six WINDOW_* -> WINDOW. The first candidate in
 * the set wins; h itself when none is. */
uint32_t hp2_car_texture(const hp2_car *c, const hp2_texset *set, uint32_t h, int skin);

#ifdef __cplusplus
}
#endif
#endif
