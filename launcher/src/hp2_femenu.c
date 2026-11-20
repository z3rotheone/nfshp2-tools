/* hp2_femenu - Options menus from the original layouts. See hp2_femenu.h. */
#include "hp2_femenu.h"
#include "hp2_jbplay.h"

#include <stdio.h>
#include <string.h>

/* string ids of OPTIONS rows: two open the settings list, Languages is not
 * shown on the US disc. Row n's help line is string 100019 + n (Audio,
 * row 1: 100020 "Set the volume and the Jukebox."). */
#define SID_AUDIO      100001u
#define SID_CONTROLLER 100002u
#define SID_DISPLAY    100004u
#define SID_PLAYER_NAMES 100006u
#define SID_LANGUAGES  100007u
#define SID_CREDITS    100008u
#define SID_HELP_BASE  100019u
#define SID_HOT_PURSUIT  10023u
#define SID_WORLD_RACING 10022u
#define SID_OPTIONS_MAIN 10005u
#define SID_PROGRESS     62001u     /* "Unlocked" on Main Menu */

#define SID_QUICK_RACE   10027u     /* "Quick Race" (Hot Pursuit's row) */

/* Main Menu and the mode menus, as the port has them: each entry's label
 * (a string id, or the port's own name) and help line (FE_<LANG>.BIN
 * strings 10006-10017), in the port's order. */
enum { GO_NONE, GO_HOT_PURSUIT, GO_WORLD_RACING, GO_QUICK, GO_OPTIONS };
typedef struct {
    uint32_t    sid;        /* label string, 0 = name only */
    const char *name;       /* the port's own label, or the fallback */
    int         own;        /* 1: name replaces the game's label */
    uint32_t    help;
    int         go;
} list_entry;

static const list_entry MAIN_LIST[] = {
    { SID_HOT_PURSUIT,  "Hot Pursuit",  0, 10016, GO_HOT_PURSUIT },  /* Be the cops or beat the cops! */
    { SID_WORLD_RACING, "World Racing", 0, 10015, GO_WORLD_RACING }, /* Take on the world's best sports cars! */
    { SID_QUICK_RACE,   "Quick Race",   0, 10017, GO_QUICK },        /* Go straight into the action! */
    { SID_OPTIONS_MAIN, "Options",      0, 10009, GO_OPTIONS },      /* Customize your settings. */
};
static const list_entry HOT_PURSUIT_LIST[] = {
    { 10029, "Ultimate Fugitive",    1, 10013, GO_NONE },   /* Ultimate Racer: Beat the heat in 30 events. */
    { 10030, "Racing But With Pigs", 1, 10012, GO_NONE },   /* Challenge: One-player and two-player challenges. */
    { 10028, "You're The Pig",       1, 10014, GO_NONE },   /* You're The Cop: Fill your ticket quota for big points! */
};
static const list_entry WORLD_RACING_LIST[] = {
    { 10026, "Ultimate Racer", 1, 10008, GO_NONE },   /* Championship: Real cars, real fast in 30 events. */
    { 10025, "Just Race",      1, 10007, GO_NONE },   /* Challenge: One-player and two-player challenges. */
};

/* Row bands of these lists: SEP_H tall lines between rows, a filler from
 * the last line down to the ticker. The band texture (e2a5c626) is 16 px
 * tall: BOTTOM_MASK at y 228, scale 7.063 ends at 341 on Main Menu, at
 * y 283, scale 3.625 on Hot Pursuit too. */
#define BAND_TEX 16.0f

/* screen ids (the hash of their names is unknown) */
#define SCR_MAIN         0xAE0F8AA1u
#define SCR_HOT_PURSUIT  0x5B35F533u
#define SCR_WORLD_RACING 0x033E7771u
#define SCR_SELECT_CAR   0xB5074AB4u
#define SID_JUKEBOX    110020u      /* "Jukebox", AUDIO_OPTIONS' row 9 */
#define OPT_JUKEBOX    0            /* orow[] value of the Jukebox entry */

/* the selected row: orange like the game's NF_H_ORANGELINE (248, 128, 7) */
static const uint8_t ORANGE[4] = { 124, 64, 4, 0x80 };

