#include "hp2_pad.h"

static const char *KIND_NAME[HP2_PAD_KIND_COUNT] = {
    "Unknown controller",
    "Xbox 360 Controller",
    "Xbox Controller",
    "DualShock 3",
    "DualShock 4",
    "DualSense",
    "Switch Pro Controller",
    "Joy-Con",
    "Virtual controller",
    "Luna Controller",
    "Stadia Controller",
    "SHIELD Controller",
    "Keyboard",
};

const char *hp2_pad_kind_name(hp2_pad_kind kind)
{
    if (kind < 0 || kind >= HP2_PAD_KIND_COUNT) return KIND_NAME[HP2_PAD_UNKNOWN];
    return KIND_NAME[kind];
}

hp2_glyph_set hp2_pad_glyph_set(hp2_pad_kind kind)
{
    switch (kind) {
    case HP2_PAD_XBOX360:
    case HP2_PAD_XBOXONE:
    case HP2_PAD_SHIELD:       /* ships with an Xbox-style face layout */
        return HP2_GLYPHS_XBOX;

    case HP2_PAD_PS3:
    case HP2_PAD_PS4:
    case HP2_PAD_PS5:
        return HP2_GLYPHS_PLAYSTATION;

    case HP2_PAD_SWITCH_PRO:
    case HP2_PAD_JOYCON:
        return HP2_GLYPHS_NINTENDO;

    case HP2_PAD_KEYBOARD:
        return HP2_GLYPHS_KEYBOARD;

    /* Luna and Stadia use their own art; neutral labels beat wrong ones.
     * Virtual and Unknown likewise. */
    default:
        return HP2_GLYPHS_GENERIC;
    }
}

hp2_glyph_set hp2_resolve_glyphs(hp2_glyph_set setting, hp2_pad_kind kind)
{
    if (setting != HP2_GLYPHS_AUTO &&
        setting >= 0 && setting < HP2_GLYPHS_COUNT)
        return setting;                     /* the player chose; respect it */
    return hp2_pad_glyph_set(kind);
}

/* ------------------------------------------------------------ axis shaping */
static float shape(float mag, float deadzone)
{
    if (deadzone < 0.0f) deadzone = 0.0f;
    if (deadzone > 0.95f) deadzone = 0.95f;
    if (mag <= deadzone) return 0.0f;
    /* rescale the live part of the travel back to a full 0..1 */
    return (mag - deadzone) / (1.0f - deadzone);
}

float hp2_axis_value(int raw, float deadzone)
{
    float v, mag, out;
    if (raw > 32767) raw = 32767;
    if (raw < -32768) raw = -32768;
    /* divide by 32767 so full positive tilt reaches exactly 1.0; the one
     * extra step of negative range is clamped below */
    v = (float)raw / 32767.0f;
    if (v < -1.0f) v = -1.0f;
    mag = v < 0.0f ? -v : v;
    out = shape(mag, deadzone);
    return v < 0.0f ? -out : out;
}

float hp2_trigger_value(int raw, float deadzone)
{
    float v;
    if (raw < 0) raw = 0;
    if (raw > 32767) raw = 32767;
    v = (float)raw / 32767.0f;
    return shape(v, deadzone);
}
