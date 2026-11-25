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
typedef struct { int finished, place; long ticks, best; } hp2_race_result;

/* Writes the text for race r (the player) and the player's place; returns
 * its length, or -1 when cap is too small. */
int hp2_race_result_format(const hp2_race *r, int place, char *out, size_t cap);
/* Reads that text back. Returns 1 when it is a result file, else 0 and
 * *out is zeroed. Unknown lines are ignored. */
int hp2_race_result_parse(const char *text, hp2_race_result *out);

#ifdef __cplusplus
}
#endif
#endif