static int has_prefix(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static int has_suffix(const char *s, const char *x)
{
    size_t n = strlen(s), k = strlen(x);
    return n >= k && strcmp(s + n - k, x) == 0;
}

static const hp2_fe_widget *named(const hp2_fe_screen *s, const char *name)
{
    int i;
    for (i = 0; s && i < s->count; i++)
        if (strcmp(s->widgets[i].name, name) == 0) return &s->widgets[i];
    return NULL;
}

/* The menu-row number of a MENUITEM_<n> widget, else 0. */
static int row_number(const hp2_fe_widget *w)
{
    int n = 0;
    const char *p;
    if (w->kind != HP2_FE_TEXT || !has_prefix(w->name, "MENUITEM_")) return 0;
    for (p = w->name + 9; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        n = n * 10 + (*p - '0');
    }
    return n;
}

#define FONT_BUTTONSOUT 0xB85DE386u   /* GLOBALB's button glyphs: i Cross, j Triangle, k Circle */

const char *hp2_femenu_text(const hp2_femenu *m, const hp2_fe_widget *w)
{
    const char *s = NULL;
    /* the corner prompt "(k) Help" shows Circle in the game; here Help is Triangle */
    if (w->font == FONT_BUTTONSOUT && !strcmp(w->text, "k")) return "j";
    if (w->string_id && m->lang) s = hp2_lang_by_id(m->lang, w->string_id);
    return s ? s : w->text;
}

int hp2_femenu_options_rows(const hp2_femenu *m) { return m->orows; }

/* OPTIONS rows the port leaves out: Player Names and Credits (not wanted),
 * Languages (not on the US disc either). */
static int hidden_row(const hp2_fe_widget *w)
{
    return w && (w->string_id == SID_PLAYER_NAMES || w->string_id == SID_LANGUAGES || w->string_id == SID_CREDITS);
}

/* ---- jukebox ------------------------------------------------------------- */

hp2_play hp2_femenu_play(const hp2_femenu *m, int song)
{
    return hp2_jbplay_status(m->jb, m->cfg, song);    /* one rule for menu and player */
}

/* The OPTIONS entries: the layout's rows less the hidden ones, with the
 * Jukebox right after Audio when there is a jukebox to show. */
static void build_options(hp2_femenu *m)
{
    int i, n = hp2_fe_row_count(m->options), cap = (int)(sizeof m->orow / sizeof m->orow[0]);
    m->orows = 0;
    for (i = 1; i <= n && m->orows < cap; i++) {
        const hp2_fe_widget *w = hp2_fe_row(m->options, i);
        if (hidden_row(w)) continue;
        m->orow[m->orows++] = i;
        if (w && w->string_id == SID_AUDIO && m->jb && m->jbscreen && m->orows < cap)
            m->orow[m->orows++] = OPT_JUKEBOX;
    }
    /* entry k takes row k's place: keep only as many as the layout has rows */
    if (m->orows > n) m->orows = n;
}

/* The OPTIONS entry that opens the Jukebox, or 0. */
static int jukebox_entry(const hp2_femenu *m)
{
    int k;
    for (k = 0; k < m->orows; k++)
        if (m->orow[k] == OPT_JUKEBOX) return k + 1;
    return 0;
}

void hp2_femenu_set_jukebox(hp2_femenu *m, const hp2_jukebox *jb)
{
    m->jb = jb && jb->count > 0 ? jb : NULL;
    if (m->options) {
        build_options(m);
        if (m->sel > m->orows && m->screen == HP2_FEM_OPTIONS) m->sel = m->orows;
        if (m->options_sel > m->orows) m->options_sel = m->orows;
    }
}

static void pick_genre(hp2_femenu *m, int g)
{
    m->genre = g;
    m->nsongs = hp2_jukebox_songs(m->jb, g, m->songs, HP2_JB_MAX);
    m->jscroll = 0;
}

static int jukebox_input(hp2_femenu *m, hp2_fem_input in)
{
    int r = 0, n = m->nsongs;
    switch (in) {
    case HP2_IN_UP:   m->jsel = m->jsel > 0 ? m->jsel - 1 : n; r = HP2_FEM_REDRAW; break;
    case HP2_IN_DOWN: m->jsel = m->jsel < n ? m->jsel + 1 : 0; r = HP2_FEM_REDRAW; break;
    case HP2_IN_LEFT:
    case HP2_IN_RIGHT: {
        int d = in == HP2_IN_LEFT ? -1 : 1;
        if (m->jsel == 0) {
            if (m->jb->genres > 1) {
                pick_genre(m, (m->genre + d + m->jb->genres) % m->jb->genres);
                r = HP2_FEM_REDRAW;
            }
        } else {
            int song = m->songs[m->jsel - 1];
            if (song < HP2_SONGS) {
                m->cfg->song[song] = (hp2_femenu_play(m, song) + d + HP2_PLAY_COUNT) % HP2_PLAY_COUNT;
                m->changed_song = song;
                r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
            }
        }
        break;
    }
    case HP2_IN_BACK:
        m->screen = HP2_FEM_OPTIONS;            /* back to OPTIONS, on Jukebox */
        m->sel = jukebox_entry(m) ? jukebox_entry(m) : 1;
        r = HP2_FEM_REDRAW;
        break;
    default: break;
    }
    if (m->jsel > 0) {   /* keep the selected song in view */
        if (m->jsel - 1 < m->jscroll) m->jscroll = m->jsel - 1;
        if (m->jsel - 1 >= m->jscroll + HP2_FEM_SONGS) m->jscroll = m->jsel - HP2_FEM_SONGS;
    } else {
        m->jscroll = 0;
    }
    return r;
}

/* The layout row an OPTIONS entry shows (NULL for the Jukebox entry), and
 * the row whose place it takes. */
static const hp2_fe_widget *options_row(const hp2_femenu *m, int sel)
{
    return sel >= 1 && sel <= m->orows && m->orow[sel - 1] != OPT_JUKEBOX
           ? hp2_fe_row(m->options, m->orow[sel - 1]) : NULL;
}

static const hp2_fe_widget *options_slot(const hp2_femenu *m, int sel)
{
    return sel >= 1 && sel <= m->orows ? hp2_fe_row(m->options, sel) : NULL;
}

static const char *options_text(const hp2_femenu *m, int sel)
{
    const hp2_fe_widget *w = options_row(m, sel);
    const char *s;
    if (w) return hp2_femenu_text(m, w);
    s = m->lang ? hp2_lang_by_id(m->lang, SID_JUKEBOX) : NULL;
    return s ? s : "Jukebox";
}

/* Main Menu and the mode menus: plain lists, row k in its own place. */
static const hp2_fe_screen *list_screen(const hp2_femenu *m)
{
    switch (m->screen) {
    case HP2_FEM_MAIN:         return m->mainm;
    case HP2_FEM_HOT_PURSUIT:  return m->hotp;
    case HP2_FEM_WORLD_RACING: return m->world;
    default:                   return NULL;
    }
}

static int list_rows(const hp2_fe_screen *s) { return s ? hp2_fe_row_count(s) : 0; }

/* ---- car select ----------------------------------------------------------- */

/* The cars of Quick Race, in the order of the game's own name list (strings
 * 41400+): the SELECT_CAR layout's thumbnail widget (THUMBNAIL_<thumb>), the
 * car's full name (string id) and where its specs sit in strings 42000
 * (0-60), 43000 (top speed, mph) and 44000 (bhp): id = base + 1 + spec. */
typedef struct { const char *thumb; uint32_t name; int spec; } car_entry;
static const car_entry CARS[HP2_CARS] = {
    { "CORVETTE", 41100, 1 }, { "VIPERGTS", 41101, 2 }, { "CLK_GTR", 41103, 5 }, { "MCLAREN", 41104, 6 },
    { "MCLARENLM", 41105, 7 }, { "MUSTANG", 41106, 8 }, { "TS50", 41107, 9 }, { "HOLDEN", 41108, 10 },
    { "CARRERAGT", 41109, 11 }, { "BMWM5", 41110, 12 }, { "BMWZ8", 41111, 13 }, { "SPIDER", 41112, 14 },
    { "F50", 41113, 15 }, { "JAGUAR", 41114, 16 }, { "VANQUISH", 41115, 17 }, { "VAUXHALL", 41120, 22 },
    { "OPEL", 41121, 23 }, { "911TURBO", 41122, 24 }, { "ELISE", 41123, 25 }, { "CL55AMG", 41124, 26 },
    { "DIABLO", 41125, 27 }, { "MURCIELAGO", 41126, 28 }, { "BARCHETTA", 41127, 40 },
};

const char *hp2_femenu_car_name(const hp2_femenu *m, int k)
{
    const char *s = NULL;
    if (k < 0 || k >= HP2_CARS) return NULL;
    if (m->lang) s = hp2_lang_by_id(m->lang, CARS[k].name);
    return s ? s : CARS[k].thumb;
}

static const char *lang_or(const hp2_femenu *m, uint32_t id, const char *dflt)
{
    const char *s = m->lang ? hp2_lang_by_id(m->lang, id) : NULL;
    return s ? s : dflt;
}

#define CAR_SWATCHES_N 8
#define STRIP_X    375.0f      /* the frame (THUMBNAIL_FRAME) */
#define STRIP_Y    399.0f
#define SPEC_LIFT   93.0f      /* the specs sit above and left of where the layout has them */
#define SPEC_SHIFT_X 183.0f

static int car_input(hp2_femenu *m, hp2_fem_input in)
{
    switch (in) {
    case HP2_IN_UP:    m->color = (m->color + CAR_SWATCHES_N - 1) % CAR_SWATCHES_N; return HP2_FEM_REDRAW;
    case HP2_IN_DOWN:  m->color = (m->color + 1) % CAR_SWATCHES_N; return HP2_FEM_REDRAW;
    case HP2_IN_LEFT:  m->car = (m->car + HP2_CARS - 1) % HP2_CARS; return HP2_FEM_REDRAW;
    case HP2_IN_RIGHT: m->car = (m->car + 1) % HP2_CARS; return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        m->screen = HP2_FEM_MAIN;
        m->sel = m->main_sel;
        return HP2_FEM_REDRAW;
    default: return 0;      /* accept: the track list is not in the port yet */
    }
}

static const list_entry *list_entries(const hp2_femenu *m, int *n)
{
    switch (m->screen) {
    case HP2_FEM_MAIN:         *n = (int)(sizeof MAIN_LIST / sizeof MAIN_LIST[0]); return MAIN_LIST;
    case HP2_FEM_HOT_PURSUIT:  *n = (int)(sizeof HOT_PURSUIT_LIST / sizeof HOT_PURSUIT_LIST[0]); return HOT_PURSUIT_LIST;
    case HP2_FEM_WORLD_RACING: *n = (int)(sizeof WORLD_RACING_LIST / sizeof WORLD_RACING_LIST[0]); return WORLD_RACING_LIST;
    default:                   *n = 0; return NULL;
    }
}

int hp2_femenu_list_count(const hp2_femenu *m)
{
    int n;
    list_entries(m, &n);
    return n;
}

const char *hp2_femenu_list_label(const hp2_femenu *m, int k)
{
    int n;
    const list_entry *e = list_entries(m, &n);
    const char *s = NULL;
    if (!e || k < 1 || k > n) return NULL;
    e += k - 1;
    if (e->own) return e->name;
    if (e->sid && m->lang) s = hp2_lang_by_id(m->lang, e->sid);
    return s ? s : e->name;
}

static const char *quick_name(int q)
{
    return q == HP2_QUICK_HOT_PURSUIT ? "Hot Pursuit" : q == HP2_QUICK_WORLD_RACING ? "World Racing" : "";
}

static void list_help(hp2_femenu *m)
{
    int n;
    const list_entry *e = list_entries(m, &n);
    const char *h;
    if (!e || m->sel < 1 || m->sel > n) return;
    e += m->sel - 1;
    if (e->go == GO_QUICK && m->quick_mode) {
        snprintf(m->help, sizeof m->help, "Quick Race: %s settings. Its race screens are not in the port yet.",
                 quick_name(m->quick_mode));
        return;
    }
    h = m->lang ? hp2_lang_by_id(m->lang, e->help) : NULL;
    if (h) snprintf(m->help, sizeof m->help, "%s", h);
}

/* The ticker after HELP: the menu buttons, named as the pad (or keyboard)
 * in use names them. */
static void buttons_line(hp2_femenu *m)
{
    hp2_glyph_set g = hp2_menu_glyphs();
    if (g == HP2_GLYPHS_KEYBOARD)
        snprintf(m->help, sizeof m->help, "Enter: accept   Esc: back   H: help");
    else
        snprintf(m->help, sizeof m->help, "%s or %s: accept   %s: back   %s: help",
                 hp2_button_label(HP2_BTN_SOUTH, g), hp2_button_label(HP2_BTN_START, g),
                 hp2_button_label(HP2_BTN_EAST, g), hp2_button_label(HP2_BTN_NORTH, g));
}

static void refresh(hp2_femenu *m)
{
    int i;
    for (i = 0; i < HP2_ROW_COUNT; i++) hp2_menu_value(m->cfg, i, m->value[i], sizeof m->value[i]);
    m->help[0] = 0;
    if (m->capture >= 0) {
        snprintf(m->value[m->capture], sizeof m->value[m->capture], "%s", "Press a button");
        snprintf(m->help, sizeof m->help, "Press the button for %s. Esc cancels.", hp2_menu_label(m->capture));
    } else if (m->show_buttons) {
        buttons_line(m);
    } else if (m->screen == HP2_FEM_SETTINGS) {
        snprintf(m->help, sizeof m->help, "%s", hp2_menu_help(m->sel));
    } else if (list_screen(m)) {
        list_help(m);
    } else if (m->screen == HP2_FEM_TITLE || m->screen == HP2_FEM_SELECT_CAR) {
        /* no ticker */
    } else if (m->screen == HP2_FEM_OPTIONS && m->sel >= 1 && m->sel <= m->orows
               && m->orow[m->sel - 1] == OPT_JUKEBOX) {
        snprintf(m->help, sizeof m->help, "%s", hp2_menu_help(HP2_ROW_JUKEBOX));
    } else if (m->lang && m->sel >= 1 && m->sel <= m->orows) {
        const char *h = hp2_lang_by_id(m->lang, SID_HELP_BASE + (uint32_t)m->orow[m->sel - 1]);
        if (h) snprintf(m->help, sizeof m->help, "%s", h);
    }
}

void hp2_femenu_refresh(hp2_femenu *m) { refresh(m); }

int hp2_femenu_capturing(const hp2_femenu *m) { return m->capture; }

int hp2_femenu_capture(hp2_femenu *m, hp2_button b)
{
    int a = hp2_menu_bind_action(m->capture), r = HP2_FEM_REDRAW;
    if (a < 0) return 0;
    if (hp2_bind(m->cfg, (hp2_action)a, b)) r |= HP2_FEM_CHANGED;
    m->capture = -1;
    refresh(m);
    return r;
}

void hp2_femenu_cancel_capture(hp2_femenu *m)
{
    m->capture = -1;
    refresh(m);
}

int hp2_femenu_init(hp2_femenu *m, const hp2_fe_layout *layout, const hp2_lang *lang, hp2_settings *cfg)
{
    memset(m, 0, sizeof *m);
    m->capture = m->changed_song = -1;
    m->layout = layout;
    m->lang = lang;
    m->cfg = cfg;
    m->options = hp2_fe_find(layout, "OPTIONS");
    m->display = hp2_fe_find(layout, "DISPLAY_OPTIONS");
    m->audio = hp2_fe_find(layout, "AUDIO_OPTIONS");
    m->jbscreen = hp2_fe_find(layout, "JUKEBOX_INGAME");
    m->title = hp2_fe_find(layout, "TITLE_SCREEN");
    m->mainm = hp2_fe_find_id(layout, SCR_MAIN);
    m->hotp = hp2_fe_find_id(layout, SCR_HOT_PURSUIT);
    m->world = hp2_fe_find_id(layout, SCR_WORLD_RACING);
    m->carsel = hp2_fe_find_id(layout, SCR_SELECT_CAR);
    if (!hp2_fe_row(m->mainm, 1)) m->mainm = NULL;
    if (!hp2_fe_row(m->hotp, 1)) m->hotp = NULL;
    if (!hp2_fe_row(m->world, 1)) m->world = NULL;
    m->main_sel = 1;
    if (!m->options || !m->display || !hp2_fe_row(m->display, 1)) return -1;
    if (m->audio && !hp2_fe_row(m->audio, 1)) m->audio = NULL;
    build_options(m);
    if (m->orows < 1) return -1;
    m->screen = HP2_FEM_OPTIONS;
    m->sel = m->options_sel = 1;
    refresh(m);
    return 0;
}

/* Opens the page of a section ("Video", "Controls", "Audio"): its rows are
 * the hp2_menu rows of that section, which are contiguous. */
static void open_page(hp2_femenu *m, const char *section)
{
    int r;
    m->first = -1;
    m->rows = 0;
    for (r = 0; r < HP2_ROW_COUNT; r++)
        if (!strcmp(hp2_menu_section(r), section)) {
            if (m->first < 0) m->first = r;
            m->rows++;
        }
    if (m->first < 0) return;
    m->options_sel = m->sel;
    m->screen = HP2_FEM_SETTINGS;
    m->page = !strcmp(section, "Audio") && m->audio ? m->audio : m->display;
    m->sel = m->scroll = m->first;
}

int hp2_femenu_start(hp2_femenu *m, hp2_fem_screen s)
{
    const hp2_fe_screen *need = s == HP2_FEM_TITLE ? m->title : s == HP2_FEM_MAIN ? m->mainm
                              : s == HP2_FEM_HOT_PURSUIT ? m->hotp : s == HP2_FEM_WORLD_RACING ? m->world
                              : s == HP2_FEM_SELECT_CAR ? m->carsel : s == HP2_FEM_OPTIONS ? m->options : NULL;
    if (!need) return 0;
    m->screen = s;
    m->sel = 1;
    m->options_from_main = s != HP2_FEM_OPTIONS;
    refresh(m);
    return 1;
}

/* Main Menu's entry that goes to go, or 1. */
static int main_row_of(int go)
{
    int k, n = (int)(sizeof MAIN_LIST / sizeof MAIN_LIST[0]);
    for (k = 1; k <= n; k++)
        if (MAIN_LIST[k - 1].go == go) return k;
    return 1;
}

/* Quick Race's dice: xorshift32 on m->seed. */
static int roll_quick(hp2_femenu *m)
{
    uint32_t x = m->seed ? m->seed : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    m->seed = x;
    return (x >> 16) & 1 ? HP2_QUICK_WORLD_RACING : HP2_QUICK_HOT_PURSUIT;
}

static int title_input(hp2_femenu *m, hp2_fem_input in)
{
    if (in == HP2_IN_BACK) return HP2_FEM_QUIT;
    if (in != HP2_IN_ACCEPT || !m->mainm) return 0;
    m->screen = HP2_FEM_MAIN;
    m->sel = m->main_sel;
    m->options_from_main = 1;
    return HP2_FEM_REDRAW;
}

static int list_input(hp2_femenu *m, hp2_fem_input in)
{
    int n;
    const list_entry *e = list_entries(m, &n);
    if (in == HP2_IN_UP || in == HP2_IN_DOWN) m->quick_mode = HP2_QUICK_NONE;   /* its note goes */
    switch (in) {
    case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : n; return HP2_FEM_REDRAW;
    case HP2_IN_DOWN: m->sel = m->sel < n ? m->sel + 1 : 1; return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT:
        if (!e || m->sel < 1 || m->sel > n) return 0;
        switch (e[m->sel - 1].go) {
        case GO_HOT_PURSUIT:  if (!m->hotp) return 0; m->screen = HP2_FEM_HOT_PURSUIT; break;
        case GO_WORLD_RACING: if (!m->world) return 0; m->screen = HP2_FEM_WORLD_RACING; break;
        case GO_OPTIONS:      m->screen = HP2_FEM_OPTIONS; break;
        case GO_QUICK:
            m->quick_mode = roll_quick(m);
            if (!m->carsel) return HP2_FEM_REDRAW;
            m->main_sel = m->sel;
            m->screen = HP2_FEM_SELECT_CAR;
            return HP2_FEM_REDRAW;
        default:              return 0;                               /* the race modes: not yet */
        }
        m->main_sel = m->sel;
        m->sel = 1;
        return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        m->quick_mode = HP2_QUICK_NONE;
        if (m->screen == HP2_FEM_MAIN) {
            m->main_sel = m->sel;
            if (!m->title) return HP2_FEM_QUIT;
            m->screen = HP2_FEM_TITLE;
            m->sel = 1;
        } else {
            m->screen = HP2_FEM_MAIN;
            m->sel = m->main_sel;
        }
        return HP2_FEM_REDRAW;
    default: return 0;
    }
}

const char *hp2_femenu_page(const hp2_femenu *m)
{
    return m->screen == HP2_FEM_SETTINGS ? hp2_menu_section(m->first) : "";
}

int hp2_femenu_input(hp2_femenu *m, hp2_fem_input in)
{
    int r = 0;
    m->changed_song = -1;
    if (m->capture >= 0) {                      /* waiting for a button: only back cancels */
        if (in == HP2_IN_BACK) { hp2_femenu_cancel_capture(m); return HP2_FEM_REDRAW; }
        return 0;
    }
    if (in == HP2_IN_HELP) {
        m->show_buttons = !m->show_buttons;
        refresh(m);
        return HP2_FEM_REDRAW;
    }
    if (m->screen == HP2_FEM_JUKEBOX) {
        r = jukebox_input(m, in);
    } else if (m->screen == HP2_FEM_SELECT_CAR) {
        r = car_input(m, in);
    } else if (m->screen == HP2_FEM_TITLE) {
        r = title_input(m, in);
    } else if (list_screen(m)) {
        r = list_input(m, in);
    } else if (m->screen == HP2_FEM_OPTIONS) {
        int n = m->orows;
        const hp2_fe_widget *w = options_row(m, m->sel);
        switch (in) {
        case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : n; r = HP2_FEM_REDRAW; break;
        case HP2_IN_DOWN: m->sel = m->sel < n ? m->sel + 1 : 1; r = HP2_FEM_REDRAW; break;
        case HP2_IN_ACCEPT:
            if (m->sel >= 1 && m->sel <= n && m->orow[m->sel - 1] == OPT_JUKEBOX && m->jb && m->jbscreen) {
                m->options_sel = m->sel;
                m->screen = HP2_FEM_JUKEBOX;
                m->jsel = 0;
                pick_genre(m, 0);
                r = HP2_FEM_REDRAW;
                break;
            }
            if (!w) break;
            if (w->string_id == SID_DISPLAY) open_page(m, "Video");
            else if (w->string_id == SID_CONTROLLER) open_page(m, "Controls");
            else if (w->string_id == SID_AUDIO) open_page(m, "Audio");
            if (m->screen == HP2_FEM_SETTINGS) r = HP2_FEM_REDRAW;
            break;
        case HP2_IN_BACK:
            if (m->options_from_main && m->mainm) {
                m->screen = HP2_FEM_MAIN;
                m->sel = main_row_of(GO_OPTIONS);
                r = HP2_FEM_REDRAW;
            } else {
                r = HP2_FEM_QUIT;
            }
            break;
        default: break;
        }
    } else {
        int last = m->first + m->rows - 1;
        switch (in) {
        case HP2_IN_UP:   m->sel = m->sel > m->first ? m->sel - 1 : last; r = HP2_FEM_REDRAW; break;
        case HP2_IN_DOWN: m->sel = m->sel < last ? m->sel + 1 : m->first; r = HP2_FEM_REDRAW; break;
        case HP2_IN_LEFT:
        case HP2_IN_RIGHT:
            if (hp2_menu_adjust(m->cfg, m->sel, in == HP2_IN_LEFT ? -1 : 1)) r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
            break;
        case HP2_IN_ACCEPT:
            if (hp2_menu_bind_action(m->sel) >= 0) {
                m->capture = m->sel;
                r = HP2_FEM_REDRAW;
            } else if (hp2_menu_kind(m->sel) == HP2_KIND_ACTION) {
                if (hp2_menu_activate(m->cfg, m->sel)) r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
            } else if (hp2_menu_kind(m->sel) == HP2_KIND_TOGGLE) {
                if (hp2_menu_adjust(m->cfg, m->sel, 1)) r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
            }
            break;
        case HP2_IN_BACK:
            m->screen = HP2_FEM_OPTIONS;
            m->sel = m->options_sel;
            r = HP2_FEM_REDRAW;
            break;
        default: break;
        }
        if (m->screen == HP2_FEM_SETTINGS) {
            if (m->sel < m->scroll) m->scroll = m->sel;
            if (m->sel >= m->scroll + HP2_FEM_VISIBLE) m->scroll = m->sel - HP2_FEM_VISIBLE + 1;
        }
    }
    if (r) refresh(m);
    return r;
}

/* ---- item lists ---------------------------------------------------------- */

typedef struct { hp2_fe_item *it; int n, cap; } list;

static hp2_fe_item *push(list *l, const hp2_fe_widget *w, const char *str)
{
    hp2_fe_item *i;
    if (!w || l->n == l->cap) return NULL;
    i = &l->it[l->n++];
    memset(i, 0, sizeof *i);
    i->w = *w;
    i->str = str;
    return i;
}

static void set_clip(hp2_fe_item *i, const float r[4])
{
    if (!i) return;
    i->clip = 1;
    memcpy(i->clip_rect, r, sizeof i->clip_rect);
}

static void grey(hp2_fe_item *i, uint8_t v)
{
    if (i) i->w.rgba[0] = i->w.rgba[1] = i->w.rgba[2] = v;
}

/* Widgets the screen code places itself, never drawn where they are stored:
 * the selected-row animation pair (parked off screen) and the arrows. */
static int animated(const hp2_fe_widget *w)
{
    return !strcmp(w->name, "ANIMATING_MENUITEM") || !strcmp(w->name, "ANIMATE_SELECTION")
        || has_suffix(w->name, "_ARROW");
}

/* The ticker in the help bar, clipped to its bar. */
static void help_bar(hp2_femenu *m, list *l, const hp2_fe_screen *s)
{
    const hp2_fe_widget *t = named(s, "SCROLLING_TEXT"), *c = named(s, "SCROLL_TEXT_CLIP");
    hp2_fe_item *i = push(l, t, m->help);
    if (i && c) {
        float r[4] = { c->x, c->y, 512.0f * c->sx, 24.0f * c->sy };   /* TOP_BORDER is 512 x 24 */
        set_clip(i, r);
    }
}

static int is_line(const hp2_fe_widget *w)
{
    return !strcmp(w->name, "MI_OVERSCORE") || !strcmp(w->name, "MI_UNDERSCORE");
}

/* The white line above and the orange line below the selected row whose
 * label sits at y. Measured on PCSX2 screenshots: the line centres are
 * 0.5 above the label's y and 32.5 below it; each texture is 8 rows tall
 * with its line in the middle. */
static void selection_lines(const hp2_femenu *m, list *l, const hp2_fe_screen *s, float y)
{
    const hp2_fe_widget *o = named(s, "MI_OVERSCORE"), *u = named(s, "MI_UNDERSCORE");
    hp2_fe_item *it;
    (void)m;
    if ((it = push(l, o, NULL)) != NULL) it->w.y = y - 4.5f;
    if ((it = push(l, u, NULL)) != NULL) it->w.y = y + 28.5f;
}

static int options_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->options;
    const hp2_fe_widget *sel = options_slot(m, m->sel);
    char mask[24];
    int i, k;
    snprintf(mask, sizeof mask, "MI_%d_MASK", sel ? m->sel : 0);
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        hp2_fe_item *it;
        if (animated(w) || is_line(w) || !strcmp(w->name, "SCROLLING_TEXT")) continue;
        if (row_number(w)) continue;                     /* the entries come below */
        it = push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
        if (it && !strcmp(w->name, mask)) it->w.visible = 0;    /* the selected row's band lifts */
    }
    for (k = 1; k <= m->orows; k++) {   /* entry k in row k's place */
        hp2_fe_item *it = push(l, options_slot(m, k), options_text(m, k));
        if (!it) continue;
        if (k == m->sel) memcpy(it->w.rgba, ORANGE, 4);
        else grey(it, 0x50);
    }
    if (sel) selection_lines(m, l, s, sel->y);
    help_bar(m, l, s);
    return l->n;
}

