/* hp2track - drive a car around a track, or fly around its scenery.
 *
 * The first steps towards the race: loads a track's scenery and textures
 * and a car from your own disc (or from extracted files) and draws them
 * with OpenGL 1.x, so any PC from the last twenty years can run it. The
 * car is driven by a simple stand-in model (hp2_drive), not the game's
 * handling, on the track's collision mesh (hp2_coll).
 *
 *   hp2track --disc DIR --track 24 [--car 911TURBO] [--skin N]
 *   hp2track --scenery TRACKB24.BUN --tex TEX24LOCATION.BIN --tex TEX24TRACK.BIN
 *            [--globalb GLOBALB.BUN --cartex TEXTURES.BIN --geometry GEOMETRY.BIN --car NAME]
 *
 * DIR is the NFSHP2 folder of your disc copy (the one with ZDIR.BIN). Track
 * numbers are 11-16, 21-26, 31-36, 41-46, 61-66: the tens digit is the
 * location, the units the variant (n and n+3 are the same roads at another
 * time of day or weather).
 *
 * Driving: W / Up accelerates, S / Down brakes and then reverses, A D /
 * Left Right steer, Space is the handbrake. On a controller: RT
 * accelerates, LT brakes, the left stick steers, A (Cross) is the
 * handbrake. C switches between the car and the free camera.
 *
 * Free camera: WASD or the arrow keys move, Q/E (or Page Down/Up) go down
 * and up, hold the right mouse button (or press M to capture the mouse) to
 * look around, Shift is faster and Ctrl slower, the mouse wheel changes
 * the speed; on a controller the left stick moves, the right stick looks,
 * the triggers go down and up.
 *
 * Both: F fog on/off, R back to the start, P prints the camera, F12 saves a
 * screenshot, Esc quits.
 *
 * A race: 3, 2, 1, GO, then --laps laps (default 3; one run on a
 * point-to-point track) on the track's main route (--reverse: the other
 * way round, the course's "II"), against opponents (--opponents N, or
 * --opponent NAME[:SKILL[:SKIN]] each), with the game's race
 * display (rev counter, speed, gear, lap, lap time, best lap, wrong way)
 * and the car's engine sound. R starts the race again.
 *
 * The tyres follow the game's (hp2_drive.h); --arcade brings back the first
 * stand-in's grip, which never runs out.
 *
 * --shot OUT.bmp renders one frame and exits: behind the car, or with
 * --cam X,Y,Z,YAW,PITCH (degrees) from there. --sim SECONDS[,STEER] drives
 * that long at full throttle first (steer -1..1), for testing.
 */
#include "hp2_ai.h"
#include "hp2_car.h"
#include "hp2_coll.h"
#include "hp2_drive.h"
#include "hp2_engine.h"
#include "hp2_ambient.h"
#include "hp2_tires.h"
#include "hp2_speech.h"
#include "hp2_oneshot.h"
#include "hp2_reverb.h"
#include "hp2_sfx.h"
#include "hp2_fe.h"
#include "hp2_font.h"
#include "hp2_hud.h"
#include "hp2_jbplay.h"
#include "hp2_settings.h"
#include "hp2_race.h"
#include "hp2_scene.h"
#include "hp2_tex.h"
#include "hp2_zdir.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_RGB_SCALE 0x8573
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_SOURCE0_ALPHA 0x8588
#define GL_SOURCE1_ALPHA 0x8589
#define GL_PRIMARY_COLOR 0x8577
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define PI 3.14159265358979f

enum { MODE_OPAQUE, MODE_CUTOUT, MODE_BLEND };

typedef struct {
    uint32_t hash;
    GLuint   id;
    int      mode;
    float    mid;           /* share of half-transparent texels */
    float    clear;         /* share of fully clear texels */
    float    clearlum;      /* their mean brightness (0 for a black shadow shape) */
    float    edge;          /* the largest share of opaque texels along one edge: a strip joined to its edge, not a sprite */
} gltex;

typedef struct {
    GLuint list;
    int    tex;             /* index into the texture table */
    int    flat;            /* nearly all its triangles lie flat: ground, not a foliage card */
} glgroup;

#define HP2_MAX_CARS 8

typedef struct {
    int      ok, skin;
    hp2_car  car;
    glgroup *carg, *tyreg, *brakeg[2];
    int      shadow_tex;
} carview;

typedef struct {
    hp2_scene  sc;
    gltex     *tex;
    int        ntex;
    glgroup  **grp;         /* per section, ng entries; sky after the sections */
    glgroup   *skyg;
    int        missing_tex;
    /* the cars: 0 is the player's, then the opponents */
    int        has_car;
    carview    cv[HP2_MAX_CARS];
    int        ncv;
    hp2_coll   coll;
    int        has_coll;
    int        arcade;          /* --arcade: the first stand-in grip */
    hp2_ai_line line;           /* the opponents' driving line */
} world;

/* Lap Knockout (--lap-knockout): see hp2_knockout. Out cars vanish. */
static hp2_knockout ko;
/* Pursuit (--pursuit): you are the cop; the others are speeders to arrest. */
static hp2_pursuit pur;
static hp2_speech speech;      /* the police radio of a Pursuit event */
static int has_speech, pursuit_called;
static hp2_oneshot cues;       /* crashes, horn, countdown beeps, finish line (the port's own triggers) */
static int has_cues;
static hp2_reverb verb;        /* the track's echo regions: caves, tunnels, mines, hangars, overpasses */
static int has_verb;
static void cue(const char *name, float gain) { if (has_cues) hp2_oneshot_play(&cues, name, gain); }

typedef struct {
    float pos[3], yaw, pitch;   /* degrees; yaw 0 looks along +x, z is up */
} camera;

/* ---- loading ------------------------------------------------------------- */

typedef struct {
    uint8_t *scen;
    size_t   scen_len;
    uint8_t *tex[4];
    size_t   tex_len[4];
    int      ntex;
    uint8_t *globalb, *cartex, *geom, *ingame, *hudtex, *msh, *msb;
    size_t   globalb_len, cartex_len, geom_len, ingame_len, hudtex_len, msh_len, msb_len;
} files;

static uint8_t *maybe_decompress(uint8_t *b, size_t *len)
{
    if (b && *len >= 4 && !memcmp(b, "COMP", 4)) {
        size_t n;
        uint8_t *d = hp2_comp_decode(b, *len, &n);
        free(b);
        *len = n;
        return d;
    }
    return b;
}

static int load_disc(files *f, hp2_zdir *zp, const char *dir, int track)
{
    hp2_zdir z;
    char path[64];
    if (hp2_zdir_open(&z, dir) != 0) {
        fprintf(stderr, "%s: no ZDIR.BIN / ZZDATA.BIN there (give the disc's NFSHP2 folder)\n", dir);
        return -1;
    }
    snprintf(path, sizeof path, "TRACKS\\TRACKB%02d.BUN", track);
    f->scen = hp2_zdir_load(&z, path, &f->scen_len);
    if (!f->scen) {
        snprintf(path, sizeof path, "TRACKS\\TRACKB%02d.LZC", track);
        f->scen = maybe_decompress(hp2_zdir_load(&z, path, &f->scen_len), &f->scen_len);
    }
    if (!f->scen) {
        fprintf(stderr, "no track %d on the disc (TRACKS\\TRACKB%02d.BUN or .LZC)\n", track, track);
        hp2_zdir_close(&z);
        return -1;
    }
    snprintf(path, sizeof path, "TRACKS\\TEX%02dLOCATION.BIN", track);
    f->tex[f->ntex] = hp2_zdir_load(&z, path, &f->tex_len[f->ntex]);
    if (f->tex[f->ntex]) f->ntex++;
    snprintf(path, sizeof path, "TRACKS\\TEX%02dTRACK.BIN", track);
    f->tex[f->ntex] = hp2_zdir_load(&z, path, &f->tex_len[f->ntex]);
    if (f->tex[f->ntex]) f->ntex++;
    if (f->ntex < 2) fprintf(stderr, "warning: track %d's texture packs not all found\n", track);
    f->globalb = hp2_zdir_load(&z, "GLOBAL\\GLOBALB.BUN", &f->globalb_len);
    if (!f->globalb) f->globalb = maybe_decompress(hp2_zdir_load(&z, "GLOBAL\\GLOBALB.LZC", &f->globalb_len), &f->globalb_len);
    f->cartex = hp2_zdir_load(&z, "CARS\\TEXTURES.BIN", &f->cartex_len);
    f->ingame = hp2_zdir_load(&z, "GLOBAL\\INGAMEB.BUN", &f->ingame_len);
    if (!f->ingame) f->ingame = maybe_decompress(hp2_zdir_load(&z, "GLOBAL\\INGAMEB.LZC", &f->ingame_len), &f->ingame_len);
    f->hudtex = hp2_zdir_load(&z, "GLOBAL\\HUDTEX.BIN", &f->hudtex_len);
    *zp = z;                            /* kept open: the car's model is chosen later */
    return 0;
}

/* The car's engine sound bank, SOUND\NEWE<NAME>.MSH + .MSB. */
static void load_engine(const hp2_zdir *z, const hp2_carrec *rec, files *f)
{
    char path[64];
    int i;
    if (!rec->engine[0]) return;
    snprintf(path, sizeof path, "SOUND\\%s.MSH", rec->engine);
    for (i = 0; path[i]; i++) path[i] = (char)toupper((unsigned char)path[i]);
    f->msh = hp2_zdir_load(z, path, &f->msh_len);
    memcpy(path + strlen(path) - 3, "MSB", 3);
    f->msb = hp2_zdir_load(z, path, &f->msb_len);
}

/* The car's model from the disc, once the record is known. */
static uint8_t *load_geometry(const hp2_zdir *z, const hp2_carrec *rec, size_t *len)
{
    uint8_t *b = hp2_zdir_load(z, rec->path, len);
    if (!b) b = maybe_decompress(hp2_zdir_load(z, rec->path_lzc, len), len);
    return b;
}

/* ---- textures ------------------------------------------------------------- */

