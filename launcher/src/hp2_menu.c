#include "hp2_menu.h"

#include <limits.h>
#include <stdio.h>

typedef struct { const char *section, *label; hp2_row_kind kind; int restart; const char *help; } row_info;

static const row_info ROWS[HP2_ROW_COUNT] = {
    /* help lines in the game's own style ("Show or hide the leaderboard.") */
    { "Video",    "Internal resolution",  HP2_KIND_CHOICE, 0, "Set the resolution the game draws at." },
    { "Video",    "Aspect ratio",         HP2_KIND_CHOICE, 0, "Set the shape of the picture." },
    { "Video",    "Window mode",          HP2_KIND_CHOICE, 0, "Play in a window or on the full screen." },
    { "Video",    "VSync",                HP2_KIND_TOGGLE, 0, "Match the display's refresh to stop tearing." },
    { "Video",    "Frame cap",            HP2_KIND_CHOICE, 0, "Set the highest frame rate." },
    { "Video",    "Anti-aliasing",        HP2_KIND_CHOICE, 1, "Smooth jagged edges. Takes effect after a restart." },
    { "Video",    "Anisotropic filter",   HP2_KIND_CHOICE, 0, "Keep the road sharp into the distance." },
    { "Video",    "Show FPS",             HP2_KIND_TOGGLE, 0, "Show or hide the frame rate." },
    { "Controls", "Control scheme",       HP2_KIND_CHOICE, 0, "Pedals on the triggers, or on the buttons." },
    { "Controls", "Button icons",         HP2_KIND_CHOICE, 0, "Choose which controller's buttons to show." },
    { "Controls", "Stick dead zone",      HP2_KIND_SLIDER, 0, "Set how far the stick moves before you steer." },
    { "Controls", "Vibration",            HP2_KIND_TOGGLE, 0, "Turn vibration on or off." },
};

/* Internal resolution presets, ordered by height. 640x448 is what the PS2
 * rendered. With the 21:9 aspect the row offers ultrawide sizes instead. */
static const int RES_WIDE[][2] = {
    { 640, 448 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }
};
static const int RES_ULTRA[][2] = {
    { 640, 448 }, { 1720, 720 }, { 2560, 1080 }, { 3440, 1440 }, { 5120, 2160 }
};
#define NRES ((int)(sizeof RES_WIDE / sizeof RES_WIDE[0]))

static const int (*res_list(hp2_aspect a))[2]
{
    return a == HP2_ASPECT_21_9 ? RES_ULTRA : RES_WIDE;
}

/* When the aspect changes between 21:9 and the others, a preset resolution
 * moves to the other list's preset of the same height (1920x1080 <->
 * 2560x1080). A custom resolution is left alone. */
static void follow_aspect(hp2_settings *s, hp2_aspect from)
{
    const int (*a)[2] = res_list(from), (*b)[2] = res_list(s->aspect);
    int i;
    if (a == b) return;
    for (i = 0; i < NRES; i++)
        if (a[i][0] == s->internal_width && a[i][1] == s->internal_height) {
            s->internal_width = b[i][0];
            s->internal_height = b[i][1];
            return;
        }
}

/* 0 in the frame cap list means "no limit" and sorts last. */
static const int CAPS[]  = { 30, 60, 72, 90, 120, 144, 165, 240, 0 };
static const int MSAAS[] = { 0, 2, 4, 8 };
static const int ANISOS[] = { 1, 2, 4, 8, 16 };
#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

int hp2_menu_row_count(void) { return HP2_ROW_COUNT; }

static int valid(int row) { return row >= 0 && row < HP2_ROW_COUNT; }

const char *hp2_menu_section(int row) { return valid(row) ? ROWS[row].section : ""; }
const char *hp2_menu_label(int row)   { return valid(row) ? ROWS[row].label : ""; }
const char *hp2_menu_help(int row)    { return valid(row) ? ROWS[row].help : ""; }
hp2_row_kind hp2_menu_kind(int row)   { return valid(row) ? ROWS[row].kind : HP2_KIND_TOGGLE; }
int hp2_menu_needs_restart(int row)   { return valid(row) ? ROWS[row].restart : 0; }

