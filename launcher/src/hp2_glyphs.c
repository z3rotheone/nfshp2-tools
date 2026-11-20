#include "hp2_glyphs.h"

#include <stdio.h>

/* Labels by physical position. The Nintendo column is the interesting one:
 * its south button is B and its east button is A, the opposite of Xbox, which
 * is exactly why binding by letter rather than by position goes wrong. */
static const char *LABEL[HP2_GLYPHS_COUNT][HP2_BTN_COUNT] = {
    /* AUTO - resolved before display; same as generic */
    { "Button 1", "Button 2", "Button 3", "Button 4",
      "L1", "R1", "L2", "R2", "Start", "Select",
      "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L Stick", "R Stick" },
    /* XBOX */
    { "A", "B", "X", "Y",
      "LB", "RB", "LT", "RT", "Menu", "View",
      "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "LS", "RS" },
    /* PLAYSTATION */
    { "Cross", "Circle", "Square", "Triangle",
      "L1", "R1", "L2", "R2", "Options", "Share",
      "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L3", "R3" },
    /* NINTENDO */
    { "B", "A", "Y", "X",
      "L", "R", "ZL", "ZR", "+", "-",
      "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L Stick", "R Stick" },
    /* GENERIC */
    { "Button 1", "Button 2", "Button 3", "Button 4",
      "L1", "R1", "L2", "R2", "Start", "Select",
      "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L Stick", "R Stick" },
    /* KEYBOARD */
    { "Space", "Ctrl", "Shift", "Tab",
      "Q", "E", "A", "D", "Enter", "Esc",
      "Up", "Down", "Left", "Right", "Z", "N" },
};

static const char *ACTION[HP2_ACT_COUNT] = {
    "Accelerate", "Brake", "Handbrake", "Shift Up", "Shift Down",
    "Look Back", "Camera", "Siren", "Pause", "Next Song"
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
        case HP2_ACT_NEXT_SONG:  return HP2_BTN_RSTICK;    /* free in both schemes */
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
    case HP2_ACT_NEXT_SONG:  return HP2_BTN_RSTICK;
    default:                 return HP2_BTN_SOUTH;
    }
}

hp2_button hp2_bound_button(const hp2_settings *s, hp2_action a)
{
    if (a < 0 || a >= HP2_ACT_COUNT) return HP2_BTN_SOUTH;
    if (s->bind[a] >= 0 && s->bind[a] < HP2_BTN_COUNT) return (hp2_button)s->bind[a];
    return hp2_action_button(a, s->scheme);
}

int hp2_action_on(const hp2_settings *s, hp2_button b)
{
    int a;
    for (a = 0; a < HP2_ACT_COUNT; a++)
        if (hp2_bound_button(s, (hp2_action)a) == b) return a;
    return -1;
}

int hp2_bind(hp2_settings *s, hp2_action a, hp2_button b)
{
    hp2_button old;
    int other;
    if (!s || a < 0 || a >= HP2_ACT_COUNT || b < 0 || b >= HP2_BTN_COUNT) return 0;
    old = hp2_bound_button(s, a);
    if (old == b) return 0;
    other = hp2_action_on(s, b);
    if (other >= 0) s->bind[other] = old;        /* swap: the other action takes a's old button */
    s->bind[a] = b;
    /* an action back on its scheme default needs no entry in the file */
    if (other >= 0 && s->bind[other] == (int)hp2_action_button((hp2_action)other, s->scheme)) s->bind[other] = -1;
    if (s->bind[a] == (int)hp2_action_button(a, s->scheme)) s->bind[a] = -1;
    return 1;
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