static int find_or_add_tex(world *w, uint32_t h)
{
    int i;
    for (i = 0; i < w->ntex; i++)
        if (w->tex[i].hash == h) return i;
    if (w->ntex % 64 == 0) {
        gltex *n = (gltex *)realloc(w->tex, (size_t)(w->ntex + 64) * sizeof *n);
        if (!n) return -1;
        w->tex = n;
    }
    w->tex[w->ntex].hash = h;
    w->tex[w->ntex].id = 0;
    w->tex[w->ntex].mode = MODE_OPAQUE;
    return w->ntex++;
}

static void upload_textures(world *w, const hp2_texset *ts, int mipmaps)
{
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    int i;
    for (i = 0; i < w->ntex; i++) {
        gltex *g = &w->tex[i];
        const hp2_tex *t = g->hash ? hp2_tex_find(ts, g->hash) : NULL;
        uint8_t *px = NULL;
        int tw = 1, th = 1;
        glGenTextures(1, &g->id);
        glBindTexture(GL_TEXTURE_2D, g->id);
        if (t && t->bpp && (px = (uint8_t *)malloc((size_t)t->w * t->h * 4)) && hp2_tex_decode(t, px) == 0) {
            long k, n = (long)t->w * t->h, mid = 0, clear = 0, lum = 0;
            for (k = 0; k < n; k++) {
                uint8_t a = px[4 * k + 3];
                if (a < 16) { clear++; lum += (px[4 * k] + px[4 * k + 1] + px[4 * k + 2]) / 3; }
                else if (a < 200) mid++;      /* 232-254 (PS2 0x74-0x7F) is opaque with noise; the road and the sea sit there */
            }
            g->clear = (float)clear / (float)n;
            g->clearlum = clear ? (float)lum / (float)clear : 0;
            {
                long e0 = 0, e1 = 0, e2 = 0, e3 = 0;
                int ix, iy;
                for (ix = 0; ix < t->w; ix++) { e0 += px[4 * ix + 3] >= 16; e1 += px[4 * ((long)(t->h - 1) * t->w + ix) + 3] >= 16; }
                for (iy = 0; iy < t->h; iy++) { e2 += px[4 * (long)iy * t->w + 3] >= 16; e3 += px[4 * ((long)iy * t->w + t->w - 1) + 3] >= 16; }
                g->edge = fmaxf(fmaxf((float)e0 / t->w, (float)e1 / t->w), fmaxf((float)e2 / t->h, (float)e3 / t->h));
            }
            g->mode = mid > n / 20 ? MODE_BLEND : clear ? MODE_CUTOUT : MODE_OPAQUE;
            g->mid = (float)mid / (float)n;
            tw = t->w;
            th = t->h;
        } else {
            if (g->hash) w->missing_tex++;
            free(px);
            px = NULL;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (mipmaps) {
            glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px ? px : white);
        free(px);
    }
}

/* ---- geometry: one display list per (section, texture) --------------------- */

static GLuint compile_verts(const hp2_vert *vs, int nv)
{
    GLuint l = glGenLists(1);
    int i;
    glNewList(l, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < nv; i++) {
        const hp2_vert *v = &vs[i];
        glColor4ub(v->c[0], v->c[1], v->c[2], v->c[3]);
        glTexCoord2f(v->u, v->v);
        glVertex3f(v->x, v->y, v->z);
    }
    glEnd();
    glEndList();
    return l;
}

static GLuint compile(const hp2_sgroup *g) { return compile_verts(g->v, g->nv); }

static int build_world(world *w, int mipmaps, const hp2_texset *ts)
{
    int i, j;
    w->grp = (glgroup **)calloc((size_t)w->sc.nsec, sizeof *w->grp);
    w->skyg = (glgroup *)calloc((size_t)(w->sc.sky.ng ? w->sc.sky.ng : 1), sizeof *w->skyg);
    if (!w->grp || !w->skyg) return -1;
    for (i = 0; i <= w->sc.nsec; i++) {
        const hp2_ssection *s = i < w->sc.nsec ? &w->sc.sec[i] : &w->sc.sky;
        glgroup *out = i < w->sc.nsec ? (w->grp[i] = (glgroup *)calloc((size_t)(s->ng ? s->ng : 1), sizeof *out)) : w->skyg;
        if (!out) return -1;
        for (j = 0; j < s->ng; j++) {
            out[j].tex = find_or_add_tex(w, s->g[j].tex);
            if (out[j].tex < 0) return -1;
            out[j].list = compile(&s->g[j]);
            {   /* ground: the strips along a road are alpha-cut to dissolve into the verge, which
                 * is not there to show through, so flat pieces are drawn without the cut (below) */
                const hp2_vert *v = s->g[j].v;
                int t, nt = s->g[j].nv / 3, flat = 0;
                for (t = 0; v && t < nt; t++) {
                    const hp2_vert *a = &v[3 * t], *b = &v[3 * t + 1], *c = &v[3 * t + 2];
                    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
                    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (l > 1e-6f && fabsf(nz) / l > 0.85f) flat++;
                }
                out[j].flat = nt > 0 && flat * 10 >= nt * 9;
            }
        }
    }
    (void)mipmaps;
    (void)ts;
    return 0;
}

/* One list per batch of a car mesh, textures resolved as the game fills
 * the car's slots. */
static glgroup *build_mesh(world *w, const carview *cv, const hp2_mesh *m, const hp2_texset *ts, int skin)
{
    glgroup *g = (glgroup *)calloc((size_t)(m->nb ? m->nb : 1), sizeof *g);
    int i;
    if (!g) return NULL;
    for (i = 0; i < m->nb; i++) {
        g[i].tex = find_or_add_tex(w, hp2_car_texture(&cv->car, ts, m->b[i].tex, skin));
        if (g[i].tex < 0) { free(g); return NULL; }
        g[i].list = compile_verts(m->v + m->b[i].first, m->b[i].count);
    }
    return g;
}

static int build_car(world *w, carview *cv, const hp2_texset *ts)
{
    char name[64];
    cv->carg = build_mesh(w, cv, &cv->car.body, ts, cv->skin);
    cv->tyreg = build_mesh(w, cv, &cv->car.tyre, ts, cv->skin);
    cv->brakeg[0] = build_mesh(w, cv, &cv->car.brake[0], ts, cv->skin);
    cv->brakeg[1] = build_mesh(w, cv, &cv->car.brake[1], ts, cv->skin);
    snprintf(name, sizeof name, "%s_SHADOWIG", cv->car.rec.model);
    cv->shadow_tex = find_or_add_tex(w, hp2_geom_hash(name));
    return cv->carg && cv->tyreg && cv->brakeg[0] && cv->brakeg[1] && cv->shadow_tex >= 0 ? 0 : -1;
}

/* ---- drawing --------------------------------------------------------------- */

static void mat_mul(float *o, const float *a, const float *b)     /* column-major o = a * b */
{
    int r, c, k;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++) {
            float s = 0;
            for (k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = s;
        }
}

static void forward(const camera *c, float f[3])
{
    float y = c->yaw * PI / 180, p = c->pitch * PI / 180;
    f[0] = cosf(p) * cosf(y);
    f[1] = cosf(p) * sinf(y);
    f[2] = sinf(p);
}

/* The view matrix (z up), column-major. */
static void view_matrix(const camera *c, float *m)
{
    float f[3], s[3], u[3], l;
    forward(c, f);
    s[0] = f[1];                    /* s = f x (0, 0, 1) */
    s[1] = -f[0];
    s[2] = 0;
    l = sqrtf(s[0] * s[0] + s[1] * s[1]);
    if (l < 1e-5f) { s[0] = 0; s[1] = -1; l = 1; }
    s[0] /= l;
    s[1] /= l;
    u[0] = s[1] * f[2] - s[2] * f[1];
    u[1] = s[2] * f[0] - s[0] * f[2];
    u[2] = s[0] * f[1] - s[1] * f[0];
    memset(m, 0, 16 * sizeof *m);
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -(s[0] * c->pos[0] + s[1] * c->pos[1] + s[2] * c->pos[2]);
    m[13] = -(u[0] * c->pos[0] + u[1] * c->pos[1] + u[2] * c->pos[2]);
    m[14] = f[0] * c->pos[0] + f[1] * c->pos[1] + f[2] * c->pos[2];
    m[15] = 1;
}

/* 1 if the box may be in view of the clip matrix. */
static int box_visible(const float *m, const float *b)
{
    int p, k;
    for (p = 0; p < 6; p++) {
        int row = p / 2, sign = p % 2 ? -1 : 1, inside = 0;
        for (k = 0; k < 8 && !inside; k++) {
            float x = b[(k & 1) ? 3 : 0], y = b[(k & 2) ? 4 : 1], z = b[(k & 4) ? 5 : 2];
            float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
            float cr = m[row] * x + m[4 + row] * y + m[8 + row] * z + m[12 + row];
            if (cw + sign * cr >= 0) inside = 1;
        }
        if (!inside) return 0;
    }
    return 1;
}

static void set_mode(int mode)
{
    if (mode == MODE_BLEND) {
        glEnable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        if (mode == MODE_CUTOUT) glEnable(GL_ALPHA_TEST);
        else glDisable(GL_ALPHA_TEST);
    }
}

static uint32_t hide_tex;      /* --hide-tex HASH: skip a texture's pieces (finding what draws a stray surface) */
static int keep_cuts;      /* --keep-cuts: flat pieces keep their alpha cut too (the old look) */
static int holes;          /* --holes: magenta background, no sky, so missing geometry shows */
static int blend_opaque;   /* B: draw the blended pieces as solid (finding which ones the ground is made of) */

static void draw_groups(const world *w, const glgroup *g, int n, int pass)
{
    int j;
    for (j = 0; j < n; j++) {
        const gltex *t = &w->tex[g[j].tex];
        int mode = t->mode == MODE_BLEND && blend_opaque ? MODE_OPAQUE : t->mode;
        if (hide_tex && t->hash == hide_tex) continue;
        if (g[j].flat && !keep_cuts && t->clear > 0.02f && (t->clear < 0.5f || t->edge > 0.9f) && t->clearlum > 8.0f && t->mid < 0.5f) mode = MODE_OPAQUE;
        if ((mode == MODE_BLEND) != pass) continue;
        set_mode(mode);
        glBindTexture(GL_TEXTURE_2D, t->id);
        glCallList(g[j].list);
    }
}

/* The car at its place: body, then a tyre and a brake disc on each hub
 * (mirrored for the left side, the tyre scaled to the wheel's own size,
 * the front ones turned by the steering). */
static void draw_car(const world *w, const carview *cv, const hp2_drive *d, int pass)
{
    const hp2_car *c = &cv->car;
    int k;
    glPushMatrix();
    glTranslatef(d->pos[0], d->pos[1], d->pos[2] + d->ride);
    glRotatef(d->heading * 180 / PI, 0, 0, 1);
    glRotatef(-d->pitch * 180 / PI, 0, 1, 0);
    glRotatef(d->roll * 180 / PI, 1, 0, 0);
    /* the body leans on its springs; the wheels stay on the road */
    glPushMatrix();
    glTranslatef(0, 0, d->heave);
    glRotatef(-d->body_pitch * 180 / PI, 0, 1, 0);
    glRotatef(d->body_roll * 180 / PI, 1, 0, 0);
    draw_groups(w, cv->carg, c->body.nb, pass);
    glPopMatrix();
    for (k = 0; k < 4; k++) {
        const hp2_wheel *wh = &c->rec.wheel[k];
        int front = k == HP2_WHEEL_FL || k == HP2_WHEEL_FR;
        float sy = wh->y > 0 ? -1.0f : 1.0f;     /* the model's tyre is a right-hand one */
        float sr = c->tyre_radius > 0 ? wh->radius / c->tyre_radius : 1;
        float sw = c->tyre_width > 0 ? wh->width / c->tyre_width : 1;
        glPushMatrix();
        glTranslatef(wh->x, wh->y, wh->z);
        if (front) glRotatef(-d->steer_angle * 180 / PI, 0, 0, 1);
        glPushMatrix();
        glScalef(1, sy, 1);
        draw_groups(w, cv->brakeg[front ? 0 : 1], c->brake[front ? 0 : 1].nb, pass);
        glPopMatrix();
        glRotatef(fmodf(d->spin, 2 * PI) * 180 / PI, 0, 1, 0);
        glScalef(sr, sy * sw, sr);
        draw_groups(w, cv->tyreg, c->tyre.nb, pass);
        glPopMatrix();
    }
    glPopMatrix();
}

/* The car's shadow, a quad on the ground under it. */
static void draw_shadow(const world *w, const carview *cv, const hp2_drive *d)
{
    const gltex *t = &w->tex[cv->shadow_tex];
    float fx = cosf(d->heading), fy = sinf(d->heading), l = d->half_len + 0.2f, h = d->half_wid + 0.2f;
    float cx[4] = { l, l, -l, -l }, cy[4] = { h, -h, -h, h }, u[4] = { 0, 1, 1, 0 }, v[4] = { 0, 0, 1, 1 };
    int k;
    if (t->hash != 0 && !t->id) return;
    set_mode(MODE_BLEND);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2, -2);
    glBegin(GL_TRIANGLE_FAN);
    for (k = 0; k < 4; k++) {
        glColor4ub(128, 128, 128, 128);
        glTexCoord2f(u[k], v[k]);
        glVertex3f(d->pos[0] + fx * cx[k] - fy * cy[k], d->pos[1] + fy * cx[k] + fx * cy[k], d->pos[2] + 0.03f);
    }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
}

