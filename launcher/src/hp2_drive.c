/* hp2_drive - a simple stand-in driving model. See hp2_drive.h. */
#include "hp2_drive.h"

#include <math.h>
#include <string.h>

#define STEP    (1.0f / 120.0f)
#define G       9.81f
#define MPH     0.44704f

void hp2_drive_init(hp2_drive *d, const hp2_carrec *rec, float x, float y, float z, float heading)
{
    int k;
    float ride = 0;
    memset(d, 0, sizeof *d);
    d->pos[0] = x;
    d->pos[1] = y;
    d->pos[2] = z;
    d->heading = heading;
    d->hub_x_front = (rec->wheel[HP2_WHEEL_FL].x + rec->wheel[HP2_WHEEL_FR].x) / 2;
    d->hub_x_rear = (rec->wheel[HP2_WHEEL_RL].x + rec->wheel[HP2_WHEEL_RR].x) / 2;
    d->wheelbase = d->hub_x_front - d->hub_x_rear;
    if (d->wheelbase < 1.5f) d->wheelbase = 2.6f;
    d->track = rec->wheel[HP2_WHEEL_FL].y - rec->wheel[HP2_WHEEL_FR].y;
    if (d->track < 1) d->track = 1.6f;
    d->half_len = rec->length > 2 ? rec->length / 2 : 2.3f;
    d->half_wid = rec->width > 1 ? rec->width / 2 : 0.9f;
    d->max_speed = (rec->top_speed > 50 ? rec->top_speed : 150) * MPH;
    for (k = 0; k < 4; k++) {
        float r = rec->wheel[k].radius - rec->wheel[k].z;
        if (r > ride) ride = r;
    }
    d->ride = ride;
    d->gears = rec->gears > 0 ? rec->gears : 0;
    for (k = 0; k < d->gears; k++) d->ratio[k] = rec->ratio[k];
    if (!d->gears) { d->gears = 1; d->ratio[0] = 1; }
    d->reverse = rec->reverse < 0 ? -rec->reverse : rec->reverse > 0 ? rec->reverse : 3.5f;
    d->final_drive = rec->final_drive > 0 ? rec->final_drive : 3.5f;
    d->wheel_radius = rec->wheel[HP2_WHEEL_RL].radius > 0.1f ? rec->wheel[HP2_WHEEL_RL].radius : 0.32f;
    d->idle_rpm = rec->idle_rpm > 300 ? rec->idle_rpm : 900;
    d->redline_rpm = rec->redline_rpm > d->idle_rpm + 1000 ? rec->redline_rpm : 7000;
    d->rpm = d->idle_rpm;
    d->gear = 1;
    d->on_ground = 1;
    d->hit = 1000;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Ground height under a point near the car, or the car's own height. */
static float ground_at(const hp2_coll *c, float x, float y, float zref, float fallback)
{
    float z;
    if (hp2_coll_ground(c, x, y, zref, 1.5f, &z, NULL, NULL)) return z;
    return fallback;
}

/* An automatic gearbox: up near the redline, down when the gear below
 * has room; the engine speed follows the wheels, or the throttle when the
 * clutch slips at a standstill. */
static void gearbox(hp2_drive *d, const hp2_drive_in *in, float h)
{
    float wheel = fabsf(d->speed) / (2 * 3.14159265f * d->wheel_radius) * 60.0f, target, ratio;
    if (d->shift > 0) d->shift -= h;
    if (d->speed < -0.5f) d->gear = -1;
    else if (d->gear < 1 && (d->speed > 0.5f || in->throttle > 0)) d->gear = 1;
    if (d->gear >= 1 && d->shift <= 0) {
        float up = wheel * d->ratio[d->gear - 1] * d->final_drive;
        if (up > 0.94f * d->redline_rpm && d->gear < d->gears) {
            d->gear++;
            d->shift = 0.25f;
        } else if (d->gear > 1 && up < 0.5f * d->redline_rpm
                   && wheel * d->ratio[d->gear - 2] * d->final_drive < 0.8f * d->redline_rpm) {
            d->gear--;
            d->shift = 0.2f;
        }
    }
    ratio = d->gear < 0 ? d->reverse : d->gear >= 1 ? d->ratio[d->gear - 1] : 0;
    target = wheel * ratio * d->final_drive;
    /* the clutch slips at low speed: the engine runs up with the throttle */
    if ((d->gear == 1 || d->gear == -1) && in->throttle + in->brake > 0) {
        float slip = d->idle_rpm + ((d->gear == 1 ? in->throttle : in->brake))
                     * (0.55f * d->redline_rpm - d->idle_rpm);
        if (slip > target) target = slip;
    }
    if (target < d->idle_rpm) target = d->idle_rpm;
    if (target > d->redline_rpm * 1.02f) target = d->redline_rpm * 1.02f;
    if (d->shift > 0.1f) target = d->rpm;                    /* the clutch is out */
    d->rpm += (target - d->rpm) * fminf(1, 12 * h);
    d->load += ((d->gear == -1 ? in->brake : in->throttle) * (d->shift > 0 ? 0.3f : 1.0f) - d->load) * fminf(1, 10 * h);
}

static void substep(hp2_drive *d, const hp2_coll *c, const hp2_drive_in *in)
{
    float h = STEP, fx = cosf(d->heading), fy = sinf(d->heading);
    float lx = -fy, ly = fx;                     /* left */
    float vf = d->vel[0] * fx + d->vel[1] * fy, vl = d->vel[0] * lx + d->vel[1] * ly;
    float maxsteer = 0.55f / (1 + fabsf(vf) / 12), target, a = 0, grip, yaw_target, nz[3] = { 0, 0, 1 };
    float gz, ox = d->pos[0], oy = d->pos[1];
    int surface = 0, on;

    /* steering eases towards the stick, less lock at speed */
    target = clampf(in->steer, -1, 1) * maxsteer;
    if (d->steer_angle < target) d->steer_angle = fminf(target, d->steer_angle + 3.0f * h);
    else d->steer_angle = fmaxf(target, d->steer_angle - 3.0f * h);

    if (d->on_ground) {
        /* engine, brakes, reverse */
        float top = d->max_speed;
        if (in->throttle > 0 && vf > -0.5f)
            a += 9.0f * in->throttle * clampf(1 - vf / top, 0, 1) * (d->shift > 0 ? 0.3f : 1.0f);
        if (in->brake > 0) {
            if (vf > 0.5f) a -= 14.0f * in->brake;
            else if (vf > -12) a -= 5.0f * in->brake;
        }
        if (in->throttle > 0 && vf < -0.5f) a += 14.0f * in->throttle;   /* braking while backing up */
        if (in->handbrake) a -= (vf > 0 ? 6.0f : vf < 0 ? -6.0f : 0);
        /* rolling resistance and air */
        a -= (vf > 0 ? 1 : vf < 0 ? -1 : 0) * (0.3f + 0.00035f * vf * vf);
        if (fabsf(vf) < 0.3f && in->throttle <= 0 && in->brake <= 0) { vf = 0; a = 0; }
        vf += a * h;

        /* tyres: sideways slip dies away; the handbrake lets the back go */
        grip = in->handbrake ? 1.5f : 9.0f;
        if ((d->surface & 0xF) == 2 || (d->surface & 0xF) == 3 || (d->surface & 0xF) == 6) grip *= 0.7f;
        vl *= expf(-grip * h);
        yaw_target = -vf * tanf(d->steer_angle) / d->wheelbase;
        if (in->handbrake) yaw_target *= 1.6f;
        d->yawrate += (yaw_target - d->yawrate) * fminf(1, 10 * h);
    } else {
        d->yawrate *= expf(-1.0f * h);
    }
    d->heading += d->yawrate * h;
    fx = cosf(d->heading);
    fy = sinf(d->heading);
    lx = -fy;
    ly = fx;
    d->vel[0] = fx * vf + lx * vl;
    d->vel[1] = fy * vf + ly * vl;

    /* slopes pull downhill */
    on = hp2_coll_ground(c, d->pos[0], d->pos[1], d->pos[2] + 1.5f, 1.5f, &gz, &surface, nz);
    if (on && d->on_ground) {
        d->vel[0] += G * nz[2] * nz[0] * h;
        d->vel[1] += G * nz[2] * nz[1] * h;
    }

    d->pos[0] += d->vel[0] * h;
    d->pos[1] += d->vel[1] * h;

    /* walls: two circles, front and rear */
    {
        float r = d->half_wid, off = d->half_len - d->half_wid, n[2], sx = 0, sy = 0, wn[2] = { 0, 0 };
        int k, hit = 0;
        for (k = 0; k < 2; k++) {
            float s = k ? -off : off, cx = d->pos[0] + fx * s, cy = d->pos[1] + fy * s, px = cx, py = cy;
            if (hp2_coll_push(c, &px, &py, d->pos[2], r, n)) {
                sx += px - cx;
                sy += py - cy;
                wn[0] += n[0];
                wn[1] += n[1];
                hit = 1;
            }
        }
        if (hit) {
            float l = sqrtf(wn[0] * wn[0] + wn[1] * wn[1]), into;
            d->pos[0] += sx;
            d->pos[1] += sy;
            if (l > 0) {
                wn[0] /= l;
                wn[1] /= l;
                into = d->vel[0] * wn[0] + d->vel[1] * wn[1];
                if (into < 0) {
                    /* lose what drove into the wall (with a little bounce),
                     * and some of the rest by how hard the hit was: a
                     * glancing touch slides along, a head-on one stops */
                    float keep = 1 - fminf(0.6f, -into * 0.04f);
                    d->vel[0] -= 1.2f * into * wn[0];
                    d->vel[1] -= 1.2f * into * wn[1];
                    d->vel[0] *= keep;
                    d->vel[1] *= keep;
                }
            }
            d->hit = 0;
        } else if (d->hit < 1000) {
            d->hit++;
        }
    }

    /* the ground */
    on = hp2_coll_ground(c, d->pos[0], d->pos[1], d->pos[2] + 1.5f, 1.5f, &gz, &surface, nz);
    if (!on) {                          /* off the edge of the world: stay put */
        d->pos[0] = ox;
        d->pos[1] = oy;
        d->vel[0] = d->vel[1] = 0;
        return;
    }
    d->surface = surface;
    if (d->pos[2] > gz + 0.05f) {       /* in the air */
        d->vel[2] -= G * h;
        d->pos[2] += d->vel[2] * h;
        if (d->pos[2] <= gz) { d->pos[2] = gz; d->vel[2] = 0; d->on_ground = 1; }
        else d->on_ground = 0;
    } else {
        float climb = (gz - d->pos[2]) / h;
        d->pos[2] = gz;
        d->on_ground = 1;
        /* leave the ground over a crest: keep the vertical speed */
        d->vel[2] = climb > 0 ? climb : 0;
    }
    d->speed = d->vel[0] * fx + d->vel[1] * fy;
    d->spin += d->speed / d->wheel_radius * h;
    gearbox(d, in, h);

    /* lean with the ground under the four wheels */
    {
        float fxh = d->hub_x_front, rxh = d->hub_x_rear, hw = d->track / 2, zf, zr, zl, zrt, p, r, k;
        zf = ground_at(c, d->pos[0] + fx * fxh, d->pos[1] + fy * fxh, d->pos[2] + 1.5f, d->pos[2]);
        zr = ground_at(c, d->pos[0] + fx * rxh, d->pos[1] + fy * rxh, d->pos[2] + 1.5f, d->pos[2]);
        zl = ground_at(c, d->pos[0] + lx * hw, d->pos[1] + ly * hw, d->pos[2] + 1.5f, d->pos[2]);
        zrt = ground_at(c, d->pos[0] - lx * hw, d->pos[1] - ly * hw, d->pos[2] + 1.5f, d->pos[2]);
        p = atan2f(zf - zr, d->wheelbase);
        r = atan2f(zl - zrt, d->track);
        k = fminf(1, 12 * h);
        if (d->on_ground) {
            d->pitch += (p - d->pitch) * k;
            d->roll += (r - d->roll) * k;
        }
    }
}

void hp2_drive_step(hp2_drive *d, const hp2_coll *c, const hp2_drive_in *in, float dt)
{
    d->acc += dt;
    if (d->acc > 0.25f) d->acc = 0.25f;
    while (d->acc >= STEP) {
        substep(d, c, in);
        d->acc -= STEP;
    }
}
