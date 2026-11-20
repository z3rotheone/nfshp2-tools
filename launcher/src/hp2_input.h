/* hp2_input - the SDL2 side of the input layer.
 *
 * Everything that needs SDL lives here. The identification and shaping logic
 * it relies on is in hp2_pad.c, which has no SDL dependency and is unit
 * tested; this file is the adapter between the two.
 */
#ifndef HP2_INPUT_H
#define HP2_INPUT_H

#include "hp2_settings.h"
#include "hp2_glyphs.h"
#include "hp2_pad.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp2_input hp2_input;

/* The live state of every action, refreshed by hp2_input_update().
 * Throttle and brake are analog under the modern scheme and 0/1 under
 * classic, so the game can read one field either way. */
typedef struct {
    float steer;                    /* -1 left .. +1 right */
    float throttle;                 /* 0 .. 1 */
    float brake;                    /* 0 .. 1 */
    int   pressed[HP2_ACT_COUNT];   /* 1 while held */
    int   just_pressed[HP2_ACT_COUNT];
} hp2_input_state;

/* Starts SDL's game controller subsystem and opens the first pad it finds.
 * Returns NULL on failure, with the reason in hp2_input_error(). */
hp2_input *hp2_input_create(const hp2_settings *settings);
void       hp2_input_destroy(hp2_input *in);
const char *hp2_input_error(void);

/* Pump SDL events and refresh the state. Returns 0 if the user asked to quit
 * (window closed), 1 otherwise. Handles pads being plugged in and unplugged
 * mid-session, re-detecting glyphs when that happens. */
int hp2_input_update(hp2_input *in, hp2_input_state *out);

/* Re-read the settings, for when the player changes them in a menu. */
void hp2_input_apply_settings(hp2_input *in, const hp2_settings *settings);

/* What is connected now. kind is HP2_PAD_KEYBOARD when no pad is present. */
hp2_pad_kind  hp2_input_pad_kind(const hp2_input *in);
const char   *hp2_input_pad_name(const hp2_input *in);
hp2_glyph_set hp2_input_glyph_set(const hp2_input *in);  /* already resolved */

/* The kind of an SDL_GameController (passed as void * so this header needs
 * no SDL); HP2_PAD_KEYBOARD for NULL. */
hp2_pad_kind hp2_input_kind_of(void *sdl_game_controller);

/* The hp2_button an SDL controller button is, or -1 (SDL's button number). */
int hp2_input_button_of(int sdl_button);

/* Brief rumble, ignored when the setting is off or the pad cannot. */
void hp2_input_rumble(hp2_input *in, float strength, int ms);

#ifdef __cplusplus
}
#endif
#endif /* HP2_INPUT_H */