static int draw(world *w, const camera *c, int W, int H, int fog, const hp2_drive *const *cars)
{
    float view[16], proj[16], clip[16], n = 0.5f, fa = 30000.0f, aspect = (float)W / (H ? H : 1);
    float t = n * tanf(55.0f * PI / 360.0f), r = t * aspect;
    int i, j, pass, drawn = 0;
    (void)j;
    const hp2_scene *sc = &w->sc;
    float fogc[4] = { 0.55f, 0.6f, 0.75f, 1 };

    if (sc->has_fog) {
        for (i = 0; i < 3; i++) fogc[i] = sc->fog_rgba[i] / 255.0f;
    }
    glViewport(0, 0, W, H);
    if (holes) { fogc[0] = 1; fogc[1] = 0; fogc[2] = 1; }
    glClearColor(fogc[0], fogc[1], fogc[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    memset(proj, 0, sizeof proj);
    proj[0] = n / r;
    proj[5] = n / t;
    proj[10] = -(fa + n) / (fa - n);
    proj[11] = -1;
    proj[14] = -2 * fa * n / (fa - n);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    view_matrix(c, view);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view);
    mat_mul(clip, proj, view);

    glEnable(GL_TEXTURE_2D);
    /* the PS2's modulate: texture x vertex colour x 2 (128 is 1.0) */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
    glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 2.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glAlphaFunc(GL_GREATER, 0.5f);
    glDisable(GL_CULL_FACE);        /* the strips' winding is not yet relied on */

    /* the sky: behind everything, never fogged */
    glDisable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    for (j = 0; j < sc->sky.ng && !holes; j++) {
        set_mode(w->tex[w->skyg[j].tex].mode == MODE_BLEND ? MODE_BLEND : MODE_OPAQUE);
        glDepthMask(GL_FALSE);
        glBindTexture(GL_TEXTURE_2D, w->tex[w->skyg[j].tex].id);
        glCallList(w->skyg[j].list);
    }
    if (fog) {
        float start = sc->has_fog ? sc->fog_start : 200, dens = sc->has_fog ? sc->fog_density : 0.3f;
        float end = start + 600.0f / (dens > 0.05f ? dens : 0.05f);
        if (end < start + 800) end = start + 800;
        if (end > 8000) end = 8000;
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fogc);
        glFogf(GL_FOG_START, start);
        glFogf(GL_FOG_END, end);
    }
    /* opaque and cut-out first, then the blended ones over them; the car
     * in between, so the scenery's blended pieces (shadows, glass) go over it */
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1 && cars) {
            int k;
            for (k = 0; k < w->ncv; k++) {
                const float b[6] = { cars[k]->pos[0] - 3, cars[k]->pos[1] - 3, cars[k]->pos[2] - 1,
                                     cars[k]->pos[0] + 3, cars[k]->pos[1] + 3, cars[k]->pos[2] + 3 };
                if (!w->cv[k].ok || (k > 0 && ko.out[k]) || !box_visible(clip, b)) continue;
                draw_shadow(w, &w->cv[k], cars[k]);
                draw_car(w, &w->cv[k], cars[k], 0);
                draw_car(w, &w->cv[k], cars[k], 1);
            }
        }
        for (i = 0; i < sc->nsec; i++) {
            if (!sc->sec[i].ng || !box_visible(clip, sc->sec[i].box)) continue;
            if (pass == 0) drawn++;
            draw_groups(w, w->grp[i], sc->sec[i].ng, pass);
        }
    }
    set_mode(MODE_OPAQUE);
    return drawn;
}

/* ---- main ------------------------------------------------------------------ */

static int save_shot(const char *path, int W, int H)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
    uint8_t *row;
    int y, ok;
    if (!s) return -1;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    row = (uint8_t *)malloc((size_t)s->pitch);
    for (y = 0; row && y < H / 2; y++) {      /* GL reads bottom up */
        uint8_t *a = (uint8_t *)s->pixels + (size_t)y * s->pitch, *b = (uint8_t *)s->pixels + (size_t)(H - 1 - y) * s->pitch;
        memcpy(row, a, (size_t)s->pitch);
        memcpy(a, b, (size_t)s->pitch);
        memcpy(b, row, (size_t)s->pitch);
    }
    free(row);
    ok = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return ok;
}

static void start_camera(const hp2_scene *sc, camera *c)
{
    if (sc->has_start) {
        c->pos[0] = sc->start[0];
        c->pos[1] = sc->start[1];
        c->pos[2] = sc->start[2] + 3;
        c->yaw = atan2f(sc->ahead[1] - sc->start[1], sc->ahead[0] - sc->start[0]) * 180 / PI;
        c->pitch = -5;
    } else {
        c->pos[0] = (sc->box[0] + sc->box[3]) / 2;
        c->pos[1] = (sc->box[1] + sc->box[4]) / 2;
        c->pos[2] = sc->box[5] + 50;
        c->yaw = 0;
        c->pitch = -30;
    }
}

/* Behind and above the car, looking a little ahead of it. k = how far to
 * move towards that spot this frame (1 = all the way). */
static void chase_camera(const hp2_drive *d, camera *c, float k)
{
    float fx = cosf(d->heading), fy = sinf(d->heading), want[3], t[3], dx, dy, dz;
    int i;
    want[0] = d->pos[0] - fx * 7.0f;
    want[1] = d->pos[1] - fy * 7.0f;
    want[2] = d->pos[2] + 2.4f;
    for (i = 0; i < 3; i++) c->pos[i] += (want[i] - c->pos[i]) * k;
    t[0] = d->pos[0] + fx * 3.0f;
    t[1] = d->pos[1] + fy * 3.0f;
    t[2] = d->pos[2] + 1.0f;
    dx = t[0] - c->pos[0];
    dy = t[1] - c->pos[1];
    dz = t[2] - c->pos[2];
    c->yaw = atan2f(dy, dx) * 180 / PI;
    c->pitch = atan2f(dz, sqrtf(dx * dx + dy * dy)) * 180 / PI;
}

/* The race display: the third-person rev counter (its face by the car's
 * redline), speed and gear, the lap panel, wrong way, the countdown and the
 * result. Positions, fonts and colours are the game's race screen's. */
typedef struct {
    hp2_hud              hud;
    const hp2_fe_screen *race, *pre;
} hudstate;

static const char *ordinal(int n)
{
    if (n % 100 >= 11 && n % 100 <= 13) return "th";
    return n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
}

static int current_place(const hp2_race *races, int n);
/* The player's place among the n cars (1-based). */
static int player_place(const hp2_race *races, int n)
{
    const hp2_race *p[HP2_MAX_CARS];
    int order[HP2_MAX_CARS], i;
    for (i = 0; i < n; i++) p[i] = &races[i];
    hp2_race_order(p, n, order);
    for (i = 0; i < n; i++)
        if (order[i] == 0) return i + 1;
    return 1;
}

static int current_place(const hp2_race *races, int n)
{
    if (ko.on) {
        int order[HP2_MAX_CARS], i;
        hp2_knockout_order(&ko, races, n, order);
        for (i = 0; i < n; i++) if (order[i] == 0) return i + 1;
    }
    return player_place(races, n);
}

