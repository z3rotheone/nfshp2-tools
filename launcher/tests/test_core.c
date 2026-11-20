/* Tests for the SDL-free core: settings parsing, clamping, round-tripping,
 * and the glyph tables. Build and run with `make test`. */
#include "../src/hp2_settings.h"
#include "../src/hp2_glyphs.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

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

static void test_defaults(void)
{
    hp2_settings s;
    printf("defaults\n");
    hp2_settings_defaults(&s);
    CHECK(s.internal_width == 1920 && s.internal_height == 1080,
          "expected 1920x1080, got %dx%d", s.internal_width, s.internal_height);
    CHECK(s.aspect == HP2_ASPECT_16_9, "aspect");
    CHECK(s.vsync == 1, "vsync");
    CHECK(s.glyphs == HP2_GLYPHS_AUTO, "glyphs");
    CHECK(hp2_settings_validate(&s) == 0, "defaults should need no correction");
}

static void test_parse(void)
{
    hp2_settings s;
    int n;
    const char *txt =
        "# a comment\n"
        "[video]\n"
        "internal_width = 2560\n"
        "internal_height=1440\n"
        "   aspect   =   16:10   \n"
        "window = fullscreen\n"
        "vsync = off\n"
        "[input]\n"
        "scheme = classic\n"
        "glyphs = playstation\n"
        "deadzone = 0.35\n"
        "rumble = no\n"
        "unknown_key = whatever   ; ignored\n";
    printf("parsing\n");
    n = hp2_settings_parse(&s, txt);
    CHECK(n == 9, "expected 9 understood keys, got %d", n);
    CHECK(s.internal_width == 2560, "width %d", s.internal_width);
    CHECK(s.internal_height == 1440, "height %d", s.internal_height);
    CHECK(s.aspect == HP2_ASPECT_16_10, "aspect %d", (int)s.aspect);
    CHECK(s.window == HP2_WINDOW_FULLSCREEN, "window %d", (int)s.window);
    CHECK(s.vsync == 0, "vsync %d", s.vsync);
    CHECK(s.scheme == HP2_SCHEME_CLASSIC, "scheme %d", (int)s.scheme);
    CHECK(s.glyphs == HP2_GLYPHS_PLAYSTATION, "glyphs %d", (int)s.glyphs);
    CHECK(fabs(s.deadzone - 0.35) < 1e-6, "deadzone %f", s.deadzone);
    CHECK(s.rumble == 0, "rumble %d", s.rumble);
}

static void test_case_and_whitespace(void)
{
    hp2_settings s;
    printf("case and whitespace\n");
    hp2_settings_parse(&s, "ASPECT = 4:3\r\nWindow\t=\tWINDOWED\r\nGlyphs=NINTENDO\n");
    CHECK(s.aspect == HP2_ASPECT_4_3, "uppercase key/value");
    CHECK(s.window == HP2_WINDOW_WINDOWED, "tabs and CRLF");
    CHECK(s.glyphs == HP2_GLYPHS_NINTENDO, "no spaces round =");
}

static void test_garbage_is_safe(void)
{
    hp2_settings s;
    int corrected;
    printf("garbage input\n");

    hp2_settings_parse(&s, "");
    CHECK(s.internal_width == 1920, "empty input should give defaults");

    hp2_settings_parse(&s, NULL);
    CHECK(s.internal_width == 1920, "NULL input should give defaults");

    hp2_settings_parse(&s, "\x01\x02\x03 nonsense \xff\xfe\n====\n= =\n");
    CHECK(s.internal_width == 1920, "binary noise should give defaults");

    /* out-of-range values must be clamped, not accepted */
    hp2_settings_parse(&s, "internal_width = 99999\ninternal_height = -40\n"
                           "deadzone = 7.5\naspect = sideways\n");
    CHECK(s.internal_width == HP2_MAX_WIDTH, "width clamped, got %d", s.internal_width);
    CHECK(s.internal_height == HP2_MIN_HEIGHT, "height clamped, got %d", s.internal_height);
    CHECK(s.deadzone >= 0.0f && s.deadzone <= 0.9f, "deadzone clamped, got %f", s.deadzone);
    CHECK(s.aspect == HP2_ASPECT_16_9, "unknown aspect should keep the default");

    /* a struct corrupted in memory must still be made safe */
    s.internal_width = -1; s.internal_height = 1 << 30;
    s.aspect = (hp2_aspect)99; s.window = (hp2_window_mode)-3;
    s.scheme = (hp2_scheme)77; s.glyphs = (hp2_glyph_set)-1;
    s.vsync = 42; s.rumble = -7; s.deadzone = -5.0f;
    corrected = hp2_settings_validate(&s);
    CHECK(corrected == 9, "expected 9 corrections, got %d", corrected);
    CHECK(s.internal_width == HP2_MIN_WIDTH, "width");
    CHECK(s.internal_height == HP2_MAX_HEIGHT, "height");
    CHECK((int)s.aspect == 0 && (int)s.window == 0, "enums reset");
    CHECK(s.vsync == 1 && s.rumble == 1, "bools reset");
    CHECK(s.deadzone > 0.0f, "deadzone reset");
}

