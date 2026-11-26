/* hp2_race - laps, the race clock and wrong-way detection along a track's
 * main route (docs/formats.md 11, "Routes").
 *
 * The main route is a list of points, each with its progress: the distance
 * along the route from the start. A car's progress is that of the nearest
 * point on the route's polyline; on a circuit the change from one step to
 * the next is taken the short way round, so crossing the start line adds a
 * whole lap and driving it backwards takes the lap away again. Shortcuts
 * need nothing extra: their points carry the main route's progress.
 *
 * A race: a countdown (3, 2, 1, GO), then the clock runs until the last lap
 * is done (on a point-to-point track: until the finish). No SDL, plain C99.
 */
#ifndef HP2_RACE_H
#define HP2_RACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_RACE_MAX_LAPS 16

enum { HP2_RACE_COUNTDOWN, HP2_RACE_RUNNING, HP2_RACE_FINISHED };

typedef struct {
    /* the route (not owned) */
    const float *route;         /* n x (x, y, progress) */
    int          n, loop;
    float        len;
    /* the race */
    int          laps;          /* to run */
    int          state;
    float        countdown;     /* seconds left before GO */
    float        clock;         /* since GO */
    float        lap_start;     /* clock when the current lap began */
    float        lap_time[HP2_RACE_MAX_LAPS];
    float        best;          /* best lap, 0 = none yet */
    int          lap;           /* 1-based lap being run; laps + 1 when finished */
    float        progress;      /* along the route, 0 .. len */
    float        travelled;     /* progress summed over laps; < 0 behind the line */
    int          wrong_way;     /* 1 while driving against the route */
    float        wrong_time;
    float        dir[2];        /* the route's direction at the car */
    float        finish_time;   /* clock at the finish, 0 = not yet */
} hp2_race;

/* route: n points of (x, y, progress); loop: a circuit; len: a lap, or the
 * distance to the finish. laps is forced to 1 on a point-to-point track. */
void hp2_race_init(hp2_race *r, const float *route, int n, int loop, float len, int laps);
/* Progress of a point along the route, and the route's direction there. */
float hp2_race_progress(const hp2_race *r, float x, float y, float dir[2]);
/* Places sorted by who is furthest on (finished first by finishing time):
 * order[0] is the leader. Returns n. */
int hp2_race_order(const hp2_race *const *cars, int n, int *order);

/* Advances the race by dt seconds with the car at (x, y), moving (vx, vy).
 * Returns 1 the step a lap (or the race) is completed. */
int hp2_race_step(hp2_race *r, float dt, float x, float y, float vx, float vy);

/* The point of the route at a progress (wrapped on a circuit, held to the
 * ends otherwise), and the route's direction there. */
void hp2_race_point(const hp2_race *r, float progress, float xy[2], float dir[2]);

/* "1:23.45" (minutes, seconds, hundredths) into buf. */
void hp2_race_time(float t, char *buf, int size);

/* How a race ended, as hp2track hands it to the menus (--result FILE): a
 * few "key value" lines. Times are in the game's ticks, 4000 a second.
 * place 0 = no opponents; ticks and best are 0 unless finished. */
#define HP2_RESULT_CARS 8
typedef struct {
    int finished, place;
    long ticks, best;
    int norder, order[HP2_RESULT_CARS];   /* the cars from the leader down: 0 is the player, 1.. the
                                             opponents in the order they were named */
} hp2_race_result;

/* Lap Knockout ("Last place on each lap eliminated"): each time the leader
 * completes a lap, the car furthest behind (least travelled) is out. It is
 * decided when the player (car 0) is out or one car is left. */
typedef struct {
    int on, lap, done, nout;
    int out[HP2_RESULT_CARS], elim[HP2_RESULT_CARS];   /* out[i]: car i is out; elim: in the order they went */
} hp2_knockout;
void hp2_knockout_reset(hp2_knockout *k);                         /* keeps on */
void hp2_knockout_update(hp2_knockout *k, const hp2_race *races, int n);
/* The cars from the first down: those still in by their race order, then the
 * knocked-out ones, the last to go first. */
void hp2_knockout_order(const hp2_knockout *k, const hp2_race *races, int n, int *order);

/* A Pursuit event: you are the cop (car 0) and the other cars are the
 * speeders. Stay within HP2_ARREST_RANGE metres of one for HP2_ARREST_TIME
 * seconds (the time runs down at the same pace when you fall away) and it
 * is arrested: it stops. The event is done when all are. The range and the
 * time are the port's own: the game's arrest rule is not decoded. */
/* You're The Cop: arrest four speeders within 150 seconds (the screens say four; the time is the port's own). */
#define HP2_COP_QUOTA    4
#define HP2_COP_TIME     150.0f
#define HP2_ARREST_RANGE 12.0f
#define HP2_ARREST_TIME  2.5f
typedef struct {
    int on, speeders, narrested, done;
    int quota;                       /* arrests that finish the chase; 0 = every speeder */
    int arrested[HP2_RESULT_CARS];
    float hold[HP2_RESULT_CARS];
    float time;                      /* the race clock when the last was arrested */
    int ram;                         /* 1: arrests come from ramming (below), not from staying close */
    float health[HP2_RESULT_CARS], cool[HP2_RESULT_CARS];
} hp2_pursuit;
/* Ramming (the port's own numbers: the game's arrest rule is not decoded). Each speeder has HP2_RAM_HEALTH; a
 * hit closing faster than HP2_RAM_MIN m/s takes (closing - HP2_RAM_MIN) x HP2_RAM_DAMAGE off it, at most one hit
 * a half second; at 0 it is disabled and arrested. Returns the damage done (0 if none), and sets
 * *arrested when this hit arrested it. */
#define HP2_RAM_HEALTH 100.0f
#define HP2_RAM_MIN    3.0f
#define HP2_RAM_DAMAGE 3.5f
float hp2_pursuit_ram(hp2_pursuit *p, int car, float closing, float clock, int *arrested);
/* Damage from backup (roadblock, helicopter): takes dmg off a speeder, arrests it at 0 (the port's own). */
float hp2_pursuit_damage(hp2_pursuit *p, int car, float dmg, float clock, int *arrested);
void hp2_pursuit_reset(hp2_pursuit *p);                           /* keeps on */
/* One step for n cars at (x[i], y[i]); clock = seconds since GO. Returns
 * the number arrested this step. */
int hp2_pursuit_step(hp2_pursuit *p, float dt, float clock, int n, const float *x, const float *y);

/* Writes the text for race r (the player), the player's place and the
 * order of the norder cars (may be NULL / 0); returns its length, or -1 when
 * cap is too small. */
int hp2_race_result_format(const hp2_race *r, int place, const int *order, int norder, char *out, size_t cap);
/* Reads that text back. Returns 1 when it is a result file, else 0 and
 * *out is zeroed. Unknown lines are ignored. */
int hp2_race_result_parse(const char *text, hp2_race_result *out);

#ifdef __cplusplus
}
#endif
#endif
