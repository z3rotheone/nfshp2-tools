/* Tests for the options menu model and the video settings it drives:
 * frame cap, anti-aliasing, anisotropic filtering, show FPS. */
#include "../src/hp2_menu.h"

#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        checks++;                                                   \
        if (!(cond)) {                                              \
            failures++;                                             \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);           \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
        }                                                           \
    } while (0)

static const char *val(const hp2_settings *s, int row)
{
    static char buf[HP2_MENU_VALUE];
    hp2_menu_value(s, row, buf, sizeof buf);
    return buf;
}

static void test_rows(void)
{
    hp2_settings s;
    int i;
    printf("rows\n");
    hp2_settings_defaults(&s);
    CHECK(hp2_menu_row_count() == HP2_ROW_COUNT, "row count");
    for (i = 0; i < hp2_menu_row_count(); i++) {
        CHECK(hp2_menu_label(i)[0] != 0, "row %d has a label", i);
        CHECK(hp2_menu_section(i)[0] != 0, "row %d has a section", i);
        CHECK(hp2_menu_help(i)[0] != 0 && strlen(hp2_menu_help(i)) < 60, "row %d has a short help line", i);
        if (hp2_menu_kind(i) == HP2_KIND_ACTION)
            CHECK(val(&s, i)[0] == 0, "button row %d (%s) has no value", i, hp2_menu_label(i));
        else
            CHECK(val(&s, i)[0] != 0, "row %d (%s) has a value", i, hp2_menu_label(i));
    }
    CHECK(!strcmp(hp2_menu_label(-1), ""), "bad row has an empty label");
    CHECK(!strcmp(hp2_menu_label(99), ""), "bad row has an empty label");
    CHECK(hp2_menu_adjust(&s, 99, 1) == 0, "bad row adjusts nothing");
    CHECK(hp2_menu_adjust(NULL, 0, 1) == 0, "NULL settings");
    CHECK(hp2_menu_adjust(&s, 0, 0) == 0, "dir 0 does nothing");
}

static void test_default_text(void)
{
    hp2_settings s;
    printf("default text\n");
    hp2_settings_defaults(&s);
    CHECK(!strcmp(val(&s, HP2_ROW_INTERNAL_RES), "1920 x 1080"), "res '%s'", val(&s, HP2_ROW_INTERNAL_RES));
    CHECK(!strcmp(val(&s, HP2_ROW_ASPECT), "16:9"), "aspect");
    CHECK(!strcmp(val(&s, HP2_ROW_WINDOW), "Borderless"), "window");
    CHECK(!strcmp(val(&s, HP2_ROW_VSYNC), "On"), "vsync");
    CHECK(!strcmp(val(&s, HP2_ROW_FRAME_CAP), "60 fps"), "cap '%s'", val(&s, HP2_ROW_FRAME_CAP));
    CHECK(!strcmp(val(&s, HP2_ROW_MSAA), "4x"), "msaa");
    CHECK(!strcmp(val(&s, HP2_ROW_ANISO), "8x"), "aniso");
    CHECK(!strcmp(val(&s, HP2_ROW_SHOW_FPS), "Off"), "show fps");
    CHECK(!strcmp(val(&s, HP2_ROW_DEADZONE), "20%"), "dead zone '%s'", val(&s, HP2_ROW_DEADZONE));
    s.frame_cap = 0;
    CHECK(!strcmp(val(&s, HP2_ROW_FRAME_CAP), "No limit"), "no limit text");
    s.internal_width = 640; s.internal_height = 448;
    CHECK(!strcmp(val(&s, HP2_ROW_INTERNAL_RES), "640 x 448 (PS2)"), "PS2 res '%s'", val(&s, HP2_ROW_INTERNAL_RES));
    s.internal_width = 1600; s.internal_height = 900;
    CHECK(!strcmp(val(&s, HP2_ROW_INTERNAL_RES), "1600 x 900"), "custom res");
}