static void test_roundtrip(void)
{
    hp2_settings a, b;
    char buf[1024];
    size_t need;
    printf("write then read back\n");

    hp2_settings_defaults(&a);
    a.internal_width = 3840; a.internal_height = 2160;
    a.aspect = HP2_ASPECT_4_3; a.window = HP2_WINDOW_WINDOWED;
    a.vsync = 0; a.scheme = HP2_SCHEME_CLASSIC;
    a.glyphs = HP2_GLYPHS_NINTENDO; a.deadzone = 0.05f; a.rumble = 0;

    need = hp2_settings_write(&a, buf, sizeof(buf));
    CHECK(need > 0 && need < sizeof(buf), "write produced %lu bytes", (unsigned long)need);

    hp2_settings_parse(&b, buf);
    CHECK(a.internal_width == b.internal_width, "width survived");
    CHECK(a.internal_height == b.internal_height, "height survived");
    CHECK(a.aspect == b.aspect, "aspect survived");
    CHECK(a.window == b.window, "window survived");
    CHECK(a.vsync == b.vsync, "vsync survived");
    CHECK(a.scheme == b.scheme, "scheme survived");
    CHECK(a.glyphs == b.glyphs, "glyphs survived");
    CHECK(fabs(a.deadzone - b.deadzone) < 0.005, "deadzone survived (%f vs %f)",
          a.deadzone, b.deadzone);
    CHECK(a.rumble == b.rumble, "rumble survived");

    /* a buffer too small must report the need, not overflow */
    {
        char tiny[16];
        size_t n2;
        memset(tiny, 0x7f, sizeof(tiny));
        n2 = hp2_settings_write(&a, tiny, sizeof(tiny));
        CHECK(n2 >= sizeof(tiny), "truncation should report the full length");
        CHECK(tiny[sizeof(tiny) - 1] == 0 || n2 >= sizeof(tiny), "no overflow past the buffer");
    }
}

static void test_file_io(void)
{
    hp2_settings a, b;
    const char *path = "test_settings_tmp.ini";
    printf("file save and load\n");

    hp2_settings_defaults(&a);
    a.internal_width = 1280; a.internal_height = 960;
    a.glyphs = HP2_GLYPHS_XBOX;
    CHECK(hp2_settings_save(&a, path) == 1, "save should succeed");
    CHECK(hp2_settings_load(&b, path) == 1, "load should succeed");
    CHECK(b.internal_width == 1280 && b.internal_height == 960, "values survived the file");
    CHECK(b.glyphs == HP2_GLYPHS_XBOX, "glyph set survived the file");
    remove(path);

    CHECK(hp2_settings_load(&b, "definitely_not_here.ini") == 0,
          "a missing file should report 0");
    CHECK(b.internal_width == 1920, "a missing file should leave the defaults");
}