static int is_band(const hp2_fe_widget *w)
{
    return !strcmp(w->name, "BOTTOM_MASK") || (has_prefix(w->name, "MI_") && has_suffix(w->name, "_MASK"));
}

/* The topmost BOTTOM_MASK: the line under row 1. */
static const hp2_fe_widget *first_line(const hp2_fe_screen *s)
{
    const hp2_fe_widget *best = NULL;
    int i;
    for (i = 0; i < s->count; i++)
        if (!strcmp(s->widgets[i].name, "BOTTOM_MASK") && (!best || s->widgets[i].y < best->y)) best = &s->widgets[i];
    return best;
}

/* Where the bands end: the bottom of the lowest BOTTOM_MASK (the filler). */
static float bands_end(const hp2_fe_screen *s)
{
    float end = 0.0f;
    int i;
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        if (!strcmp(w->name, "BOTTOM_MASK") && w->y + w->sy * BAND_TEX > end) end = w->y + w->sy * BAND_TEX;
    }
    return end;
}

/* Main Menu and the mode menus: the screen's backdrop, then entry k in row
 * k's place (past the layout's last row: that row moved down a row
 * pitch per entry), with the bands laid out again for the entry count. */
static int list_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = list_screen(m);
    const hp2_fe_widget *r1 = hp2_fe_row(s, 1), *r2 = hp2_fe_row(s, 2);
    const hp2_fe_widget *band = named(s, "MI_1_MASK"), *line = first_line(s);
    int i, k, n, rows = list_rows(s);
    float pitch = r1 && r2 ? r2->y - r1->y : 55.0f, sel_y = -1.0f, end = bands_end(s);
    list_entries(m, &n);
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        if (animated(w) || is_line(w) || is_band(w) || row_number(w) || !strcmp(w->name, "SCROLLING_TEXT")) continue;
        /* the progress line ("Unlocked", the easter egg): no progress yet */
        if (w->string_id == SID_PROGRESS || !strcmp(w->name, "EASTER_EGG_STRING")) continue;
        push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
    }
    for (k = 1; k <= n; k++) {
        const hp2_fe_widget *slot = hp2_fe_row(s, k <= rows ? k : rows);
        hp2_fe_item *it = push(l, slot, hp2_femenu_list_label(m, k));
        if (it) {
            if (k > rows) it->w.y += pitch * (float)(k - rows);
            if (k == m->sel) { memcpy(it->w.rgba, ORANGE, 4); sel_y = it->w.y; }
            else grey(it, 0x50);
        }
        if (band && k != m->sel && (it = push(l, band, NULL)) != NULL)   /* the selected row's band lifts */
            it->w.y += pitch * (float)(k - 1);
    }
    if (line) {
        hp2_fe_item *it;
        for (k = 1; k < n; k++)                      /* the lines between rows */
            if ((it = push(l, line, NULL)) != NULL) it->w.y += pitch * (float)(k - 1);
        if (n > 0 && (it = push(l, line, NULL)) != NULL) {   /* the filler below the last */
            it->w.y += pitch * (float)(n - 1);
            it->w.sy = end > it->w.y ? (end - it->w.y) / BAND_TEX : 0.0f;
        }
    }
    if (sel_y >= 0.0f) selection_lines(m, l, s, sel_y);
    help_bar(m, l, s);
    return l->n;
}

