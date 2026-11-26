/* hp2_race - laps and the race clock. See hp2_race.h. */
#include "hp2_race.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* The progress nearest (x, y); with near >= 0, a stretch of the route far
 * (more than 400 m) along from that progress counts as further away the
 * further it is, so a car is not moved to another part of the route that
 * passes close by (a road alongside, a crossing) while shortcuts still
 * count. */
static float nearest(const hp2_race *r, float x, float y, float dir[2], float near)
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
        pa = a[2];
        pb = (i + 1 == r->n) ? r->len : b[2];         /* the closing segment ends at a full lap */
        if (near >= 0) {
            float j = fabsf(pa + t * (pb - pa) - near);
            if (r->loop && j > r->len / 2) j = r->len - j;
            if (j > 400) d += (j - 400) * (j - 400);
        }
        if (d < best) {
            best = d;
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

float hp2_race_progress(const hp2_race *r, float x, float y, float dir[2])
{
    return nearest(r, x, y, dir, -1);
}

int hp2_race_step(hp2_race *r, float dt, float x, float y, float vx, float vy)
{
    float p, d, speed;
    int done = 0;
    if (!r->n) return 0;
    p = r->state == HP2_RACE_COUNTDOWN ? hp2_race_progress(r, x, y, r->dir) : nearest(r, x, y, r->dir, r->progress);
    if (r->state == HP2_RACE_COUNTDOWN) {
        r->countdown -= dt;
        r->progress = p;
        /* a grid slot behind the line counts as not yet there */
        r->travelled = r->loop && p > r->len / 2 ? p - r->len : p;
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
            if (r->lap > r->laps) {
                r->state = HP2_RACE_FINISHED;
                r->finish_time = r->clock;
            }
        }
    } else if (r->travelled >= r->len - 10 || p >= r->len - 10) {
        r->lap_time[0] = r->clock;
        r->best = r->clock;
        r->lap = 2;
        r->state = HP2_RACE_FINISHED;
        r->finish_time = r->clock;
        done = 1;
    }
    /* wrong way: moving against the route for over half a second */
    speed = sqrtf(vx * vx + vy * vy);
    if (speed > 3 && (vx * r->dir[0] + vy * r->dir[1]) < -0.5f * speed) r->wrong_time += dt;
    else r->wrong_time = 0;
    r->wrong_way = r->wrong_time > 0.5f;
    return done;
}

/* Point k of the route, wrapped on a circuit, held to the ends otherwise. */
static const float *pt(const hp2_race *r, int k)
{
    if (r->loop) k = ((k % r->n) + r->n) % r->n;
    else if (k < 0) k = 0;
    else if (k >= r->n) k = r->n - 1;
    return r->route + 3 * k;
}

/* The route is a smooth curve through its points (its progress is that
 * curve's length, docs/formats.md 11): here a Catmull-Rom spline. A
 * straight line between two points 100 m apart cuts the inside of a bend,
 * through the walls along it. */
