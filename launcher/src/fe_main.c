/* hp2fe - the original Options menus, drawn by the port.
 *
 *   hp2fe --disc DIR [options]          DIR = the NFSHP2 folder of your own
 *                                       disc copy (ZDIR.BIN + ZZDATA.BIN)
 *   hp2fe --files FRONTB.BUN GLOBALB.BUN FE_ENGLISH.BIN [options]
 *                                       or extracted entries 21, 302, 3
 * options:
 *   --language NAME    FE_<NAME>.BIN with --disc (default ENGLISH)
 *   --jukebox FILE     with --files: the music bank (entry 322,
 *                      SOUND\JUKEBOXM.JBC) for the Jukebox screen; --disc
 *                      reads it from the archive. Only its song table is
 *                      read.
 *   --config PATH      settings file (default hp2.ini, shared with the
 *                      launcher; saved when you leave the settings list)
 *   --size WxH         window size (default 1280x720)
 *   --fit fit|stretch|expand    how the 640x480 canvas fills the window
 *                      (default: fit for aspect 4:3, else expand)
 *   --no-music         no menu music (it plays the Jukebox's songs set to
 *                      Menu, Menu/Racing or Always, at Menu Music volume)
 *   --shot OUT.bmp     render one frame offscreen, save it, exit
 *   --keys LIST        inputs to apply first, e.g. down,down,accept,right
 *
 * Keys: arrows, Enter/Space = accept, Esc/Backspace = back. A controller's
 * d-pad or left stick, A (south) = accept, B (east) = back.
 */
#include "hp2_fe.h"
#include "hp2_fedraw.h"
#include "hp2_jbplay.h"
#include "hp2_femenu.h"
#include "hp2_font.h"
#include "hp2_lang.h"
#include "hp2_settings.h"
#include "hp2_tex.h"
#include "hp2_zdir.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 160

typedef struct {
    uint8_t *frontb, *globalb, *lang, *bank;
    size_t   frontb_len, globalb_len, lang_len, bank_len;
    char      bank_path[1024];   /* where the music bank's audio is streamed from */
    long long bank_base;
} files;

static int load_files(files *f, const char *disc, const char *language, char **loose, const char *bank)
{
    memset(f, 0, sizeof *f);
    if (disc) {
        hp2_zdir z;
        char path[64];
        if (hp2_zdir_open(&z, disc) != 0) {
            fprintf(stderr, "no ZDIR.BIN / ZZDATA.BIN in %s\n", disc);
            return -1;
        }
        snprintf(path, sizeof path, "LANGUAGES\\FE_%s.BIN", language);
        f->frontb = hp2_zdir_load(&z, "FRONTEND\\FRONTB.BUN", &f->frontb_len);
        f->globalb = hp2_zdir_load(&z, "GLOBAL\\GLOBALB.BUN", &f->globalb_len);
        f->lang = hp2_zdir_load(&z, path, &f->lang_len);
        f->bank = hp2_zdir_load_part(&z, "SOUND\\JUKEBOXM.JBC", hp2_jukebox_header_size(), &f->bank_len);
        {
            uint32_t size;
            if (hp2_zdir_locate(&z, "SOUND\\JUKEBOXM.JBC", &f->bank_base, &size) == 0)
                snprintf(f->bank_path, sizeof f->bank_path, "%s", z.data);
        }
        hp2_zdir_close(&z);
        if (!f->lang) fprintf(stderr, "%s not in the archive; showing the layouts' own text\n", path);
    } else {
        f->frontb = hp2_read_file(loose[0], &f->frontb_len);
        f->globalb = hp2_read_file(loose[1], &f->globalb_len);
        f->lang = hp2_read_file(loose[2], &f->lang_len);
        if (bank) {
            f->bank = hp2_read_file_part(bank, hp2_jukebox_header_size(), &f->bank_len);
            snprintf(f->bank_path, sizeof f->bank_path, "%s", bank);
        }
    }
    if (!f->frontb || !f->globalb) {
        fprintf(stderr, "could not read FRONTB.BUN and GLOBALB.BUN\n");
        return -1;
    }
    return 0;
}

static int parse_input(const char *s, hp2_fem_input *in)
{
    static const char *names[] = { "up", "down", "left", "right", "accept", "back" };
    int i;
    for (i = 0; i < 6; i++)
        if (!strcmp(s, names[i])) { *in = (hp2_fem_input)i; return 1; }
    return 0;
}

static hp2_fe_fit fit_for(const hp2_settings *cfg, int forced)
{
    if (forced >= 0) return (hp2_fe_fit)forced;
    return cfg->aspect == HP2_ASPECT_4_3 ? HP2_FE_FIT : HP2_FE_EXPAND;
}

static void apply_window_mode(SDL_Window *w, const hp2_settings *cfg)
{
    Uint32 f = cfg->window == HP2_WINDOW_FULLSCREEN ? SDL_WINDOW_FULLSCREEN
             : cfg->window == HP2_WINDOW_BORDERLESS ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_SetWindowFullscreen(w, f);
}