/* SELECT_CAR, placed as on a PCSX2 screenshot of the game: the grid fading
 * out at the top, the name top-left, the colour swatches down the right with
 * their arrows, and along the bottom the left arrow, the strip of thumbnails
 * (two to the left of the frame, the selected car in it, four to the right),
 * the right arrow and "Cars Unlocked". The 3D car and the floor spotlight are
 * rendered by the game and are not in the port; the specs, which the game
 * shows on a later screen, sit at the left. */
#define SWATCH_X     586.0f
#define SWATCH_Y     115.5f     /* first swatch; they are 16 px, 18.6 apart */
#define SWATCH_STEP  18.6f
#define CAR_SWATCHES 8
#define SOLID_TEXTURE 0xFFFFFFFFu       /* hp2_fedraw's flat white (HP2_FEDRAW_SOLID) */
/* the colours of the game's swatch column (read off a screenshot of the BMW M5) */
static const uint8_t SWATCH_RGB[CAR_SWATCHES][3] = {
    { 232, 232, 232 }, { 124, 120, 122 }, { 170, 185, 220 }, { 190, 24, 28 },
    { 24, 36, 200 }, { 30, 72, 36 }, { 58, 58, 58 }, { 24, 24, 24 },
};
#define STRIP_STEP   38.0f