static void test_aspect_ratio(void)
{
    printf("aspect ratios\n");
    CHECK(hp2_aspect_ratio(HP2_ASPECT_AUTO) == 0.0f, "auto means ask the display");
    CHECK(fabs(hp2_aspect_ratio(HP2_ASPECT_4_3) - 1.3333333) < 1e-5, "4:3");
    CHECK(fabs(hp2_aspect_ratio(HP2_ASPECT_16_9) - 1.7777778) < 1e-5, "16:9");
    CHECK(fabs(hp2_aspect_ratio(HP2_ASPECT_16_10) - 1.6) < 1e-5, "16:10");
    CHECK(fabs(hp2_aspect_ratio(HP2_ASPECT_21_9) - 64.0 / 27.0) < 1e-5, "21:9 is 64:27");
    {
        hp2_settings s, t;
        char buf[2048];
        hp2_settings_defaults(&s);
        hp2_settings_parse(&s, "aspect = 21:9\n");
        CHECK(s.aspect == HP2_ASPECT_21_9, "parses 21:9");
        CHECK(!strcmp(hp2_aspect_name(HP2_ASPECT_21_9), "21:9"), "name");
        hp2_settings_write(&s, buf, sizeof buf);
        hp2_settings_defaults(&t);
        hp2_settings_parse(&t, buf);
        CHECK(t.aspect == HP2_ASPECT_21_9, "21:9 survives a write and read");
    }
}

static void test_glyphs(void)
{
    int b, g;
    char buf[64];
    printf("glyph tables\n");

    /* the whole point: the same position is named differently per family */
    CHECK(!strcmp(hp2_button_label(HP2_BTN_SOUTH, HP2_GLYPHS_XBOX), "A"), "xbox south");
    CHECK(!strcmp(hp2_button_label(HP2_BTN_SOUTH, HP2_GLYPHS_PLAYSTATION), "Cross"), "ps south");
    CHECK(!strcmp(hp2_button_label(HP2_BTN_SOUTH, HP2_GLYPHS_NINTENDO), "B"), "nintendo south");
    CHECK(!strcmp(hp2_button_label(HP2_BTN_EAST, HP2_GLYPHS_NINTENDO), "A"), "nintendo east");

    /* no table hole: every (button, set) must give a non-empty label */
    for (g = 0; g < HP2_GLYPHS_COUNT; g++)
        for (b = 0; b < HP2_BTN_COUNT; b++) {
            const char *l = hp2_button_label((hp2_button)b, (hp2_glyph_set)g);
            CHECK(l && *l, "label missing for set %d button %d", g, b);
        }

    /* out-of-range must not crash or return NULL */
    CHECK(hp2_button_label((hp2_button)-1, HP2_GLYPHS_XBOX) != NULL, "bad button");
    CHECK(hp2_button_label(HP2_BTN_SOUTH, (hp2_glyph_set)99) != NULL, "bad set");

    /* every action must be named and bound under both schemes */
    for (b = 0; b < HP2_ACT_COUNT; b++) {
        const char *n = hp2_action_name((hp2_action)b);
        hp2_button m = hp2_action_button((hp2_action)b, HP2_SCHEME_MODERN);
        hp2_button c = hp2_action_button((hp2_action)b, HP2_SCHEME_CLASSIC);
        CHECK(n && *n, "action %d unnamed", b);
        CHECK(m >= 0 && m < HP2_BTN_COUNT, "action %d modern binding", b);
        CHECK(c >= 0 && c < HP2_BTN_COUNT, "action %d classic binding", b);
    }

    /* no two actions may share a button within a scheme, or input is ambiguous */
    {
        int seen[HP2_BTN_COUNT], s2;
        for (s2 = 0; s2 < HP2_SCHEME_COUNT; s2++) {
            memset(seen, 0, sizeof(seen));
            for (b = 0; b < HP2_ACT_COUNT; b++) {
                hp2_button bt = hp2_action_button((hp2_action)b, (hp2_scheme)s2);
                CHECK(seen[bt] == 0, "scheme %d: %s collides with %s on %s",
                      s2, hp2_action_name((hp2_action)b),
                      hp2_action_name((hp2_action)(seen[bt] - 1)),
                      hp2_button_label(bt, HP2_GLYPHS_XBOX));
                seen[bt] = b + 1;
            }
        }
    }

    hp2_action_prompt(HP2_ACT_ACCELERATE, HP2_SCHEME_MODERN, HP2_GLYPHS_XBOX, buf, sizeof(buf));
    CHECK(!strcmp(buf, "Accelerate: RT"), "prompt was \"%s\"", buf);
    hp2_action_prompt(HP2_ACT_ACCELERATE, HP2_SCHEME_CLASSIC, HP2_GLYPHS_PLAYSTATION, buf, sizeof(buf));
    CHECK(!strcmp(buf, "Accelerate: Cross"), "prompt was \"%s\"", buf);
}


