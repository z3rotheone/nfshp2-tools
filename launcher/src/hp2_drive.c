/* hp2_drive - a simple stand-in driving model. See hp2_drive.h. */
#include "hp2_drive.h"

#include <math.h>
#include <string.h>

#define STEP    (1.0f / 240.0f)
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
    {
        /* torque split between the axles, as the game's table blends them */
        float a = rec->rear_share > 0 && rec->rear_share <= 1 ? rec->rear_share : 1;
        float ff = rec->final_front > 0 ? rec->final_front : rec->final_drive;
        float fr = rec->final_rear > 0 ? rec->final_rear : rec->final_drive;
        float rf = rec->wheel[HP2_WHEEL_FL].radius, rr = rec->wheel[HP2_WHEEL_RR].radius;
        d->final_drive = (1 - a) * ff + a * fr;
        if (d->final_drive <= 0) d->final_drive = 3.5f;
        d->wheel_radius = (1 - a) * rf + a * rr;
        d->rear_share = a;
        if (d->wheel_radius < 0.1f) d->wheel_radius = 0.32f;
    }
    d->mass = rec->mass > 0.3f ? rec->mass : 1.4f;
    d->drag = rec->drag > 0 ? rec->drag : 0.0004f;
    d->efficiency = rec->efficiency > 0.3f && rec->efficiency <= 1 ? rec->efficiency : 0.8f;
    for (k = 0; k < 9; k++) d->torque[k] = rec->torque[k] > 0 ? rec->torque[k] : 0.4f;
    d->idle_rpm = rec->idle_rpm > 300 ? rec->idle_rpm : 900;
    d->redline_rpm = rec->redline_rpm > d->idle_rpm + 1000 ? rec->redline_rpm : 7000;
    for (k = 0; k < 4; k++) {
        d->hub[k][0] = rec->wheel[k].x;
        d->hub[k][1] = rec->wheel[k].y;
    }
    for (k = 0; k < 8; k++) {
        d->tyre[0][k] = rec->tyre[0][k];
        d->tyre[1][k] = rec->tyre[1][k];
    }
    for (k = 0; k < 2; k++) {
        if (d->tyre[k][0] <= 0) d->tyre[k][0] = 12;
        if (d->tyre[k][1] <= 0) d->tyre[k][1] = 1;
        if (d->tyre[k][2] <= 0) d->tyre[k][2] = 1;
        if (d->tyre[k][4] <= 0) d->tyre[k][4] = 1.4f;
    }
    /* Tire::ComputeForce: brake 4 kN m x [0x304], handbrake 10 kN m x [0x308] */
    d->brake_torque = 4.0f * (rec->brakes[1] > 0 ? rec->brakes[1] : 1);
    d->handbrake_torque = 10.0f * (rec->brakes[2] > 0 ? rec->brakes[2] : 0.8f);
    d->yaw_inertia = rec->inertia[2] > 0.5f ? rec->inertia[2] : 3.0f;
    d->rpm = d->idle_rpm;
    d->gear = 1;
    d->on_ground = 1;
    d->hit = 1000;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

float hp2_drive_torque(const hp2_drive *d, float rpm)
{
    float x;
    int j;
    if (rpm > d->redline_rpm * 1.02f) return 0;
    if (rpm < d->idle_rpm) rpm = d->idle_rpm;
    x = (rpm - d->idle_rpm) / (d->redline_rpm - d->idle_rpm) * 8;
    if (x > 8) x = 8;
    j = (int)x;
    if (j > 7) j = 7;
    return d->torque[j] + (d->torque[j + 1] - d->torque[j]) * (x - j);
}

/* 60 / 2 pi, as the executable has it */
#define RPM_PER_RAD_S 9.549304f

/* The road surfaces' grip (+0x24) and rolling factor (+0x28) in the
 * executable's table at 0x318C00 (NONE, ROAD, DIRT, DIRTROAD, PUDDLE,
 * DEEP_WATER, GRASS, SAND, SNOW, DIRT_SHOULDER, SAND_SHOULDER,
 * SNOW_SHOULDER, SIDEWALK ...), taken to be indexed by the collision
 * surface's low nibble: the names agree with what the polygons are drawn as
 * for 1, 2, 3, 4, 6, 9 and C; 5 (bridge decks) and 7 (road shoulders) are
 * given the road's values rather than deep water's and sand's. */
static const float SURF_GRIP[16] = { 1, 1, 0.958f, 0.981f, 0.951f, 1, 0.948f, 1, 0.938f, 0.971f, 0.961f,
                                     0.958f, 1, 0.958f, 1, 1 };
static const float SURF_ROLL[16] = { 1, 1, 1, 1, 1.1f, 1, 1.05f, 1, 1, 1, 1.2f, 1, 1, 1, 1.01f, 1 };
static float surface_grip(int s) { return SURF_GRIP[s & 0xF]; }
static float surface_roll(int s) { return SURF_ROLL[s & 0xF]; }

