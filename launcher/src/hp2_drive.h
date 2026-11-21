/* hp2_drive - a first, simple driving model: a car that accelerates,
 * brakes, steers and slides on the track's collision mesh, stays on the
 * ground and stops at walls.
 *
 * This is NOT the game's handling. The game's physics are not decoded yet;
 * this is a stand-in so a car can be driven around a track while they are.
 * It uses the car database's wheelbase, wheel hubs and top speed, and the
 * collision mesh (hp2_coll) for the ground and the walls. Fixed steps of
 * 1/120 s. World axes: z up; heading 0 faces +x and turns anticlockwise.
 * No SDL, plain C99.
 */
#ifndef HP2_DRIVE_H
#define HP2_DRIVE_H

#include "hp2_car.h"
#include "hp2_coll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float throttle;             /* 0..1 */
    float brake;                /* 0..1; held when stopped it reverses */
    float steer;                /* -1 left .. +1 right */
    int   handbrake;
} hp2_drive_in;

typedef struct {
    /* state */
    float pos[3];               /* the car's model origin's ground point */
    float heading;              /* radians */
    float vel[3];               /* m/s, world */
    float yawrate;
    float steer_angle;          /* radians, + = right */
    float pitch, roll;          /* radians, nose up / left side up */
    float spin;                 /* wheel rotation, radians */
    float speed;                /* forward speed, m/s (negative backing up) */
    int   on_ground, surface;
    int   hit;                  /* frames since the last wall hit, 0 = this one */
    float acc;                  /* time not yet stepped */
    /* from the car */
    float wheelbase, track, half_len, half_wid, max_speed, ride;
    float hub_x_front, hub_x_rear;
} hp2_drive;

/* ride: how far the model's origin sits above the ground (the tyres' lowest
 * point is at z = hub z - radius). */
void hp2_drive_init(hp2_drive *d, const hp2_carrec *rec, float x, float y, float z, float heading);
/* Advances dt seconds (in 1/120 s steps). */
void hp2_drive_step(hp2_drive *d, const hp2_coll *c, const hp2_drive_in *in, float dt);

#ifdef __cplusplus
}
#endif
#endif