/* ------------------------------------------------------------ list stepping */
static int key_of(int v, int zero_is_top)
{
    return (v == 0 && zero_is_top) ? INT_MAX : v;
}

/* Move `cur` one entry along an ascending list. `cur` need not be in the list
 * (a hand-edited file may hold 75 fps): then right goes to the next entry
 * above it and left to the next below. Stops at the ends. */
static int step_list(const int *list, int n, int cur, int dir, int zero_top)
{
    int k = key_of(cur, zero_top), i, at = -1;
    for (i = 0; i < n; i++)
        if (key_of(list[i], zero_top) >= k) { at = i; break; }
    if (at < 0) at = n;                       /* above every entry */
    if (dir > 0) {
        if (at < n && key_of(list[at], zero_top) == k) at++;
        if (at > n - 1) at = n - 1;
    } else {
        at--;
        if (at < 0) at = 0;
    }
    return list[at];
}

static int wrap(int v, int count, int dir)
{
    v += (dir > 0) ? 1 : -1;
    if (v < 0) v = count - 1;
    if (v >= count) v = 0;
    return v;
}

/* ------------------------------------------------------------------- value */
void hp2_menu_value(const hp2_settings *s, int row, char *buf, size_t cap)
{
    int i;
    if (!buf || cap == 0) return;
    buf[0] = 0;
    if (!s || !valid(row)) return;

    switch ((hp2_row)row) {
    case HP2_ROW_INTERNAL_RES: {
        const int (*RES)[2] = res_list(s->aspect);
        for (i = 0; i < NRES; i++)
            if (RES[i][0] == s->internal_width && RES[i][1] == s->internal_height) {
                if (i == 0) snprintf(buf, cap, "%d x %d (PS2)", RES[i][0], RES[i][1]);
                else        snprintf(buf, cap, "%d x %d", RES[i][0], RES[i][1]);
                return;
            }
        snprintf(buf, cap, "%d x %d", s->internal_width, s->internal_height);
        break;
    }
    case HP2_ROW_ASPECT: {
        const char *a = hp2_aspect_name(s->aspect);
        snprintf(buf, cap, "%s", s->aspect == HP2_ASPECT_AUTO ? "Auto" : a);
        break;
    }
    case HP2_ROW_WINDOW: {
        static const char *N[] = { "Windowed", "Borderless", "Fullscreen" };
        snprintf(buf, cap, "%s", N[(s->window >= 0 && s->window < HP2_WINDOW_COUNT) ? s->window : 0]);
        break;
    }
    case HP2_ROW_VSYNC:    snprintf(buf, cap, "%s", s->vsync ? "On" : "Off"); break;
    case HP2_ROW_FRAME_CAP:
        if (s->frame_cap) snprintf(buf, cap, "%d fps", s->frame_cap);
        else              snprintf(buf, cap, "No limit");
        break;
    case HP2_ROW_MSAA:
        if (s->msaa) snprintf(buf, cap, "%dx", s->msaa);
        else         snprintf(buf, cap, "Off");
        break;
    case HP2_ROW_ANISO:
        if (s->aniso > 1) snprintf(buf, cap, "%dx", s->aniso);
        else              snprintf(buf, cap, "Off");
        break;
    case HP2_ROW_SHOW_FPS: snprintf(buf, cap, "%s", s->show_fps ? "On" : "Off"); break;
    case HP2_ROW_SCHEME: {
        static const char *N[] = { "Modern", "Classic" };
        snprintf(buf, cap, "%s", N[(s->scheme >= 0 && s->scheme < HP2_SCHEME_COUNT) ? s->scheme : 0]);
        break;
    }
    case HP2_ROW_GLYPHS: {
        static const char *N[] = { "Auto", "Xbox", "PlayStation", "Nintendo", "Generic", "Keyboard" };
        snprintf(buf, cap, "%s", N[(s->glyphs >= 0 && s->glyphs < HP2_GLYPHS_COUNT) ? s->glyphs : 0]);
        break;
    }
    case HP2_ROW_DEADZONE: snprintf(buf, cap, "%d%%", (int)(s->deadzone * 100.0f + 0.5f)); break;
    case HP2_ROW_RUMBLE:   snprintf(buf, cap, "%s", s->rumble ? "On" : "Off"); break;
    case HP2_ROW_COUNT:    break;
    }
}

