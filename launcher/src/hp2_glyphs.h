/* hp2_glyphs - what to call each button, per controller family.
 *
 * Also SDL-free, so it can be unit tested and so the game's menus can ask
 * "what is this action called on the pad in the player's hands" without
 * pulling in the input backend.
 *
 * The problem it solves: the same physical button is called A on an Xbox pad,
 * Cross on a PlayStation pad, and B on a Nintendo pad, because Nintendo lays
 * its face buttons out the other way round. A game that hard-codes "press A"
 * is wrong for two thirds of pads.
 */
#ifndef HP2_GLYPHS_H
#define HP2_GLYPHS_H

#include "hp2_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* hp2_button and hp2_action live in hp2_settings.h (the settings file stores
 * bindings). */

/* The label for a button on a given family, e.g. (SOUTH, PLAYSTATION) ->
 * "Cross". Never NULL. HP2_GLYPHS_AUTO is treated as GENERIC, because by the
 * time anything asks for a label the auto choice has been resolved. */
const char *hp2_button_label(hp2_button b, hp2_glyph_set set);

/* A human name for an action, e.g. "Handbrake". Never NULL. */
const char *hp2_action_name(hp2_action a);

/* Which button an action sits on by default, under a control scheme. Modern
 * puts throttle and brake on the triggers; classic keeps them on the face
 * buttons where the PS2 release had them. */
hp2_button hp2_action_button(hp2_action a, hp2_scheme scheme);

/* The button an action is bound to: the player's remap if any, else the
 * scheme's default. */
hp2_button hp2_bound_button(const hp2_settings *s, hp2_action a);

/* Binds action a to button b. An action already on b takes a's old button
 * (a swap), so no two actions ever share a button and none is left without
 * one. Returns 1 if anything changed. */
int hp2_bind(hp2_settings *s, hp2_action a, hp2_button b);

/* The action on button b, or -1. */
int hp2_action_on(const hp2_settings *s, hp2_button b);

/* "Accelerate: Right Trigger" for the pad in hand. Writes into buf and
 * returns buf, so it can be used inline. */
const char *hp2_action_prompt(hp2_action a, hp2_scheme scheme,
                              hp2_glyph_set set, char *buf, int cap);

#ifdef __cplusplus
}
#endif
#endif /* HP2_GLYPHS_H */
