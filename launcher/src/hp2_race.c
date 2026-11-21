/* hp2_race - laps and the race clock. See hp2_race.h. */
#include "hp2_race.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void hp2_race_init(hp2_race *r, const float *route, int n, int loop, float len, int laps)
{
    memset(r, 0, sizeof *r);
    r->route = route;
    r->n = n;
    r->loop = loop;
    r->len = len > 0 ? len : 1;
    r->laps = loop ? (laps < 1 ? 1 : laps > HP2_RACE_MAX_LAPS ? HP2_RACE_MAX_LAPS : laps) : 1;
    r->state = HP2_RACE_COUNTDOWN;
    r->countdown = 3.0f;
    r->lap = 1;
}

float hp2_race_progress(const hp2_race *r, float x, float y, float dir[2])
{
    float best = 1e30f, prog = 0, dx = 1, dy = 0;
    int i, segs = r->loop ? r->n : r->n - 1;
    for (i = 0; i < segs; i++) {
        const float *a = r->route + 3 * i, *b = r->route + 3 * ((i + 1) % r->n);
        float ex = b[0] - a[0], ey = b[1] - a[1], l2 = ex * ex + ey * ey, t, px, py, d, pa, pb;
        if (l2 < 1e-6f) continue;
        t = ((x - a[0]) * ex + (y - a[1]) * ey) / l2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        px = a[0] + t * ex - x;
        py = a[1] + t * ey - y;
        d = px * px + py * py;
        if (d < best) {
            best = d;
            pa = a[2];
            pb = (i + 1 == r->n) ? r->len : b[2];     /* the closing segment ends at a full lap */
            prog = pa + t * (pb - pa);
            l2 = sqrtf(l2);
            dx = ex / l2;
            dy = ey / l2;
        }
    }
    if (dir) { dir[0] = dx; dir[1] = dy; }
    if (r->loop) {
        prog = fmodf(prog, r->len);
        if (prog < 0) prog += r->len;
    }
    return prog;
}

int hp2_race_step(hp2_race *r, float dt, float x, float y, float vx, float vy)
{
    float p, d, speed;
    int done = 0;
    if (!r->n) return 0;
    p = hp2_race_progress(r, x, y, r->dir);
    if (r->state == HP2_RACE_COUNTDOWN) {
        r->countdown -= dt;
        r->progress = p;
        r->travelled = 0;
        if (r->countdown <= 0) {
            r->state = HP2_RACE_RUNNING;
            r->countdown = 0;
        }
        return 0;
    }
    /* how far along since the last step, the short way round on a circuit */
    d = p - r->progress;
    if (r->loop) {
        if (d > r->len / 2) d -= r->len;
        if (d < -r->len / 2) d += r->len;
    }
    r->progress = p;
    if (r->state == HP2_RACE_FINISHED) return 0;
    r->clock += dt;
    r->travelled += d;
    if (r->loop) {
        /* the start line sits at progress 0; a lap counts once travelled
         * reaches it, never twice for driving back and forth over it */
        while (r->travelled >= r->len * r->lap && r->state == HP2_RACE_RUNNING) {
            float t = r->clock - r->lap_start;
            r->lap_time[r->lap - 1] = t;
            if (r->best == 0 || t < r->best) r->best = t;
            r->lap_start = r->clock;
            r->lap++;
            done = 1;
            if (r->lap > r->laps) r->state = HP2_RACE_FINISHED;
        }
    } else if (r->travelled >= r->len - 10 || p >= r->len - 10) {
        r->lap_time[0] = r->clock;
        r->best = r->clock;
        r->lap = 2;
        r->state = HP2_RACE_FINISHED;
        done = 1;
    }
    /* wrong way: moving against the route for over half a second */
    speed = sqrtf(vx * vx + vy * vy);
    if (speed > 3 && (vx * r->dir[0] + vy * r->dir[1]) < -0.5f * speed) r->wrong_time += dt;
    else r->wrong_time = 0;
    r->wrong_way = r->wrong_time > 0.5f;
    return done;
}

void hp2_race_time(float t, char *buf, int size)
{
    int cs = (int)(t * 100 + 0.5f);
    if (cs < 0) cs = 0;
    snprintf(buf, (size_t)size, "%d:%02d.%02d", cs / 6000, cs / 100 % 60, cs % 100);
}