static void draw_hud(hudstate *hs, const hp2_drive *d, const hp2_race *races, int ncars, int W, int H)
{
    const hp2_race *r = &races[0];
    int place = ncars > 1 ? current_place(races, ncars) : 0;
    static const float pivot[2] = { 12, 56 };   /* the needle's hub in its texture */
    hp2_hud *h = &hs->hud;
    const hp2_fe_widget *x;
    char buf[48], t1[24], t2[24];
    float mph = fabsf(d->speed) / 0.44704f;
    int face = (int)ceilf(d->redline_rpm / 1000.0f);
    if (!hs->race) return;
    if (face < 6) face = 6;
    if (face > 10) face = 10;
    hp2_hud_begin(h, W, H);
    /* rev counter: 0 sits 130 degrees left of straight up, the face's top
     * figure 30 degrees right of it (measured on 3RDPERSON_7000 and _10000) */
    if ((x = hp2_hud_widget(hs->race, "SPEEDOMETER"))) {
        snprintf(buf, sizeof buf, "3RDPERSON_%d000", face);
        hp2_hud_image(h, x, hp2_fe_hash(buf), 0, NULL, 1);
    }
    if ((x = hp2_hud_widget(hs->race, "TACH_NEEDLE"))) {
        /* the needle hangs down from its hub in the texture: 180 turns it up */
        float a = 180.0f - 130.0f + 160.0f * d->rpm / (face * 1000.0f);
        hp2_hud_image(h, x, 0, a, pivot, 1);
    }
    if ((x = hp2_hud_widget(hs->race, "SPEED_STRING3"))) {
        /* the digits end at the right of a three-figure field, so 53 sits against the gear, not at the box's left edge */
        hp2_fe_widget sp = *x;
        const hp2_font *sf = hp2_font_find(h->fonts, x->font);
        snprintf(buf, sizeof buf, "%d", (int)(mph + 0.5f));
        if (sf) {
            if (mph >= 99.5f) { sp.sx *= 0.8f; sp.sy *= 0.8f; }      /* three figures must stay inside the box */
            sp.x = x->x + hp2_text_width(sf, "000") * x->sx + 6.0f;
            sp.align = HP2_FE_ALIGN_RIGHT;
        }
        hp2_hud_text(h, &sp, buf);
    }
    if ((x = hp2_hud_widget(hs->race, "GEAR_STRING3"))) {
        snprintf(buf, sizeof buf, "%s", d->gear < 0 ? "R" : d->gear == 0 ? "N" : "");
        if (d->gear > 0) snprintf(buf, sizeof buf, "%d", d->gear);
        hp2_hud_text(h, x, buf);
    }
    /* the lap panel: Lap n/N, this lap's time, the best lap */
    if (r->n) {
        int i, lap = r->lap > r->laps ? r->laps : r->lap;
        static const char *bg[3] = { "TIME_TRIAL_BG_1", "TIME_TRIAL_BG_2", "TIME_TRIAL_BG_3" };
        for (i = 0; i < 3; i++)
            if ((x = hp2_hud_widget(hs->race, bg[i]))) hp2_hud_image(h, x, 0, 0, NULL, 1);
        if ((x = hp2_hud_widget(hs->race, "TIME_TITLE_1"))) hp2_hud_text(h, x, "Lap");
        if ((x = hp2_hud_widget(hs->race, "TIME_TITLE_2"))) hp2_hud_text(h, x, "Time");
        if ((x = hp2_hud_widget(hs->race, "TIME_TITLE_3"))) hp2_hud_text(h, x, "Best");
        snprintf(buf, sizeof buf, "%d/%d", lap, r->laps);
        if ((x = hp2_hud_widget(hs->race, "LAP_COUNTER"))) hp2_hud_text(h, x, buf);
        hp2_race_time(r->state == HP2_RACE_FINISHED ? r->lap_time[r->laps - 1] : r->clock - r->lap_start, t1, sizeof t1);
        if (pur.on) hp2_race_time(pur.done ? pur.time : r->clock, t1, sizeof t1);      /* the whole chase, not a lap */
        if ((x = hp2_hud_widget(hs->race, "BEST_LAP_TIME"))) hp2_hud_text(h, x, t1);
        if (r->best > 0) hp2_race_time(r->best, t2, sizeof t2);
        else snprintf(t2, sizeof t2, "-:--.--");
        if ((x = hp2_hud_widget(hs->race, "LAP_TIME"))) hp2_hud_text(h, x, t2);
        /* the place, top left: big figure, its "st"/"nd", "/n" */
        if (pur.on) {                  /* arrests instead of a place */
            snprintf(buf, sizeof buf, "%d", pur.narrested);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_POSITION"))) hp2_hud_text(h, x, buf);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_EXTENSION"))) hp2_hud_text(h, x, "");
            snprintf(buf, sizeof buf, "/%d", ncars - 1);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_TOTAL"))) hp2_hud_text(h, x, buf);
        } else if (place) {
            snprintf(buf, sizeof buf, "%d", place);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_POSITION"))) hp2_hud_text(h, x, buf);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_EXTENSION"))) hp2_hud_text(h, x, ordinal(place));
            snprintf(buf, sizeof buf, "/%d", ncars);
            if ((x = hp2_hud_widget(hs->race, "BIGRACE_TOTAL"))) hp2_hud_text(h, x, buf);
        }
        if (r->wrong_way && r->state == HP2_RACE_RUNNING && (x = hp2_hud_widget(hs->race, "WRONG_WAY_IMAGE")))
            hp2_hud_image(h, x, 0, 0, NULL, 1);
        if (r->state == HP2_RACE_COUNTDOWN && hs->pre) {
            const char *n = r->countdown > 2 ? "PRERACE_3" : r->countdown > 1 ? "PRERACE_2" : "PRERACE_1";
            if ((x = hp2_hud_widget(hs->pre, n))) hp2_hud_image(h, x, 0, 0, NULL, 1);
        } else if (r->state == HP2_RACE_RUNNING && r->clock < 1 && hs->pre && (x = hp2_hud_widget(hs->pre, "PRERACE_GO"))) {
            hp2_fe_widget go = *x;
            go.x = 288;                 /* the layout keeps GO below the screen, for the code to bring in */
            go.y = 183;
            hp2_hud_image(h, &go, 0, 0, NULL, 1 - r->clock);
        }
        if (pur.on && pur.done) {
            hp2_race_time(pur.time, t1, sizeof t1);
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_1"))) hp2_hud_text(h, x, "All speeders arrested");
            snprintf(buf, sizeof buf, "Time %s", t1);
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_2"))) hp2_hud_text(h, x, buf);
        } else if (ko.on && ko.done) {
            snprintf(buf, sizeof buf, "You finished %d%s", place, ordinal(place));
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_1"))) hp2_hud_text(h, x, buf);
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_2"))) hp2_hud_text(h, x, ko.out[0] ? "You were knocked out!" : "Last car standing");
        } else if (r->state == HP2_RACE_FINISHED) {
            float total = 0;
            for (i = 0; i < r->laps; i++) total += r->lap_time[i];
            if (place) snprintf(buf, sizeof buf, "You finished %d%s", place, ordinal(place));
            else snprintf(buf, sizeof buf, "Race Over");
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_1"))) hp2_hud_text(h, x, buf);
            hp2_race_time(total, t1, sizeof t1);
            snprintf(buf, sizeof buf, "Time %s", t1);
            if ((x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_2"))) hp2_hud_text(h, x, buf);
            hp2_race_time(r->best, t2, sizeof t2);
            snprintf(buf, sizeof buf, "Best Lap %s", t2);
            if (r->laps > 1 && (x = hp2_hud_widget(hs->race, "RACE_OVER_LINE_3"))) hp2_hud_text(h, x, buf);
        }
    }
    hp2_hud_end();
}

/* A car on the starting grid: two abreast, 8 m between rows; slot 0 is
 * the front left. Without opponents the player starts on the line. */
static void start_car(const world *w, int k, hp2_drive *d, const hp2_race *route)
{
    const hp2_scene *sc = &w->sc;
    float x = sc->start[0], y = sc->start[1], z = sc->start[2], hd = 0;
    if (sc->has_start) hd = atan2f(sc->ahead[1] - sc->start[1], sc->ahead[0] - sc->start[0]);
    if (w->ncv > 1 && route && route->n) {
        /* the player at the back of the grid, the opponents ahead */
        int slot = k == 0 ? w->ncv - 1 : k - 1, row = slot / 2, rows = (w->ncv + 1) / 2;
        float p = (route->loop ? 0 : 6) + 8.0f * (rows - 1 - row) - (route->loop ? 4 : 0), xy[2], dir[2];
        float side = (slot % 2 ? -1.8f : 1.8f);
        hp2_race_point(route, p, xy, dir);
        x = xy[0] - dir[1] * side;
        y = xy[1] + dir[0] * side;
        hd = atan2f(dir[1], dir[0]);
    }
    if (w->has_coll) hp2_coll_ground(&w->coll, x, y, z + 5, 5, &z, NULL, NULL);
    hp2_drive_init(d, &w->cv[k].car.rec, x, y, z, hd);
    d->arcade = w->arcade;
}

static float axis(SDL_GameController *p, SDL_GameControllerAxis a)
{
    float v = p ? SDL_GameControllerGetAxis(p, a) / 32767.0f : 0;
    if (v > -0.15f && v < 0.15f) return 0;
    return v;
}

/* One step of the race for every car: the player's pedals (in), the
 * opponents' from their drivers; everyone held at the line during the
 * countdown (engines revving); then the bumps between cars and each car's
 * laps. */
