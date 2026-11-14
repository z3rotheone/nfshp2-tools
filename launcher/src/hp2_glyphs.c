#include "hp2_glyphs.h"

#include <stdio.h>

/* Labels by physical position. The Nintendo column is the interesting one:
 * its south button is B and its east button is A, the opposite of Xbox, which
 * is exactly why binding by letter rather than by position goes wrong. */
static const char *LABEL[HP2_GLYPHS_COUNT][HP2_BTN_COUNT] = {
    /* AUTO - resolved before display; same as generic */
    { "Button 1", "Button 2", "Button 3", "Button 4",
      "L1", "R1", "L2", "R2", "Start", "Select" },
    /* XBOX */
    { "A", "B", "X", "Y",
      "LB", "RB", "LT", "RT", "Menu", "View" },
    /* PLAYSTATION */
    { "Cross", "Circle", "Square", "Triangle",
      "L1", "R1", "L2", "R2", "Options", "Share" },
    /* NINTENDO */
    { "B", "A", "Y", "X",
      "L", "R", "ZL", "ZR", "+", "-" },
    /* GENERIC */
    { "Button 1", "Button 2", "Button 3", "Button 4",
      "L1", "R1", "L2", "R2", "Start", "Select" },
    /* KEYBOARD */
    { "Space", "Ctrl", "Shift", "Tab",
      "Q", "E", "A", "D", "Enter", "Esc" },
};

static const char *ACTION[HP2_ACT_COUNT] = {
    "Accelerate", "Brake", "Handbrake", "Shift Up", "Shift Down",
    "Look Back", "Camera", "Siren", "Pause"
};

const char *hp2_button_label(hp2_button b, hp2_glyph_set set)
{
    if (b < 0 || b >= HP2_BTN_COUNT) return "?";
    if (set < 0 || set >= HP2_GLYPHS_COUNT) set = HP2_GLYPHS_GENERIC;
    return LABEL[set][b];
}

const char *hp2_action_name(hp2_action a)
{
    return (a >= 0 && a < HP2_ACT_COUNT) ? ACTION[a] : "?";
}

hp2_button hp2_action_button(hp2_action a, hp2_scheme scheme)
{
    if (scheme == HP2_SCHEME_CLASSIC) {
        switch (a) {
        case HP2_ACT_ACCELERATE: return HP2_BTN_SOUTH;     /* Cross on the PS2 pad */
        case HP2_ACT_BRAKE:      return HP2_BTN_WEST;      /* Square */
        case HP2_ACT_HANDBRAKE:  return HP2_BTN_EAST;      /* Circle */
        case HP2_ACT_SHIFT_UP:   return HP2_BTN_RSHOULDER;
        case HP2_ACT_SHIFT_DOWN: return HP2_BTN_LSHOULDER;
        case HP2_ACT_LOOK_BACK:  return HP2_BTN_NORTH;
        case HP2_ACT_CAMERA:     return HP2_BTN_SELECT;
        case HP2_ACT_SIREN:      return HP2_BTN_LTRIGGER;
        case HP2_ACT_PAUSE:      return HP2_BTN_START;
        default:                 return HP2_BTN_SOUTH;
        }
    }
    /* modern: analog throttle and brake where a driver expects them */
    switch (a) {
    case HP2_ACT_ACCELERATE: return HP2_BTN_RTRIGGER;
    case HP2_ACT_BRAKE:      return HP2_BTN_LTRIGGER;
    case HP2_ACT_HANDBRAKE:  return HP2_BTN_SOUTH;
    case HP2_ACT_SHIFT_UP:   return HP2_BTN_RSHOULDER;
    case HP2_ACT_SHIFT_DOWN: return HP2_BTN_LSHOULDER;
    case HP2_ACT_LOOK_BACK:  return HP2_BTN_WEST;
    case HP2_ACT_CAMERA:     return HP2_BTN_NORTH;
    case HP2_ACT_SIREN:      return HP2_BTN_EAST;
    case HP2_ACT_PAUSE:      return HP2_BTN_START;
    default:                 return HP2_BTN_SOUTH;
    }
}

const char *hp2_action_prompt(hp2_action a, hp2_scheme scheme,
                              hp2_glyph_set set, char *buf, int cap)
{
    if (!buf || cap <= 0) return "";
    snprintf(buf, (size_t)cap, "%s: %s",
             hp2_action_name(a),
             hp2_button_label(hp2_action_button(a, scheme), set));
    return buf;
}
