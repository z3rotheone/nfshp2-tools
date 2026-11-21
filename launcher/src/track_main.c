/* hp2track - fly around a track's scenery.
 *
 * The first step towards the race: loads a track's scenery and textures
 * from your own disc (or from extracted files) and draws them with
 * OpenGL 1.x, so any PC from the last twenty years can run it.
 *
 *   hp2track --disc DIR --track 24
 *   hp2track --scenery TRACKB24.BUN --tex TEX24LOCATION.BIN --tex TEX24TRACK.BIN
 *
 * DIR is the NFSHP2 folder of your disc copy (the one with ZDIR.BIN). Track
 * numbers are 11-16, 21-26, 31-36, 41-46, 61-66: the tens digit is the
 * location, the units the variant (n and n+3 are the same roads at another
 * time of day or weather).
 *
 * Controls: WASD or the arrow keys move, Q/E (or Page Down/Up) go down and
 * up, hold the right mouse button (or press M to capture the mouse) to
 * look around, Shift is faster and Ctrl slower, the mouse wheel changes
 * the speed. F fog on/off, R back to the start, P prints the camera,
 * F12 saves a screenshot, Esc quits. A game controller works too: left
 * stick moves, right stick looks, the triggers go down and up.
 *
 * --shot OUT.bmp renders one frame and exits (with --cam X,Y,Z,YAW,PITCH,
 * degrees, to choose the view).
 */
#include "hp2_scene.h"
#include "hp2_tex.h"
#include "hp2_zdir.h"

#include <SDL.h>
#include <SDL_opengl.h>

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
} gltex;

typedef struct {
    GLuint list;
    int    tex;             /* index into the texture table */
} glgroup;

typedef struct {
    hp2_scene  sc;
    gltex     *tex;
    int        ntex;
    glgroup  **grp;         /* per section, ng entries; sky after the sections */
    glgroup   *skyg;
    int        missing_tex;
} world;

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

static int load_disc(files *f, const char *dir, int track)
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
    hp2_zdir_close(&z);
    return 0;
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
            long k, n = (long)t->w * t->h, mid = 0, clear = 0;
            for (k = 0; k < n; k++) {
                uint8_t a = px[4 * k + 3];
                if (a < 16) clear++;
                else if (a < 240) mid++;
            }
            g->mode = mid > n / 20 ? MODE_BLEND : clear ? MODE_CUTOUT : MODE_OPAQUE;
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

static GLuint compile(const hp2_sgroup *g)
{
    GLuint l = glGenLists(1);
    int i;
    glNewList(l, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < g->nv; i++) {
        const hp2_vert *v = &g->v[i];
        glColor4ub(v->c[0], v->c[1], v->c[2], v->c[3]);
        glTexCoord2f(v->u, v->v);
        glVertex3f(v->x, v->y, v->z);
    }
    glEnd();
    glEndList();
    return l;
}

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
        }
    }
    upload_textures(w, ts, mipmaps);
    return 0;
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