void hp2_race_point(const hp2_race *r, float p, float xy[2], float dir[2])
{
    int i, last = r->loop ? r->n : r->n - 1;
    if (r->loop) {
        p = fmodf(p, r->len);
        if (p < 0) p += r->len;
    } else {
        if (p < 0) p = 0;
        if (p > r->route[3 * (r->n - 1) + 2]) p = r->route[3 * (r->n - 1) + 2];
    }
    for (i = 0; i < last; i++) {
        const float *p0 = pt(r, i - 1), *p1 = pt(r, i), *p2 = pt(r, i + 1), *p3 = pt(r, i + 2);
        float pb = (i + 1 == r->n) ? r->len : p2[2], t, t2, t3, dx, dy, l;
        int k;
        if (p > pb && i + 1 < last) continue;
        t = pb > p1[2] ? (p - p1[2]) / (pb - p1[2]) : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        t2 = t * t;
        t3 = t2 * t;
        for (k = 0; k < 2; k++)
            xy[k] = 0.5f * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                            + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3);
        dx = 0.5f * ((-p0[0] + p2[0]) + 2 * (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t + 3 * (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t2);
        dy = 0.5f * ((-p0[1] + p2[1]) + 2 * (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t + 3 * (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t2);
        l = sqrtf(dx * dx + dy * dy);
        if (dir) {
            dir[0] = l > 0 ? dx / l : 1;
            dir[1] = l > 0 ? dy / l : 0;
        }
        return;
    }
    xy[0] = r->route[0];
    xy[1] = r->route[1];
    if (dir) { dir[0] = 1; dir[1] = 0; }
}

static const hp2_race *const *sort_cars;
static int by_place(const void *a, const void *b)
{
    const hp2_race *x = sort_cars[*(const int *)a], *y = sort_cars[*(const int *)b];
    int fx = x->state == HP2_RACE_FINISHED, fy = y->state == HP2_RACE_FINISHED;
    if (fx != fy) return fx ? -1 : 1;
    if (fx && x->finish_time != y->finish_time) return x->finish_time < y->finish_time ? -1 : 1;
    if (x->travelled != y->travelled) return x->travelled > y->travelled ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

int hp2_race_order(const hp2_race *const *cars, int n, int *order)
{
    int i;
    for (i = 0; i < n; i++) order[i] = i;
    sort_cars = cars;
    qsort(order, (size_t)n, sizeof *order, by_place);
    return n;
}

void hp2_race_time(float t, char *buf, int size)
{
    int cs = (int)(t * 100 + 0.5f);
    if (cs < 0) cs = 0;
    snprintf(buf, (size_t)size, "%d:%02d.%02d", cs / 6000, cs / 100 % 60, cs % 100);
}

int hp2_race_result_format(const hp2_race *r, int place, const int *order, int norder, char *out, size_t cap)
{
    long ticks = 0, best = 0;
    int fin = r->state == HP2_RACE_FINISHED, i, n;
    if (fin) {
        float total = 0;
        for (i = 0; i < r->laps && i < HP2_RACE_MAX_LAPS; i++) total += r->lap_time[i];
        ticks = (long)(total * 4000.0f + 0.5f);
        best = (long)(r->best * 4000.0f + 0.5f);
    }
    n = snprintf(out, cap, "hp2result 1\nfinished %d\nplace %d\nticks %ld\nbest %ld\n", fin, place < 0 ? 0 : place, ticks, best);
    if (n < 0 || (size_t)n >= cap) return -1;
    if (order && norder > 0) {
        int k, m;
        if (norder > HP2_RESULT_CARS) norder = HP2_RESULT_CARS;
        m = snprintf(out + n, cap - (size_t)n, "order");
        if (m < 0 || (size_t)(n + m) >= cap) return -1;
        n += m;
        for (k = 0; k < norder; k++) {
            m = snprintf(out + n, cap - (size_t)n, " %d", order[k]);
            if (m < 0 || (size_t)(n + m) >= cap) return -1;
            n += m;
        }
        m = snprintf(out + n, cap - (size_t)n, "\n");
        if (m < 0 || (size_t)(n + m) >= cap) return -1;
        n += m;
    }
    return n;
}

int hp2_race_result_parse(const char *text, hp2_race_result *out)
{
    const char *p = text;
    int ok = 0;
    memset(out, 0, sizeof *out);
    if (!text) return 0;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[64];
        long v;
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = 0;
            if (!strcmp(line, "hp2result 1")) ok = 1;
            else if (sscanf(line, "finished %ld", &v) == 1) out->finished = v != 0;
            else if (sscanf(line, "place %ld", &v) == 1) out->place = v < 0 || v > 99 ? 0 : (int)v;
            else if (sscanf(line, "ticks %ld", &v) == 1) out->ticks = v < 0 ? 0 : v;
            else if (sscanf(line, "best %ld", &v) == 1) out->best = v < 0 ? 0 : v;
            else if (!strncmp(line, "order", 5) && (line[5] == ' ' || !line[5])) {
                const char *q = line + 5;
                char *end;
                out->norder = 0;
                while (out->norder < HP2_RESULT_CARS) {
                    long c = strtol(q, &end, 10);
                    if (end == q) break;
                    if (c < 0 || c >= HP2_RESULT_CARS) { out->norder = 0; break; }
                    out->order[out->norder++] = (int)c;
                    q = end;
                }
            }
        }
        if (!e) break;
        p = e + 1;
    }
    if (!ok) memset(out, 0, sizeof *out);
    return ok;
}

void hp2_knockout_reset(hp2_knockout *k)
{
    int on = k->on;
    memset(k, 0, sizeof *k);
    k->on = on;
}

void hp2_knockout_update(hp2_knockout *k, const hp2_race *races, int n)
{
    int i, alive = 0, done_laps = 0;
    if (!k->on || k->done) return;
    if (n > HP2_RESULT_CARS) n = HP2_RESULT_CARS;
    for (i = 0; i < n; i++) {
        int d = races[i].lap - 1;
        if (k->out[i]) continue;
        alive++;
        if (d > races[i].laps) d = races[i].laps;
        if (d > done_laps) done_laps = d;
    }
    while (k->lap < done_laps && alive > 1 && !k->done) {
        int worst = -1;
        for (i = 0; i < n; i++)
            if (!k->out[i] && (worst < 0 || races[i].travelled < races[worst].travelled)) worst = i;
        k->out[worst] = 1;
        k->elim[k->nout++] = worst;
        alive--;
        k->lap++;
        if (worst == 0 || alive == 1) k->done = 1;
    }
}

void hp2_knockout_order(const hp2_knockout *k, const hp2_race *races, int n, int *order)
{
    const hp2_race *p[HP2_RESULT_CARS];
    int idx[HP2_RESULT_CARS], ord[HP2_RESULT_CARS], m = 0, i, c = 0;
    if (n > HP2_RESULT_CARS) n = HP2_RESULT_CARS;
    for (i = 0; i < n; i++)
        if (!k->out[i]) { idx[m] = i; p[m] = &races[i]; m++; }
    hp2_race_order(p, m, ord);
    for (i = 0; i < m; i++) order[c++] = idx[ord[i]];
    for (i = k->nout - 1; i >= 0; i--) order[c++] = k->elim[i];
}

void hp2_pursuit_reset(hp2_pursuit *p)
{
    int on = p->on, ram = p->ram, i;
    memset(p, 0, sizeof *p);
    p->on = on;
    p->ram = ram;
    for (i = 0; i < HP2_RESULT_CARS; i++) p->health[i] = HP2_RAM_HEALTH;
}

int hp2_pursuit_step(hp2_pursuit *p, float dt, float clock, int n, const float *x, const float *y)
{
    int i, got = 0;
    if (!p->on || p->done || n < 2) return 0;
    if (n > HP2_RESULT_CARS) n = HP2_RESULT_CARS;
    p->speeders = n - 1;
    for (i = 1; i < n; i++) {
        float dx = x[i] - x[0], dy = y[i] - y[0];
        if (p->arrested[i]) continue;
        if (p->cool[i] > 0) p->cool[i] -= dt;
        if (p->ram) continue;                          /* arrests come from hp2_pursuit_ram */
        if (dx * dx + dy * dy <= HP2_ARREST_RANGE * HP2_ARREST_RANGE) p->hold[i] += dt;
        else p->hold[i] -= dt;
        if (p->hold[i] < 0) p->hold[i] = 0;
        if (p->hold[i] >= HP2_ARREST_TIME) {
            p->arrested[i] = 1;
            p->narrested++;
            got++;
        }
    }
    if (p->narrested >= p->speeders) { p->done = 1; p->time = clock; }
    return got;
}

float hp2_pursuit_ram(hp2_pursuit *p, int car, float closing, float clock, int *arrested)
{
    float dmg;
    if (arrested) *arrested = 0;
    if (!p->on || !p->ram || p->done || car < 1 || car >= HP2_RESULT_CARS || p->arrested[car] || p->cool[car] > 0) return 0;
    if (closing <= HP2_RAM_MIN) return 0;
    if (p->health[car] <= 0) p->health[car] = HP2_RAM_HEALTH;
    dmg = (closing - HP2_RAM_MIN) * HP2_RAM_DAMAGE;
    p->cool[car] = 0.5f;
    p->health[car] -= dmg;
    if (p->health[car] <= 0) {
        p->health[car] = 0;
        p->arrested[car] = 1;
        p->narrested++;
        if (arrested) *arrested = 1;
        if (p->speeders > 0 && p->narrested >= p->speeders) { p->done = 1; p->time = clock; }
    }
    return dmg;
}