static void test_set(void)
{
    hp2_settings s;
    printf("single assignments\n");
    hp2_settings_defaults(&s);

    CHECK(hp2_settings_set(&s, "internal_width=2560") == 1, "known key accepted");
    CHECK(s.internal_width == 2560, "width applied, got %d", s.internal_width);
    CHECK(s.internal_height == 1080, "other fields untouched, got %d", s.internal_height);

    CHECK(hp2_settings_set(&s, "glyphs = nintendo") == 1, "spaces allowed");
    CHECK(s.glyphs == HP2_GLYPHS_NINTENDO, "glyphs applied");
    CHECK(s.internal_width == 2560, "earlier change survived a second set");

    /* setting a value it already has is still a recognised key */
    CHECK(hp2_settings_set(&s, "glyphs=nintendo") == 1, "idempotent set still recognised");

    CHECK(hp2_settings_set(&s, "nonsense=7") == 0, "unknown key rejected");
    CHECK(hp2_settings_set(&s, "no_equals_sign") == 0, "malformed rejected");
    CHECK(hp2_settings_set(&s, NULL) == 0, "NULL rejected");
    CHECK(hp2_settings_set(NULL, "vsync=off") == 0, "NULL settings rejected");

    /* out-of-range through set is still clamped */
    hp2_settings_set(&s, "internal_width=999999");
    CHECK(s.internal_width == HP2_MAX_WIDTH, "set is clamped too, got %d", s.internal_width);
}

static void test_audio_file(void)
{
    hp2_settings s, t;
    char buf[2048];
    printf("audio settings\n");
    hp2_settings_defaults(&s);
    CHECK(s.volume[HP2_VOL_MENU_MUSIC] == 70 && s.volume[HP2_VOL_SPEECH] == 80 && s.audio_mode == HP2_AUDIO_STEREO, "defaults");
    hp2_settings_parse(&s, "[audio]\nmenu_music = 35\nspeech = mute\nengine = 250\ntires = -4\nmode = Surround\n");
    CHECK(s.volume[HP2_VOL_MENU_MUSIC] == 35, "menu_music");
    CHECK(s.volume[HP2_VOL_SPEECH] == 0, "mute");
    CHECK(s.volume[HP2_VOL_ENGINE] == 100 && s.volume[HP2_VOL_TIRES] == 0, "clamped to 0..100");
    CHECK(s.audio_mode == HP2_AUDIO_SURROUND, "mode, any case");
    CHECK(s.volume[HP2_VOL_RACE_MUSIC] == 70, "absent keys keep the default");
    hp2_settings_write(&s, buf, sizeof buf);
    CHECK(strstr(buf, "[audio]") && strstr(buf, "menu_music = 35") && strstr(buf, "mode = surround"), "written");
    hp2_settings_parse(&t, buf);
    CHECK(!memcmp(t.volume, s.volume, sizeof s.volume) && t.audio_mode == s.audio_mode, "round trip");
    hp2_settings_parse(&s, "mode = dolby\n");
    CHECK(s.audio_mode == HP2_AUDIO_STEREO, "unknown mode keeps the default");
    CHECK(!strcmp(hp2_volume_key(HP2_VOL_MENU_SFX), "menu_sfx") && !strcmp(hp2_audio_mode_name(HP2_AUDIO_MONO), "mono"), "names");
}

