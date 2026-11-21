/* hp2_drive - a first, simple driving model: a car that accelerates,
 * brakes, steers and slides on the track's collision mesh, stays on the
 * ground and stops at walls.
 *
 * This is NOT the game's handling. The game's physics (PhysicsCar: rigid
 * body, engine, drivetrain, suspension, tyres, aerodynamics) are not
 * decoded yet; this is a stand-in so a car can be driven around a track
 * while they are. Going forward, though, it is each car's own: the engine's
 * torque curve, gearing, tyre size, mass and air drag from the car
 * database, combined the way the game's own acceleration table combines
 * them (hp2_car.h), with an automatic gearbox that changes up at 95% of the
 * redline as that table does. Grip, braking and steering are made up. It
 * also uses the wheelbase and wheel hubs, and the
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
    /* the gearbox: automatic, from the car's own ratios */
    int   gear;                 /* -1 reverse, 0 neutral, 1.. */
    int   gears;
    float ratio[6], reverse, final_drive, wheel_radius;
    float idle_rpm, redline_rpm, rpm;
    float shift;                /* seconds left in a gear change */
    float load;                 /* 0..1, how hard the engine pulls (for its sound) */
    /* the engine and drivetrain (hp2_car.h) */
    float torque[9], mass, drag, efficiency, final_drive_used;
} hp2_drive;

/* Engine torque (kN m) at an engine speed: the car's curve, flat below
 * idle, nothing past the redline. */
float hp2_drive_torque(const hp2_drive *d, float rpm);
/* Driving force over mass (m/s^2) at full throttle in a gear at speed v
 * (m/s), less air drag; what accelerates the car before grip limits it. */
float hp2_drive_accel(const hp2_drive *d, float v, int gear);

/* ride: how far the model's origin sits above the ground (the tyres' lowest
 * point is at z = hub z - radius). */
void hp2_drive_init(hp2_drive *d, const hp2_carrec *rec, float x, float y, float z, float heading);
/* Advances dt seconds (in 1/120 s steps). */
void hp2_drive_step(hp2_drive *d, const hp2_coll *c, const hp2_drive_in *in, float dt);

#ifdef __cplusplus
}
#endif
#endif