static int car_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->carsel;
    const car_entry *c = &CARS[m->car];
    static char name[96], top[24], acc[24], pwr[24], unl[48];
    int i;
    hp2_fe_item *it;
    snprintf(name, sizeof name, "%s", hp2_femenu_car_name(m, m->car));
    snprintf(acc, sizeof acc, "%s %s", lang_or(m, 42001u + (uint32_t)c->spec, "?"), lang_or(m, 42000u, "sec"));
    snprintf(top, sizeof top, "%s mph", lang_or(m, 43001u + (uint32_t)c->spec, "?"));
    snprintf(pwr, sizeof pwr, "%s %s", lang_or(m, 44001u + (uint32_t)c->spec, "?"), lang_or(m, 44000u, "BHP"));
    snprintf(unl, sizeof unl, "%s %d/%d", lang_or(m, 49071u, "Cars Unlocked:"), HP2_CARS, HP2_CARS);
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        const char *str = NULL;
        if (w->x >= 640.0f || w->y >= 480.0f) continue;                /* parked off screen */
        if (!strcmp(w->name, "CAR_FADE_OVERLAY") || (!strcmp(w->name, "GRID_SHADOW") && w->sy >= 29.0f)) continue;
        if (has_prefix(w->name, "THUMBNAIL_") && strcmp(w->name, "THUMBNAIL_FRAME") && strcmp(w->name, "THUMBNAIL_RIGHT_FILM")) continue;
        if (has_prefix(w->name, "LOCKED") || has_prefix(w->name, "SWATCH") || !strcmp(w->name, "MENUITEM_1")
            || !strcmp(w->name, "POINTS") || !strcmp(w->name, "CAR_LOGO") || !strcmp(w->name, "SCROLLING_TEXT")
            || !strcmp(w->name, "ANIMATING_MENUITEM") || !strcmp(w->name, "ANIMATE_SELECTION")
            || !strcmp(w->name, "UP_ARROW") || !strcmp(w->name, "DOWN_ARROW")) continue;
        if (w->kind == HP2_FE_TEXT) {
            str = hp2_femenu_text(m, w);
            if (!strcmp(w->name, "SPEC_TOP_SPEED")) str = top;
            else if (!strcmp(w->name, "SPEC_ACCEL")) str = acc;
            else if (!strcmp(w->name, "SPEC_POWER")) str = pwr;
            else if (!strcmp(w->name, "CARSUNLOCKED")) str = unl;
        }
        it = push(l, w, str);
        if (!it) continue;
        if (!strcmp(w->name, "CARSUNLOCKED")) it->w.x = 233.0f;
        if (has_prefix(w->name, "SPEC")) { it->w.x -= SPEC_SHIFT_X; it->w.y -= SPEC_LIFT; }
    }
    for (i = -2; i <= 4; i++) {   /* the strip */
        char nm[32];
        it = NULL;
        snprintf(nm, sizeof nm, "THUMBNAIL_%s", CARS[(m->car + i + 2 * HP2_CARS) % HP2_CARS].thumb);
        it = push(l, named(s, nm), NULL);
        if (it) {
            it->w.x = i <= 0 ? STRIP_X + STRIP_STEP * (float)i : STRIP_X + 44.0f + STRIP_STEP * (float)(i - 1);
            it->w.y = STRIP_Y;
        }
    }
    for (i = 0; i < CAR_SWATCHES; i++) {   /* colours, the chosen one framed */
        char nm[16];
        snprintf(nm, sizeof nm, "SWATCH_%d", i);
        it = push(l, named(s, nm), NULL);
        if (it) {   /* no art for the swatches in the pack: a flat 16 px square of the colour */
            int k, ch;
            it->w.x = SWATCH_X; it->w.y = SWATCH_Y + SWATCH_STEP * (float)i;
            it->w.texture = SOLID_TEXTURE;
            it->w.visible = 1;
            it->w.sx = it->w.sy = 1.0f;
            for (k = 0; k < 4; k++) {
                for (ch = 0; ch < 3; ch++) it->w.vcol[k][ch] = (uint8_t)(SWATCH_RGB[i][ch] * 128 / 255);
                it->w.vcol[k][3] = 0x80;
            }
        }
        if (i == m->color && (it = push(l, named(s, "SWATCH_FRAME"), NULL)) != NULL) {
            it->w.x = SWATCH_X - 2.0f;
            it->w.y = SWATCH_Y + SWATCH_STEP * (float)i - 2.0f;
        }
    }
    if ((it = push(l, named(s, "UP_ARROW"), NULL)) != NULL) { it->w.x = 587.0f; it->w.y = 93.0f; }
    if ((it = push(l, named(s, "DOWN_ARROW"), NULL)) != NULL) { it->w.x = 587.0f; it->w.y = 266.0f; }
    {   /* the car's name where the game puts its logo */
        it = push(l, named(s, "CLASS"), name);
        if (it) { it->w.x = 37.0f; it->w.y = 20.0f; it->w.visible = 1; }
    }
    return l->n;
}