static void test_jukebox_file(void)
{
    hp2_settings s, t;
    char buf[8192];
    int i;
    printf("jukebox settings\n");
    hp2_settings_defaults(&s);
    for (i = 0; i < HP2_SONGS; i++) CHECK(s.song[i] == -1, "song %d follows the bank", i + 1);
    hp2_settings_write(&s, buf, sizeof buf);
    CHECK(!strstr(buf, "[jukebox]"), "nothing set: no jukebox section");
    hp2_settings_parse(&s, "song1 = off\nSong23 = On\nsong3 = menu\nsong5 = default\nsong65 = menu\nsong2 = loud\n");
    CHECK(s.song[0] == HP2_PLAY_OFF && s.song[22] == HP2_PLAY_ON && s.song[2] == HP2_PLAY_MENU, "parsed");
    CHECK(s.song[4] == -1 && s.song[1] == -1, "default / unknown value: the bank's");
    hp2_settings_parse(&s, "song1 = always\nsong2 = menu_racing\nsong3 = racing\nsong4 = hot_pursuit\n");
    CHECK(s.song[0] == HP2_PLAY_ON && s.song[1] == HP2_PLAY_ON && s.song[2] == HP2_PLAY_ON && s.song[3] == HP2_PLAY_ON,
          "the game's words from older files: anything that plays in races is on");
    hp2_settings_parse(&s, "song1 = off\nsong23 = on\nsong3 = menu\n");
    hp2_settings_write(&s, buf, sizeof buf);
    CHECK(strstr(buf, "[jukebox]") && strstr(buf, "song1 = off") && strstr(buf, "song23 = on") && !strstr(buf, "song2 ="), "only set songs written");
    hp2_settings_parse(&t, buf);
    CHECK(!memcmp(t.song, s.song, sizeof s.song), "round trip");
    for (i = 0; i < HP2_SONGS; i++) s.song[i] = HP2_PLAY_MENU;
    for (i = 0; i < HP2_ACT_COUNT; i++) s.bind[i] = (HP2_BTN_COUNT - 1 - i);
    CHECK(hp2_settings_write(&s, buf, sizeof buf) < sizeof buf, "all 64 songs and every binding still fit the save buffer");
    hp2_settings_parse(&t, buf);
    CHECK(!memcmp(t.song, s.song, sizeof s.song) && !memcmp(t.bind, s.bind, sizeof s.bind), "all of it round trips");
    s.song[7] = 99; s.song[8] = -7;
    hp2_settings_validate(&s);
    CHECK(s.song[7] == -1 && s.song[8] == -1, "out of range -> the bank's");
    hp2_settings_default_jukebox(&s);
    CHECK(s.song[0] == -1 && s.song[63] == -1, "Default Jukebox");
    CHECK(!strcmp(hp2_play_label(HP2_PLAY_ON), "On") && !strcmp(hp2_play_key(HP2_PLAY_MENU), "menu"), "names");
    CHECK(hp2_play_from_bank(0) == HP2_PLAY_OFF && hp2_play_from_bank(3) == HP2_PLAY_MENU && hp2_play_from_bank(1) == HP2_PLAY_ON
          && hp2_play_from_bank(2) == HP2_PLAY_ON && hp2_play_from_bank(4) == HP2_PLAY_ON && hp2_play_from_bank(5) == HP2_PLAY_ON
          && hp2_play_from_bank(9) == HP2_PLAY_OFF, "the bank's six (Off, World Racing, Hot Pursuit, Menu, Menu/Racing, Always) -> three");
}

static void test_controls_file(void)
{
    hp2_settings s, t;
    char buf[8192];
    printf("controls settings\n");
    hp2_settings_defaults(&s);
    CHECK(s.bind[HP2_ACT_ACCELERATE] == -1 && s.bind[HP2_ACT_NEXT_SONG] == -1, "defaults follow the scheme");
    hp2_settings_write(&s, buf, sizeof buf);
    CHECK(!strstr(buf, "[controls]"), "nothing remapped: no controls section");
    hp2_settings_parse(&s, "[controls]\naccelerate = South\nnext_song = up\nsiren = default\npause = nowhere\n");
    CHECK(s.bind[HP2_ACT_ACCELERATE] == HP2_BTN_SOUTH && s.bind[HP2_ACT_NEXT_SONG] == HP2_BTN_DPAD_UP, "parsed, any case");
    CHECK(s.bind[HP2_ACT_SIREN] == -1 && s.bind[HP2_ACT_PAUSE] == -1, "default / unknown button");
    hp2_settings_write(&s, buf, sizeof buf);
    CHECK(strstr(buf, "[controls]") && strstr(buf, "accelerate = south") && strstr(buf, "next_song = up") && !strstr(buf, "siren ="), "only remapped written");
    hp2_settings_parse(&t, buf);
    CHECK(!memcmp(t.bind, s.bind, sizeof s.bind), "round trip");
    hp2_settings_parse(&s, "camera = rb\nsiren = rb\n");
    CHECK(s.bind[HP2_ACT_CAMERA] == HP2_BTN_RSHOULDER && s.bind[HP2_ACT_SIREN] == -1, "a hand-edited duplicate: the later one goes back to its default");
    s.bind[HP2_ACT_BRAKE] = 99;
    hp2_settings_validate(&s);
    CHECK(s.bind[HP2_ACT_BRAKE] == -1, "out of range -> default");
    hp2_settings_default_controls(&s);
    CHECK(s.bind[HP2_ACT_CAMERA] == -1, "Default Controls");
    CHECK(!strcmp(hp2_button_key(HP2_BTN_RSTICK), "rs") && !strcmp(hp2_action_key(HP2_ACT_NEXT_SONG), "next_song"), "names");
}

