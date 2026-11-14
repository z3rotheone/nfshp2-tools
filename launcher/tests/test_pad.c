/* Tests for controller identification and axis shaping. */
#include "../src/hp2_pad.h"
#include "../src/hp2_glyphs.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                  \
    do { checks++; if (!(cond)) { failures++;             \
        printf("  FAIL %s:%d  ", __FILE__, __LINE__);     \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_families(void)
{
    printf("controller families\n");
    CHECK(hp2_pad_glyph_set(HP2_PAD_XBOX360) == HP2_GLYPHS_XBOX, "360");
    CHECK(hp2_pad_glyph_set(HP2_PAD_XBOXONE) == HP2_GLYPHS_XBOX, "xbox one");
    CHECK(hp2_pad_glyph_set(HP2_PAD_PS3) == HP2_GLYPHS_PLAYSTATION, "ds3");
    CHECK(hp2_pad_glyph_set(HP2_PAD_PS4) == HP2_GLYPHS_PLAYSTATION, "ds4");
    CHECK(hp2_pad_glyph_set(HP2_PAD_PS5) == HP2_GLYPHS_PLAYSTATION, "dualsense");
    CHECK(hp2_pad_glyph_set(HP2_PAD_SWITCH_PRO) == HP2_GLYPHS_NINTENDO, "switch pro");
    CHECK(hp2_pad_glyph_set(HP2_PAD_JOYCON) == HP2_GLYPHS_NINTENDO, "joycon");
    CHECK(hp2_pad_glyph_set(HP2_PAD_KEYBOARD) == HP2_GLYPHS_KEYBOARD, "keyboard");

    /* unrecognised hardware must fall back to neutral, never to a guess */
    CHECK(hp2_pad_glyph_set(HP2_PAD_UNKNOWN) == HP2_GLYPHS_GENERIC, "unknown");
    CHECK(hp2_pad_glyph_set(HP2_PAD_VIRTUAL) == HP2_GLYPHS_GENERIC, "virtual");
    CHECK(hp2_pad_glyph_set(HP2_PAD_LUNA) == HP2_GLYPHS_GENERIC, "luna");
    CHECK(hp2_pad_glyph_set(HP2_PAD_STADIA) == HP2_GLYPHS_GENERIC, "stadia");
    CHECK(hp2_pad_glyph_set((hp2_pad_kind)999) == HP2_GLYPHS_GENERIC, "out of range");
    CHECK(hp2_pad_glyph_set((hp2_pad_kind)-5) == HP2_GLYPHS_GENERIC, "negative");

    /* every kind must be named */
    {
        int k;
        for (k = 0; k < HP2_PAD_KIND_COUNT; k++) {
            const char *n = hp2_pad_kind_name((hp2_pad_kind)k);
            CHECK(n && *n, "kind %d unnamed", k);
        }
        CHECK(hp2_pad_kind_name((hp2_pad_kind)-1) != NULL, "bad kind still names");
    }
}

static void test_resolve(void)
{
    printf("auto versus explicit choice\n");
    /* AUTO follows the pad */
    CHECK(hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_PS5) == HP2_GLYPHS_PLAYSTATION, "auto + ps5");
    CHECK(hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_SWITCH_PRO) == HP2_GLYPHS_NINTENDO, "auto + switch");
    CHECK(hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_UNKNOWN) == HP2_GLYPHS_GENERIC, "auto + unknown");
    /* an explicit choice overrides the hardware */
    CHECK(hp2_resolve_glyphs(HP2_GLYPHS_XBOX, HP2_PAD_PS5) == HP2_GLYPHS_XBOX, "explicit wins");
    CHECK(hp2_resolve_glyphs(HP2_GLYPHS_NINTENDO, HP2_PAD_XBOX360) == HP2_GLYPHS_NINTENDO, "explicit wins 2");
}

