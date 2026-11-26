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
    char      engine[33];       /* engine sound: "NewEBxr" -> SOUND\NEWEBXR.MSH/.MSB; "" for traffic */
    /* the drivetrain, as carinfo.cpp's acceleration table (0x188DF0 in the
     * executable) reads it: wheel force = torque x ratio x final drive /
     * tyre radius; with the mass in tonnes and the torque in kN m, that over
     * the mass is m/s^2. docs/formats.md 12. */
    float     mass;             /* 0x1E0, tonnes */
    float     efficiency;       /* 0x270: 0.8 on every car; a drivetrain loss, probably */
    float     final_front;      /* 0x278 */
    float     final_rear;       /* 0x27C (= 0x278 but on the four-wheel-drive Lamborghinis) */
    float     rear_share;       /* 0x280: torque to the rear axle; 0.6 on the four-wheel-drive cars */
    float     drag;             /* 0x2BC: air drag, kN per (m/s)^2 */
    float     torque[9];        /* 0x2C0: kN m, idle to redline in eight equal steps */
    /* the tyres, as the game's Tire reads them (docs/formats.md 12, "Tyres") */
    float     tyre[2][8];       /* 0x1A0 front, 0x1C0 rear: [0] slip-angle span (12), [1] load
                                 * scale, [2] force scale, [3] ?, [4] sliding friction */
    float     aero[2];          /* 0x2F4, 0x2F8: the aerodynamics block's front and rear downforce (0x1ADD70) */
    float     brakes[7];        /* 0x300: [1] brake torque scale, [2] handbrake torque scale */
    float     inertia[3];       /* 0x1F0, 0x204, 0x218: the diagonal of the body's 4x4 inertia
                                 * matrix at 0x1F0 (roll, pitch, yaw; t m^2, inferred) */
    float     susp[2][8];       /* 0x230 front, 0x250 rear: [0] progression, [1] spring kN/m,
                                 * [2] bump and [3] rebound damping kN s/m, [4] anti-roll kN/m,
                                 * [5] bump and [6] droop travel m, [7] ? */
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
