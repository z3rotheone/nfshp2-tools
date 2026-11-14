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

/* Physical positions, named the way SDL does: by location, not by letter. */
typedef enum {
    HP2_BTN_SOUTH = 0,   /* bottom face button */
    HP2_BTN_EAST,        /* right face button  */
    HP2_BTN_WEST,        /* left face button   */
    HP2_BTN_NORTH,       /* top face button    */
    HP2_BTN_LSHOULDER,
    HP2_BTN_RSHOULDER,
    HP2_BTN_LTRIGGER,
    HP2_BTN_RTRIGGER,
    HP2_BTN_START,
    HP2_BTN_SELECT,
    HP2_BTN_COUNT
} hp2_button;

/* The actions a Hot Pursuit 2 port needs to bind. */
typedef enum {
    HP2_ACT_ACCELERATE = 0,
    HP2_ACT_BRAKE,
    HP2_ACT_HANDBRAKE,
    HP2_ACT_SHIFT_UP,
    HP2_ACT_SHIFT_DOWN,
    HP2_ACT_LOOK_BACK,
    HP2_ACT_CAMERA,
    HP2_ACT_SIREN,
    HP2_ACT_PAUSE,
    HP2_ACT_COUNT
} hp2_action;

/* The label for a button on a given family, e.g. (SOUTH, PLAYSTATION) ->
 * "Cross". Never NULL. HP2_GLYPHS_AUTO is treated as GENERIC, because by the
 * time anything asks for a label the auto choice has been resolved. */
const char *hp2_button_label(hp2_button b, hp2_glyph_set set);

/* A human name for an action, e.g. "Handbrake". Never NULL. */
const char *hp2_action_name(hp2_action a);

/* Which button an action sits on, under a control scheme. Modern puts
 * throttle and brake on the triggers; classic keeps them on the face buttons
 * where the PS2 release had them. */
hp2_button hp2_action_button(hp2_action a, hp2_scheme scheme);

/* "Accelerate: Right Trigger" for the pad in hand. Writes into buf and
 * returns buf, so it can be used inline. */
const char *hp2_action_prompt(hp2_action a, hp2_scheme scheme,
                              hp2_glyph_set set, char *buf, int cap);

#ifdef __cplusplus
}
#endif
#endif /* HP2_GLYPHS_H */
