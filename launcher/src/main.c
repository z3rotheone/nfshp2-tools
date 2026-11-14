/* hp2launcher - prototype of the settings file and input layer for a
 * Hot Pursuit 2 port, standing on its own until there is a port to host it.
 *
 *   hp2launcher [--config PATH] [--show] [--set key=value ...]
 *
 * With no arguments it loads hp2.ini (creating it if absent), opens the first
 * game controller it finds, and shows the live input with the button names
 * that belong to that controller. Press Escape or Ctrl+C to leave; the
 * settings are written back on the way out.
 *
 *   hp2launcher --show
 *   hp2launcher --set internal_width=2560 --set glyphs=playstation --show
 */
#include "hp2_settings.h"
#include "hp2_glyphs.h"
#include "hp2_pad.h"
#include "hp2_input.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_settings(const hp2_settings *s)
{
    printf("settings\n");
    printf("  internal resolution  %d x %d\n", s->internal_width, s->internal_height);
    printf("  aspect               %s\n", hp2_aspect_name(s->aspect));
    printf("  window               %s\n", hp2_window_name(s->window));
    printf("  vsync                %s\n", s->vsync ? "on" : "off");
    printf("  control scheme       %s\n", hp2_scheme_name(s->scheme));
    printf("  button glyphs        %s\n", hp2_glyph_set_name(s->glyphs));
    printf("  dead zone            %.2f\n", (double)s->deadzone);
    printf("  rumble               %s\n", s->rumble ? "on" : "off");
}

static void print_bindings(const hp2_settings *s, hp2_glyph_set resolved)
{
    char buf[64];
    int a;
    printf("\ncontrols (%s scheme, %s glyphs)\n",
           hp2_scheme_name(s->scheme), hp2_glyph_set_name(resolved));
    for (a = 0; a < HP2_ACT_COUNT; a++)
        printf("  %s\n", hp2_action_prompt((hp2_action)a, s->scheme, resolved,
                                           buf, (int)sizeof(buf)));
}

static void bar(float v, int width, char *out)
{
    int i, n;
    if (v < 0.0f) v = -v;
    if (v > 1.0f) v = 1.0f;
    n = (int)(v * width + 0.5f);
    for (i = 0; i < width; i++) out[i] = (i < n) ? '#' : '.';
    out[width] = 0;
}

int main(int argc, char **argv)
{
    const char *path = "hp2.ini";
    hp2_settings cfg;
    hp2_input *in;
    hp2_input_state st;
    int i, show_only = 0, existed;
    int sets = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--show")) show_only = 1;
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) sets++;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: %s [--config PATH] [--show] [--set key=value ...]\n", argv[0]);
            return 0;
        }
    }

    existed = hp2_settings_load(&cfg, path);
    printf("hp2launcher - settings and input prototype\n\n");
    printf("config file: %s%s\n\n", path, existed ? "" : "  (not found, using defaults)");

    /* apply any --set overrides */
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--set") && i + 1 < argc)
            if (!hp2_settings_set(&cfg, argv[++i]))
                printf("could not apply --set %s\n", argv[i]);

    if (sets || !existed) {
        if (hp2_settings_save(&cfg, path))
            printf("wrote %s\n\n", path);
        else
            printf("could not write %s\n\n", path);
    }

    print_settings(&cfg);

    if (show_only) {
        print_bindings(&cfg, hp2_resolve_glyphs(cfg.glyphs, HP2_PAD_KEYBOARD));
        return 0;
    }

    if (SDL_Init(0) != 0) {
        printf("\nSDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    in = hp2_input_create(&cfg);
    if (!in) {
        printf("\ninput init failed: %s\n", hp2_input_error());
        SDL_Quit();
        return 1;
    }

    printf("\ndetected: %s  (%s)\n", hp2_input_pad_name(in),
           hp2_pad_kind_name(hp2_input_pad_kind(in)));
    print_bindings(&cfg, hp2_input_glyph_set(in));
    printf("\nmove the sticks and press buttons. Escape or Ctrl+C to quit.\n\n");

    for (;;) {
        char sb[33], tb[33], bb[33];
        int a, first = 1;

        if (!hp2_input_update(in, &st)) break;

        bar(st.steer, 24, sb);
        bar(st.throttle, 24, tb);
        bar(st.brake, 24, bb);
        printf("\r\033[Ksteer [%s] %+.2f   throttle [%s]   brake [%s]  ",
               sb, (double)st.steer, tb, bb);
        for (a = 0; a < HP2_ACT_COUNT; a++) {
            if (st.pressed[a]) {
                printf("%s%s", first ? " | " : ", ", hp2_action_name((hp2_action)a));
                first = 0;
            }
            if (st.just_pressed[a] && a == HP2_ACT_HANDBRAKE)
                hp2_input_rumble(in, 0.6f, 120);
        }
        fflush(stdout);
        SDL_Delay(16);
    }

    printf("\n\n");
    if (hp2_settings_save(&cfg, path))
        printf("saved %s\n", path);
    hp2_input_destroy(in);
    SDL_Quit();
    return 0;
}