static void test_frame_cap(void)
{
    hp2_settings s;
    printf("frame cap\n");
    hp2_settings_defaults(&s);
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, +1) == 1 && s.frame_cap == 72, "60 -> 72, got %d", s.frame_cap);
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, -1) == 1 && s.frame_cap == 60, "72 -> 60");
    s.frame_cap = 240;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, +1) == 1 && s.frame_cap == 0, "240 -> no limit");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, +1) == 0 && s.frame_cap == 0, "no limit is the end");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, -1) == 1 && s.frame_cap == 240, "no limit -> 240");
    s.frame_cap = 30;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, -1) == 0 && s.frame_cap == 30, "30 is the low end");
    s.frame_cap = 75;                 /* hand-edited value between steps */
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, +1) == 1 && s.frame_cap == 90, "75 up -> 90, got %d", s.frame_cap);
    s.frame_cap = 75;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, -1) == 1 && s.frame_cap == 72, "75 down -> 72, got %d", s.frame_cap);
    s.frame_cap = 500;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, +1) == 1 && s.frame_cap == 0, "500 up -> no limit, got %d", s.frame_cap);
    s.frame_cap = 500;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_FRAME_CAP, -1) == 1 && s.frame_cap == 240, "500 down -> 240, got %d", s.frame_cap);
}

static void test_lists(void)
{
    hp2_settings s;
    printf("anti-aliasing, anisotropic, resolution\n");
    hp2_settings_defaults(&s);
    CHECK(hp2_menu_adjust(&s, HP2_ROW_MSAA, +1) == 1 && s.msaa == 8, "msaa 4 -> 8");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_MSAA, +1) == 0, "msaa stops at 8");
    s.msaa = 0;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_MSAA, -1) == 0, "msaa stops at off");
    CHECK(hp2_menu_needs_restart(HP2_ROW_MSAA) == 1, "msaa needs a restart");
    CHECK(hp2_menu_needs_restart(HP2_ROW_VSYNC) == 0, "vsync does not");

    s.aniso = 16;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ANISO, +1) == 0, "aniso stops at 16");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ANISO, -1) == 1 && s.aniso == 8, "aniso 16 -> 8");
    s.aniso = 1;
    CHECK(!strcmp(val(&s, HP2_ROW_ANISO), "Off"), "aniso 1 reads Off");

    hp2_settings_defaults(&s);
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 1 && s.internal_width == 2560 && s.internal_height == 1440, "1080p -> 1440p");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 1 && s.internal_height == 2160, "-> 4K");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 0, "4K is the top");
    s.internal_width = 640; s.internal_height = 448;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, -1) == 0, "PS2 res is the bottom");
    s.internal_width = 1600; s.internal_height = 900;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 1 && s.internal_height == 1080, "900p up -> 1080p");
    s.internal_width = 1600; s.internal_height = 900;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, -1) == 1 && s.internal_height == 720, "900p down -> 720p");

    /* 21:9: ultrawide presets, and the resolution follows the aspect */
    hp2_settings_defaults(&s);
    s.aspect = HP2_ASPECT_16_10;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1) == 1 && s.aspect == HP2_ASPECT_21_9, "to 21:9");
    CHECK(s.internal_width == 2560 && s.internal_height == 1080, "1920x1080 -> 2560x1080, got %dx%d", s.internal_width, s.internal_height);
    CHECK(!strcmp(val(&s, HP2_ROW_INTERNAL_RES), "2560 x 1080"), "res '%s'", val(&s, HP2_ROW_INTERNAL_RES));
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 1 && s.internal_width == 3440 && s.internal_height == 1440, "-> 3440x1440");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 1 && s.internal_width == 5120 && s.internal_height == 2160, "-> 5120x2160");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, +1) == 0, "5120x2160 is the top");
    s.internal_width = 2560; s.internal_height = 1080;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, -1) == 1 && s.internal_width == 1720 && s.internal_height == 720, "-> 1720x720");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_INTERNAL_RES, -1) == 1 && s.internal_width == 640, "-> PS2");
    CHECK(!strcmp(val(&s, HP2_ROW_INTERNAL_RES), "640 x 448 (PS2)"), "PS2 in either list");
    s.internal_width = 3440; s.internal_height = 1440;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1) == 1 && s.aspect == HP2_ASPECT_AUTO, "21:9 -> auto");
    CHECK(s.internal_width == 2560 && s.internal_height == 1440, "3440x1440 -> 2560x1440, got %dx%d", s.internal_width, s.internal_height);
    s.aspect = HP2_ASPECT_16_10; s.internal_width = 2000; s.internal_height = 900;
    hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1);
    CHECK(s.internal_width == 2000 && s.internal_height == 900, "a custom resolution is kept");
    s.aspect = HP2_ASPECT_4_3; s.internal_width = 1920; s.internal_height = 1080;
    hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1);
    CHECK(s.internal_width == 1920, "4:3 -> 16:9 leaves the resolution alone");
}

