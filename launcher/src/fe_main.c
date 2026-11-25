/* hp2fe - the original Options menus, drawn by the port.
 *
 *   hp2fe --disc DIR [options]          DIR = the NFSHP2 folder of your own
 *                                       disc copy (ZDIR.BIN + ZZDATA.BIN)
 *   hp2fe --files FRONTB.BUN GLOBALB.BUN FE_ENGLISH.BIN [options]
 *                                       or extracted entries 21, 302, 3
 * options:
 *   --language NAME    FE_<NAME>.BIN with --disc (default ENGLISH)
 *   --jukebox FILE     the music bank (entry 322, SOUND\JUKEBOXM.JBC) for
 *                      the Jukebox and the menu music, e.g. one you modded
 *                      with hp2_music.py. Without it, --disc uses the
 *                      archive's own bank.
 *   --config PATH      settings file (default hp2.ini, shared with the
 *                      launcher; saved when you leave the settings list)
 *   --size WxH         window size (default 1280x720)
 *   --fit fit|stretch|expand    how the 640x480 canvas fills the window
 *                      (default: fit for aspect 4:3, else expand)
 *   --options          start on Options instead of the title screen
 *   --no-music         no menu music (it plays the Jukebox's songs set to
 *                      Menu or On, at Menu Music volume). The Next Song
 *                      button (default: right stick click; N on the
 *                      keyboard) skips to another.
 *   --shot OUT.bmp     render one frame offscreen, save it, exit
 *   --songbox N        with --shot: show song N's box (as when it starts)
 *   --keys LIST        inputs to apply first, e.g. down,down,accept,right
 *                      (up down left right accept back help); a race they
 *                      start is printed, not run
 *   --hp2track PATH    the track program races run in (default: hp2track
 *                      beside hp2fe)
 *
 * Races: accept on SELECT LAPS (Quick Race, Single Race) or on an event's
 * details starts hp2track with that race (course, II for the other way
 * round, laps, your car and colour, the opponents); the menus come back
 * when its window closes.
 *
 * Keys: arrows, Enter/Space = accept, Esc/Backspace = back, N = next song.
 * A controller's d-pad or left stick, A (south) = accept, B (east) = back,
 * the Next Song button (default right stick click) skips the song. On a
 * Controls page action row, accept waits for the button to bind: press it
 * on the controller (Esc cancels).
 */
#include "hp2_fe.h"
#include "hp2_fedraw.h"
#include "hp2_input.h"
#include "hp2_jbplay.h"
#include "hp2_race.h"
#include "hp2_femenu.h"
#include "hp2_font.h"
#include "hp2_lang.h"
#include "hp2_settings.h"
#include "hp2_tex.h"
#include "hp2_tree.h"
#include "hp2_zdir.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

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
        if (!bank) {
            uint32_t size;
            f->bank = hp2_zdir_load_part(&z, "SOUND\\JUKEBOXM.JBC", hp2_jukebox_header_size(), &f->bank_len);
            if (hp2_zdir_locate(&z, "SOUND\\JUKEBOXM.JBC", &f->bank_base, &size) == 0)
                snprintf(f->bank_path, sizeof f->bank_path, "%s", z.data);
        }
        hp2_zdir_close(&z);
        if (!f->lang) fprintf(stderr, "%s not in the archive; showing the layouts' own text\n", path);
    } else {
        f->frontb = hp2_read_file(loose[0], &f->frontb_len);
        f->globalb = hp2_read_file(loose[1], &f->globalb_len);
        f->lang = hp2_read_file(loose[2], &f->lang_len);
    }
    if (bank) {   /* a bank of your own, with --disc or --files */
        f->bank = hp2_read_file_part(bank, hp2_jukebox_header_size(), &f->bank_len);
        f->bank_base = 0;
        snprintf(f->bank_path, sizeof f->bank_path, "%s", bank);
        if (!f->bank) fprintf(stderr, "could not read the music bank %s\n", bank);
    }
    if (!f->frontb || !f->globalb) {
        fprintf(stderr, "could not read FRONTB.BUN and GLOBALB.BUN\n");
        return -1;
    }
    return 0;
}

