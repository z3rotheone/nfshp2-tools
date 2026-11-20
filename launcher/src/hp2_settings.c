#include "hp2_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ names */
static const char *ASPECT[] = { "auto", "4:3", "16:9", "16:10", "21:9" };
static const char *WINDOW[] = { "windowed", "borderless", "fullscreen" };
static const char *SCHEME[] = { "modern", "classic" };
static const char *AUDIO_MODE[] = { "stereo", "mono", "surround" };
static const char *VOLUME_KEY[] = { "menu_music", "race_music", "ambient", "tires",
                                    "engine", "menu_sfx", "speech" };
static const int VOLUME_DEFAULT[] = { 70, 70, 80, 80, 80, 80, 80 };
static const char *PLAY_KEY[] = { "off", "racing", "hot_pursuit", "menu", "menu_racing", "always" };
static const char *PLAY_LABEL[] = { "Off", "World Racing", "Hot Pursuit", "Menu", "Menu/Racing", "Always" };
static const char *GLYPHS[] = { "auto", "xbox", "playstation", "nintendo",
                                "generic", "keyboard" };

const char *hp2_audio_mode_name(hp2_audio_mode m)
{
    return (m >= 0 && m < HP2_AUDIO_COUNT) ? AUDIO_MODE[m] : AUDIO_MODE[0];
}

const char *hp2_volume_key(hp2_volume v)
{
    return (v >= 0 && v < HP2_VOL_COUNT) ? VOLUME_KEY[v] : "";
}

const char *hp2_play_key(hp2_play p)   { return (p >= 0 && p < HP2_PLAY_COUNT) ? PLAY_KEY[p] : "default"; }
const char *hp2_play_label(hp2_play p) { return (p >= 0 && p < HP2_PLAY_COUNT) ? PLAY_LABEL[p] : ""; }

void hp2_settings_default_jukebox(hp2_settings *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < HP2_SONGS; i++) s->song[i] = -1;
}

void hp2_settings_default_volumes(hp2_settings *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < HP2_VOL_COUNT; i++) s->volume[i] = VOLUME_DEFAULT[i];
}

const char *hp2_aspect_name(hp2_aspect a)
{
    return (a >= 0 && a < HP2_ASPECT_COUNT) ? ASPECT[a] : ASPECT[0];
}
const char *hp2_window_name(hp2_window_mode w)
{
    return (w >= 0 && w < HP2_WINDOW_COUNT) ? WINDOW[w] : WINDOW[0];
}
const char *hp2_scheme_name(hp2_scheme c)
{
    return (c >= 0 && c < HP2_SCHEME_COUNT) ? SCHEME[c] : SCHEME[0];
}
const char *hp2_glyph_set_name(hp2_glyph_set g)
{
    return (g >= 0 && g < HP2_GLYPHS_COUNT) ? GLYPHS[g] : GLYPHS[0];
}

float hp2_aspect_ratio(hp2_aspect a)
{
    switch (a) {
    case HP2_ASPECT_4_3:   return 4.0f / 3.0f;
    case HP2_ASPECT_16_9:  return 16.0f / 9.0f;
    case HP2_ASPECT_16_10: return 16.0f / 10.0f;
    /* "21:9" is a name: the panels are 64:27 (2560x1080, 5120x2160) or
     * 43:18 (3440x1440). 64:27 is what games mean by it. */
    case HP2_ASPECT_21_9:  return 64.0f / 27.0f;
    default:               return 0.0f;   /* AUTO */
    }
}

static int lookup(const char *v, const char **tab, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(v, tab[i]) == 0)
            return i;
    return -1;
}

/* --------------------------------------------------------------- defaults */
void hp2_settings_defaults(hp2_settings *s)
{
    if (!s) return;
    s->internal_width  = 1920;
    s->internal_height = 1080;
    s->aspect          = HP2_ASPECT_16_9;
    s->window          = HP2_WINDOW_BORDERLESS;
    s->vsync           = 1;
    s->frame_cap       = 60;
    s->msaa            = 4;
    s->aniso           = 8;
    s->show_fps        = 0;
    s->scheme          = HP2_SCHEME_MODERN;
    s->glyphs          = HP2_GLYPHS_AUTO;
    s->deadzone        = 0.20f;
    s->rumble          = 1;
    hp2_settings_default_volumes(s);
    s->audio_mode      = HP2_AUDIO_STEREO;
    hp2_settings_default_jukebox(s);
}