/* The title screen. Its splash (SPLASH_SCREEN) is a black placeholder the
 * game fills at run time, and its other picture is a movie texture, so the
 * port shows Main Menu's backdrop (grid, blueprints, borders) with the NFS
 * logo in the middle, and the title's own PRESS START. */
static int title_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->title, *bg = m->mainm;
    int i;
    for (i = 0; bg && i < bg->count; i++) {
        const hp2_fe_widget *w = &bg->widgets[i];
        hp2_fe_item *it;
        if (w->kind != HP2_FE_IMAGE || animated(w) || is_line(w)) continue;
        if (has_prefix(w->name, "MI_") || !strcmp(w->name, "BOTTOM_MASK") || !strcmp(w->name, "SCROLL_TEXT_CLIP")
            || !strcmp(w->name, "WHITEOUT_BAR") || (w->y > 60.0f && w->y < 80.0f && w->x > 100.0f)) continue;
        it = push(l, w, NULL);
        if (it && !strcmp(w->name, "TITLE_SCREEN_TITLE")) {   /* the logo, 296 x 80, centred */
            it->w.x = 320.0f - 148.0f;
            it->w.y = 170.0f;
        }
    }
    for (i = 0; i < s->count; i++) {   /* PRESS START on its band, in front of the backdrop */
        const hp2_fe_widget *w = &s->widgets[i];
        hp2_fe_item *it = NULL;
        if (w->kind == HP2_FE_TEXT && !animated(w) && !row_number(w)) {
            if ((it = push(l, w, hp2_femenu_text(m, w))) != NULL) it->w.layer = 0;
        } else if (w->kind == HP2_FE_IMAGE && w->y > 270.0f && w->y < 290.0f) {
            if ((it = push(l, w, NULL)) != NULL) it->w.layer = 1;
        }
    }
    return l->n;
}