/* hp2track's path: --hp2track, else beside this program. */
static void track_path(char *out, size_t cap, const char *given, const char *argv0)
{
    const char *slash = NULL, *p;
    if (given) { snprintf(out, cap, "%s", given); return; }
    for (p = argv0; *p; p++)
        if (*p == '/' || *p == '\\') slash = p;
#ifdef _WIN32
    if (slash) snprintf(out, cap, "%.*shp2track.exe", (int)(slash - argv0 + 1), argv0);
    else snprintf(out, cap, "hp2track.exe");
#else
    if (slash) snprintf(out, cap, "%.*shp2track", (int)(slash - argv0 + 1), argv0);
    else snprintf(out, cap, "./hp2track");
#endif
}

/* The race the menus asked for, as hp2track's command line: program, --disc
 * DIR, the race. Returns the argument count (args NULL-terminated). */
#define RACE_ARGS 48
static int race_command(const hp2_femenu *m, const char *prog, const char *disc, int W, int H,
                        const char *result, char buf[][32], const char **args)
{
    int n = 0, k, r = hp2_femenu_race_args(&m->go, buf, RACE_ARGS - 8);
    args[n++] = prog;
    args[n++] = "--disc";
    args[n++] = disc;
    for (k = 0; k < r; k++) args[n++] = buf[k];
    snprintf(buf[r], 32, "--size");
    snprintf(buf[r + 1], 32, "%dx%d", W, H);
    args[n++] = buf[r];
    args[n++] = buf[r + 1];
    if (result) { args[n++] = "--result"; args[n++] = result; }
    args[n] = NULL;
    return n;
}

static void print_command(const char *const *args)
{
    int k;
    printf("race:");
    for (k = 0; args[k]; k++) printf(" %s", args[k]);
    printf("\n");
    fflush(stdout);
}

/* Reads the file hp2track wrote when it closed, scores it (the event's
 * objective) and removes it. */
static void take_result(hp2_femenu *m, const char *path)
{
    char txt[256];
    hp2_race_result r;
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return;
    n = fread(txt, 1, sizeof txt - 1, f);
    fclose(f);
    remove(path);
    txt[n] = 0;
    if (!hp2_race_result_parse(txt, &r)) return;
    printf("result: finished %d place %d ticks %ld best %ld objective %s\n", r.finished, r.place, r.ticks, r.best,
           hp2_femenu_result(m, &r) ? "met" : "not met");
    fflush(stdout);
}

/* Runs hp2track and waits for it. 0 when it ran. */
static int run_race(const char *const *args)
{
#ifdef _WIN32
    return _spawnv(_P_WAIT, args[0], args) == -1 ? -1 : 0;
#else
    pid_t pid;
    int st;
    if (posix_spawn(&pid, args[0], NULL, NULL, (char *const *)args, environ) != 0) return -1;
    while (waitpid(pid, &st, 0) < 0) {}
    return 0;
#endif
}

