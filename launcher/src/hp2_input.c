#include "hp2_input.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

static char g_error[256];

struct hp2_input {
    SDL_GameController *pad;
    SDL_JoystickID      id;
    hp2_pad_kind        kind;
    hp2_glyph_set       glyphs;
    char                name[128];
    hp2_settings        cfg;
    int                 prev[HP2_ACT_COUNT];
};

const char *hp2_input_error(void) { return g_error; }

/* SDL's own constants map to our kinds here, so no numeric value of SDL's is
 * assumed anywhere else in the project. */
static hp2_pad_kind kind_from_sdl(SDL_GameController *c)
{
    if (!c) return HP2_PAD_KEYBOARD;
    switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_XBOX360:             return HP2_PAD_XBOX360;
    case SDL_CONTROLLER_TYPE_XBOXONE:             return HP2_PAD_XBOXONE;
    case SDL_CONTROLLER_TYPE_PS3:                 return HP2_PAD_PS3;
    case SDL_CONTROLLER_TYPE_PS4:                 return HP2_PAD_PS4;
    case SDL_CONTROLLER_TYPE_PS5:                 return HP2_PAD_PS5;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return HP2_PAD_SWITCH_PRO;
#ifdef SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
                                                  return HP2_PAD_JOYCON;
#endif
#ifdef SDL_CONTROLLER_TYPE_VIRTUAL
    case SDL_CONTROLLER_TYPE_VIRTUAL:             return HP2_PAD_VIRTUAL;
#endif
#ifdef SDL_CONTROLLER_TYPE_AMAZON_LUNA
    case SDL_CONTROLLER_TYPE_AMAZON_LUNA:         return HP2_PAD_LUNA;
#endif
#ifdef SDL_CONTROLLER_TYPE_GOOGLE_STADIA
    case SDL_CONTROLLER_TYPE_GOOGLE_STADIA:       return HP2_PAD_STADIA;
#endif
#ifdef SDL_CONTROLLER_TYPE_NVIDIA_SHIELD
    case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD:       return HP2_PAD_SHIELD;
#endif
    default:                                      return HP2_PAD_UNKNOWN;
    }
}

static void adopt(hp2_input *in, SDL_GameController *c)
{
    const char *n;
    in->pad = c;
    in->kind = kind_from_sdl(c);
    in->glyphs = hp2_resolve_glyphs(in->cfg.glyphs, in->kind);
    n = c ? SDL_GameControllerName(c) : NULL;
    if (!n) n = hp2_pad_kind_name(in->kind);
    strncpy(in->name, n, sizeof(in->name) - 1);
    in->name[sizeof(in->name) - 1] = 0;
    if (c) {
        SDL_Joystick *j = SDL_GameControllerGetJoystick(c);
        in->id = j ? SDL_JoystickInstanceID(j) : -1;
    } else {
        in->id = -1;
    }
}

static void open_first_pad(hp2_input *in)
{
    int i, n = SDL_NumJoysticks();
    for (i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *c = SDL_GameControllerOpen(i);
            if (c) { adopt(in, c); return; }
        }
    }
    adopt(in, NULL);               /* keyboard fallback */
}

hp2_input *hp2_input_create(const hp2_settings *settings)
{
    hp2_input *in;

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        snprintf(g_error, sizeof(g_error),
                 "SDL_InitSubSystem(GAMECONTROLLER): %s", SDL_GetError());
        return NULL;
    }
    in = (hp2_input *)calloc(1, sizeof(*in));
    if (!in) {
        snprintf(g_error, sizeof(g_error), "out of memory");
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        return NULL;
    }
    if (settings) in->cfg = *settings;
    else          hp2_settings_defaults(&in->cfg);

    open_first_pad(in);
    return in;
}

