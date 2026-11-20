/* hp2_menu - the settings as rows of an options menu.
 *
 * The launcher and the in-game Options screen show the same list, so this
 * module owns it: which rows exist, what each is called, what its current
 * value reads as, and what left/right does to it. A front end (SDL window,
 * the game's own menu, a test) only draws the rows and forwards the input.
 * No SDL, no allocation, plain C99.
 *
 *     for (i = 0; i < hp2_menu_row_count(); i++) {
 *         char v[HP2_MENU_VALUE];
 *         hp2_menu_value(&cfg, i, v, sizeof v);
 *         draw(hp2_menu_section(i), hp2_menu_label(i), v);
 *     }
 *     if (pressed_right) hp2_menu_adjust(&cfg, row, +1);
 *
 * hp2_menu_adjust only edits the struct; the caller decides when to save and
 * which changes need a restart (hp2_menu_needs_restart says which).
 */
#ifndef HP2_MENU_H
#define HP2_MENU_H

#include "hp2_settings.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_MENU_VALUE 32     /* room for any row's value text */

typedef enum {
    HP2_ROW_INTERNAL_RES = 0,
    HP2_ROW_ASPECT,
    HP2_ROW_WINDOW,
    HP2_ROW_VSYNC,
    HP2_ROW_FRAME_CAP,
    HP2_ROW_MSAA,
    HP2_ROW_ANISO,
    HP2_ROW_SHOW_FPS,
    HP2_ROW_SCHEME,
    HP2_ROW_GLYPHS,
    HP2_ROW_DEADZONE,
    HP2_ROW_RUMBLE,
    HP2_ROW_VOL_MENU_MUSIC,     /* Audio: the game's own rows, in its order */
    HP2_ROW_VOL_RACE_MUSIC,
    HP2_ROW_VOL_AMBIENT,
    HP2_ROW_VOL_TIRES,
    HP2_ROW_VOL_ENGINE,
    HP2_ROW_VOL_MENU_SFX,
    HP2_ROW_VOL_SPEECH,
    HP2_ROW_AUDIO_MODE,
    HP2_ROW_DEFAULT_VOLUMES,
    HP2_ROW_COUNT
} hp2_row;

typedef enum {
    HP2_KIND_CHOICE,   /* a list of values; left/right steps through it */
    HP2_KIND_TOGGLE,   /* on/off; either direction flips it */
    HP2_KIND_SLIDER,   /* a number in steps */
    HP2_KIND_ACTION    /* does something when accepted; no value */
} hp2_row_kind;

int          hp2_menu_row_count(void);
const char  *hp2_menu_section(int row);   /* "Video", "Controls" or "Audio" */
const char  *hp2_menu_label(int row);     /* "Internal resolution" ... */
const char  *hp2_menu_help(int row);      /* one line for the help bar */
hp2_row_kind hp2_menu_kind(int row);

/* The value as the menu shows it ("1920 x 1080", "60 fps", "Off", "4x"). */
void hp2_menu_value(const hp2_settings *s, int row, char *buf, size_t cap);

/* Step the row one notch: dir > 0 right, dir < 0 left. Lists that run out
 * stop at their end; enumerations wrap. Returns 1 if the setting changed. */
int hp2_menu_adjust(hp2_settings *s, int row, int dir);

/* An ACTION row's deed (Default Volumes: put the volumes back). Returns 1 if
 * a setting changed; 0 for other rows. */
int hp2_menu_activate(hp2_settings *s, int row);

/* Where a volume row's slider stands, 0 (mute) .. 1 (full); -1 for rows
 * that show their value as text. */
float hp2_menu_fraction(const hp2_settings *s, int row);

/* 1 if changing this row only takes effect after the game restarts. */
int hp2_menu_needs_restart(int row);

#ifdef __cplusplus
}
#endif
#endif /* HP2_MENU_H */
