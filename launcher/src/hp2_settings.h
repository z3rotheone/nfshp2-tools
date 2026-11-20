/* hp2_settings - settings file for a Hot Pursuit 2 port.
 *
 * Deliberately free of SDL and of any rendering code: the launcher and the
 * game both link this, so one file is read and written by both and the
 * settings stay identical in each. Plain C99, no allocation.
 *
 * The file is INI-like and meant to be hand-editable:
 *
 *     # hp2 settings
 *     [video]
 *     internal_width = 1920
 *     internal_height = 1080
 *     aspect = 16:9
 *     window = borderless
 *     vsync = on
 *     frame_cap = 60
 *     msaa = 4
 *     aniso = 8
 *     show_fps = off
 *     [input]
 *     scheme = modern
 *     glyphs = auto
 *     deadzone = 0.20
 *
 * Unknown keys are ignored rather than rejected, so a newer game can read an
 * older file and vice versa. Every value is clamped to something usable on
 * load, so a corrupt or hand-mangled file can never put the game in a state
 * it cannot start from.
 */
#ifndef HP2_SETTINGS_H
#define HP2_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP2_ASPECT_AUTO = 0,   /* follow the display */
    HP2_ASPECT_4_3,        /* as the PS2 original rendered */
    HP2_ASPECT_16_9,
    HP2_ASPECT_16_10,
    HP2_ASPECT_21_9,       /* ultrawide: 64:27, as 2560x1080 and 5120x2160 */
    HP2_ASPECT_COUNT
} hp2_aspect;

typedef enum {
    HP2_WINDOW_WINDOWED = 0,
    HP2_WINDOW_BORDERLESS,
    HP2_WINDOW_FULLSCREEN,
    HP2_WINDOW_COUNT
} hp2_window_mode;

typedef enum {
    HP2_SCHEME_MODERN = 0, /* analog throttle/brake on triggers */
    HP2_SCHEME_CLASSIC,    /* face buttons, as the PS2 release shipped */
    HP2_SCHEME_COUNT
} hp2_scheme;

typedef enum {
    HP2_GLYPHS_AUTO = 0,   /* detect from the connected pad */
    HP2_GLYPHS_XBOX,
    HP2_GLYPHS_PLAYSTATION,
    HP2_GLYPHS_NINTENDO,
    HP2_GLYPHS_GENERIC,
    HP2_GLYPHS_KEYBOARD,
    HP2_GLYPHS_COUNT
} hp2_glyph_set;

/* Limits. The internal resolution is what the game renders at; the window may
 * differ. The low bound is the PS2's own 512x448, so "render as the original
 * did, present at any size" is expressible. */
#define HP2_MIN_WIDTH   512
#define HP2_MIN_HEIGHT  448
#define HP2_MAX_WIDTH   7680
#define HP2_MAX_HEIGHT  4320

/* Frame cap in frames per second; 0 means no limit. */
#define HP2_MIN_FRAME_CAP  20
#define HP2_MAX_FRAME_CAP  1000

typedef struct {
    /* video */
    int             internal_width;
    int             internal_height;
    hp2_aspect      aspect;
    hp2_window_mode window;
    int             vsync;          /* 0 or 1 */
    int             frame_cap;      /* fps limit, 0 = none, else 20..1000 */
    int             msaa;           /* anti-aliasing samples: 0, 2, 4 or 8 */
    int             aniso;          /* anisotropic filtering: 1, 2, 4, 8, 16 */
    int             show_fps;       /* 0 or 1 */

    /* input */
    hp2_scheme      scheme;
    hp2_glyph_set   glyphs;
    float           deadzone;       /* 0.0 .. 0.9 */
    int             rumble;         /* 0 or 1 */
} hp2_settings;

/* Fill with the defaults: 1920x1080, 16:9, borderless, vsync on, 60 fps cap,
 * 4x MSAA, 8x anisotropic, modern controls, automatic glyphs. */
void hp2_settings_defaults(hp2_settings *s);

/* Clamp every field into range. Always leaves `s` usable. Returns the number
 * of fields it had to correct, so a caller can warn that a file was repaired. */
int hp2_settings_validate(hp2_settings *s);

/* Parse from a NUL-terminated buffer. Starts from the defaults, applies what
 * it recognises, then validates. Never fails: an empty or garbage buffer
 * yields the defaults. Returns the number of keys it understood. */
int hp2_settings_parse(hp2_settings *s, const char *text);

/* Render to `buf`. Returns the length that a complete write needs, in the
 * manner of snprintf, so a return >= cap means the output was truncated. */
size_t hp2_settings_write(const hp2_settings *s, char *buf, size_t cap);

/* Apply one "key=value" assignment on top of the current settings, using the
 * same spellings the file uses so the two can never drift apart. Returns 1 if
 * the key was recognised, 0 otherwise (and leaves `s` unchanged on 0). */
int hp2_settings_set(hp2_settings *s, const char *assignment);

/* Convenience wrappers over the two above. load() returns 1 if the file was
 * read, 0 if it was missing (leaving `s` at the defaults). save() returns 1
 * on success. */
int hp2_settings_load(hp2_settings *s, const char *path);
int hp2_settings_save(const hp2_settings *s, const char *path);

/* Names, for menus and for the file. Return a static string, never NULL. */
const char *hp2_aspect_name(hp2_aspect a);
const char *hp2_window_name(hp2_window_mode w);
const char *hp2_scheme_name(hp2_scheme c);
const char *hp2_glyph_set_name(hp2_glyph_set g);

/* The aspect as a ratio, or 0 for AUTO (meaning: ask the display). */
float hp2_aspect_ratio(hp2_aspect a);

#ifdef __cplusplus
}
#endif
#endif /* HP2_SETTINGS_H */