static void test_axis(void)
{
    float v;
    printf("stick shaping\n");

    CHECK(hp2_axis_value(0, 0.2f) == 0.0f, "centre is zero");
    CHECK(hp2_axis_value(3000, 0.2f) == 0.0f, "inside the dead zone is zero");
    CHECK(hp2_axis_value(-3000, 0.2f) == 0.0f, "inside the dead zone, negative");

    v = hp2_axis_value(32767, 0.2f);
    CHECK(fabs(v - 1.0f) < 1e-5, "full right should reach 1.0, got %f", v);
    v = hp2_axis_value(-32768, 0.2f);
    CHECK(fabs(v + 1.0f) < 1e-5, "full left should reach -1.0, got %f", v);

    /* just past the dead zone should be a small value, not a jump to full */
    v = hp2_axis_value((int)(32767 * 0.25f), 0.2f);
    CHECK(v > 0.0f && v < 0.15f, "just past the dead zone should be small, got %f", v);

    /* halfway in raw terms, with a 0.2 dead zone, is (0.5-0.2)/0.8 = 0.375 */
    v = hp2_axis_value((int)(32767 * 0.5f), 0.2f);
    CHECK(fabs(v - 0.375f) < 0.01f, "rescaling should be linear, got %f", v);

    /* monotonic and symmetric */
    {
        int i; float prev = -2.0f;
        for (i = -32768; i <= 32767; i += 257) {
            float a = hp2_axis_value(i, 0.15f);
            CHECK(a >= prev - 1e-6, "should never decrease at raw %d", i);
            CHECK(a >= -1.0001f && a <= 1.0001f, "out of range at raw %d: %f", i, a);
            prev = a;
        }
        for (i = 1; i < 32767; i += 1013) {
            float a = hp2_axis_value(i, 0.3f), b = hp2_axis_value(-i, 0.3f);
            CHECK(fabs(a + b) < 1e-5, "should be symmetric at %d: %f vs %f", i, a, b);
        }
    }

    /* a zero dead zone must pass values straight through */
    v = hp2_axis_value((int)(32767 * 0.5f), 0.0f);
    CHECK(fabs(v - 0.5f) < 0.01f, "no dead zone should be linear, got %f", v);

    /* nonsense dead zones must not produce nonsense output */
    CHECK(hp2_axis_value(32767, -1.0f) <= 1.0f, "negative dead zone");
    CHECK(hp2_axis_value(32767, 5.0f) <= 1.0f, "huge dead zone");
    CHECK(hp2_axis_value(99999, 0.2f) <= 1.0f, "raw above range clamped");
    CHECK(hp2_axis_value(-99999, 0.2f) >= -1.0f, "raw below range clamped");
}

static void test_trigger(void)
{
    float v;
    printf("trigger shaping\n");
    CHECK(hp2_trigger_value(0, 0.1f) == 0.0f, "released is zero");
    CHECK(hp2_trigger_value(-500, 0.1f) == 0.0f, "negative clamps to zero");
    v = hp2_trigger_value(32767, 0.1f);
    CHECK(fabs(v - 1.0f) < 1e-5, "fully pressed should reach 1.0, got %f", v);
    v = hp2_trigger_value((int)(32767 * 0.55f), 0.1f);
    CHECK(v > 0.4f && v < 0.6f, "half press should be near half, got %f", v);
    {
        int i; float prev = -1.0f;
        for (i = 0; i <= 32767; i += 311) {
            float a = hp2_trigger_value(i, 0.12f);
            CHECK(a >= prev - 1e-6, "trigger should never decrease at %d", i);
            CHECK(a >= 0.0f && a <= 1.0f, "trigger out of range at %d: %f", i, a);
            prev = a;
        }
    }
}

static void test_prompts_end_to_end(void)
{
    char buf[64];
    printf("prompts for a detected pad\n");
    /* a DualSense on auto, modern scheme: accelerate is the right trigger,
     * which PlayStation calls R2 */
    hp2_action_prompt(HP2_ACT_ACCELERATE, HP2_SCHEME_MODERN,
                      hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_PS5),
                      buf, sizeof(buf));
    CHECK(!strcmp(buf, "Accelerate: R2"), "got \"%s\"", buf);

    /* the same action on a Switch Pro pad is ZR */
    hp2_action_prompt(HP2_ACT_ACCELERATE, HP2_SCHEME_MODERN,
                      hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_SWITCH_PRO),
                      buf, sizeof(buf));
    CHECK(!strcmp(buf, "Accelerate: ZR"), "got \"%s\"", buf);

    /* handbrake on classic is the east button: Circle on PlayStation */
    hp2_action_prompt(HP2_ACT_HANDBRAKE, HP2_SCHEME_CLASSIC,
                      hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_PS4),
                      buf, sizeof(buf));
    CHECK(!strcmp(buf, "Handbrake: Circle"), "got \"%s\"", buf);

    /* the same on a Switch pad is A, because its east button is A */
    hp2_action_prompt(HP2_ACT_HANDBRAKE, HP2_SCHEME_CLASSIC,
                      hp2_resolve_glyphs(HP2_GLYPHS_AUTO, HP2_PAD_JOYCON),
                      buf, sizeof(buf));
    CHECK(!strcmp(buf, "Handbrake: A"), "got \"%s\"", buf);
}

int main(void)
{
    test_families();
    test_resolve();
    test_axis();
    test_trigger();
    test_prompts_end_to_end();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