static int draw(world *w, const camera *c, int W, int H, int fog)
{
    float view[16], proj[16], clip[16], n = 0.5f, fa = 30000.0f, aspect = (float)W / (H ? H : 1);
    float t = n * tanf(55.0f * PI / 360.0f), r = t * aspect;
    int i, j, pass, drawn = 0;
    const hp2_scene *sc = &w->sc;
    float fogc[4] = { 0.55f, 0.6f, 0.75f, 1 };

    if (sc->has_fog) {
        for (i = 0; i < 3; i++) fogc[i] = sc->fog_rgba[i] / 255.0f;
    }
    glViewport(0, 0, W, H);
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
    for (j = 0; j < sc->sky.ng; j++) {
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
    /* opaque and cut-out first, then the blended ones over them */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < sc->nsec; i++) {
            if (!sc->sec[i].ng || !box_visible(clip, sc->sec[i].box)) continue;
            if (pass == 0) drawn++;
            for (j = 0; j < sc->sec[i].ng; j++) {
                const gltex *g = &w->tex[w->grp[i][j].tex];
                if ((g->mode == MODE_BLEND) != pass) continue;
                set_mode(g->mode);
                glBindTexture(GL_TEXTURE_2D, g->id);
                glCallList(w->grp[i][j].list);
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

static float axis(SDL_GameController *p, SDL_GameControllerAxis a)
{
    float v = p ? SDL_GameControllerGetAxis(p, a) / 32767.0f : 0;
    if (v > -0.15f && v < 0.15f) return 0;
    return v;
}

int main(int argc, char **argv)
{
    const char *disc = NULL, *scen = NULL, *texf[4], *shot = NULL, *cam = NULL;
    int ntexf = 0, track = 0, W = 1280, H = 720, i, quit = 0, fog = 1, grab = 0, shots = 0;
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
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--track") && i + 1 < argc) track = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scenery") && i + 1 < argc) scen = argv[++i];
        else if (!strcmp(argv[i], "--tex") && i + 1 < argc && ntexf < 4) texf[ntexf++] = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--cam") && i + 1 < argc) cam = argv[++i];
        else if (!strcmp(argv[i], "--no-fog")) fog = 0;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 64 || H < 48) { W = 1280; H = 720; } }
        else {
            fprintf(stderr, "usage: %s --disc DIR --track NN | --scenery FILE [--tex FILE]...\n"
                            "       [--size WxH] [--no-fog] [--shot OUT.bmp] [--cam X,Y,Z,YAW,PITCH]\n"
                            "tracks: 11-16 21-26 31-36 41-46 61-66\n", argv[0]);
            return 2;
        }
    }
    if (disc) {
        if (track < 11 || track > 66 || track % 10 < 1 || track % 10 > 6 || track / 10 == 5) {
            fprintf(stderr, "give --track: 11-16, 21-26, 31-36, 41-46 or 61-66\n");
            return 2;
        }
        if (load_disc(&f, disc, track) != 0) return 1;
    } else if (scen) {
        f.scen = maybe_decompress(hp2_read_file(scen, &f.scen_len), &f.scen_len);
        if (!f.scen) { fprintf(stderr, "cannot read %s\n", scen); return 1; }
        for (i = 0; i < ntexf; i++) {
            f.tex[f.ntex] = hp2_read_file(texf[i], &f.tex_len[f.ntex]);
            if (!f.tex[f.ntex]) { fprintf(stderr, "cannot read %s\n", texf[i]); return 1; }
            f.ntex++;
        }
    } else {
        fprintf(stderr, "give --disc DIR --track NN (DIR = your disc's NFSHP2 folder), or --scenery FILE\n");
        return 2;
    }
    if (hp2_scene_build(&w.sc, f.scen, f.scen_len, 0) != 0) { fprintf(stderr, "no track scenery in that file\n"); return 1; }
    for (i = 0; i < f.ntex; i++)
        if (hp2_tex_scan(&ts, f.tex[i], f.tex_len[i]) < 0) { fprintf(stderr, "out of memory\n"); return 1; }
    hp2_tex_scan(&ts, f.scen, f.scen_len);         /* any pack inside the scenery itself */
    printf("%d sections, %d models placed, %ld triangles, %d textures\n", w.sc.nsec, w.sc.placed, w.sc.tris, ts.count);

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
        if (build_world(&w, major > 1 || minor >= 4, &ts) != 0) { fprintf(stderr, "out of memory\n"); return 1; }
        printf("OpenGL %s; %d textures used, %d not found\n", ver ? ver : "?", w.ntex, w.missing_tex);
    }
    hp2_scene_drop_vertices(&w.sc);   /* the display lists hold the geometry now */
    start_camera(&w.sc, &c);
    if (cam) {
        float v[5];
        if (sscanf(cam, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
            c.pos[0] = v[0]; c.pos[1] = v[1]; c.pos[2] = v[2]; c.yaw = v[3]; c.pitch = v[4];
        }
    }

    if (shot) {
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        draw(&w, &c, dw, dh, fog);
        glFinish();
        if (save_shot(shot, dw, dh) != 0) { fprintf(stderr, "could not save %s\n", shot); return 1; }
        printf("wrote %s (%dx%d)\n", shot, dw, dh);
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    printf("WASD/arrows move, Q/E down/up, right mouse or M to look, Shift fast, Ctrl slow, wheel speed,\n"
           "F fog, R start, P print camera, F12 screenshot, Esc quit\n");
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
                case SDLK_r: start_camera(&w.sc, &c); break;
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
            } else if (ev.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode()) {
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
        draw(&w, &c, dw, dh, fog);
        SDL_GL_SwapWindow(win);
    }
    if (pad) SDL_GameControllerClose(pad);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
