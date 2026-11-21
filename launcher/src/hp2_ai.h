/* hp2_ai - opponents: a stand-in driver for the cars the player races,
 * using the same driving model (hp2_drive) and the pedals and steering a
 * player has. The game's own AI (its racing lines, rubber-banding, its use
 * of the shortcuts) is not decoded. This one follows a driving line found
 * over the track's collision ground (hp2_ai_line), steers for a point on it
 * in its own lane, and slows for bends to what its tyres can take.
 * No SDL, plain C99.
 */
#ifndef HP2_AI_H
#define HP2_AI_H

#include "hp2_coll.h"
#include "hp2_drive.h"
#include "hp2_race.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The driving line. The main route's points are 100 m or more apart and a
 * curve through them can leave the road by tens of metres (across the
 * ground between a fork, through walls), and the road quads run down the
 * middle of a road, along the barrier of a divided one. So the line is
 * searched for over the collision ground: 2 m cells within 60 m of the
 * route, joined where the ground runs on and no wall stands between, and
 * from the route every 150 m to the next the cheapest way across them,
 * kept near the route (not a shortcut, not the other side of a fork) and
 * away from the road's edges. Without collision it is the route itself. */
typedef struct {
    int    n, loop;
    float  len;                 /* along the line; a lap on a circuit */
    float *pt;                  /* n x (x, y, z, half the road's width), HP2_AI_STEP apart */
} hp2_ai_line;

#define HP2_AI_STEP 4.0f

/* c may be NULL. Returns 0 on success. */
int  hp2_ai_line_build(hp2_ai_line *l, const hp2_race *r, const hp2_coll *c);
void hp2_ai_line_free(hp2_ai_line *l);
/* The line at a distance s along it: point, direction and half-width. */
void hp2_ai_line_point(const hp2_ai_line *l, float s, float xy[2], float dir[2], float *half);
/* Distance along the line nearest (x, y, z), searched from `from` - 40 to
 * `from` + 80 (from < 0: everywhere). */
float hp2_ai_line_find(const hp2_ai_line *l, float x, float y, float z, float from);

typedef struct {
    float lane;                 /* metres left (+) or right (-) of the line */
    float skill;                /* 0.7 .. 1: how close to the limit it corners */
    float stuck;                /* seconds spent barely moving */
    float reverse;              /* seconds left backing out */
    float at;                   /* where it is along the line; set < 0 to search */
} hp2_ai;

/* Sets the pedals and steering for one step. r is this car's race (it only
 * drives once the race is running). */
void hp2_ai_drive(hp2_ai *ai, const hp2_race *r, const hp2_ai_line *l, const hp2_drive *d, float dt, hp2_drive_in *in);

#ifdef __cplusplus
}
#endif
#endif