/* y of the stored row (MENUITEM_n) nearest a widget: a template is placed
 * relative to its own row (DISPLAY_OPTIONS keeps its value and arrows by row
 * 1, AUDIO_OPTIONS by row 8, Audio Mode). */
static float row_y_near(const hp2_fe_screen *s, float y)
{
    float best = 0.0f, d = 1e9f;
    int i;
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        float e = w->y - y;
        if (e < 0) e = -e;
        if (row_number(w) && e < d) { d = e; best = w->y; }
    }
    return best;
}

/* SLIDER1_BAR is 128 x 16; the marker (SLIDER1_SLIDER, 8 x 16 at x scale
 * 0.5) is 4 wide and sits centred on the value. Checked against PCSX2
 * screenshots: full volume puts it at the bar's right end. */
#define SLIDER_W 128.0f
#define MARKER_W 4.0f

static int settings_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->page;
    const hp2_fe_widget *lab = hp2_fe_row(s, 1), *lab2 = hp2_fe_row(s, 2);
    const hp2_fe_widget *mask = named(s, "MI_1_MASK"), *clipw = named(s, "SCROLLING_MENU_CLIP_REG");
    const hp2_fe_widget *la = named(s, "LEFT_ARROW"), *ra = named(s, "RIGHT_ARROW");
    const hp2_fe_widget *ua = named(s, "UP_ARROW"), *da = named(s, "DOWN_ARROW");
    const hp2_fe_widget *bar = named(s, "AUDIO_BAR1"), *mark = named(s, "AUDIO_BAR1_V");
    const hp2_fe_widget *val = NULL, *sep = NULL;
    float pitch = lab2 ? lab2->y - lab->y : 37.0f, clip[4] = { 0, 0, 640, 480 };
    float val_dy = 0, arrow_dy = 0;
    int i, k, end = m->first + m->rows;

    if (clipw) {   /* WHITE16X16 scaled */
        clip[0] = clipw->x; clip[1] = clipw->y; clip[2] = 16.0f * clipw->sx; clip[3] = 16.0f * clipw->sy;
    }
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        int in_list = w->y >= clip[1] - 8.0f && w->y < clip[1] + clip[3];
        /* the value template: the first centred text in the list */
        if (!val && w->kind == HP2_FE_TEXT && (w->align & HP2_FE_ALIGN_CENTRE) && in_list && !animated(w))
            val = w;
        /* a thin separator under row 1 */
        if (!strcmp(w->name, "BOTTOM_MASK") && w->sy < 1.0f && (!sep || w->y < sep->y)) sep = w;
    }
    if (val) val_dy = val->y - row_y_near(s, val->y);
    if (la) arrow_dy = la->y - row_y_near(s, la->y);

    /* the screen itself: everything that is not a row of the stored list */
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        hp2_fe_item *it;
        if (animated(w) || is_line(w) || row_number(w) || w == val || !strcmp(w->name, "SCROLLING_TEXT")) continue;
        if (has_prefix(w->name, "MI_") && has_suffix(w->name, "_MASK")) continue;
        if (!strcmp(w->name, "BOTTOM_MASK") || has_prefix(w->name, "AUDIO_BAR")) continue;
        if (!strcmp(w->name, "SONGNAME")) continue;                  /* the jukebox's, parked */
        if (w->kind == HP2_FE_TEXT && (w->align & HP2_FE_ALIGN_CENTRE) && w->y >= clip[1] - 8.0f
            && w->y < clip[1] + clip[3]) continue;                  /* other rows' values */
        it = push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
        /* the title (the big text above the list) names the section */
        if (it && w->kind == HP2_FE_TEXT && w->y < clip[1] - 30.0f && w->string_id)
            it->str = hp2_menu_section(m->first);
    }

    /* the rows */
    for (k = m->scroll; k < end && k < m->scroll + HP2_FEM_VISIBLE; k++) {
        float y = lab->y + pitch * (float)(k - m->scroll);
        float f = hp2_menu_fraction(m->cfg, k);
        int on = k == m->sel;
        hp2_row_kind kind = hp2_menu_kind(k);
        hp2_fe_item *it;
        it = push(l, lab, hp2_menu_label(k));
        if (it) {
            it->w.y = y;
            if (on) memcpy(it->w.rgba, ORANGE, 4); else grey(it, 0x50);
            set_clip(it, clip);
        }
        if (f >= 0.0f && bar && mark) {                             /* a volume: the slider */
            it = push(l, bar, NULL);
            if (it) { it->w.y = y + (bar->y - lab->y); set_clip(it, clip); }
            it = push(l, mark, NULL);
            if (it) {
                it->w.x = bar->x + f * SLIDER_W - MARKER_W * 0.5f;
                it->w.y = y + (mark->y - lab->y);
                set_clip(it, clip);
            }
        } else if (val && kind != HP2_KIND_ACTION) {
            it = push(l, val, m->value[k]);
            if (it) { it->w.y = y + val_dy; set_clip(it, clip); }
        }
        if (mask && !on) {
            it = push(l, mask, NULL);
            if (it) { it->w.y = y + (mask->y - lab->y); it->w.visible = 1; set_clip(it, clip); }
        }
        if (sep) {
            it = push(l, sep, NULL);
            if (it) { it->w.y = y + (sep->y - lab->y); set_clip(it, clip); }
        }
        if (on) selection_lines(m, l, s, y);
        /* arrows on every row that left/right changes; the game leaves them
         * off on/off rows (a deliberate difference, chosen for the port) */
        if (on && la && ra && kind != HP2_KIND_ACTION) {
            it = push(l, la, NULL);
            if (it) it->w.y = y + arrow_dy;
            it = push(l, ra, NULL);
            if (it) it->w.y = y + arrow_dy;
        }
    }
    if (ua && m->scroll > m->first) push(l, ua, NULL);
    if (da && m->scroll + HP2_FEM_VISIBLE < end) push(l, da, NULL);
    help_bar(m, l, s);
    return l->n;
}