static void draw(hp2_fedraw *d, hp2_femenu *m, SDL_Renderer *r, int W, int H, hp2_fe_fit fit)
{
    static hp2_fe_item items[MAX_ITEMS];
    hp2_fe_view v;
    int i, n;
    hp2_fe_view_make(&v, W, H, 640, 480, fit);
    n = hp2_femenu_items(m, items, MAX_ITEMS);
    for (i = 0; i < n; i++) items[i].extend = fit == HP2_FE_EXPAND;
    d->clip_canvas = fit != HP2_FE_EXPAND;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    hp2_fedraw_items(d, &v, items, n);
}

int main(int argc, char **argv)
{
    const char *disc = NULL, *language = "ENGLISH", *config = "hp2.ini", *shot = NULL, *keys = NULL, *bankpath = NULL;
    static hp2_jukebox jb;
    char *loose[3] = { NULL, NULL, NULL };
    int W = 1280, H = 720, forced_fit = -1, i, quit = 0, dirty = 0, music = 1;
    SDL_AudioDeviceID audio = 0;
    hp2_jbplay player;
    files f;
    hp2_fe_layout layout;
    hp2_texset tex = { 0 };
    hp2_fontset fonts = { 0 };
    hp2_lang lang;
    hp2_settings cfg;
    hp2_femenu menu;
    SDL_Window *win;
    SDL_Renderer *ren;
    hp2_fedraw d;
    SDL_GameController *pad = NULL;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--files") && i + 3 < argc) { loose[0] = argv[++i]; loose[1] = argv[++i]; loose[2] = argv[++i]; }
        else if (!strcmp(argv[i], "--language") && i + 1 < argc) language = argv[++i];
        else if (!strcmp(argv[i], "--jukebox") && i + 1 < argc) bankpath = argv[++i];
        else if (!strcmp(argv[i], "--no-music")) music = 0;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 64 || H < 48) { W = 1280; H = 720; } }
        else if (!strcmp(argv[i], "--fit") && i + 1 < argc) {
            const char *s = argv[++i];
            forced_fit = !strcmp(s, "fit") ? HP2_FE_FIT : !strcmp(s, "stretch") ? HP2_FE_STRETCH : !strcmp(s, "expand") ? HP2_FE_EXPAND : -1;
        }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else {
            fprintf(stderr, "usage: %s --disc DIR | --files FRONTB.BUN GLOBALB.BUN FE_LANG.BIN\n"
                            "       [--language NAME] [--jukebox BANK] [--no-music] [--config PATH] [--size WxH]\n"
                            "       [--fit fit|stretch|expand]\n"
                            "       [--shot OUT.bmp] [--keys up,down,left,right,accept,back]\n", argv[0]);
            return 2;
        }
    }
    if (!disc && !loose[0]) {
        fprintf(stderr, "give --disc DIR (your disc's NFSHP2 folder) or --files; --help for more\n");
        return 2;
    }
    if (load_files(&f, disc, language, loose, bankpath) != 0) return 1;
    if (hp2_fe_parse(f.frontb, f.frontb_len, &layout) != 0) { fprintf(stderr, "FRONTB.BUN: no screen layouts\n"); return 1; }
    if (hp2_tex_scan(&tex, f.frontb, f.frontb_len) < 0 || hp2_tex_scan(&tex, f.globalb, f.globalb_len) < 0
        || hp2_font_scan(&fonts, f.frontb, f.frontb_len) < 0 || hp2_font_scan(&fonts, f.globalb, f.globalb_len) < 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (!f.lang || hp2_lang_open(&lang, f.lang, f.lang_len) != 0) memset(&lang, 0, sizeof lang);
    hp2_settings_load(&cfg, config);       /* 1 = read; defaults either way first */
    if (hp2_femenu_init(&menu, &layout, lang.count ? &lang : NULL, &cfg) != 0) {
        fprintf(stderr, "FRONTB.BUN lacks the OPTIONS / DISPLAY_OPTIONS screens\n");
        return 1;
    }
    if (f.bank && hp2_jukebox_parse(&jb, f.bank, f.bank_len) == 0) hp2_femenu_set_jukebox(&menu, &jb);
    else if (disc || bankpath) fprintf(stderr, "no jukebox song table; the Jukebox row does nothing\n");
    if (keys) {
        char buf[512], *t;
        snprintf(buf, sizeof buf, "%s", keys);
        for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            hp2_fem_input in;
            if (parse_input(t, &in)) dirty |= hp2_femenu_input(&menu, in) & HP2_FEM_CHANGED;
            else fprintf(stderr, "unknown input '%s'\n", t);
        }
    }

    if (shot) SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO | (shot ? 0 : SDL_INIT_GAMECONTROLLER)) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("Hot Pursuit 2 - Options", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                           shot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, shot ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    hp2_fedraw_init(&d, ren, &tex, &fonts);

    if (shot) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
        draw(&d, &menu, ren, W, H, fit_for(&cfg, forced_fit));
        if (!s || SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch) != 0 || SDL_SaveBMP(s, shot) != 0) {
            fprintf(stderr, "could not save %s: %s\n", shot, SDL_GetError());
            return 1;
        }
        printf("wrote %s (%dx%d)%s\n", shot, W, H, d.missing ? ", some textures or fonts missing" : "");
        SDL_FreeSurface(s);
        quit = 1;
        dirty = 0;      /* a screenshot never writes the settings file */
    } else {
        apply_window_mode(win, &cfg);
        /* menu music: the Jukebox's songs, streamed from the bank */
        if (music && menu.jb && f.bank_path[0] && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof want);
            want.freq = jb.rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 2048;
            audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* SDL converts if it must */
            if (audio) {
                hp2_jbplay_init(&player, &jb, &cfg, f.bank_path, f.bank_base, (uint32_t)SDL_GetTicks() | 1u);
                SDL_PauseAudioDevice(audio, 0);
            } else {
                fprintf(stderr, "no sound: %s\n", SDL_GetError());
            }
        }
    }

    while (!quit) {
        SDL_Event e;
        int ow, oh;
        if (audio) {
            /* keep about half a second queued: a volume change or a song
             * switched off is heard within that */
            static int16_t pcm[2 * 2048];
            while (SDL_GetQueuedAudioSize(audio) < (Uint32)(jb.rate * 4 / 2)) {
                int n = hp2_jbplay_fill(&player, pcm, 2048);
                if (n <= 0) break;
                SDL_QueueAudio(audio, pcm, (Uint32)n * 4);
            }
            if (!SDL_WaitEventTimeout(&e, 40)) continue;
        } else if (!SDL_WaitEvent(&e)) {
            break;
        }
        do {
            hp2_fem_input in;
            int have = 0;
            if (e.type == SDL_QUIT) quit = 1;
            else if (e.type == SDL_KEYDOWN) {
                have = 1;
                switch (e.key.keysym.sym) {
                case SDLK_UP: in = HP2_IN_UP; break;
                case SDLK_DOWN: in = HP2_IN_DOWN; break;
                case SDLK_LEFT: in = HP2_IN_LEFT; break;
                case SDLK_RIGHT: in = HP2_IN_RIGHT; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: in = HP2_IN_ACCEPT; break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: in = HP2_IN_BACK; break;
                default: have = 0;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(e.cdevice.which);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                have = 1;
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: in = HP2_IN_UP; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in = HP2_IN_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: in = HP2_IN_LEFT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in = HP2_IN_RIGHT; break;
                case SDL_CONTROLLER_BUTTON_A: in = HP2_IN_ACCEPT; break;
                case SDL_CONTROLLER_BUTTON_B: in = HP2_IN_BACK; break;
                default: have = 0;
                }
            } else if (e.type == SDL_CONTROLLERAXISMOTION) {
                /* left stick as a d-pad: one step when it crosses half way */
                static int last[2];
                int axis = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? 1 : -1;
                if (axis >= 0) {
                    int dir = e.caxis.value > 16000 ? 1 : e.caxis.value < -16000 ? -1 : 0;
                    if (dir && dir != last[axis]) {
                        have = 1;
                        in = axis ? (dir > 0 ? HP2_IN_DOWN : HP2_IN_UP) : (dir > 0 ? HP2_IN_RIGHT : HP2_IN_LEFT);
                    }
                    last[axis] = dir;
                }
            }
            if (have) {
                hp2_fem_screen before = menu.screen;
                int res = hp2_femenu_input(&menu, in);
                if (res & HP2_FEM_CHANGED) {
                    dirty = 1;
                    if (menu.sel == HP2_ROW_WINDOW) apply_window_mode(win, &cfg);
                }
                if (res & HP2_FEM_QUIT) quit = 1;
                if (dirty && before == HP2_FEM_SETTINGS && menu.screen != before) {
                    if (!hp2_settings_save(&cfg, config)) fprintf(stderr, "could not save %s\n", config);
                    dirty = 0;
                }
            }
        } while (!quit && SDL_PollEvent(&e));
        if (quit) break;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        draw(&d, &menu, ren, ow, oh, fit_for(&cfg, forced_fit));
        SDL_RenderPresent(ren);
    }
    if (dirty && !hp2_settings_save(&cfg, config)) fprintf(stderr, "could not save %s\n", config);   /* 1 = saved */

    hp2_fedraw_free(&d);
    if (audio) {
        SDL_CloseAudioDevice(audio);
        hp2_jbplay_close(&player);
    }
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    hp2_font_free(&fonts);
    hp2_tex_free(&tex);
    hp2_fe_free(&layout);
    free(f.frontb);
    free(f.globalb);
    free(f.lang);
    free(f.bank);
    return 0;
}