static int parse_input(const char *s, hp2_fem_input *in)
{
    static const char *names[] = { "up", "down", "left", "right", "accept", "back", "help" };
    int i;
    for (i = 0; i < 7; i++)
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

/* Bind rows name buttons the way the pad in hand does. */
static void update_glyphs(hp2_femenu *m, const hp2_settings *cfg, SDL_GameController *pad)
{
    hp2_menu_set_glyphs(hp2_resolve_glyphs(cfg->glyphs, hp2_input_kind_of(pad)));
    hp2_femenu_refresh(m);
}

/* A song set to Menu or On in the Jukebox starts now (unless it is already
 * playing). One set to Off keeps playing to its end and is not picked again. */
/* A song's status changed in the Jukebox: set to Menu or On, it plays now;
 * the song playing set to Off stops the music (until a song is set to Menu
 * or On, Next Song, or leaving the Jukebox). */
static void follow_jukebox(hp2_femenu *m, hp2_jbplay *p, SDL_AudioDeviceID audio)
{
    int s = m->changed_song;
    if (!audio || s < 0) return;
    if (hp2_jbplay_in_menus(hp2_femenu_play(m, s))) {
        if (hp2_jbplay_now(p) == s) return;
        hp2_jbplay_play(p, s);
    } else {
        if (hp2_jbplay_now(p) != s) return;
        hp2_jbplay_stop(p);
    }
    SDL_ClearQueuedAudio(audio);
}

static void next_song(hp2_jbplay *p, SDL_AudioDeviceID audio)
{
    if (!audio) return;
    hp2_jbplay_skip(p);
    SDL_ClearQueuedAudio(audio);
}

/* The song box: shown for a few seconds when a song starts. */
typedef struct {
    const hp2_fe_screen *screen;    /* GLOBALB's 0x54987EB2, or NULL */
    const hp2_jukebox   *jb;
    int                  song;      /* shown, or -1 */
    int                  last;      /* the last song seen playing */
    Uint32               until;
    char                 quoted[80];
} songbox;

#define SONGBOX_MS 6000

/* Shows the box when a new song starts and hides it when its time is up.
 * Returns 1 if the screen needs drawing again. */
static int songbox_update(songbox *b, int now_playing, Uint32 ticks)
{
    int redraw = 0;
    if (now_playing >= 0 && now_playing != b->last && b->screen) {
        b->song = now_playing;
        b->until = ticks + SONGBOX_MS;
        redraw = 1;
    }
    b->last = now_playing;           /* stopped: -1, so the song shows again when restarted */
    if (b->song >= 0 && (now_playing < 0 || (int32_t)(ticks - b->until) >= 0)) { b->song = -1; redraw = 1; }
    return redraw;
}

static void draw(hp2_fedraw *d, hp2_femenu *m, songbox *box, SDL_Renderer *r, int W, int H, hp2_fe_fit fit)
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
    if (box->song >= 0 && box->jb) {   /* on top of the menu */
        n = hp2_femenu_songbox(box->screen, &box->jb->track[box->song], box->quoted, sizeof box->quoted, items, MAX_ITEMS);
        hp2_fedraw_items(d, &v, items, n);
    }
}