/* JUKEBOX_INGAME's slot widgets: ARTISTn (the bigger line: the title, as
 * the game shows it), SONGn (the smaller line: the artist), SONGnONOFF (play
 * status) and LEFT_ARROWn / RIGHT_ARROWn, for n = 1..5. */
static const hp2_fe_widget *slot(const hp2_fe_screen *s, const char *what, int n)
{
    char name[24];
    snprintf(name, sizeof name, "%s%d", what, n);
    return named(s, name);
}

static int is_slot_widget(const hp2_fe_widget *w)
{
    static const char *pre[] = { "ARTIST", "SONG", "LEFT_ARROW", "RIGHT_ARROW", "UP_ARROW", "DOWN_ARROW", "FAVORITE" };
    size_t i;
    for (i = 0; i < sizeof pre / sizeof pre[0]; i++)
        if (has_prefix(w->name, pre[i])) return 1;
    return !strcmp(w->name, "MENUITEM_1");
}

static int jukebox_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->jbscreen;
    hp2_fe_item *it;
    int i, k;
    for (i = 0; i < s->count; i++)
        if (!is_slot_widget(&s->widgets[i]))
            push(l, &s->widgets[i], s->widgets[i].kind == HP2_FE_TEXT ? hp2_femenu_text(m, &s->widgets[i]) : NULL);
    /* the genre row */
    it = push(l, named(s, "MENUITEM_1"), m->jb->genre[m->genre]);
    if (it && m->jsel == 0) memcpy(it->w.rgba, ORANGE, 4);
    if (m->jsel == 0) {
        push(l, named(s, "LEFT_ARROW"), NULL);
        push(l, named(s, "RIGHT_ARROW"), NULL);
    }
    /* the songs */
    for (k = 0; k < HP2_FEM_SONGS && m->jscroll + k < m->nsongs; k++) {
        int song = m->songs[m->jscroll + k], on = m->jsel == m->jscroll + k + 1;
        const hp2_jb_track *t = &m->jb->track[song];
        it = push(l, slot(s, "ARTIST", k + 1), t->title);
        if (it && on) memcpy(it->w.rgba, ORANGE, 4);
        it = push(l, slot(s, "SONG", k + 1), t->artist);
        if (it && on) memcpy(it->w.rgba, ORANGE, 4);           /* the game colours both lines */
        {
            char name[24];
            snprintf(name, sizeof name, "SONG%dONOFF", k + 1);
            push(l, named(s, name), hp2_play_label(hp2_femenu_play(m, song)));
        }
        if (on) {
            push(l, slot(s, "LEFT_ARROW", k + 1), NULL);
            push(l, slot(s, "RIGHT_ARROW", k + 1), NULL);
        }
    }
    if (m->jscroll > 0) push(l, named(s, "UP_ARROW"), NULL);
    if (m->jscroll + HP2_FEM_SONGS < m->nsongs) push(l, named(s, "DOWN_ARROW"), NULL);
    return l->n;
}

int hp2_femenu_songbox(const hp2_fe_screen *box, const hp2_jb_track *t, char *quoted, size_t qcap,
                       hp2_fe_item *items, int cap)
{
    list l;
    int i;
    l.it = items;
    l.n = 0;
    l.cap = cap;
    if (!box || !t) return 0;
    snprintf(quoted, qcap, "\"%s\"", t->title);
    for (i = 0; i < box->count; i++) {
        const hp2_fe_widget *w = &box->widgets[i];
        hp2_fe_item *it;
        if (!strcmp(w->name, "RECORD_TITLE_TM")) continue;                /* the EA logo's TM */
        if (!strcmp(w->name, "RECORD_LABEL") && !t->album[0]) continue;   /* no album: no line */
        it = push(&l, w, NULL);
        if (!it || w->kind != HP2_FE_TEXT) continue;
        if (!strcmp(w->name, "SONG_TITLE")) it->str = quoted;
        else if (!strcmp(w->name, "ARTIST_NAME")) it->str = t->artist;
        else if (!strcmp(w->name, "RECORD_LABEL")) it->str = t->album;
        else if (!strcmp(w->name, "RECORD_TITLE")) it->str = HP2_SONGBOX_LINE;
    }
    return l.n;
}

int hp2_femenu_items(hp2_femenu *m, hp2_fe_item *items, int cap)
{
    list l;
    l.it = items;
    l.n = 0;
    l.cap = cap;
    if (m->screen == HP2_FEM_JUKEBOX) return jukebox_items(m, &l);
    if (m->screen == HP2_FEM_TITLE) return title_items(m, &l);
    if (m->screen == HP2_FEM_SELECT_CAR) return car_items(m, &l);
    if (list_screen(m)) return list_items(m, &l);
    return m->screen == HP2_FEM_OPTIONS ? options_items(m, &l) : settings_items(m, &l);
}
