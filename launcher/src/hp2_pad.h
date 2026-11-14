/* hp2_pad - controller identity and axis shaping, with no SDL dependency.
 *
 * The SDL layer translates SDL's own controller-type constants into
 * hp2_pad_kind, so nothing here depends on SDL's numbering and all of it can
 * be unit tested. This is where "auto glyph detection" actually lives.
 */
#ifndef HP2_PAD_H
#define HP2_PAD_H

#include "hp2_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP2_PAD_UNKNOWN = 0,
    HP2_PAD_XBOX360,
    HP2_PAD_XBOXONE,
    HP2_PAD_PS3,
    HP2_PAD_PS4,
    HP2_PAD_PS5,
    HP2_PAD_SWITCH_PRO,
    HP2_PAD_JOYCON,
    HP2_PAD_VIRTUAL,
    HP2_PAD_LUNA,
    HP2_PAD_STADIA,
    HP2_PAD_SHIELD,
    HP2_PAD_KEYBOARD,
    HP2_PAD_KIND_COUNT
} hp2_pad_kind;

/* Which glyph family a pad belongs to. Anything unrecognised becomes
 * GENERIC rather than guessing Xbox, so an unknown pad shows neutral
 * labels instead of confidently wrong ones. */
hp2_glyph_set hp2_pad_glyph_set(hp2_pad_kind kind);

/* A readable name for a pad kind, e.g. "DualSense". Never NULL. */
const char *hp2_pad_kind_name(hp2_pad_kind kind);

/* Resolve the setting against the pad actually in hand: anything but AUTO is
 * the player's explicit choice and wins; AUTO asks the pad. */
hp2_glyph_set hp2_resolve_glyphs(hp2_glyph_set setting, hp2_pad_kind kind);

/* Axis shaping ----------------------------------------------------------
 * raw is SDL's -32768..32767. Returns -1..1 with the dead zone removed and
 * the remaining travel rescaled, so the stick still reaches full tilt
 * rather than being clipped short by the dead zone. */
float hp2_axis_value(int raw, float deadzone);

/* Same for a trigger, whose raw range is 0..32767 and whose result is 0..1. */
float hp2_trigger_value(int raw, float deadzone);

#ifdef __cplusplus
}
#endif
#endif /* HP2_PAD_H */