static void race_tick(const world *w, hp2_drive *drv, hp2_race *races, hp2_ai *ai, const hp2_drive_in *player, float dt)
{
    int i, j;
    static float prev_speed, hit_wait, prev_cd = 99, siren_wait, splash_wait;
    static int prev_surf;
    static int prev_state = HP2_RACE_COUNTDOWN;
    if (has_cues) {
        hit_wait -= dt;
        if (races[0].state == HP2_RACE_COUNTDOWN) {          /* a beep each second, a high one at GO */
            if ((int)ceilf(races[0].countdown) != (int)ceilf(prev_cd) && races[0].countdown > 0) cue("CountDownLow", 0.8f);
            prev_cd = races[0].countdown;
        } else if (prev_state == HP2_RACE_COUNTDOWN) cue("CountDownHigh", 0.8f);
        if (races[0].state == HP2_RACE_FINISHED && prev_state != HP2_RACE_FINISHED) cue("FinishLine", 1.0f);
        prev_state = races[0].state;
    }
    if (has_cues) {
        /* a puddle (surface 4) splashes as you hit it at speed */
        int sf = drv[0].surface & 15;
        splash_wait -= dt;
        if (sf == 4 && prev_surf != 4 && drv[0].on_ground && fabsf(drv[0].speed) > 8.0f && splash_wait <= 0) {
            char nm[8];
            snprintf(nm, sizeof nm, "Splash%d", 1 + (int)(fabsf(drv[0].speed) * 3) % 3);
            cue(nm, 0.9f);
            splash_wait = 0.5f;
        }
        prev_surf = sf;
        /* your siren in a Pursuit event: the long wail, repeated */
        siren_wait -= dt;
        if (pur.on && races[0].state == HP2_RACE_RUNNING && !pur.done && siren_wait <= 0) { cue("CopSiren01", 0.5f); siren_wait = 5.3f; }
    }
    for (i = 0; i < w->ncv; i++) {
        hp2_drive_in in;
        if (i > 0 && ko.out[i]) continue;                  /* knocked out: gone */
        if (i == 0) in = *player;
        else hp2_ai_drive(&ai[i], &races[i], &w->line, &drv[i], dt, &in);
        if (i == 0 && ko.out[0]) {                          /* you are out: coast to a stop */
            memset(&in, 0, sizeof in);
            in.brake = drv[0].speed > 1 ? 0.4f : 0;
        }
        if ((races[i].state == HP2_RACE_FINISHED || pur.arrested[i]) && i > 0) {
            memset(&in, 0, sizeof in);              /* opponents coast to a stop after the line */
            in.brake = drv[i].speed > 1 ? 0.4f : 0;
        }
        if (races[0].state == HP2_RACE_COUNTDOWN) {
            /* held at the line: the engine revs with the throttle */
            float want = drv[i].idle_rpm + in.throttle * (0.7f * drv[i].redline_rpm - drv[i].idle_rpm);
            drv[i].rpm += (want - drv[i].rpm) * fminf(1, 8 * dt);
            drv[i].load += (in.throttle - drv[i].load) * fminf(1, 10 * dt);
        } else {
            hp2_drive_step(&drv[i], &w->coll, &in, dt);
        }
    }
    if (has_cues && drv[0].hit == 0 && hit_wait <= 0 && fabsf(prev_speed) > 3.0f) {   /* a wall: by how fast you were going */
        float v = fabsf(prev_speed);
        char nm[24];
        snprintf(nm, sizeof nm, "CrashMetal%s0%d", v < 10 ? "Small" : v < 25 ? "Medium" : "Hard", 1 + (int)(v * 7) % 3);
        cue(nm, 1.0f);
        hit_wait = 0.4f;
    }
    prev_speed = drv[0].speed;
    for (i = 0; i < w->ncv; i++)
        for (j = i + 1; j < w->ncv; j++)
            if (!ko.out[i] && !ko.out[j] && hp2_drive_collide(&drv[i], &drv[j]) && i == 0 && hit_wait <= 0) {
                cue("CrashMetalMedium02", 0.9f);
                if (has_speech && pur.on) hp2_speech_say(&speech, "CopHit", 0);
                hit_wait = 0.4f;
            }
    for (i = 0; i < w->ncv; i++) {
        if (i > 0 && ko.out[i]) continue;
        hp2_race_step(&races[i], dt, drv[i].pos[0], drv[i].pos[1], drv[i].vel[0], drv[i].vel[1]);
    }
    hp2_knockout_update(&ko, races, w->ncv);
    if (pur.on && races[0].state == HP2_RACE_RUNNING) {
        float px[HP2_MAX_CARS], py[HP2_MAX_CARS];
        for (i = 0; i < w->ncv; i++) { px[i] = drv[i].pos[0]; py[i] = drv[i].pos[1]; }
        {   int got = hp2_pursuit_step(&pur, dt, races[0].clock, w->ncv, px, py);
            /* the port's own radio triggers: the game's event rules are not decoded */
            if (has_speech && pursuit_called) {
                /* more of the port's own radio: a speeder crashing, getting away, coming back, and chatter */
                static float lost_t, chat_t, crash_t;
                static int lost;
                float near = 1e9f;
                int k;
                crash_t -= dt; chat_t -= dt;
                for (k = 1; k < w->ncv; k++) {
                    float d;
                    if (pur.arrested[k]) continue;
                    d = hypotf(px[k] - px[0], py[k] - py[0]);
                    if (d < near) near = d;
                    if (drv[k].hit == 0 && crash_t <= 0 && fabsf(drv[k].speed) > 8.0f) {
                        hp2_speech_say(&speech, "SusCrash", 0);
                        crash_t = 12.0f;
                    }
                }
                if (near > 150.0f) lost_t += dt; else lost_t = 0;
                if (!lost && lost_t > 5.0f) { lost = 1; hp2_speech_say(&speech, "SusLost", 0); }
                else if (lost && near < 80.0f) { lost = 0; hp2_speech_say(&speech, "SusFnd", 0); }
                if (chat_t <= 0) { hp2_speech_say(&speech, "chat", 0); chat_t = 25.0f + (float)(rand() % 20); }
            }
            if (has_speech) {
                if (!pursuit_called) { pursuit_called = 1; hp2_speech_say(&speech, "BPaGeneral", 0); }
                else if (got) hp2_speech_say(&speech, pur.done ? "TermPursuit" : "Arrest", 1);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *disc = NULL, *scen = NULL, *texf[4], *shot = NULL, *cam = NULL, *camroad = NULL, *carname = "911TURBO";
    const char *gbf = NULL, *ctf = NULL, *geof = NULL, *sim = NULL, *resultf = NULL;
    int ntexf = 0, track = 0, W = 1280, H = 720, i, quit = 0, fog = 1, grab = 0, shots = 0;
    int want_car = 1, driving = 0, skin = 1, have_zdir = 0, titled = 0, laps = 3, sound = 1;
    const char *igf = NULL, *htf = NULL, *mshf = NULL, *msbf = NULL;
    hp2_fontset fonts = { 0 };
    hp2_fe_layout layout;
    hudstate hs;

    hp2_engine eng;
    int has_eng = 0;
    SDL_AudioDeviceID audio = 0;
    hp2_zdir zdir;
    hp2_settings cfg;                 /* from --config: the race music's volume and song list */
    static hp2_jukebox jb;
    hp2_jbplay jbp;
    uint8_t *jbhead = NULL;
    size_t jbhead_len = 0;
    const char *config = NULL;
    int music = 1, has_music = 0, ambient = 1, has_amb = 0, tiresnd = 1, speechsnd = 1, cuesnd = 1, reverbfx = 1;
    hp2_ambient amb;
    hp2_sfx ambsfx, ingsfx;
    hp2_tires tires;
    int has_tires = 0;
    hp2_drive drv[HP2_MAX_CARS];
    const hp2_drive *dptr[HP2_MAX_CARS];
    hp2_race races[HP2_MAX_CARS];
    hp2_ai ai[HP2_MAX_CARS];
    const char *oppname[HP2_MAX_CARS], *oppfile[HP2_MAX_CARS];
    int oskill[HP2_MAX_CARS];
    int nopp = -1, nopp_named = 0, oppskill[HP2_MAX_CARS], oppskin[HP2_MAX_CARS], reverse = 0, scene_flags = 0;
    hp2_carrec recs[HP2_CARDB_MAX];
    files f;
    hp2_texset ts;
    world w;
    camera c;
    float speed = 40;
    SDL_Window *win;
    SDL_GLContext gl;
    SDL_GameController *pad = NULL;
    Uint32 last;

    memset(&f, 0, sizeof f);
    memset(&ts, 0, sizeof ts);
    memset(&w, 0, sizeof w);
    for (i = 0; i < HP2_MAX_CARS; i++) oskill[i] = -1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--track") && i + 1 < argc) track = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scenery") && i + 1 < argc) scen = argv[++i];
        else if (!strcmp(argv[i], "--tex") && i + 1 < argc && ntexf < 4) texf[ntexf++] = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--no-ambient")) ambient = 0;
        else if (!strcmp(argv[i], "--no-tires")) tiresnd = 0;
        else if (!strcmp(argv[i], "--no-speech")) speechsnd = 0;
        else if (!strcmp(argv[i], "--no-cues")) cuesnd = 0;
        else if (!strcmp(argv[i], "--no-echo")) reverbfx = 0;
        else if (!strcmp(argv[i], "--cam") && i + 1 < argc) cam = argv[++i];
        else if (!strcmp(argv[i], "--cam-road") && i + 1 < argc) camroad = argv[++i];
        else if (!strcmp(argv[i], "--no-fog")) fog = 0;
        else if (!strcmp(argv[i], "--car") && i + 1 < argc) carname = argv[++i];
        else if (!strcmp(argv[i], "--no-car")) want_car = 0;
        else if (!strcmp(argv[i], "--skin") && i + 1 < argc) skin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--globalb") && i + 1 < argc) gbf = argv[++i];
        else if (!strcmp(argv[i], "--cartex") && i + 1 < argc) ctf = argv[++i];
        else if (!strcmp(argv[i], "--geometry") && i + 1 < argc) geof = argv[++i];
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) sim = argv[++i];
        else if (!strcmp(argv[i], "--laps") && i + 1 < argc) laps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--result") && i + 1 < argc) resultf = argv[++i];
        else if (!strcmp(argv[i], "--lap-knockout")) ko.on = 1;
        else if (!strcmp(argv[i], "--pursuit")) pur.on = 1;
        else if (!strcmp(argv[i], "--no-sound")) sound = 0;
        else if (!strcmp(argv[i], "--no-music")) music = 0;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--arcade")) w.arcade = 1;
        else if (!strcmp(argv[i], "--reverse")) reverse = 1;
        else if (!strcmp(argv[i], "--blend-opaque")) blend_opaque = 1;
        else if (!strcmp(argv[i], "--holes")) holes = 1;
        else if (!strcmp(argv[i], "--keep-cuts")) keep_cuts = 1;
        else if (!strcmp(argv[i], "--hide-tex") && i + 1 < argc) hide_tex = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--no-scenery-shadows")) scene_flags = HP2_SCENE_NO_SHADOWS;
        else if (!strcmp(argv[i], "--opponents") && i + 1 < argc) nopp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--opponent") && i + 1 < argc && nopp_named < HP2_MAX_CARS - 1) {
            /* NAME[:SKILL[:SKIN]][=GEOMETRY.BIN]: skill 0-9 as the game's
             * event tables give it, GEOMETRY with extracted files */
            char *eq, *co;
            oppname[nopp_named] = argv[++i];
            oppfile[nopp_named] = NULL;
            oppskill[nopp_named] = oppskin[nopp_named] = -1;
            if ((eq = strchr(argv[i], '='))) { *eq = 0; oppfile[nopp_named] = eq + 1; }
            if ((co = strchr(argv[i], ':'))) {
                *co++ = 0;
                oppskill[nopp_named] = atoi(co);
                if ((co = strchr(co, ':'))) oppskin[nopp_named] = atoi(co + 1);
            }
            nopp_named++;
        }
        else if (!strcmp(argv[i], "--ingame") && i + 1 < argc) igf = argv[++i];
        else if (!strcmp(argv[i], "--hudtex") && i + 1 < argc) htf = argv[++i];
        else if (!strcmp(argv[i], "--msh") && i + 1 < argc) mshf = argv[++i];
        else if (!strcmp(argv[i], "--msb") && i + 1 < argc) msbf = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 64 || H < 48) { W = 1280; H = 720; } }
        else {
            fprintf(stderr, "usage: %s --disc DIR --track NN | --scenery FILE [--tex FILE]...\n"
                            "       [--car NAME] [--skin N] [--no-car]\n"
                            "       [--globalb FILE --cartex FILE --geometry FILE]   (the car, with --scenery)\n"
                            "       [--ingame INGAMEB.BUN --hudtex HUDTEX.BIN --msh F --msb F]   (display, engine)\n"
                            "       [--laps N] [--reverse] [--no-sound] [--arcade] [--opponents N]\n"
                            "       [--opponent NAME[:SKILL[:SKIN]][=GEOMETRY]]...\n"
                            "       [--size WxH] [--no-fog] [--shot OUT.bmp] [--cam X,Y,Z,YAW,PITCH] [--sim S[,STEER]]\n"
                            "tracks: 11-16 21-26 31-36 41-46 61-66\n", argv[0]);
            return 2;
        }
    }
    if (disc) {
        if (track < 11 || track > 66 || track % 10 < 1 || track % 10 > 6 || track / 10 == 5) {
            fprintf(stderr, "give --track: 11-16, 21-26, 31-36, 41-46 or 61-66\n");
            return 2;
        }
        if (load_disc(&f, &zdir, disc, track) != 0) return 1;
        have_zdir = 1;
    } else if (scen) {
        f.scen = maybe_decompress(hp2_read_file(scen, &f.scen_len), &f.scen_len);
        if (!f.scen) { fprintf(stderr, "cannot read %s\n", scen); return 1; }
        for (i = 0; i < ntexf; i++) {
            f.tex[f.ntex] = hp2_read_file(texf[i], &f.tex_len[f.ntex]);
            if (!f.tex[f.ntex]) { fprintf(stderr, "cannot read %s\n", texf[i]); return 1; }
            f.ntex++;
        }
        if (gbf) f.globalb = maybe_decompress(hp2_read_file(gbf, &f.globalb_len), &f.globalb_len);
        if (ctf) f.cartex = hp2_read_file(ctf, &f.cartex_len);
        if (geof) f.geom = maybe_decompress(hp2_read_file(geof, &f.geom_len), &f.geom_len);
        if (igf) f.ingame = maybe_decompress(hp2_read_file(igf, &f.ingame_len), &f.ingame_len);
        if (htf) f.hudtex = hp2_read_file(htf, &f.hudtex_len);
        if (mshf) f.msh = hp2_read_file(mshf, &f.msh_len);
        if (msbf) f.msb = hp2_read_file(msbf, &f.msb_len);
    } else {
        fprintf(stderr, "give --disc DIR --track NN (DIR = your disc's NFSHP2 folder), or --scenery FILE\n");
        return 2;
    }
    if (hp2_scene_build(&w.sc, f.scen, f.scen_len, scene_flags) != 0) { fprintf(stderr, "no track scenery in that file\n"); return 1; }
    if (reverse) hp2_scene_reverse_route(&w.sc);   /* the course's II: the other way round */
    for (i = 0; i < f.ntex; i++)
        if (hp2_tex_scan(&ts, f.tex[i], f.tex_len[i]) < 0) { fprintf(stderr, "out of memory\n"); return 1; }
    hp2_tex_scan(&ts, f.scen, f.scen_len);         /* any pack inside the scenery itself */
    printf("%d sections, %d models placed, %ld triangles, %d textures\n", w.sc.nsec, w.sc.placed, w.sc.tris, ts.count);
    w.has_coll = hp2_coll_parse(&w.coll, f.scen, f.scen_len) == 0;

    /* the car: its record, model and textures (the car pack, then GLOBALB's
     * GlobalTextures for the windows, plate and underside) */
    if (want_car && f.globalb && w.has_coll) {
        int n = hp2_cardb_parse(f.globalb, f.globalb_len, recs, HP2_CARDB_MAX);
        const hp2_carrec *rec = n > 0 ? hp2_cardb_find(recs, n, carname) : NULL;
        if (!rec) {
            fprintf(stderr, "no car '%s' in the car database; the cars are:\n", carname);
            for (i = 0; i < n; i++) fprintf(stderr, " %s", recs[i].name);
            fprintf(stderr, "\n");
            return 2;
        }
        if (!f.geom && have_zdir) f.geom = load_geometry(&zdir, rec, &f.geom_len);
        if (!f.msh && have_zdir) load_engine(&zdir, rec, &f);
        if (f.msh && f.msb && hp2_engine_load(&eng, f.msh, f.msh_len, f.msb, f.msb_len, rec->idle_rpm, rec->redline_rpm) == 0)
            has_eng = 1;
        if (f.geom && hp2_car_build(&w.cv[0].car, f.geom, f.geom_len, rec) == 0) {
            w.has_car = 1;
            w.cv[0].ok = 1;
            w.cv[0].skin = skin;
            w.ncv = 1;
            if (f.cartex) hp2_tex_scan(&ts, f.cartex, f.cartex_len);
            hp2_tex_scan(&ts, f.globalb, f.globalb_len);
            printf("car %s (%s): %d body triangles\n", rec->name, rec->model, w.cv[0].car.body.nv / 3);
        } else {
            fprintf(stderr, "could not load %s's model (%s)\n", rec->name, rec->path);
        }
        /* the opponents: named ones, or the first of a list of fast cars
         * that are not the player's (three with a disc, none without) */
        if (w.has_car) {
            static const char *field[] = { "F50", "MCLAREN", "DIABLO", "CARRERAGT", "VANQUISH", "MURCIELAGO", "CLK_GTR",
                                           "VIPERGTS", "911TURBO", NULL };
            int want = nopp >= 0 ? nopp : nopp_named ? nopp_named : have_zdir ? 3 : 0, j, fi = 0;
            if (want > HP2_MAX_CARS - 1) want = HP2_MAX_CARS - 1;
            for (j = 0; j < want; j++) {
                const char *name = j < nopp_named ? oppname[j] : NULL;
                const hp2_carrec *orec = NULL;
                uint8_t *g = NULL;
                size_t gl_len = 0;
                carview *ov = &w.cv[w.ncv];
                while (!name && field[fi]) {
                    if (strcmp(field[fi], rec->name)) name = field[fi];
                    fi++;
                }
                if (!name) break;
                orec = hp2_cardb_find(recs, n, name);
                if (!orec) { fprintf(stderr, "no car '%s' for an opponent\n", name); continue; }
                if (j < nopp_named && oppfile[j]) g = maybe_decompress(hp2_read_file(oppfile[j], &gl_len), &gl_len);
                else if (have_zdir) g = load_geometry(&zdir, orec, &gl_len);
                if (!g || hp2_car_build(&ov->car, g, gl_len, orec) != 0) {
                    fprintf(stderr, "could not load %s's model for an opponent\n", name);
                    free(g);
                    continue;
                }
                free(g);
                ov->ok = 1;
                ov->skin = 1 + (w.ncv * 2 + (!strcmp(orec->model, rec->model) ? 1 : 0)) % 5;
                if (!strcmp(orec->model, rec->model) && ov->skin == skin) ov->skin = skin % 5 + 1;
                if (j < nopp_named && oppskin[j] >= 1) ov->skin = oppskin[j];
                oskill[w.ncv] = j < nopp_named ? oppskill[j] : -1;
                printf("opponent %d: %s\n", w.ncv, orec->name);
                w.ncv++;
            }
        }
    } else if (want_car && !w.has_coll) {
        fprintf(stderr, "no collision mesh in the scenery: no car\n");
    }
    /* the disc stays open: the race's music, ambience and tyre sounds are read from it later */
    driving = w.has_car;
    /* the race display: the race screen of INGAMEB.BUN, its textures, the
     * gauge faces of HUDTEX.BIN and the fonts of GLOBALB and INGAMEB */
    memset(&layout, 0, sizeof layout);
    memset(&hs, 0, sizeof hs);
    if (f.ingame && hp2_fe_parse(f.ingame, f.ingame_len, &layout) == 0) {
        hs.race = hp2_fe_find_id(&layout, HP2_HUD_SCREEN);
        hs.pre = hp2_fe_find_id(&layout, HP2_PRERACE_SCREEN);
        hp2_tex_scan(&ts, f.ingame, f.ingame_len);
        hp2_font_scan(&fonts, f.ingame, f.ingame_len);
    }
    if (f.hudtex) hp2_tex_scan(&ts, f.hudtex, f.hudtex_len);
    if (f.globalb) hp2_font_scan(&fonts, f.globalb, f.globalb_len);
    hp2_hud_init(&hs.hud, &ts, &fonts);
    if (pur.on) laps = HP2_RACE_MAX_LAPS;      /* the speeders keep running until they are caught */
    for (i = 0; i < HP2_MAX_CARS; i++) {
        hp2_race_init(&races[i], w.sc.route, w.sc.route_n, w.sc.route_loop, w.sc.route_len, laps);
        memset(&ai[i], 0, sizeof ai[i]);
        ai[i].lane = (i % 2 ? 1.5f : -1.5f) * (1 + (i / 2) * 0.3f);
        /* skill 0-9 (the event tables'): 0.78 .. 0.96 of the tyres' grip */
        ai[i].skill = oskill[i] >= 0 ? 0.78f + 0.02f * (oskill[i] > 9 ? 9 : oskill[i]) : 0.95f - 0.03f * i;
        ai[i].at = -1;
        dptr[i] = &drv[i];
    }
    if (w.ncv > 1 && hp2_ai_line_build(&w.line, &races[0], w.has_coll ? &w.coll : NULL) == 0)
        printf("opponents' line: %.0f m\n", w.line.len);
    if (w.has_car)
        printf("race: %d lap%s of %.0f m (%s); %s\n", races[0].laps, races[0].laps > 1 ? "s" : "", races[0].len,
               races[0].loop ? "circuit" : "point to point", has_eng ? "engine sound loaded" : "no engine sound");

    if (SDL_Init(SDL_INIT_VIDEO | (shot ? 0 : SDL_INIT_GAMECONTROLLER)) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("Hot Pursuit 2 - track viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                           SDL_WINDOW_OPENGL | (shot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    gl = win ? SDL_GL_CreateContext(win) : NULL;
    if (!gl) { fprintf(stderr, "OpenGL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    {
        const char *ver = (const char *)glGetString(GL_VERSION);
        int major = 1, minor = 0;
        if (ver) sscanf(ver, "%d.%d", &major, &minor);
        if (build_world(&w, major > 1 || minor >= 4, &ts) != 0) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        for (i = 0; i < w.ncv; i++)
            if (build_car(&w, &w.cv[i], &ts) != 0) { fprintf(stderr, "out of memory\n"); return 1; }
        upload_textures(&w, &ts, major > 1 || minor >= 4);
        printf("OpenGL %s; %d textures used, %d not found\n", ver ? ver : "?", w.ntex, w.missing_tex);
    }
    hp2_scene_drop_vertices(&w.sc);   /* the display lists hold the geometry now */
    start_camera(&w.sc, &c);
    if (w.has_car) {
        for (i = 0; i < w.ncv; i++) start_car(&w, i, &drv[i], &races[0]);
        if (sim) {
            hp2_drive_in in;
            float secs = 0, steer = 0, t;
            sscanf(sim, "%f,%f", &secs, &steer);
            memset(&in, 0, sizeof in);
            in.throttle = 1;
            in.steer = steer;
            for (i = 0; i < w.ncv; i++) races[i].countdown = 0;
            for (t = 0; t < secs; t += 0.05f) race_tick(&w, drv, races, ai, &in, 0.05f);
            for (i = 1; i < w.ncv; i++)
                printf("opponent %d: %.1f mph, progress %.0f, travelled %.0f\n", i, drv[i].speed / 0.44704f, races[i].progress,
                       races[i].travelled);
            printf("after %.1f s: %.1f mph, gear %d at %.0f rpm, at %.1f %.1f %.1f, heading %.1f, progress %.0f\n", secs,
                   drv[0].speed / 0.44704f, drv[0].gear, drv[0].rpm, drv[0].pos[0], drv[0].pos[1], drv[0].pos[2], drv[0].heading * 180 / PI,
                   races[0].progress);
        }
        chase_camera(&drv[0], &c, 1);
    }
    if (cam) {
        driving = 0;
        float v[5];
        if (sscanf(cam, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
            c.pos[0] = v[0]; c.pos[1] = v[1]; c.pos[2] = v[2]; c.yaw = v[3]; c.pitch = v[4];
            /* --cam X,Y,Z,... with Z given as "g" height is not parsed; see --cam-road */
        }
    }

    if (camroad) {                 /* --cam-road X,Y,YAW,PITCH: on the road there, 2.5 above the ground */
        float v[4], z = 0;
        if (sscanf(camroad, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4 && w.has_coll
            && hp2_coll_ground(&w.coll, v[0], v[1], 10000.0f, 20000.0f, &z, NULL, NULL)) {
            driving = 0;
            c.pos[0] = v[0]; c.pos[1] = v[1]; c.pos[2] = z + 2.5f; c.yaw = v[2]; c.pitch = v[3];
        } else fprintf(stderr, "--cam-road: no ground there\n");
    }

    if (shot) {
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        draw(&w, &c, dw, dh, fog, w.has_car ? dptr : NULL);
        if (w.has_car && !cam) draw_hud(&hs, &drv[0], races, w.ncv, dw, dh);
        glFinish();
        if (save_shot(shot, dw, dh) != 0) { fprintf(stderr, "could not save %s\n", shot); return 1; }
        printf("wrote %s (%dx%d)\n", shot, dw, dh);
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    printf("Car: W/Up accelerate, S/Down brake/reverse, A/D or Left/Right steer, Space handbrake;\n"
           "C switches to the free camera: WASD/arrows move, Q/E down/up, right mouse or M to look,\n"
           "Shift fast, Ctrl slow, wheel speed. F fog, R start, P print camera, F12 screenshot, Esc quit\n");
    /* the race's music: the jukebox songs set to On, at the Race Music volume */
    hp2_settings_defaults(&cfg);
    if (config) {
        int got = hp2_settings_load(&cfg, config);
        printf("hp2track built %s %s; settings: %s %s; volumes: race music %d, ambient %d, tires %d, engine %d\n", __DATE__, __TIME__, config,
               got ? "read" : "NOT FOUND (defaults)", cfg.volume[HP2_VOL_RACE_MUSIC], cfg.volume[HP2_VOL_AMBIENT], cfg.volume[HP2_VOL_TIRES],
               cfg.volume[HP2_VOL_ENGINE]);
    } else printf("settings: no --config given, default volumes\n");
    if (music && sound && have_zdir) {
        long long base = 0;
        uint32_t size = 0;
        jbhead = hp2_zdir_load_part(&zdir, "SOUND\\JUKEBOXM.JBC", hp2_jukebox_header_size(), &jbhead_len);
        if (jbhead && hp2_jukebox_parse(&jb, jbhead, jbhead_len) == 0 && hp2_zdir_locate(&zdir, "SOUND\\JUKEBOXM.JBC", &base, &size) == 0) {
            hp2_jbplay_init(&jbp, &jb, &cfg, zdir.data, base, (uint32_t)SDL_GetTicks() | 1u);
            hp2_jbplay_set_race(&jbp, 1);
            has_music = 1;
            printf("race music: the jukebox's songs set to On, at %d%%\n", cfg.volume[HP2_VOL_RACE_MUSIC]);
        }
    }
    /* the course's ambient sounds: its zones from the track file, the location's sample bank */
    memset(&amb, 0, sizeof amb);
    memset(&ambsfx, 0, sizeof ambsfx);
    if (ambient && sound && have_zdir && track > 0 && hp2_ambient_bank_name(track) && hp2_ambient_parse(&amb, f.scen, f.scen_len) > 0) {
        char bp[64];
        size_t hl = 0, bl = 0;
        uint8_t *bh, *bb;
        snprintf(bp, sizeof bp, "SOUND\\%s.MSH", hp2_ambient_bank_name(track));
        bh = hp2_zdir_load(&zdir, bp, &hl);
        snprintf(bp, sizeof bp, "SOUND\\%s.MSB", hp2_ambient_bank_name(track));
        bb = hp2_zdir_load(&zdir, bp, &bl);
        if (bh && bb && hp2_sfx_load(&ambsfx, bh, hl, bb, bl) > 0) {
            hp2_ambient_bind(&amb, &ambsfx, (uint32_t)SDL_GetTicks() | 1u);
            has_amb = 1;
            printf("ambient sounds: %d zones (%s)\n", amb.nz, hp2_ambient_bank_name(track));
        }
        free(bh);
        free(bb);
    }
    /* the tyres' sounds: the race bank's road, gravel and skid samples */
    memset(&ingsfx, 0, sizeof ingsfx);
    memset(&tires, 0, sizeof tires);
    if (tiresnd && sound && have_zdir && w.has_car) {
        size_t hl = 0, bl = 0;
        uint8_t *bh = hp2_zdir_load(&zdir, "SOUND\\INGAME.MSH", &hl), *bb = hp2_zdir_load(&zdir, "SOUND\\INGAME.MSB", &bl);
        if (bh && bb && hp2_sfx_load(&ingsfx, bh, hl, bb, bl) > 0) {
            hp2_tires_bind(&tires, &ingsfx);
            has_tires = 1;
            printf("tyre sounds: %d samples\n", ingsfx.count);
        }
        free(bh);
        free(bb);
    }
    /* the police radio, for the Pursuit event only */
    if (pur.on && sound && have_zdir && speechsnd) {
        size_t bl = 0, vl = 0;
        long long off = 0;
        uint32_t sz = 0;
        uint8_t *bb = hp2_zdir_load(&zdir, "SOUND\\SPEECH\\ENGLISHBAN.PCK", &bl), *vb = hp2_zdir_load_id(&zdir, 0xcea9f930u, &vl);
        if (bb && vb && !hp2_zdir_locate(&zdir, "SOUND\\SPEECH\\ENGLISHDAT.PCK", &off, &sz)
            && hp2_speech_open(&speech, bb, bl, vb, vl, zdir.data, off) == 0) {
            has_speech = 1;
            printf("police radio: %d clips\n", speech.nclips);
        }
        free(bb);
        free(vb);
    }
    /* echo regions: caves, tunnels, overpasses (the port's own reverb) */
    hp2_reverb_init(&verb);
    if (reverbfx && sound && f.scen && hp2_reverb_parse(&verb, f.scen, f.scen_len) > 0) {
        has_verb = 1;
        printf("echo regions: %d\n", verb.nz);
    }
    /* crashes, horn, countdown and finish sounds, from the race bank (INGAME) */
    if (cuesnd && sound && have_zdir && w.has_car) {
        if (!has_tires) {                   /* the tyre block loads INGAME only for itself */
            size_t hl = 0, bl = 0;
            uint8_t *bh = hp2_zdir_load(&zdir, "SOUND\\INGAME.MSH", &hl), *bb = hp2_zdir_load(&zdir, "SOUND\\INGAME.MSB", &bl);
            if (bh && bb) hp2_sfx_load(&ingsfx, bh, hl, bb, bl);
            free(bh); free(bb);
        }
        if (ingsfx.count > 0) { hp2_oneshot_bind(&cues, &ingsfx); has_cues = 1; }
    }
    if ((has_eng || has_music || has_amb || has_tires || has_speech || has_cues || has_verb) && sound && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof want);
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);
        else fprintf(stderr, "no sound: %s\n", SDL_GetError());
    }
    last = SDL_GetTicks();
    while (!quit) {
        SDL_Event ev;
        const Uint8 *k;
        float dt, f3[3], mv[3] = { 0, 0, 0 }, sp, side[2];
        Uint32 now;
        int dw, dh;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (grab) { grab = 0; SDL_SetRelativeMouseMode(SDL_FALSE); }
                    else quit = 1;
                    break;
                case SDLK_f: fog = !fog; break;
                case SDLK_b: blend_opaque = !blend_opaque; printf("blended pieces drawn %s\n", blend_opaque ? "solid" : "as blended"); break;
                case SDLK_r:
                    if (driving) {
                        hp2_knockout_reset(&ko);
                        hp2_pursuit_reset(&pur); pursuit_called = 0;
                        for (i = 0; i < w.ncv; i++) {
                            hp2_race_init(&races[i], w.sc.route, w.sc.route_n, w.sc.route_loop, w.sc.route_len, laps);
                            start_car(&w, i, &drv[i], &races[0]);
                            ai[i].stuck = ai[i].reverse = 0;
                            ai[i].at = -1;
                        }
                        chase_camera(&drv[0], &c, 1);
                    }
                    else start_camera(&w.sc, &c);
                    break;
                case SDLK_c:
                    if (w.has_car) {
                        driving = !driving;
                        if (driving) chase_camera(&drv[0], &c, 1);
                    }
                    break;
                case SDLK_m: grab = !grab; SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE); break;
                case SDLK_p: printf("--cam %.1f,%.1f,%.1f,%.1f,%.1f\n", c.pos[0], c.pos[1], c.pos[2], c.yaw, c.pitch); break;
                case SDLK_F12: {
                    char name[64];
                    snprintf(name, sizeof name, "hp2track_%02d_%d.bmp", track, shots++);
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                    if (save_shot(name, dw, dh) == 0) printf("wrote %s\n", name);
                    break;
                }
                default: break;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT && !grab) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT && !grab) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
            } else if (ev.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode() && !driving) {
                c.yaw -= ev.motion.xrel * 0.15f;
                c.pitch -= ev.motion.yrel * 0.15f;
            } else if (ev.type == SDL_MOUSEWHEEL) {
                speed *= ev.wheel.y > 0 ? 1.25f : 0.8f;
                if (speed < 2) speed = 2;
                if (speed > 2000) speed = 2000;
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad
                       && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad);
                pad = NULL;
            }
        }
        now = SDL_GetTicks();
        dt = (now - last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last = now;
        k = SDL_GetKeyboardState(NULL);
        if (driving) {
            hp2_drive_in in;
            float lt = axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT), rt = axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            in.throttle = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) ? 1.0f : rt;
            in.brake = (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) ? 1.0f : lt;
            in.steer = (float)((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]))
                       + axis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            {   static int horn_was;
                if (k[SDL_SCANCODE_H] && !horn_was) cue("Horn01", 0.9f);
                horn_was = k[SDL_SCANCODE_H];
            }
            in.handbrake = k[SDL_SCANCODE_SPACE] || (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A));
            race_tick(&w, drv, races, ai, &in, dt);
            chase_camera(&drv[0], &c, 1 - expf(-8 * dt));
            if (now / 250 != (Uint32)titled) {
                char title[96];
                titled = (int)(now / 250);
                snprintf(title, sizeof title, "Hot Pursuit 2 - %s - %.0f mph", drv[0].speed > -1 ? w.cv[0].car.rec.name : "reverse",
                         fabsf(drv[0].speed) / 0.44704f);
                SDL_SetWindowTitle(win, title);
            }
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            draw(&w, &c, dw, dh, fog, dptr);
            draw_hud(&hs, &drv[0], races, w.ncv, dw, dh);
            SDL_GL_SwapWindow(win);
            if (audio) {
                static int16_t mixbuf[2 * 1024];
                /* keep about 60 ms queued: enough to never run dry, little enough to answer the throttle */
                while (SDL_GetQueuedAudioSize(audio) < 44100 * 4 * 6 / 100) {
                    if (has_eng) hp2_engine_mix(&eng, drv[0].rpm, drv[0].load, 0.6f * (float)cfg.volume[HP2_VOL_ENGINE] / 80.0f, mixbuf, 512, 44100);
                    else memset(mixbuf, 0, sizeof mixbuf);
                    if (has_amb) {      /* from the car, or the camera when it flies free */
                        float ax = driving ? drv[0].pos[0] : c.pos[0], ay = driving ? drv[0].pos[1] : c.pos[1];
                        hp2_ambient_mix(&amb, ax, ay, (float)cfg.volume[HP2_VOL_AMBIENT] / 100.0f, mixbuf, 512);
                    }
                    if (has_tires && driving) {
                        hp2_tires_in ti;
                        ti.speed = drv[0].speed; ti.surface = drv[0].surface; ti.on_ground = drv[0].on_ground;
                        ti.sliding = drv[0].sliding[0] + drv[0].sliding[1] + drv[0].sliding[2] + drv[0].sliding[3];
                        ti.steer = drv[0].steer_angle; ti.throttle = drv[0].load;
                        hp2_tires_mix(&tires, &ti, (float)cfg.volume[HP2_VOL_TIRES] / 100.0f, mixbuf, 512);
                    }
                    if (has_cues) hp2_oneshot_mix(&cues, (float)cfg.volume[HP2_VOL_AMBIENT] / 100.0f, mixbuf, 512);
                    if (has_verb) {        /* the echo takes the effects, not the music or the radio */
                        float ex = driving ? drv[0].pos[0] : c.pos[0], ey = driving ? drv[0].pos[1] : c.pos[1];
                        hp2_reverb_apply(&verb, ex, ey, mixbuf, 512);
                    }
                    if (has_music) {
                        static int16_t mus[2 * 512];
                        int got = hp2_jbplay_fill(&jbp, mus, 512), q;
                        for (q = 0; q < 2 * got; q++) {
                            int v = mixbuf[q] + mus[q];
                            mixbuf[q] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                        }
                    }
                    if (has_speech) hp2_speech_mix(&speech, (float)cfg.volume[HP2_VOL_SPEECH] / 100.0f, mixbuf, 512);
                    SDL_QueueAudio(audio, mixbuf, 512 * 4);
                }
            }
            continue;
        }
        c.yaw -= axis(pad, SDL_CONTROLLER_AXIS_RIGHTX) * 120 * dt;
        c.pitch -= axis(pad, SDL_CONTROLLER_AXIS_RIGHTY) * 90 * dt;
        if (c.pitch > 89) c.pitch = 89;
        if (c.pitch < -89) c.pitch = -89;
        forward(&c, f3);
        side[0] = sinf(c.yaw * PI / 180);       /* to the right */
        side[1] = -cosf(c.yaw * PI / 180);
        {
            float fw = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) - axis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            float rt = (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) + axis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            float up = (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_PAGEUP]) - (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_PAGEDOWN])
                       + axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) - axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            mv[0] = f3[0] * fw + side[0] * rt;
            mv[1] = f3[1] * fw + side[1] * rt;
            mv[2] = f3[2] * fw + up;
        }
        sp = speed * (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? 5.0f : 1.0f)
             * (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] ? 0.2f : 1.0f);
        for (i = 0; i < 3; i++) c.pos[i] += mv[i] * sp * dt;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        draw(&w, &c, dw, dh, fog, w.has_car ? dptr : NULL);
        SDL_GL_SwapWindow(win);
    }
    if (resultf && w.has_car) {      /* how the race ended, for the menus */
        char txt[256];
        const hp2_race *pr[HP2_MAX_CARS];
        int ord[HP2_MAX_CARS], ci, n, place = 0;
        hp2_race me = races[0];
        if (pur.on && pur.done) {          /* the chase is over when the last speeder is arrested */
            me.state = HP2_RACE_FINISHED;
            memset(me.lap_time, 0, sizeof me.lap_time);
            me.laps = 1;
            me.lap_time[0] = pur.time;
            me.best = 0;
        }
        if (ko.on) {                  /* the order is the knockout's; the player is finished when it is decided */
            hp2_knockout_order(&ko, races, w.ncv, ord);
            if (ko.done) {
                me.state = HP2_RACE_FINISHED;
                memset(me.lap_time, 0, sizeof me.lap_time);
                me.best = 0;
                if (!ko.out[0]) { me.laps = 1; me.lap_time[0] = me.clock; }
            }
        } else {
            for (ci = 0; ci < w.ncv; ci++) pr[ci] = &races[ci];
            hp2_race_order(pr, w.ncv, ord);
        }
        for (ci = 0; ci < w.ncv; ci++) if (ord[ci] == 0) place = ci + 1;
        n = hp2_race_result_format(&me, w.ncv > 1 && !pur.on ? place : 0, ord, w.ncv, txt, sizeof txt);
        FILE *rf = n > 0 ? fopen(resultf, "wb") : NULL;
        if (rf) { fwrite(txt, 1, (size_t)n, rf); fclose(rf); }
        else fprintf(stderr, "could not write %s\n", resultf);
    }
    if (audio) SDL_CloseAudioDevice(audio);
    if (has_music) hp2_jbplay_close(&jbp);
    if (has_verb) hp2_reverb_free(&verb);
    if (has_speech) hp2_speech_close(&speech);
    if (have_zdir) hp2_zdir_close(&zdir);
    free(jbhead);
    if (has_eng) hp2_engine_free(&eng);
    hp2_ambient_free(&amb);
    hp2_sfx_free(&ambsfx);
    hp2_sfx_free(&ingsfx);
    hp2_ai_line_free(&w.line);
    if (pad) SDL_GameControllerClose(pad);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