static void test_enums_toggles_slider(void)
{
    hp2_settings s;
    printf("enums, toggles, slider\n");
    hp2_settings_defaults(&s);
    s.aspect = HP2_ASPECT_16_10;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1) == 1 && s.aspect == HP2_ASPECT_21_9, "16:10 -> 21:9");
    CHECK(!strcmp(val(&s, HP2_ROW_ASPECT), "21:9"), "reads 21:9");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, +1) == 1 && s.aspect == HP2_ASPECT_AUTO, "aspect wraps forward");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, -1) == 1 && s.aspect == HP2_ASPECT_21_9, "aspect wraps back");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_ASPECT, -1) == 1 && s.aspect == HP2_ASPECT_16_10, "21:9 back to 16:10");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_VSYNC, +1) == 1 && s.vsync == 0, "vsync toggles off");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_VSYNC, -1) == 1 && s.vsync == 1, "either direction toggles");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_SHOW_FPS, +1) == 1 && s.show_fps == 1, "show fps on");

    CHECK(hp2_menu_adjust(&s, HP2_ROW_DEADZONE, +1) == 1, "dead zone up");
    CHECK(!strcmp(val(&s, HP2_ROW_DEADZONE), "25%"), "20 -> 25%%, got %s", val(&s, HP2_ROW_DEADZONE));
    s.deadzone = 0.9f;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_DEADZONE, +1) == 0, "dead zone tops out at 90%%");
    s.deadzone = 0.0f;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_DEADZONE, -1) == 0, "dead zone bottoms at 0");
}

static void test_audio(void)
{
    hp2_settings s;
    int i;
    printf("audio\n");
    hp2_settings_defaults(&s);
    CHECK(!strcmp(hp2_menu_section(HP2_ROW_VOL_MENU_MUSIC), "Audio") && !strcmp(hp2_menu_section(HP2_ROW_DEFAULT_VOLUMES), "Audio"), "Audio section");
    CHECK(!strcmp(hp2_menu_label(HP2_ROW_VOL_AMBIENT), "Ambient Sound Effects"), "the game's row names");
    CHECK(!strcmp(hp2_menu_help(HP2_ROW_VOL_SPEECH), "Set volume from mute to maximum."), "the game's help line");
    CHECK(!strcmp(val(&s, HP2_ROW_VOL_MENU_MUSIC), "70%"), "menu music 70%%, got %s", val(&s, HP2_ROW_VOL_MENU_MUSIC));
    CHECK(hp2_menu_fraction(&s, HP2_ROW_VOL_MENU_MUSIC) > 0.69f && hp2_menu_fraction(&s, HP2_ROW_VOL_MENU_MUSIC) < 0.71f, "slider at 0.7");
    CHECK(hp2_menu_fraction(&s, HP2_ROW_AUDIO_MODE) < 0.0f && hp2_menu_fraction(&s, HP2_ROW_VSYNC) < 0.0f, "no slider on other rows");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_VOL_ENGINE, +1) == 1 && s.volume[HP2_VOL_ENGINE] == 85, "engine 80 -> 85");
    for (i = 0; i < 10; i++) hp2_menu_adjust(&s, HP2_ROW_VOL_ENGINE, +1);
    CHECK(s.volume[HP2_VOL_ENGINE] == 100 && hp2_menu_adjust(&s, HP2_ROW_VOL_ENGINE, +1) == 0, "stops at 100");
    for (i = 0; i < 30; i++) hp2_menu_adjust(&s, HP2_ROW_VOL_SPEECH, -1);
    CHECK(s.volume[HP2_VOL_SPEECH] == 0 && !strcmp(val(&s, HP2_ROW_VOL_SPEECH), "0%"), "mute is 0%%");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_VOL_SPEECH, -1) == 0, "stops at mute");
    s.volume[HP2_VOL_TIRES] = 73;
    CHECK(hp2_menu_adjust(&s, HP2_ROW_VOL_TIRES, +1) == 1 && s.volume[HP2_VOL_TIRES] == 80, "a hand-edited 73 steps onto the 5%% grid");
    CHECK(!strcmp(val(&s, HP2_ROW_AUDIO_MODE), "Stereo"), "stereo");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_AUDIO_MODE, +1) == 1 && !strcmp(val(&s, HP2_ROW_AUDIO_MODE), "Mono"), "mono");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_AUDIO_MODE, +1) == 1 && !strcmp(val(&s, HP2_ROW_AUDIO_MODE), "Surround"), "surround");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_AUDIO_MODE, +1) == 1 && s.audio_mode == HP2_AUDIO_STEREO, "wraps");
    CHECK(hp2_menu_kind(HP2_ROW_DEFAULT_VOLUMES) == HP2_KIND_ACTION, "Default Volumes is a button");
    CHECK(hp2_menu_adjust(&s, HP2_ROW_DEFAULT_VOLUMES, +1) == 0, "left/right do nothing on it");
    CHECK(hp2_menu_activate(&s, HP2_ROW_DEFAULT_VOLUMES) == 1, "pressing it changes the volumes");
    CHECK(s.volume[HP2_VOL_SPEECH] == 80 && s.volume[HP2_VOL_ENGINE] == 80 && s.volume[HP2_VOL_TIRES] == 80, "back to the defaults");
    CHECK(hp2_menu_activate(&s, HP2_ROW_DEFAULT_VOLUMES) == 0, "pressing it again changes nothing");
    CHECK(hp2_menu_activate(&s, HP2_ROW_VSYNC) == 0, "other rows are not buttons");
    CHECK(hp2_menu_kind(HP2_ROW_JUKEBOX) == HP2_KIND_ACTION && hp2_menu_activate(&s, HP2_ROW_JUKEBOX) == 0, "Jukebox: a button the front end handles");
    s.song[4] = HP2_PLAY_ALWAYS;
    CHECK(hp2_menu_activate(&s, HP2_ROW_DEFAULT_JUKEBOX) == 1 && s.song[4] == -1, "Default Jukebox resets the songs");
    CHECK(hp2_menu_activate(&s, HP2_ROW_DEFAULT_JUKEBOX) == 0, "and again changes nothing");
    CHECK(!strcmp(hp2_menu_help(HP2_ROW_DEFAULT_JUKEBOX), "Restore factory Jukebox settings."), "its help line");
}