/* The game's lateral force tables (0x310F60.., built into curves at
 * 0x1A57DC): force against load, one curve per 2 degrees of slip angle
 * from 0 to 12, each sampled at load 0, 2, 4, 6, 8, 10. */
static const float LATERAL[7][6] = {
    { 0, 0, 0, 0, 0, 0 },
    { 0, 1.2f, 2.3f, 3.0f, 3.0f, 2.8f },
    { 0, 1.7f, 3.2f, 4.3f, 5.1f, 5.2f },
    { 0, 1.8f, 3.5f, 4.9f, 5.8f, 6.1f },
    { 0, 1.83f, 3.6f, 5.0f, 5.96f, 6.4f },
    { 0, 1.86f, 3.7f, 5.1f, 6.13f, 6.7f },
    { 0, 1.9f, 3.8f, 5.2f, 6.3f, 7.1f },
};

static float curve6(const float *c, float x)          /* 0..10 in 6 points */
{
    float t;
    int j;
    if (x <= 0) return c[0];
    if (x >= 10) return c[5];
    t = x / 2;
    j = (int)t;
    return c[j] + (c[j + 1] - c[j]) * (t - j);
}

/* Tire's lateral force (0x1A4D28): the slip angle picks two curves (per
 * span/12 x 2 degrees), the load (x 0.8 / [1]) reads them, x 2.5 x [2]. */
static float lateral_force(const float *tb, float load, float angle)
{
    float deg = angle * 57.29578f, x = deg * (12.0f / tb[0]) * 0.5f, l = load * 0.8f / tb[1], a, b;
    int i = (int)x;
    if (i >= 6) return tb[2] * 2.5f * curve6(LATERAL[6], l);
    a = curve6(LATERAL[i], l);
    b = curve6(LATERAL[i + 1], l);
    return tb[2] * 2.5f * (a + (b - a) * (x - i));
}