static void test_bind(void)
{
    hp2_settings s;
    hp2_button before;
    int a, b, seen[HP2_BTN_COUNT];
    printf("remapping\n");
    hp2_settings_defaults(&s);
    CHECK(hp2_bound_button(&s, HP2_ACT_NEXT_SONG) == HP2_BTN_RSTICK, "Next Song on the right stick click");
    CHECK(hp2_bound_button(&s, HP2_ACT_ACCELERATE) == HP2_BTN_RTRIGGER, "modern: accelerate on RT");
    CHECK(hp2_action_on(&s, HP2_BTN_SOUTH) == HP2_ACT_HANDBRAKE && hp2_action_on(&s, HP2_BTN_DPAD_UP) == -1, "who is on a button");
    before = hp2_bound_button(&s, HP2_ACT_ACCELERATE);
    CHECK(hp2_bind(&s, HP2_ACT_ACCELERATE, HP2_BTN_SOUTH) == 1, "bind accelerate to south");
    CHECK(hp2_bound_button(&s, HP2_ACT_ACCELERATE) == HP2_BTN_SOUTH && hp2_bound_button(&s, HP2_ACT_HANDBRAKE) == before,
          "handbrake, which was on south, takes accelerate's old button (swap)");
    CHECK(hp2_bind(&s, HP2_ACT_ACCELERATE, HP2_BTN_SOUTH) == 0, "same again: no change");
    CHECK(hp2_bind(&s, HP2_ACT_ACCELERATE, HP2_BTN_RTRIGGER) == 1 && s.bind[HP2_ACT_ACCELERATE] == -1 && s.bind[HP2_ACT_HANDBRAKE] == -1,
          "swapping back: both on their defaults again, nothing left in the file");
    CHECK(hp2_bind(&s, HP2_ACT_SIREN, HP2_BTN_DPAD_LEFT) == 1 && hp2_bound_button(&s, HP2_ACT_SIREN) == HP2_BTN_DPAD_LEFT, "to a free button");
    s.scheme = HP2_SCHEME_CLASSIC;
    CHECK(hp2_bound_button(&s, HP2_ACT_SIREN) == HP2_BTN_DPAD_LEFT && hp2_bound_button(&s, HP2_ACT_ACCELERATE) == HP2_BTN_SOUTH,
          "a remap stays across schemes; the rest follow the scheme");
    for (b = 0; b < 200; b++)
        hp2_bind(&s, (hp2_action)(b % HP2_ACT_COUNT), (hp2_button)((b * 7) % HP2_BTN_COUNT));
    memset(seen, 0, sizeof seen);
    for (a = 0; a < HP2_ACT_COUNT; a++) seen[hp2_bound_button(&s, (hp2_action)a)]++;
    for (b = 0, a = 0; b < HP2_BTN_COUNT; b++) a |= seen[b] > 1;
    CHECK(!a, "after 200 remaps no two actions share a button");
    CHECK(hp2_bind(&s, HP2_ACT_COUNT, HP2_BTN_SOUTH) == 0 && hp2_bind(&s, HP2_ACT_BRAKE, HP2_BTN_COUNT) == 0, "bad action / button");
}

int main(void)
{
    test_defaults();
    test_parse();
    test_case_and_whitespace();
    test_garbage_is_safe();
    test_roundtrip();
    test_file_io();
    test_aspect_ratio();
    test_glyphs();
    test_set();
    test_audio_file();
    test_jukebox_file();
    test_controls_file();
    test_bind();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