static void test_settings_file(void)
{
    hp2_settings s, t;
    char buf[2048];
    size_t n;
    printf("settings file\n");
    hp2_settings_defaults(&s);
    s.frame_cap = 144; s.msaa = 8; s.aniso = 16; s.show_fps = 1;
    n = hp2_settings_write(&s, buf, sizeof buf);
    CHECK(n > 0 && n < sizeof buf, "write fits");
    hp2_settings_parse(&t, buf);
    CHECK(t.frame_cap == 144 && t.msaa == 8 && t.aniso == 16 && t.show_fps == 1, "round trip");

    s.frame_cap = 0;
    n = hp2_settings_write(&s, buf, sizeof buf);
    CHECK(strstr(buf, "frame_cap = off") != NULL, "no limit is written as off");
    hp2_settings_parse(&t, buf);
    CHECK(t.frame_cap == 0, "off reads back as 0");

    hp2_settings_parse(&t, "frame_cap = unlimited\nmsaa = off\naniso = off\n");
    CHECK(t.frame_cap == 0 && t.msaa == 0 && t.aniso == 1, "words for off");
    hp2_settings_parse(&t, "frame_cap = 5\nmsaa = 3\naniso = 7\nshow_fps = maybe\n");
    CHECK(t.frame_cap == HP2_MIN_FRAME_CAP, "cap raised to the minimum, got %d", t.frame_cap);
    CHECK(t.msaa == 4 && t.aniso == 8, "bad msaa/aniso fall back, got %d/%d", t.msaa, t.aniso);
    CHECK(t.show_fps == 0, "bad bool keeps the default");
    hp2_settings_parse(&t, "frame_cap = 99999\n");
    CHECK(t.frame_cap == HP2_MAX_FRAME_CAP, "cap clamped to the maximum");
    hp2_settings_defaults(&t);
    CHECK(hp2_settings_set(&t, "frame_cap=120") == 1 && t.frame_cap == 120, "set frame_cap");
    CHECK(hp2_settings_set(&t, "aniso=2") == 1 && t.aniso == 2, "set aniso");
}

static void test_never_invalid(void)
{
    hp2_settings s;
    int row, k, d;
    printf("any sequence of steps leaves valid settings\n");
    hp2_settings_defaults(&s);
    for (row = 0; row < hp2_menu_row_count(); row++)
        for (d = -1; d <= 1; d += 2)
            for (k = 0; k < 40; k++) {
                hp2_settings_defaults(&s);
                { int j; for (j = 0; j < k; j++) hp2_menu_adjust(&s, row, d); }
                CHECK(hp2_settings_validate(&s) == 0, "row %d dir %d after %d steps needed repair", row, d, k);
            }
}

int main(void)
{
    test_rows();
    test_audio();
    test_default_text();
    test_frame_cap();
    test_lists();
    test_enums_toggles_slider();
    test_settings_file();
    test_never_invalid();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