void hp2_input_destroy(hp2_input *in)
{
    if (!in) return;
    if (in->pad) SDL_GameControllerClose(in->pad);
    free(in);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void hp2_input_apply_settings(hp2_input *in, const hp2_settings *s)
{
    if (!in || !s) return;
    in->cfg = *s;
    in->glyphs = hp2_resolve_glyphs(in->cfg.glyphs, in->kind);
}

hp2_pad_kind  hp2_input_pad_kind(const hp2_input *in)  { return in ? in->kind : HP2_PAD_KEYBOARD; }
const char   *hp2_input_pad_name(const hp2_input *in)  { return in ? in->name : "none"; }
hp2_glyph_set hp2_input_glyph_set(const hp2_input *in) { return in ? in->glyphs : HP2_GLYPHS_KEYBOARD; }

void hp2_input_rumble(hp2_input *in, float strength, int ms)
{
    Uint16 mag;
    if (!in || !in->pad || !in->cfg.rumble) return;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    mag = (Uint16)(strength * 65535.0f);
    SDL_GameControllerRumble(in->pad, mag, mag, (Uint32)(ms > 0 ? ms : 0));
}

/* which SDL button sits at each of our physical positions */
static SDL_GameControllerButton sdl_button(hp2_button b)
{
    switch (b) {
    case HP2_BTN_SOUTH:     return SDL_CONTROLLER_BUTTON_A;
    case HP2_BTN_EAST:      return SDL_CONTROLLER_BUTTON_B;
    case HP2_BTN_WEST:      return SDL_CONTROLLER_BUTTON_X;
    case HP2_BTN_NORTH:     return SDL_CONTROLLER_BUTTON_Y;
    case HP2_BTN_LSHOULDER: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case HP2_BTN_RSHOULDER: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case HP2_BTN_START:     return SDL_CONTROLLER_BUTTON_START;
    case HP2_BTN_SELECT:    return SDL_CONTROLLER_BUTTON_BACK;
    default:                return SDL_CONTROLLER_BUTTON_INVALID;
    }
}

/* SDL names face buttons by position, not by the letter printed on the pad,
 * so A is always the south button even on a Nintendo controller. That is why
 * binding by position and labelling separately is the correct split. */
static int held(hp2_input *in, hp2_button b)
{
    float dz;
    if (!in->pad) return 0;
    dz = in->cfg.deadzone;
    if (b == HP2_BTN_LTRIGGER)
        return hp2_trigger_value(SDL_GameControllerGetAxis(in->pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT), dz) > 0.5f;
    if (b == HP2_BTN_RTRIGGER)
        return hp2_trigger_value(SDL_GameControllerGetAxis(in->pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), dz) > 0.5f;
    {
        SDL_GameControllerButton sb = sdl_button(b);
        if (sb == SDL_CONTROLLER_BUTTON_INVALID) return 0;
        return SDL_GameControllerGetButton(in->pad, sb) ? 1 : 0;
    }
}

int hp2_input_update(hp2_input *in, hp2_input_state *out)
{
    SDL_Event e;
    int running = 1, a;

    if (!in || !out) return 0;
    memset(out, 0, sizeof(*out));

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            running = 0;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!in->pad) {
                SDL_GameController *c = SDL_GameControllerOpen(e.cdevice.which);
                if (c) adopt(in, c);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (in->pad && e.cdevice.which == in->id) {
                SDL_GameControllerClose(in->pad);
                in->pad = NULL;
                open_first_pad(in);       /* fall back, or pick up another pad */
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            break;
        default:
            break;
        }
    }

    if (in->pad) {
        float dz = in->cfg.deadzone;
        out->steer = hp2_axis_value(SDL_GameControllerGetAxis(in->pad, SDL_CONTROLLER_AXIS_LEFTX), dz);
        if (in->cfg.scheme == HP2_SCHEME_MODERN) {
            out->throttle = hp2_trigger_value(SDL_GameControllerGetAxis(in->pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), dz);
            out->brake    = hp2_trigger_value(SDL_GameControllerGetAxis(in->pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT), dz);
        } else {
            out->throttle = held(in, hp2_action_button(HP2_ACT_ACCELERATE, HP2_SCHEME_CLASSIC)) ? 1.0f : 0.0f;
            out->brake    = held(in, hp2_action_button(HP2_ACT_BRAKE,      HP2_SCHEME_CLASSIC)) ? 1.0f : 0.0f;
        }
    }

    for (a = 0; a < HP2_ACT_COUNT; a++) {
        int now = held(in, hp2_action_button((hp2_action)a, in->cfg.scheme));
        out->pressed[a] = now;
        out->just_pressed[a] = (now && !in->prev[a]);
        in->prev[a] = now;
    }
    return running;
}