int main(int argc, char **argv)
{
    int open_all = 0;
    char prog_path[1100];
    static hp2_progress progress;
    const char *disc = NULL, *language = "ENGLISH", *config = "hp2.ini", *shot = NULL, *keys = NULL, *bankpath = NULL;
    const char *trackprog = NULL, *race_args[RACE_ARGS + 1];
    char prog[1024], result_path[1100], race_buf[RACE_ARGS][32];
    static hp2_jukebox jb;
    static hp2_tree trees[2];
    char *loose[3] = { NULL, NULL, NULL };
    int W = 1280, H = 720, forced_fit = -1, i, quit = 0, dirty = 0, music = 1, shot_song = -1, start_options = 0;
    hp2_fe_layout glayout;
    songbox box;
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
        else if (!strcmp(argv[i], "--options")) start_options = 1;
        else if (!strcmp(argv[i], "--songbox") && i + 1 < argc) shot_song = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--open")) open_all = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 64 || H < 48) { W = 1280; H = 720; } }
        else if (!strcmp(argv[i], "--fit") && i + 1 < argc) {
            const char *s = argv[++i];
            forced_fit = !strcmp(s, "fit") ? HP2_FE_FIT : !strcmp(s, "stretch") ? HP2_FE_STRETCH : !strcmp(s, "expand") ? HP2_FE_EXPAND : -1;
        }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else if (!strcmp(argv[i], "--hp2track") && i + 1 < argc) trackprog = argv[++i];
        else {
            fprintf(stderr, "usage: %s --disc DIR | --files FRONTB.BUN GLOBALB.BUN FE_LANG.BIN\n"
                            "       [--language NAME] [--jukebox BANK] [--options] [--no-music] [--config PATH] [--size WxH] [--open]\n"
                            "       [--fit fit|stretch|expand]\n"
                            "       [--shot OUT.bmp] [--keys up,down,left,right,accept,back,help] [--hp2track PATH]\n", argv[0]);
            return 2;
        }
    }
    track_path(prog, sizeof prog, trackprog, argv[0]);
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
    snprintf(prog_path, sizeof prog_path, "%s.save", config);
    if (shot) open_all = 1;                /* screenshots show every event open */
    if (!open_all) hp2_progress_load(&progress, prog_path);
    if (hp2_femenu_init(&menu, &layout, lang.count ? &lang : NULL, &cfg) != 0) {
        fprintf(stderr, "FRONTB.BUN lacks the OPTIONS / DISPLAY_OPTIONS screens\n");
        return 1;
    }
    if (!open_all) menu.prog = &progress;
    if (hp2_tree_parse(f.frontb, f.frontb_len, trees) == 2) menu.trees = trees;   /* the career events */
    if (!start_options && !hp2_femenu_start(&menu, HP2_FEM_TITLE)) hp2_femenu_start(&menu, HP2_FEM_MAIN);
    menu.seed = (uint32_t)time(NULL) ^ (uint32_t)SDL_GetTicks() * 2654435761u;   /* Quick Race's dice */
    if (f.bank && hp2_jukebox_parse(&jb, f.bank, f.bank_len) == 0) hp2_femenu_set_jukebox(&menu, &jb);
    memset(&box, 0, sizeof box);
    box.song = box.last = -1;
    /* GLOBALB.BUN has screen layouts of its own: the song box is one */
    if (hp2_fe_parse(f.globalb, f.globalb_len, &glayout) == 0) {
        box.screen = hp2_fe_find_id(&glayout, HP2_SONGBOX_ID);
    } else {
        memset(&glayout, 0, sizeof glayout);
    }
    box.jb = menu.jb;
    if (shot && shot_song >= 0 && menu.jb && shot_song < jb.count) box.song = shot_song;
    if (!menu.jb && (disc || bankpath)) fprintf(stderr, "no jukebox song table; no Jukebox\n");
    if (keys) {
        char buf[512], *t;
        snprintf(buf, sizeof buf, "%s", keys);
        for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            hp2_fem_input in;
            if (parse_input(t, &in)) {
                int res = hp2_femenu_input(&menu, in);
                dirty |= res & HP2_FEM_CHANGED;
                if (res & HP2_FEM_RACE) {   /* scripted: say what would run */
                    race_command(&menu, "hp2track", disc ? disc : "DIR", W, H, NULL, race_buf, race_args);
                    print_command(race_args);
                }
            }
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
    update_glyphs(&menu, &cfg, pad);           /* button names: the settings, then the pad in hand */

    if (shot) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
        draw(&d, &menu, &box, ren, W, H, fit_for(&cfg, forced_fit));
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
        int ow, oh, got = 1, redraw = 0;
        if (audio) {
            /* keep about half a second queued: a volume change or a song
             * switched off is heard within that */
            static int16_t pcm[2 * 2048];
            while (SDL_GetQueuedAudioSize(audio) < (Uint32)(jb.rate * 4 / 2)) {
                int n = hp2_jbplay_fill(&player, pcm, 2048);
                if (n <= 0) break;
                SDL_QueueAudio(audio, pcm, (Uint32)n * 4);
            }
            redraw = songbox_update(&box, hp2_jbplay_now(&player), SDL_GetTicks());
            got = SDL_WaitEventTimeout(&e, 40);
            if (!got && !redraw) continue;
        } else if (!SDL_WaitEvent(&e)) {
            break;
        }
        if (got) do {
            hp2_fem_input in;
            int have = 0, pressed = -1;          /* pressed: an hp2_button from the pad */
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
                case SDLK_h: case SDLK_F1: in = HP2_IN_HELP; break;
                case SDLK_n: have = 0; if (hp2_femenu_capturing(&menu) < 0) next_song(&player, audio); break;
                default: have = 0;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(e.cdevice.which);
                update_glyphs(&menu, &cfg, pad);
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad
                       && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == e.cdevice.which) {
                SDL_GameControllerClose(pad);
                pad = NULL;
                update_glyphs(&menu, &cfg, pad);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                pressed = hp2_input_button_of(e.cbutton.button);
                have = hp2_femenu_capturing(&menu) < 0;   /* while binding, a press is the answer */
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: in = HP2_IN_UP; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in = HP2_IN_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: in = HP2_IN_LEFT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in = HP2_IN_RIGHT; break;
                /* by position, as on the PS2: Cross (south) or Start
                 * accepts, Circle (east) goes back, Triangle (north) is help */
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: in = HP2_IN_ACCEPT; break;
                case SDL_CONTROLLER_BUTTON_B: in = HP2_IN_BACK; break;
                case SDL_CONTROLLER_BUTTON_Y: in = HP2_IN_HELP; break;
                default: have = 0;
                }
            } else if (e.type == SDL_CONTROLLERAXISMOTION) {
                /* left stick as a d-pad: one step when it crosses half way */
                static int last[2];
                int axis = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? 1 : -1;
                static int trig[2];
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, down = e.caxis.value > 16000;
                    if (down && !trig[t]) pressed = t ? HP2_BTN_RTRIGGER : HP2_BTN_LTRIGGER;
                    trig[t] = down;
                }
                if (axis >= 0 && hp2_femenu_capturing(&menu) < 0) {
                    int dir = e.caxis.value > 16000 ? 1 : e.caxis.value < -16000 ? -1 : 0;
                    if (dir && dir != last[axis]) {
                        have = 1;
                        in = axis ? (dir > 0 ? HP2_IN_DOWN : HP2_IN_UP) : (dir > 0 ? HP2_IN_RIGHT : HP2_IN_LEFT);
                    }
                    last[axis] = dir;
                }
            }
            if (pressed >= 0) {
                if (hp2_femenu_capturing(&menu) >= 0) {
                    if (hp2_femenu_capture(&menu, (hp2_button)pressed) & HP2_FEM_CHANGED) dirty = 1;
                } else if (pressed == (int)hp2_bound_button(&cfg, HP2_ACT_NEXT_SONG)) {
                    next_song(&player, audio);
                }
            }
            if (have) {
                hp2_fem_screen before = menu.screen;
                int res = hp2_femenu_input(&menu, in);
                if (res & HP2_FEM_CHANGED) {
                    dirty = 1;
                    if (menu.screen == HP2_FEM_SETTINGS && menu.sel == HP2_ROW_WINDOW) apply_window_mode(win, &cfg);
                    if (menu.screen == HP2_FEM_SETTINGS && menu.sel == HP2_ROW_GLYPHS) update_glyphs(&menu, &cfg, pad);
                    if (menu.screen == HP2_FEM_JUKEBOX) follow_jukebox(&menu, &player, audio);
                }
                if (before == HP2_FEM_JUKEBOX && menu.screen != before && hp2_jbplay_stopped(&player))
                    hp2_jbplay_resume(&player);    /* the menus have music again */
                if (res & HP2_FEM_QUIT) quit = 1;
                if (res & HP2_FEM_RACE) {
                    /* the race: hp2track in its own window, the menus
                     * hidden and their music paused until it closes */
                    int ww, wh;
                    SDL_GetWindowSize(win, &ww, &wh);
                    snprintf(result_path, sizeof result_path, "%s.race", config);
                    race_command(&menu, prog, disc ? disc : "", ww, wh, result_path, race_buf, race_args);
                    print_command(race_args);
                    if (!disc) {
                        fprintf(stderr, "a race needs your disc: start hp2fe with --disc\n");
                    } else {
                        if (audio) SDL_PauseAudioDevice(audio, 1);
                        hp2_femenu_result(&menu, NULL);        /* the last race is forgotten */
                        remove(result_path);
                        SDL_HideWindow(win);
                        if (run_race(race_args) != 0) fprintf(stderr, "could not run %s (give --hp2track PATH)\n", prog);
                        take_result(&menu, result_path);
                        if (menu.prog_dirty) { hp2_progress_save(&progress, prog_path); menu.prog_dirty = 0; }
                        SDL_ShowWindow(win);
                        SDL_RaiseWindow(win);
                        if (audio) SDL_PauseAudioDevice(audio, 0);
                        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
                    }
                }
                if (dirty && (before == HP2_FEM_SETTINGS || before == HP2_FEM_JUKEBOX) && menu.screen != before) {
                    if (!hp2_settings_save(&cfg, config)) fprintf(stderr, "could not save %s\n", config);
                    dirty = 0;
                }
            }
        } while (!quit && SDL_PollEvent(&e));
        if (quit) break;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        draw(&d, &menu, &box, ren, ow, oh, fit_for(&cfg, forced_fit));
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
    hp2_fe_free(&glayout);
    free(f.frontb);
    free(f.globalb);
    free(f.lang);
    free(f.bank);
    return 0;
}