float hp2_drive_accel(const hp2_drive *d, float v, int gear)
{
    float ratio = gear < 0 ? d->reverse : gear >= 1 && gear <= d->gears ? d->ratio[gear - 1] : 0;
    float rpm = fabsf(v) / d->wheel_radius * d->final_drive * ratio * RPM_PER_RAD_S;
    return (d->efficiency * hp2_drive_torque(d, rpm) * ratio * d->final_drive / d->wheel_radius - d->drag * v * v) / d->mass;
}

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
        if (up > 0.95f * d->redline_rpm && d->gear < d->gears) {     /* 0.95: the executable's table */
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

    if (d->on_ground && d->arcade) {
        /* the first stand-in: grip that never runs out, slip that dies away */
        /* engine (the car's torque through its gearing), brakes, reverse;
         * the drive is held to what the tyres could take (a guess: 1.1 g)
         * and cut at the car's listed top speed */
        if (in->throttle > 0 && vf > -0.5f) {
            int g = d->gear >= 1 ? d->gear : 1;
            float ratio = d->ratio[g - 1];
            float rpm = fabsf(vf) / d->wheel_radius * d->final_drive * ratio * RPM_PER_RAD_S;
            float drive = d->efficiency * hp2_drive_torque(d, rpm > d->rpm ? rpm : d->rpm) * ratio * d->final_drive
                          / d->wheel_radius / d->mass;
            if (drive > 1.1f * G) drive = 1.1f * G;
            if (vf > d->max_speed) drive = 0;
            a += drive * in->throttle * (d->shift > 0 ? 0.0f : 1.0f);
        }
        if (in->brake > 0) {
            if (vf > 0.5f) a -= 14.0f * in->brake;
            else if (vf > -12) a -= 5.0f * in->brake;
        }
        if (in->throttle > 0 && vf < -0.5f) a += 14.0f * in->throttle;   /* braking while backing up */
        if (in->handbrake) a -= (vf > 0 ? 6.0f : vf < 0 ? -6.0f : 0);
        /* rolling resistance (made up) and the car's air drag */
        a -= (vf > 0 ? 1 : vf < 0 ? -1 : 0) * (0.15f + d->drag * vf * vf / d->mass);
        if (fabsf(vf) < 0.3f && in->throttle <= 0 && in->brake <= 0) { vf = 0; a = 0; }
        vf += a * h;

        /* tyres: sideways slip dies away; the handbrake lets the back go */
        grip = in->handbrake ? 1.5f : 9.0f;
        if ((d->surface & 0xF) == 2 || (d->surface & 0xF) == 3 || (d->surface & 0xF) == 6) grip *= 0.7f;
        vl *= expf(-grip * h);
        yaw_target = -vf * tanf(d->steer_angle) / d->wheelbase;
        if (in->handbrake) yaw_target *= 1.6f;
        d->yawrate += (yaw_target - d->yawrate) * fminf(1, 10 * h);
    } else if (d->on_ground) {
        /* the drive force at the wheels: the car's torque through its gearing,
         * cut at the listed top speed and while changing gear */
        float drive = 0, fsum[2] = { 0, 0 }, mz = 0, share = 1, g_load = G * d->mass, hcg = 0.45f;
        float rear_x = -d->hub_x_rear, front_x = d->hub_x_front;
        int k;
        if (in->throttle > 0 && vf > -0.5f && d->shift <= 0 && vf < d->max_speed) {
            int g = d->gear >= 1 ? d->gear : 1;
            float ratio = d->ratio[g - 1];
            float rpm = fabsf(vf) / d->wheel_radius * d->final_drive * ratio * RPM_PER_RAD_S;
            drive = d->efficiency * hp2_drive_torque(d, rpm > d->rpm ? rpm : d->rpm) * ratio * d->final_drive
                    / d->wheel_radius * in->throttle;
        }
        if (in->brake > 0 && vf < 0.5f && vf > -12) drive = -0.5f * d->mass * G * in->brake;   /* reverse */
        if (in->throttle > 0 && vf < -0.5f) drive = 0;
        share = d->final_drive > 0 ? 1 : 1;
        for (k = 0; k < 4; k++) {
            int front = k == HP2_WHEEL_FL || k == HP2_WHEEL_FR, left = d->hub[k][1] > 0;
            const float *tb = d->tyre[front ? 0 : 1];
            float x = d->hub[k][0], y = d->hub[k][1];
            /* load, kN: the static share by the hubs, moved by the last
             * step's acceleration about a guessed centre of gravity */
            float n = g_load * (front ? rear_x : front_x) / d->wheelbase * 0.5f
                      + (front ? -1 : 1) * d->mass * d->accel[0] * hcg / d->wheelbase * 0.5f
                      + (left ? -1 : 1) * d->mass * d->accel[1] * hcg / d->track * 0.5f;
            float mu = tb[4] * surface_grip(d->surface), cmax, wa, cx, cy, wx, wy, fx, fy, bx, by, m_i;
            float torque_share = front ? (1 - d->rear_share) : d->rear_share;
            if (n < 0) n = 0;
            cmax = mu * n;
            m_i = n / G;                        /* the mass this wheel carries, t */
            /* the contact's velocity, then turned into the wheel's frame */
            cx = vf - d->yawrate * y;
            cy = vl + d->yawrate * x;
            wa = front ? -d->steer_angle : 0;
            wx = cx * cosf(wa) + cy * sinf(wa);
            wy = -cx * sinf(wa) + cy * cosf(wa);
            /* the force along the wheel: drive, brakes, handbrake (rear) */
            fx = drive * torque_share * 0.5f;
            if (in->brake > 0 && vf > 0.5f) fx -= d->brake_torque * in->brake / d->wheel_radius * (wx > 0 ? 1 : -1);
            if (in->throttle > 0 && vf < -0.5f) fx += d->brake_torque * in->throttle / d->wheel_radius;
            if (in->handbrake && !front) {
                /* a locked wheel slides against its own motion */
                float sp = sqrtf(wx * wx + wy * wy);
                if (sp > 0.05f) { fx = -cmax * wx / sp; fy = -cmax * wy / sp; }
                else { fx = 0; fy = 0; }
                d->sliding[k] = 1;
            } else if (fabsf(wy) > 0.5f) {
                /* sliding sideways: the sliding friction, shared with the drive */
                fy = -cmax * (wy > 0 ? 1 : -1);
                d->sliding[k] = 1;
            } else {
                float ang = atan2f(fabsf(wy), fmaxf(fabsf(wx), 2.0f));
                fy = -(wy > 0 ? 1 : -1) * lateral_force(tb, n, ang);
                d->sliding[k] = 0;
            }
            /* each direction held to the sliding friction on its own (a
             * friction circle that made the drive eat the rear's sideways
             * grip spun the lighter cars; the game's wheels spin up before
             * they share, which this model does not have) */
            if (fx > cmax) fx = cmax;
            if (fx < -cmax) fx = -cmax;
            if (fy > cmax) fy = cmax;
            if (fy < -cmax) fy = -cmax;
            /* never more sideways force than stops the slip this step */
            if (m_i > 0 && fabsf(fy) * h > m_i * fabsf(wy)) fy = -wy * m_i / h;
            /* back into the body's frame */
            bx = fx * cosf(wa) - fy * sinf(wa);
            by = fx * sinf(wa) + fy * cosf(wa);
            fsum[0] += bx;
            fsum[1] += by;
            mz += x * by - y * bx;
        }
        /* air drag and rolling resistance (the surface's factor at +0x28) */
        fsum[0] -= (vf > 0 ? 1 : vf < 0 ? -1 : 0) * (d->drag * vf * vf + 0.015f * g_load * surface_roll(d->surface));
        a = fsum[0] / d->mass;
        d->accel[0] = a;
        d->accel[1] = fsum[1] / d->mass;
        vf += (a + d->yawrate * vl) * h;
        vl += (fsum[1] / d->mass - d->yawrate * vf) * h;
        d->yawrate += mz / d->yaw_inertia * h;
        if (fabsf(vf) < 0.2f && fabsf(vl) < 0.2f && in->throttle <= 0 && in->brake <= 0) {
            vf = vl = 0;
            d->yawrate *= 0.9f;
        }
        (void)share; (void)grip; (void)yaw_target;
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