/* ------------------------------------------------------------------ adjust */
int hp2_menu_adjust(hp2_settings *s, int row, int dir)
{
    hp2_settings before;
    int i, n;

    if (!s || !valid(row) || dir == 0) return 0;
    before = *s;

    switch ((hp2_row)row) {
    case HP2_ROW_INTERNAL_RES: {
        /* presets ordered by height; step relative to the current height */
        const int (*RES)[2] = res_list(s->aspect);
        int h = s->internal_height, at = -1;
        for (i = 0; i < NRES; i++)
            if (RES[i][1] >= h) { at = i; break; }
        if (at < 0) at = NRES;
        if (dir > 0) {
            if (at < NRES && RES[at][1] == h) at++;
            if (at > NRES - 1) at = NRES - 1;
        } else {
            at--;
            if (at < 0) at = 0;
        }
        s->internal_width = RES[at][0];
        s->internal_height = RES[at][1];
        break;
    }
    case HP2_ROW_ASPECT:
        s->aspect = (hp2_aspect)wrap((int)s->aspect, HP2_ASPECT_COUNT, dir);
        follow_aspect(s, before.aspect);
        break;
    case HP2_ROW_WINDOW: s->window = (hp2_window_mode)wrap((int)s->window, HP2_WINDOW_COUNT, dir); break;
    case HP2_ROW_VSYNC:  s->vsync = !s->vsync; break;
    case HP2_ROW_FRAME_CAP:
        s->frame_cap = step_list(CAPS, COUNT(CAPS), s->frame_cap, dir, 1);
        break;
    case HP2_ROW_MSAA:   s->msaa = step_list(MSAAS, COUNT(MSAAS), s->msaa, dir, 0); break;
    case HP2_ROW_ANISO:  s->aniso = step_list(ANISOS, COUNT(ANISOS), s->aniso, dir, 0); break;
    case HP2_ROW_SHOW_FPS: s->show_fps = !s->show_fps; break;
    case HP2_ROW_SCHEME: s->scheme = (hp2_scheme)wrap((int)s->scheme, HP2_SCHEME_COUNT, dir); break;
    case HP2_ROW_GLYPHS: s->glyphs = (hp2_glyph_set)wrap((int)s->glyphs, HP2_GLYPHS_COUNT, dir); break;
    case HP2_ROW_DEADZONE:
        n = (int)(s->deadzone * 20.0f + 0.5f) + (dir > 0 ? 1 : -1);   /* 5% steps */
        if (n < 0) n = 0;
        if (n > 18) n = 18;                                           /* 90% */
        s->deadzone = (float)n / 20.0f;
        break;
    case HP2_ROW_RUMBLE: s->rumble = !s->rumble; break;
    case HP2_ROW_COUNT:  break;
    }
    hp2_settings_validate(s);
    return s->internal_width != before.internal_width ||
           s->internal_height != before.internal_height ||
           s->aspect != before.aspect || s->window != before.window ||
           s->vsync != before.vsync || s->frame_cap != before.frame_cap ||
           s->msaa != before.msaa || s->aniso != before.aniso ||
           s->show_fps != before.show_fps || s->scheme != before.scheme ||
           s->glyphs != before.glyphs || s->deadzone != before.deadzone ||
           s->rumble != before.rumble;
}