/* --------------------------------------------------------------- validate */
#define CLAMP_INT(field, lo, hi)                      \
    do {                                              \
        if (s->field < (lo)) { s->field = (lo); n++; }\
        else if (s->field > (hi)) { s->field = (hi); n++; } \
    } while (0)

#define CLAMP_ENUM(field, count)                                  \
    do {                                                          \
        if ((int)s->field < 0 || (int)s->field >= (count)) {      \
            s->field = 0; n++;                                    \
        }                                                         \
    } while (0)

int hp2_settings_validate(hp2_settings *s)
{
    int n = 0;
    if (!s) return 0;

    CLAMP_INT(internal_width,  HP2_MIN_WIDTH,  HP2_MAX_WIDTH);
    CLAMP_INT(internal_height, HP2_MIN_HEIGHT, HP2_MAX_HEIGHT);
    CLAMP_ENUM(aspect, HP2_ASPECT_COUNT);
    CLAMP_ENUM(window, HP2_WINDOW_COUNT);
    CLAMP_ENUM(scheme, HP2_SCHEME_COUNT);
    CLAMP_ENUM(glyphs, HP2_GLYPHS_COUNT);
    CLAMP_ENUM(audio_mode, HP2_AUDIO_COUNT);
    {
        int i;
        for (i = 0; i < HP2_VOL_COUNT; i++) {
            if (s->volume[i] < 0)   { s->volume[i] = 0; n++; }
            if (s->volume[i] > 100) { s->volume[i] = 100; n++; }
        }
        for (i = 0; i < HP2_SONGS; i++)
            if (s->song[i] < -1 || s->song[i] >= HP2_PLAY_COUNT) { s->song[i] = -1; n++; }
    }

    if (s->vsync  != 0 && s->vsync  != 1) { s->vsync  = 1; n++; }
    if (s->rumble != 0 && s->rumble != 1) { s->rumble = 1; n++; }
    if (s->show_fps != 0 && s->show_fps != 1) { s->show_fps = 0; n++; }

    if (s->frame_cap != 0) {
        CLAMP_INT(frame_cap, HP2_MIN_FRAME_CAP, HP2_MAX_FRAME_CAP);
    }
    if (s->msaa != 0 && s->msaa != 2 && s->msaa != 4 && s->msaa != 8) {
        s->msaa = 4; n++;
    }
    if (s->aniso != 1 && s->aniso != 2 && s->aniso != 4 &&
        s->aniso != 8 && s->aniso != 16) {
        s->aniso = 8; n++;
    }

    /* NaN fails every comparison, so test for the good range and correct
     * anything else; that catches NaN without needing math.h. */
    if (!(s->deadzone >= 0.0f && s->deadzone <= 0.9f)) {
        s->deadzone = 0.20f;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ parse */
static char *trim(char *p)
{
    char *e;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!*p) return p;
    e = p + strlen(p) - 1;
    while (e > p && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) *e-- = 0;
    return p;
}

static void lower(char *p)
{
    for (; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
}

static int parse_bool(const char *v, int *out)
{
    if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true")  || !strcmp(v, "yes")) { *out = 1; return 1; }
    if (!strcmp(v, "off")|| !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no"))  { *out = 0; return 1; }
    return 0;
}

int hp2_settings_parse(hp2_settings *s, const char *text)
{
    char line[256];
    const char *p;
    int understood = 0;

    if (!s) return 0;
    hp2_settings_defaults(s);
    if (!text) return 0;

    p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *eq, *key, *val;

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : p + strlen(p);

        {   /* strip comments: # or ; to end of line */
            char *h = strpbrk(line, "#;");
            if (h) *h = 0;
        }
        eq = strchr(line, '=');
        if (!eq) continue;              /* section headers and blanks */
        *eq = 0;
        key = trim(line);
        val = trim(eq + 1);
        if (!*key || !*val) continue;
        lower(key);
        lower(val);

        if (!strcmp(key, "internal_width")) {
            s->internal_width = atoi(val); understood++;
        } else if (!strcmp(key, "internal_height")) {
            s->internal_height = atoi(val); understood++;
        } else if (!strcmp(key, "aspect")) {
            int i = lookup(val, ASPECT, HP2_ASPECT_COUNT);
            if (i >= 0) { s->aspect = (hp2_aspect)i; understood++; }
        } else if (!strcmp(key, "window")) {
            int i = lookup(val, WINDOW, HP2_WINDOW_COUNT);
            if (i >= 0) { s->window = (hp2_window_mode)i; understood++; }
        } else if (!strcmp(key, "vsync")) {
            if (parse_bool(val, &s->vsync)) understood++;
        } else if (!strcmp(key, "frame_cap")) {
            if (!strcmp(val, "off") || !strcmp(val, "none") || !strcmp(val, "unlimited"))
                s->frame_cap = 0;
            else
                s->frame_cap = atoi(val);
            understood++;
        } else if (!strcmp(key, "msaa")) {
            s->msaa = !strcmp(val, "off") ? 0 : atoi(val); understood++;
        } else if (!strcmp(key, "aniso")) {
            s->aniso = !strcmp(val, "off") ? 1 : atoi(val); understood++;
        } else if (!strcmp(key, "show_fps")) {
            if (parse_bool(val, &s->show_fps)) understood++;
        } else if (!strcmp(key, "scheme")) {
            int i = lookup(val, SCHEME, HP2_SCHEME_COUNT);
            if (i >= 0) { s->scheme = (hp2_scheme)i; understood++; }
        } else if (!strcmp(key, "glyphs")) {
            int i = lookup(val, GLYPHS, HP2_GLYPHS_COUNT);
            if (i >= 0) { s->glyphs = (hp2_glyph_set)i; understood++; }
        } else if (!strcmp(key, "deadzone")) {
            s->deadzone = (float)atof(val); understood++;
        } else if (!strcmp(key, "rumble")) {
            if (parse_bool(val, &s->rumble)) understood++;
        } else if (!strcmp(key, "mode")) {
            int i = lookup(val, AUDIO_MODE, HP2_AUDIO_COUNT);
            if (i >= 0) { s->audio_mode = (hp2_audio_mode)i; understood++; }
        } else if (!strncmp(key, "song", 4) && key[4] >= '1' && key[4] <= '9') {
            int k = atoi(key + 4), i = lookup(val, PLAY_KEY, HP2_PLAY_COUNT);
            if (k >= 1 && k <= HP2_SONGS && (i >= 0 || !strcmp(val, "default"))) {
                s->song[k - 1] = i;
                understood++;
            }
        } else {
            int i = lookup(key, VOLUME_KEY, HP2_VOL_COUNT);
            if (i >= 0) {
                s->volume[i] = !strcmp(val, "off") || !strcmp(val, "mute") ? 0 : atoi(val);
                understood++;
            }
        }
        /* anything else: ignored on purpose, for forward compatibility */
    }
    hp2_settings_validate(s);
    return understood;
}

/* ------------------------------------------------------------------ write */
size_t hp2_settings_write(const hp2_settings *s, char *buf, size_t cap)
{
    int n;
    char cap_text[16];
    if (!s) return 0;
    if (s->frame_cap > 0) snprintf(cap_text, sizeof(cap_text), "%d", s->frame_cap);
    else                  snprintf(cap_text, sizeof(cap_text), "off");
    n = snprintf(buf, cap,
        "# Need for Speed: Hot Pursuit 2 - settings\n"
        "# Written by the launcher; safe to edit by hand.\n"
        "\n"
        "[video]\n"
        "internal_width = %d\n"
        "internal_height = %d\n"
        "aspect = %-10s # auto, 4:3, 16:9, 16:10, 21:9\n"
        "window = %-10s # windowed, borderless, fullscreen\n"
        "vsync = %s\n"
        "frame_cap = %-10s # off, or frames per second (20-1000)\n"
        "msaa = %-10d # 0, 2, 4, 8\n"
        "aniso = %-10d # 1, 2, 4, 8, 16\n"
        "show_fps = %s\n"
        "\n"
        "[input]\n"
        "scheme = %-10s # modern, classic\n"
        "glyphs = %-10s # auto, xbox, playstation, nintendo, generic, keyboard\n"
        "deadzone = %.2f\n"
        "rumble = %s\n"
        "\n"
        "[audio]                # volumes in percent, 0 = mute\n"
        "menu_music = %d\n"
        "race_music = %d\n"
        "ambient = %d\n"
        "tires = %d\n"
        "engine = %d\n"
        "menu_sfx = %d\n"
        "speech = %d\n"
        "mode = %-10s # stereo, mono, surround\n",
        s->internal_width, s->internal_height,
        hp2_aspect_name(s->aspect),
        hp2_window_name(s->window),
        s->vsync ? "on" : "off",
        cap_text, s->msaa, s->aniso,
        s->show_fps ? "on" : "off",
        hp2_scheme_name(s->scheme),
        hp2_glyph_set_name(s->glyphs),
        (double)s->deadzone,
        s->rumble ? "on" : "off",
        s->volume[0], s->volume[1], s->volume[2], s->volume[3],
        s->volume[4], s->volume[5], s->volume[6],
        hp2_audio_mode_name(s->audio_mode));
    if (n < 0) return 0;
    {   /* jukebox: only the songs set by hand; the rest follow the bank */
        int i, head = 0;
        for (i = 0; i < HP2_SONGS; i++) {
            int k;
            if (s->song[i] < 0) continue;
            k = snprintf(buf ? buf + n : NULL, (size_t)n < cap ? cap - (size_t)n : 0, "%s%s%d = %s\n",
                         head ? "" : "\n[jukebox]              # play status of song n of the music bank:\n"
                                     "                       # off, racing, hot_pursuit, menu, menu_racing, always\n",
                         "song", i + 1, hp2_play_key((hp2_play)s->song[i]));
            if (k < 0) return 0;
            n += k;
            head = 1;
        }
    }
    return (size_t)n;
}

/* Apply a single assignment without disturbing the other fields. Done by
 * serialising what we have, appending the override, and re-parsing, so there
 * is exactly one place that knows the key names. */
int hp2_settings_set(hp2_settings *s, const char *assignment)
{
    char buf[8192];   /* the whole file, with every jukebox song set: ~3.1 KB */
    size_t n;
    hp2_settings before;

    if (!s || !assignment || !strchr(assignment, '=')) return 0;
    before = *s;
    n = hp2_settings_write(s, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return 0;
    if (n + strlen(assignment) + 2 > sizeof(buf)) return 0;
    buf[n] = 0;
    strcat(buf, assignment);
    strcat(buf, "\n");
    hp2_settings_parse(s, buf);

    /* the key counts as recognised only if it actually changed something or
     * the value matched what was already set */
    if (memcmp(&before, s, sizeof(before)) != 0) return 1;
    {   /* unchanged: decide whether the key was known at all */
        hp2_settings probe;
        char one[256];
        snprintf(one, sizeof(one), "%s\n", assignment);
        hp2_settings_defaults(&probe);
        return hp2_settings_parse(&probe, one) > 0;
    }
}

/* ------------------------------------------------------------- load / save */
int hp2_settings_load(hp2_settings *s, const char *path)
{
    FILE *f;
    long len;
    char *buf;

    if (!s) return 0;
    hp2_settings_defaults(s);
    if (!path) return 0;

    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    len = ftell(f);
    if (len < 0 || len > (1 << 20)) { fclose(f); return 0; }
    rewind(f);

    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return 0; }
    len = (long)fread(buf, 1, (size_t)len, f);
    buf[len] = 0;
    fclose(f);

    hp2_settings_parse(s, buf);
    free(buf);
    return 1;
}

int hp2_settings_save(const hp2_settings *s, const char *path)
{
    char buf[8192];   /* the whole file, with every jukebox song set: ~3.1 KB */
    size_t need;
    FILE *f;

    if (!s || !path) return 0;
    need = hp2_settings_write(s, buf, sizeof(buf));
    if (need == 0 || need >= sizeof(buf)) return 0;   /* refuse a truncated write */

    f = fopen(path, "wb");
    if (!f) return 0;
    if (fwrite(buf, 1, need, f) != need) { fclose(f); return 0; }
    return (fclose(f) == 0);
}
