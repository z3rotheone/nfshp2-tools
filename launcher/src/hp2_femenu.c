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
enum { GO_NONE, GO_HOT_PURSUIT, GO_WORLD_RACING, GO_QUICK, GO_OPTIONS, GO_CHALLENGE, GO_SETUP, GO_COP, GO_TREE_WR, GO_TREE_HP };
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
    { 10029, "Ultimate Fugitive",    1, 10013, GO_TREE_HP },   /* Ultimate Racer: Beat the heat in 30 events. */
    { 10030, "Racing But With Pigs", 1, 10012, GO_CHALLENGE },   /* Challenge: One-player and two-player challenges. */
    { 10028, "You're The Pig",       1, 10014, GO_COP },   /* You're The Cop: Fill your ticket quota for big points! */
};
/* Challenge's rows: Players, Race Type, Skill Level (the game's Setup row is
 * gone: accept on any row opens Customize). Their help lines follow the
 * value (list_help). */
static const list_entry CHALLENGE_LIST[] = {
    { 20001, "Players",     0, 19000, GO_NONE },
    { 20002, "Race Type",   0, 20010, GO_NONE },
    { 20020, "Skill Level", 0, 0,     GO_NONE },
};
static const list_entry COP_LIST[] = {      /* You're The Pig: no Race Type */
    { 20001, "Players",     0, 19000, GO_NONE },
    { 20020, "Skill Level", 0, 0,     GO_NONE },
};
static const list_entry WORLD_RACING_LIST[] = {
    { 10026, "Ultimate Racer", 1, 10008, GO_TREE_WR },   /* Championship: Real cars, real fast in 30 events. */
    { 10025, "Just Race",      1, 10007, GO_CHALLENGE },   /* Challenge: One-player and two-player challenges. */
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
#define SCR_SELECT_TRACK 0x15417B53u
#define SCR_POPUP        0x037F3133u
#define SCR_CHALLENGE    0x2D62CFE2u
#define SCR_COP          0xE55F1297u
#define SCR_TREE_VIEW    0x492E86E9u
#define SCR_EVENT_INFO   0xA744BA7Bu      /* unnamed: BLURB_BOX, "Next Track:", TRACK_NAME, TRACK_LINE_1 */
#define SCR_RACE_SETUP   0xD1EADBCAu
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
    case HP2_FEM_CHALLENGE:    return m->chal_cop ? m->cop : m->chal;
    case HP2_FEM_RACE_SETUP:   return m->rsetup;
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

/* The two event lists (the game's trees, the 0x80034A00 chunks of FRONTB,
 * shown in the port as plain lists in event order): Championship (the
 * port's Ultimate Racer, World Racing) has 31 events, Hot Pursuit (the
 * port's Ultimate Fugitive) 32. Event k's title is string title + k and its blurb
 * blurb + k. */
static const struct { int events; uint32_t title, blurb; } TREES[2] = { { 31, 70000u, 70064u }, { 32, 70031u, 70095u } };

/* The car database's names by index (GLOBALB.BUN chunk 0x34600, the first
 * 49 of 64; the rest are traffic), as the event tables name cars. */
static const char *const CARDB[] = {
    "CORVETTE", "CORVETTEHP", "VIPERGTS", "VIPERGTSHP", "CROWNVIC", "CLK_GTR", "CLK_GTRHP", "MCLAREN",
    "MCLARENHP", "MCLARENLM", "MCLARENLMHP", "MUSTANG", "MUSTANGHP", "TS50", "TS50HP", "HOLDEN",
    "HOLDENHP", "CARRERAGT", "CARRERAGTHP", "BMWM5", "BMWZ8", "SPIDER", "SPIDERHP", "F50",
    "F50HP", "BARCHETTA", "BARCHETTAHP", "JAGUAR", "JAGUARHP", "VANQUISH", "VANQUISHHP", "BMWM5COP",
    "LAMBOCOP", "CORVETTECOP", "MUSTANGCOP", "VAUXHALL", "VAUXHALLHP", "OPEL", "OPELHP", "911TURBO",
    "911TURBOHP", "ELISE", "ELISEHP", "CL55AMG", "CL55AMGHP", "DIABLO", "DIABLOHP", "MURCIELAGO", "MURCIELAGOHP",
};
#define CARDB_N ((int)(sizeof CARDB / sizeof CARDB[0]))

/* Names of the cars outside the port's 23: the pursuit cars, and the
 * 360 Modena Challenge (SPIDERHP), which has no name string of its own. */
static const struct { const char *db; uint32_t sid; const char *own; } MORE_NAMES[] = {
    { "CROWNVIC", 41102u, "Pursuit Crown Victoria" }, { "BMWM5COP", 41116u, "Pursuit M5" },
    { "LAMBOCOP", 41117u, "Pursuit Murcielago" }, { "CORVETTECOP", 41118u, "Pursuit Corvette Z06" },
    { "MUSTANGCOP", 41119u, "Pursuit Mustang Cobra R" }, { "SPIDERHP", 0u, "Ferrari 360 Modena" },
};

/* Cars and courses are won as events' rewards. One that no event gives is
 * there from the start; one that some event gives is open once it is earned
 * (without a progress record everything is open). kind 0 car (by its car
 * database name), 1 course (by its number; the course itself, its "II" way
 * round is a reward of its own). */
static int reward_open(const hp2_femenu *m, int kind, const char *car, int num, int ii)
{
    int t, k, j, given = 0;
    if (!m->prog || !m->trees) return 1;
    for (t = 0; t < 2; t++) {
        const hp2_tree *tr = &m->trees[t];
        for (k = 0; k < tr->count; k++) {
            const hp2_tree_event *e = &tr->ev[k];
            for (j = 0; j < e->nrewards; j++) {
                const hp2_tree_reward *r = &e->rewards[j];
                if (r->kind != kind) continue;
                if (kind == 0 ? !(r->id >= 0 && r->id < CARDB_N && !strcmp(CARDB[r->id], car)) : r->id != num || (r->ii != 0) != (ii != 0)) continue;
                given = 1;
                if (hp2_progress_reward_got(m->prog, t, e, r)) return 1;
            }
        }
    }
    return !given;
}
static int car_open(const hp2_femenu *m, int c) { return reward_open(m, 0, CARS[c % HP2_CARS].thumb, 0, 0); }

static int step_car(const hp2_femenu *m, int k, int dir)
{
    int i;
    if (m->ev_mode) return (k + dir + m->ev_ncars) % m->ev_ncars;
    for (i = 1; i <= HP2_CARS; i++) {
        int c = (k + dir * i + HP2_CARS * 2) % HP2_CARS;
        if (car_open(m, c)) return c;
    }
    return k;
}

/* The cars car select offers: the port's 23, or an event's own. */
static int roster_count(const hp2_femenu *m) { return m->ev_mode ? m->ev_ncars : HP2_CARS; }

/* The car's name in the car database and in the layout (THUMBNAIL_<x>,
 * CARLOGO_<x>). */
static const char *roster_thumb(const hp2_femenu *m, int k)
{
    if (!m->ev_mode) return CARS[k].thumb;
    return m->ev_cars[k] >= 0 && m->ev_cars[k] < CARDB_N ? CARDB[m->ev_cars[k]] : "UNKNOWN";
}

static const char *db_car_name(const hp2_femenu *m, const char *db);

const char *hp2_femenu_car_name(const hp2_femenu *m, int k)
{
    const char *s = NULL;
    if (k < 0 || k >= roster_count(m)) return NULL;
    if (!m->ev_mode) {
        if (m->lang) s = hp2_lang_by_id(m->lang, CARS[k].name);
        return s ? s : CARS[k].thumb;
    }
    return db_car_name(m, roster_thumb(m, k));
}

/* A car's full name from its car database name. */
static const char *db_car_name(const hp2_femenu *m, const char *db)
{
    const char *s = NULL;
    size_t len;
    int i;
    for (i = 0; i < HP2_CARS; i++)
        if (!strcmp(CARS[i].thumb, db)) { if (m->lang) s = hp2_lang_by_id(m->lang, CARS[i].name); return s ? s : db; }
    for (i = 0; i < (int)(sizeof MORE_NAMES / sizeof MORE_NAMES[0]); i++)
        if (!strcmp(MORE_NAMES[i].db, db)) {
            if (MORE_NAMES[i].sid && m->lang) s = hp2_lang_by_id(m->lang, MORE_NAMES[i].sid);
            return s ? s : MORE_NAMES[i].own;
        }
    len = strlen(db);     /* another "HP" version: the base car's name */
    if (len > 2 && !strcmp(db + len - 2, "HP"))
        for (i = 0; i < HP2_CARS; i++)
            if (strlen(CARS[i].thumb) == len - 2 && !strncmp(CARS[i].thumb, db, len - 2)) {
                if (m->lang) s = hp2_lang_by_id(m->lang, CARS[i].name);
                return s ? s : CARS[i].thumb;
            }
    return db;
}

static void series_reset(hp2_femenu *m);
static const char *lang_or(const hp2_femenu *m, uint32_t id, const char *dflt)
{
    const char *s = m->lang ? hp2_lang_by_id(m->lang, id) : NULL;
    return s ? s : dflt;
}

#define CAR_SWATCHES_N 8
#define STRIP_X    375.0f      /* the frame (THUMBNAIL_FRAME) */
#define STRIP_Y    399.0f

static int car_input(hp2_femenu *m, hp2_fem_input in)
{
    if (m->car_popup) {         /* Transmission, then Handling */
        int *v = m->car_popup == 1 ? &m->trans : &m->handling, n = m->car_popup == 1 ? 3 : 2;
        switch (in) {
        case HP2_IN_LEFT:  *v = (*v + n - 1) % n; return HP2_FEM_REDRAW;
        case HP2_IN_RIGHT: *v = (*v + 1) % n; return HP2_FEM_REDRAW;
        case HP2_IN_BACK:  m->car_popup--; return HP2_FEM_REDRAW;
        case HP2_IN_ACCEPT:
            if (m->car_popup == 1) { m->car_popup = 2; return HP2_FEM_REDRAW; }
            if (m->ev_mode) {   /* the event sets the track: its details next */
                if (!m->evinfo || !m->chal) return 0;
                m->screen = HP2_FEM_EVENT_INFO;
                series_reset(m);
                m->result[0][0] = m->result[1][0] = 0;
                return HP2_FEM_REDRAW;
            }
            if (!m->tracksel) return 0;
            m->car_popup = 0;
            m->screen = HP2_FEM_SELECT_TRACK;
            m->lap_popup = 0;
            return HP2_FEM_REDRAW;
        default: return 0;
        }
    }
    switch (in) {
    case HP2_IN_UP:    m->color = (m->color + CAR_SWATCHES_N - 1) % CAR_SWATCHES_N; return HP2_FEM_REDRAW;
    case HP2_IN_DOWN:  m->color = (m->color + 1) % CAR_SWATCHES_N; return HP2_FEM_REDRAW;
    case HP2_IN_LEFT:  m->car = step_car(m, m->car, -1); return HP2_FEM_REDRAW;
    case HP2_IN_RIGHT: m->car = step_car(m, m->car, 1); return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        if (m->ev_mode) { m->ev_mode = 0; m->car = 0; m->screen = HP2_FEM_TREE; return HP2_FEM_REDRAW; }   /* back to the event list */
        if (!m->quick_mode && m->rsetup) { m->screen = HP2_FEM_RACE_SETUP; m->sel = m->setup_sel > 0 ? m->setup_sel : 1; return HP2_FEM_REDRAW; }
        if (!m->quick_mode && m->chal) { m->screen = HP2_FEM_CHALLENGE; m->sel = m->chal_cop ? 2 : 3; return HP2_FEM_REDRAW; }
        m->screen = HP2_FEM_MAIN;
        m->sel = m->main_sel;
        return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT:
        m->car_popup = 1;
        return HP2_FEM_REDRAW;
    default: return 0;
    }
}

/* ---- track select ---------------------------------------------------------- */

/* The 15 courses: the track number (TRACKS\TRACKB<n>.LZC; n + 3 is the same
 * course in its other lighting) and its length, from the track list in
 * GLOBALB.BUN (hp2_tracklist.py). The names are the game's strings
 * 50000 + 10 n (51000 + 10 n for the other way round). */
typedef struct { int num; float length; } track_entry;
static const track_entry TRACKS[HP2_TRACKS] = {
    { 11, 4.59f }, { 12, 4.62f }, { 13, 7.75f }, { 21, 5.19f }, { 22, 5.46f },
    { 23, 5.30f }, { 31, 6.24f }, { 32, 5.10f }, { 33, 9.47f }, { 41, 6.99f },
    { 42, 5.67f }, { 43, 10.98f }, { 61, 5.50f }, { 62, 7.50f }, { 63, 13.25f },
};

static int track_open(const hp2_femenu *m, int k) { return reward_open(m, 1, NULL, TRACKS[k % HP2_TRACKS].num, 0); }

/* The next open course from k, stepping dir (+1 / -1); k itself if none is. */
static int step_track(const hp2_femenu *m, int k, int dir)
{
    int i;
    for (i = 1; i <= HP2_TRACKS; i++) {
        int c = (k + dir * i + HP2_TRACKS * 2) % HP2_TRACKS;
        if (track_open(m, c)) return c;
    }
    return k;
}

const char *hp2_femenu_track_name(const hp2_femenu *m, int k, int alternate)
{
    const char *s = NULL;
    if (k < 0 || k >= HP2_TRACKS) return NULL;
    if (m->lang) s = hp2_lang_by_id(m->lang, (alternate ? 51000u : 50000u) + 10u * (uint32_t)TRACKS[k].num);
    return s ? s : "Track";
}

int hp2_femenu_track_number(const hp2_femenu *m)
{
    return TRACKS[m->track].num + (m->tod ? 3 : 0);
}

/* The other lighting: overcast in the Parkland and the Desert, sunset (or
 * dawn) in the three others. */
static int overcast_course(int k) { return TRACKS[k].num < 30; }

static void copy_name(char *d, const char *s)
{
    snprintf(d, 16, "%s", s);
}

/* Customize's settings (RACE_SETUP) */
enum { SU_COPS, SU_OPPONENTS, SU_OPPTYPE, SU_AI, SU_CATCHUP, SU_TRAFFIC, SU_DAMAGE, SU_TRACKS, SU_KINDS };

static uint32_t dice(hp2_femenu *m)
{
    m->seed = m->seed * 1664525u + 1013904223u;
    return m->seed >> 8;
}

/* A quick or single race: the course, lighting, direction and laps picked,
 * the car in its colour; opponents as Customize set them (count, Same or
 * Random cars, Opponent AI), none for a Free Run. Quick Race skips
 * Customize, so its defaults stand: a full grid of random cars. */
static void single_race(hp2_femenu *m)
{
    hp2_fem_race *g = &m->go;
    int n = 0, k;
    static const int skill[3] = { 2, 5, 8 };
    memset(g, 0, sizeof *g);
    g->track = hp2_femenu_track_number(m);
    g->reverse = m->alt;
    g->laps = m->laps > 0 ? m->laps : 1;
    copy_name(g->car, CARS[m->car % HP2_CARS].thumb);
    g->skin = m->color + 1;
    if (m->quick_mode || m->race_type != 1) n = m->setup_val[SU_OPPONENTS] >= 6 ? HP2_FEM_MAX_OPP : m->setup_val[SU_OPPONENTS] + 1;
    for (k = 0; k < n; k++) {
        int c = m->car % HP2_CARS;
        if (!m->setup_val[SU_OPPTYPE] || m->quick_mode)
            do c = (int)(dice(m) % HP2_CARS); while (c == m->car % HP2_CARS);
        copy_name(g->opp[k], CARS[c].thumb);
        g->opp_skill[k] = skill[m->setup_val[SU_AI] % 3];
        g->opp_skin[k] = -1;
    }
    g->nopp = n;
    if (!m->quick_mode && m->race_type == 3) g->race_type = HP2_RT_LAP_KNOCKOUT;   /* the Challenge's Lap Knockout */
    if (!m->quick_mode && m->race_type == 2 && n >= 2) {     /* the Challenge's Race Knockout: a series, the last car out each race */
        int alive_n = 0, o;
        hp2_fem_race all = *g;
        if (m->ser.done || m->ser.nrounds != -n) {           /* a fresh one (its size is kept as -n) */
            series_reset(m);
            m->ser.nrounds = -n;
            for (o = n + 1; o <= HP2_FEM_MAX_OPP; o++) m->ser.alive[o] = 0;
        }
        g->nopp = 0;
        for (o = 1; o <= n; o++) {
            if (!m->ser.alive[o]) continue;
            copy_name(g->opp[g->nopp], all.opp[o - 1]);
            g->opp_skill[g->nopp] = all.opp_skill[o - 1];
            g->opp_skin[g->nopp] = -1;
            g->opp_id[g->nopp] = o;
            g->nopp++;
            alive_n++;
        }
        g->race_type = HP2_RT_KNOCKOUT;
        g->round = m->ser.round;
        if (m->ser.round > 0 && !m->setup_val[SU_TRACKS]) {   /* Random: another course each race */
            int tries, t2 = m->track;
            for (tries = 0; tries < 20; tries++) {
                t2 = (int)(dice(m) % HP2_TRACKS);
                if (track_open(m, t2)) break;
            }
            g->track = TRACKS[t2].num + (m->tod ? 3 : 0);
            g->reverse = 0;                                  /* a new course runs its usual way */
        }
        (void)alive_n;
    }
}

static int track_input(hp2_femenu *m, hp2_fem_input in)
{
    if (m->lap_popup) {
        switch (in) {
        case HP2_IN_LEFT:  if (m->laps > 1) m->laps--; return HP2_FEM_REDRAW;
        case HP2_IN_RIGHT: if (m->laps < 10) m->laps++; return HP2_FEM_REDRAW;
        case HP2_IN_BACK:  m->lap_popup = 0; return HP2_FEM_REDRAW;
        case HP2_IN_UP: case HP2_IN_DOWN:     /* the course the other way round, once it is won (the port's own control) */
            if (!m->alt && !reward_open(m, 1, NULL, TRACKS[m->track].num, 1)) return 0;
            m->alt = !m->alt;
            return HP2_FEM_REDRAW;
        case HP2_IN_ACCEPT: single_race(m); m->lap_popup = 0; return HP2_FEM_RACE | HP2_FEM_REDRAW;
        default: return 0;
        }
    }
    switch (in) {
    case HP2_IN_UP:
    case HP2_IN_DOWN:  m->tod = !m->tod; return HP2_FEM_REDRAW;       /* the Time of Day column */
    case HP2_IN_LEFT:  m->track = step_track(m, m->track, -1); m->alt = 0; return HP2_FEM_REDRAW;
    case HP2_IN_RIGHT: m->track = step_track(m, m->track, 1); m->alt = 0; return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT: m->lap_popup = 1; return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        m->screen = HP2_FEM_SELECT_CAR;
        m->car_popup = 0;
        return HP2_FEM_REDRAW;
    default: return 0;
    }
}

/* RACE_SETUP ("Customize"): the settings the Setup row opens, by mode. Each
 * has a label (30001..), a set of values (strings 30008..30020) and a help
 * line (30500..). The rows are rebuilt from the mode on every call. */
static const struct { uint32_t label, help; int n; } SU_INFO[SU_KINDS] = {
    { 30001u, 0u, 2 }, { 30002u, 30500u, 7 }, { 30003u, 30501u, 2 }, { 30004u, 0u, 3 },
    { 30005u, 30502u, 2 }, { 30006u, 30503u, 4 }, { 30101u, 30504u, 2 }, { 30007u, 30505u, 2 },
};

/* The setting kinds of the screen, in row order; returns how many. */
static int setup_kinds(const hp2_femenu *m, int *kind)
{
    int n = 0;
    if (m->chal_cop) {
        kind[n++] = SU_OPPONENTS; kind[n++] = SU_AI; kind[n++] = SU_TRAFFIC; kind[n++] = SU_DAMAGE;
        return n;
    }
    if (m->race_type == 1) {                      /* Free Run: just you */
        kind[n++] = SU_TRAFFIC; kind[n++] = SU_DAMAGE;
        return n;
    }
    if (m->chal_from == HP2_FEM_HOT_PURSUIT) kind[n++] = SU_COPS;
    kind[n++] = SU_OPPONENTS; kind[n++] = SU_OPPTYPE; kind[n++] = SU_AI; kind[n++] = SU_CATCHUP;
    kind[n++] = SU_TRAFFIC; kind[n++] = SU_DAMAGE;
    if (m->race_type == 2) kind[n++] = SU_TRACKS;  /* Knockout runs a sequence of tracks */
    return n;
}

static const char *setup_value(const hp2_femenu *m, int kind, int v)
{
    static char num[16];
    static const uint32_t ai[3] = { 30015u, 30016u, 30017u }, tr[4] = { 30008u, 30018u, 30019u, 30020u };
    switch (kind) {
    case SU_COPS:      return lang_or(m, v ? 30010u : 30011u, v ? "One" : "None");
    case SU_OPPONENTS:
        if (v >= 6) return lang_or(m, 30012u, "Full Grid");
        snprintf(num, sizeof num, "%d", v + 1);
        return num;
    case SU_OPPTYPE:
    case SU_TRACKS:    return lang_or(m, v ? 30014u : 30013u, v ? "Same" : "Random");
    case SU_AI:        return lang_or(m, ai[v], "Beginner");
    case SU_TRAFFIC:   return lang_or(m, tr[v], "Off");
    default:           return lang_or(m, v ? 30009u : 30008u, v ? "On" : "Off");
    }
}

static const list_entry *list_entries(const hp2_femenu *m, int *n)
{
    if (m->screen == HP2_FEM_RACE_SETUP) {
        static list_entry rows[8];
        int kind[8], i, k = setup_kinds(m, kind);
        for (i = 0; i < k; i++) {
            rows[i].sid = SU_INFO[kind[i]].label; rows[i].name = "Setting"; rows[i].own = 0;
            rows[i].help = SU_INFO[kind[i]].help; rows[i].go = GO_NONE;
        }
        *n = k;
        return rows;
    }
    switch (m->screen) {
    case HP2_FEM_MAIN:         *n = (int)(sizeof MAIN_LIST / sizeof MAIN_LIST[0]); return MAIN_LIST;
    case HP2_FEM_HOT_PURSUIT:  *n = (int)(sizeof HOT_PURSUIT_LIST / sizeof HOT_PURSUIT_LIST[0]); return HOT_PURSUIT_LIST;
    case HP2_FEM_CHALLENGE:
        if (m->chal_cop) { *n = (int)(sizeof COP_LIST / sizeof COP_LIST[0]); return COP_LIST; }
        *n = (int)(sizeof CHALLENGE_LIST / sizeof CHALLENGE_LIST[0]); return CHALLENGE_LIST;
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

/* Challenge's row k (1-based) as 0 Players, 1 Race Type, 2 Skill Level, 3 Setup;
 * You're The Pig has no Race Type. */
static int chal_kind(const hp2_femenu *m, int k)
{
    return m->chal_cop && k >= 2 ? k : k - 1;
}

static void list_help(hp2_femenu *m)
{
    int n;
    const list_entry *e = list_entries(m, &n);
    const char *h;
    if (!e || m->sel < 1 || m->sel > n) return;
    e += m->sel - 1;
    if (e->go == GO_QUICK && m->quick_mode) {
        snprintf(m->help, sizeof m->help, "Quick Race: %s settings, a full grid of random cars.",
                 quick_name(m->quick_mode));
        return;
    }
    if (m->screen == HP2_FEM_RACE_SETUP) {
        h = e->help && m->lang ? hp2_lang_by_id(m->lang, e->help) : NULL;
        snprintf(m->help, sizeof m->help, "%s", h ? h : "");
        return;
    }
    if (m->screen == HP2_FEM_CHALLENGE) {      /* the help follows the value */
        int kd = chal_kind(m, m->sel);
        uint32_t id = kd == 0 ? 19000u + (uint32_t)m->players : kd == 1 ? 20010u + (uint32_t)m->race_type
                    : kd == 2 ? (m->chal_cop ? 19006u : 19002u) + (uint32_t)m->skill : 19005u;
        h = id && m->lang ? hp2_lang_by_id(m->lang, id) : NULL;
        snprintf(m->help, sizeof m->help, "%s", h ? h : "");
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
    } else if (m->screen == HP2_FEM_EVENT_INFO) {
        /* the bar shows the event's title, as the game's EVENT_TITLE_BAR */
        snprintf(m->help, sizeof m->help, "%s", lang_or(m, TREES[m->tree_kind].title + (uint32_t)m->tree_ev, ""));
    } else if (m->screen == HP2_FEM_SELECT_TRACK && m->lap_popup && reward_open(m, 1, NULL, TRACKS[m->track].num, 1)) {
        snprintf(m->help, sizeof m->help, "Up/Down: %s", m->alt ? "back to the course's usual direction" : "run the course the other way round");
    } else if (m->screen == HP2_FEM_TITLE || m->screen == HP2_FEM_SELECT_CAR || m->screen == HP2_FEM_SELECT_TRACK || m->screen == HP2_FEM_TREE) {
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
    m->tracksel = hp2_fe_find_id(layout, SCR_SELECT_TRACK);
    m->popscr = hp2_fe_find_id(layout, SCR_POPUP);
    m->chal = hp2_fe_find_id(layout, SCR_CHALLENGE);
    if (!hp2_fe_row(m->chal, 1)) m->chal = NULL;
    m->tree = hp2_fe_find_id(layout, SCR_TREE_VIEW);
    m->evinfo = hp2_fe_find_id(layout, SCR_EVENT_INFO);
    m->rsetup = hp2_fe_find_id(layout, SCR_RACE_SETUP);
    if (!hp2_fe_row(m->rsetup, 1)) m->rsetup = NULL;
    m->setup_val[SU_OPPONENTS] = 6; m->setup_val[SU_CATCHUP] = 1; m->setup_val[SU_TRAFFIC] = 2; m->setup_val[SU_DAMAGE] = 1;
    m->setup_val[SU_COPS] = 1;
    m->cop = hp2_fe_find_id(layout, SCR_COP);
    if (!hp2_fe_row(m->cop, 1)) m->cop = NULL;
    m->laps = 2;
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
                              : s == HP2_FEM_SELECT_CAR ? m->carsel : s == HP2_FEM_SELECT_TRACK ? m->tracksel : s == HP2_FEM_OPTIONS ? m->options : NULL;
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

/* ---- event trees -------------------------------------------------------- */

static int tree_input(hp2_femenu *m, hp2_fem_input in)
{
    int n = TREES[m->tree_kind].events;
    switch (in) {
    case HP2_IN_LEFT:
    case HP2_IN_UP:    m->tree_ev = (m->tree_ev + n - 1) % n; break;
    case HP2_IN_RIGHT:
    case HP2_IN_DOWN:  m->tree_ev = (m->tree_ev + 1) % n; break;
    case HP2_IN_BACK:
        m->screen = (hp2_fem_screen)m->tree_from;
        m->sel = m->chal_sel;
        return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT: {   /* car select with the event's own cars */
        const hp2_tree *t = m->trees ? &m->trees[m->tree_kind] : NULL;
        const hp2_tree_event *e;
        int k;
        if (!t || !t->present || m->tree_ev >= t->count || !m->carsel) return 0;
        e = &t->ev[m->tree_ev];
        if (e->ncars < 1) return 0;
        if (m->prog && !hp2_progress_open(m->prog, m->tree_kind, t, m->tree_ev + 1)) return 0;   /* locked */
        m->ev_ncars = 0;
        for (k = 0; k < e->ncars && k < HP2_TREE_MAX_CARS; k++) m->ev_cars[m->ev_ncars++] = e->cars[k];
        m->ev_mode = 1;
        m->car = 0; m->color = 0; m->car_popup = 0;
        m->screen = HP2_FEM_SELECT_CAR;
        return HP2_FEM_REDRAW;
    }
    default: return 0;
    }
    return HP2_FEM_REDRAW;
}

static const char *cardb_name(int idx)
{
    idx &= 0x3FFF;              /* Championship 31's F50 has 0x4000 set */
    return idx >= 0 && idx < CARDB_N ? CARDB[idx] : NULL;
}

static void series_reset(hp2_femenu *m)
{
    int k;
    memset(&m->ser, 0, sizeof m->ser);
    for (k = 0; k <= HP2_FEM_MAX_OPP; k++) m->ser.alive[k] = 1;
}

/* The event's next race: its first, or, in a Tournament, the next of its
 * races, or in a Race Knockout the first race's course again with the cars
 * still in; the car picked in its own colour. */
static int event_race(hp2_femenu *m)
{
    const hp2_tree *t = m->trees ? &m->trees[m->tree_kind] : NULL;
    const hp2_tree_event *e;
    hp2_fem_race *g = &m->go;
    const hp2_tree_race *rc;
    const char *car;
    int k, full = 0, series;
    if (!t || !t->present || m->tree_ev >= t->count) return 0;
    e = &t->ev[m->tree_ev];
    if (e->nraces < 1 || m->car < 0 || m->car >= m->ev_ncars || !(car = cardb_name(m->ev_cars[m->car]))) return 0;
    series = e->race_type == HP2_RT_TOURNAMENT || e->race_type == HP2_RT_KNOCKOUT;
    if (!series || m->ser.done || m->ser.nrounds < 0) series_reset(m);   /* (a Challenge Knockout leaves nrounds below 0) */
    for (k = 0; k < e->nopponents && k < HP2_FEM_MAX_OPP; k++) if (cardb_name(e->opponents[k].car)) full++;
    for (k = full + 1; k <= HP2_FEM_MAX_OPP; k++) m->ser.alive[k] = 0;      /* cars the event has not */
    full = 0;
    if (e->race_type == HP2_RT_TOURNAMENT) m->ser.nrounds = e->nraces;
    rc = &e->races[series && m->ser.round < e->nraces ? m->ser.round : series ? e->nraces - 1 : 0];   /* a Knockout with fewer courses than rounds repeats the last */
    memset(g, 0, sizeof *g);
    g->track = rc->track;
    g->reverse = rc->ii;
    g->laps = rc->laps > 0 ? rc->laps : 1;
    copy_name(g->car, car);
    g->skin = m->color + 1;
    for (k = 0; k < e->nopponents && g->nopp < HP2_FEM_MAX_OPP; k++) {
        const char *o = cardb_name(e->opponents[k].car);
        if (!o) continue;
        full++;
        if (series && !m->ser.alive[full]) continue;          /* knocked out */
        copy_name(g->opp[g->nopp], o);
        g->opp_skill[g->nopp] = e->opponents[k].skill;
        g->opp_skin[g->nopp] = e->opponents[k].colour >= 1 ? e->opponents[k].colour : -1;   /* 1-8 (never 0), as the paints */
        g->opp_id[g->nopp] = full;
        g->nopp++;
    }
    g->event = e->number;
    g->tree_kind = m->tree_kind;
    g->race_type = e->race_type;
    g->round = series ? m->ser.round : 0;
    return 1;
}

/* The event's details: back returns to Handling; accept starts the race. */
static int evinfo_input(hp2_femenu *m, hp2_fem_input in)
{
    if (in == HP2_IN_ACCEPT) return event_race(m) ? HP2_FEM_RACE : 0;
    if (in != HP2_IN_BACK) return 0;
    m->screen = HP2_FEM_SELECT_CAR;
    m->car_popup = 2;
    return HP2_FEM_REDRAW;
}

/* Customize: up/down settings, left/right values, accept goes on to car select. */
static int setup_input(hp2_femenu *m, hp2_fem_input in)
{
    int kind[8], n = setup_kinds(m, kind);
    switch (in) {
    case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : n; return HP2_FEM_REDRAW;
    case HP2_IN_DOWN: m->sel = m->sel < n ? m->sel + 1 : 1; return HP2_FEM_REDRAW;
    case HP2_IN_LEFT:
    case HP2_IN_RIGHT:
        if (m->sel < 1 || m->sel > n) return 0;
        {
            int *v = &m->setup_val[kind[m->sel - 1]], c = SU_INFO[kind[m->sel - 1]].n;
            *v = (*v + (in == HP2_IN_LEFT ? c - 1 : 1)) % c;
        }
        return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT:
        if (!m->carsel) return 0;
        m->setup_sel = m->sel;
        m->ser.done = 1;                          /* a new pass through Customize starts a Knockout afresh */
        m->screen = HP2_FEM_SELECT_CAR;
        m->car_popup = 0;
        return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        m->screen = HP2_FEM_CHALLENGE;
        m->sel = m->chal_cop ? 2 : 3;
        return HP2_FEM_REDRAW;
    default: return 0;
    }
}

/* Challenge: up/down rows, left/right the value, accept goes on to Customize. */
static int challenge_input(hp2_femenu *m, hp2_fem_input in)
{
    static const int span[3] = { 2, 4, 3 };
    int *v[3], rows = m->chal_cop ? 2 : 3, kd = chal_kind(m, m->sel);
    v[0] = &m->players; v[1] = &m->race_type; v[2] = &m->skill;
    switch (in) {
    case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : rows; return HP2_FEM_REDRAW;
    case HP2_IN_DOWN: m->sel = m->sel < rows ? m->sel + 1 : 1; return HP2_FEM_REDRAW;
    case HP2_IN_LEFT:
    case HP2_IN_RIGHT:
        if (kd > 2) return 0;
        *v[kd] = (*v[kd] + (in == HP2_IN_LEFT ? span[kd] - 1 : 1)) % span[kd];
        return HP2_FEM_REDRAW;
    case HP2_IN_ACCEPT:
        if (m->rsetup) {
            m->setup_val[SU_AI] = m->skill;        /* Opponent AI starts at the Skill Level */
            m->screen = HP2_FEM_RACE_SETUP; m->sel = 1;
            return HP2_FEM_REDRAW;
        }
        if (!m->carsel) return 0;
        m->screen = HP2_FEM_SELECT_CAR;
        m->car_popup = 0;
        return HP2_FEM_REDRAW;
    case HP2_IN_BACK:
        m->screen = (hp2_fem_screen)m->chal_from;
        m->sel = m->chal_sel;
        return HP2_FEM_REDRAW;
    default: return 0;
    }
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
        case GO_TREE_WR:
        case GO_TREE_HP:
            if (!m->tree) return 0;
            m->tree_kind = e[m->sel - 1].go == GO_TREE_HP;
            m->tree_ev = 0;
            m->tree_scroll = 0;
            m->tree_from = m->screen; m->chal_sel = m->sel;
            m->screen = HP2_FEM_TREE;
            return HP2_FEM_REDRAW;
        case GO_CHALLENGE:
        case GO_COP:
            if (!(e[m->sel - 1].go == GO_COP ? m->cop : m->chal)) return 0;
            m->chal_cop = e[m->sel - 1].go == GO_COP;
            m->chal_from = m->screen; m->chal_sel = m->sel;
            m->screen = HP2_FEM_CHALLENGE;
            m->sel = 1;
            return HP2_FEM_REDRAW;
        case GO_QUICK:
            m->quick_mode = roll_quick(m);
            if (!m->carsel) return HP2_FEM_REDRAW;
            m->main_sel = m->sel;
            m->screen = HP2_FEM_SELECT_CAR;
            m->car_popup = 0;
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
    if (m->popup) {                             /* a question: Yes or No */
        switch (in) {
        case HP2_IN_LEFT: case HP2_IN_RIGHT: case HP2_IN_UP: case HP2_IN_DOWN: m->popup_sel = !m->popup_sel; return HP2_FEM_REDRAW;
        case HP2_IN_ACCEPT:
            m->popup = 0;
            if (m->popup_sel == 0 && hp2_menu_activate(m->cfg, m->popup_row)) { refresh(m); return HP2_FEM_REDRAW | HP2_FEM_CHANGED; }
            return HP2_FEM_REDRAW;
        case HP2_IN_BACK: m->popup = 0; return HP2_FEM_REDRAW;
        default: return 0;
        }
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
    } else if (m->screen == HP2_FEM_SELECT_TRACK) {
        r = track_input(m, in);
    } else if (m->screen == HP2_FEM_TITLE) {
        r = title_input(m, in);
    } else if (m->screen == HP2_FEM_TREE) {
        r = tree_input(m, in);
    } else if (m->screen == HP2_FEM_EVENT_INFO) {
        r = evinfo_input(m, in);
    } else if (m->screen == HP2_FEM_RACE_SETUP) {
        r = setup_input(m, in);
    } else if (m->screen == HP2_FEM_CHALLENGE) {
        r = challenge_input(m, in);
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
                if (m->popscr && !strncmp(hp2_menu_label(m->sel), "Default ", 8)) {   /* "Are you sure?" first */
                    m->popup = 1; m->popup_sel = 1; m->popup_row = m->sel;
                    r = HP2_FEM_REDRAW;
                } else if (hp2_menu_activate(m->cfg, m->sel)) r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
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
 * label sits at y. Measured on PCSX2 screenshots the line centres are 0.5
 * above the label's y and 32.5 below it (each texture is 8 rows tall with its
 * line in the middle, so its top is 4 higher); on the user's full-screen
 * window that looked too high, so both textures are put 4 lower than that. */
static void selection_lines(const hp2_femenu *m, list *l, const hp2_fe_screen *s, float y)
{
    const hp2_fe_widget *o = named(s, "MI_OVERSCORE"), *u = named(s, "MI_UNDERSCORE");
    hp2_fe_item *it;
    (void)m;
    if ((it = push(l, o, NULL)) != NULL) it->w.y = y - 0.5f;
    if ((it = push(l, u, NULL)) != NULL) it->w.y = y + 32.5f;
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
        if (m->screen == HP2_FEM_RACE_SETUP && (has_prefix(w->name, "LINE_") || !strcmp(w->name, "LEFT_ARROW")
            || !strcmp(w->name, "RIGHT_ARROW") || !strcmp(w->name, "UP_ARROW") || !strcmp(w->name, "DOWN_ARROW")
            || !strcmp(w->name, "ANIMATE_SELECTION"))) continue;
        if (m->screen == HP2_FEM_CHALLENGE) {
            static char title[64];
            static const uint32_t rt[4] = { 20005u, 20006u, 20007u, 20008u }, sk[3] = { 55004u, 55005u, 55006u };
            const char *v = NULL;
            if (!strcmp(w->name, "ANIMATE_SELECTION") || !strcmp(w->name, "LEFT_ARROW") || !strcmp(w->name, "RIGHT_ARROW")) continue;
            if (!strcmp(w->name, "GAME_TYPE")) v = lang_or(m, 20003u + (uint32_t)m->players, "One Player");
            else if (!strcmp(w->name, "RACE_MODE")) v = lang_or(m, rt[m->race_type], "Single Race");
            else if (!strcmp(w->name, "SKILL_LEVEL")) v = lang_or(m, sk[m->skill], "Beginner");
            if (!strcmp(w->name, "CHALLENGE_TITLE")) {      /* the name of the row it was opened from */
                int scr = m->screen, sel = m->sel;
                m->screen = m->chal_from;
                snprintf(title, sizeof title, "%s", hp2_femenu_list_label(m, m->chal_sel));
                m->screen = scr; m->sel = sel;
                v = title;
            }
            if (v) { push(l, w, v); continue; }
        }
        push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
    }
    if (m->screen == HP2_FEM_RACE_SETUP) {   /* each setting's value, and arrows on the chosen row */
        int kind[8], cnt = setup_kinds(m, kind);
        char nm[16];
        for (k = 1; k <= cnt; k++) {
            const hp2_fe_widget *lw;
            hp2_fe_item *a;
            snprintf(nm, sizeof nm, "LINE_%d", k);
            lw = named(s, nm);
            a = push(l, lw, setup_value(m, kind[k - 1], m->setup_val[kind[k - 1]]));
            if (a && k != m->sel) grey(a, 0x50);
            if (k == m->sel && lw) {
                int j;
                for (j = 0; j < 2; j++) {
                    a = push(l, named(s, j ? "RIGHT_ARROW" : "LEFT_ARROW"), NULL);
                    if (a) { a->w.y = lw->y; a->w.visible = 1; }
                }
            }
        }
    }
    if (m->screen == HP2_FEM_CHALLENGE && chal_kind(m, m->sel) <= 2) {   /* the arrows of the chosen row's value */
        static const float row_y[3] = { 84.0f, 138.0f, 192.0f };
        int k;
        for (k = 0; k < 2; k++) {
            hp2_fe_item *a = push(l, named(s, k ? "RIGHT_ARROW" : "LEFT_ARROW"), NULL);
            if (a) { a->w.y = row_y[m->sel - 1] + 1.0f; a->w.visible = 1; }
        }
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
#define POP_X        77.0f      /* left edge of the Transmission / Handling panel */

static int car_items(hp2_femenu *m, list *l)
{
    if (!m->ev_mode && !car_open(m, m->car)) m->car = step_car(m, m->car, 1);   /* a car not yet won is not offered */
    const hp2_fe_screen *s = m->carsel;
    int nr = roster_count(m);
    static char unl[48];
    int i;
    hp2_fe_item *it;
    {   int c, open = 0;
        for (c = 0; c < HP2_CARS; c++) open += car_open(m, c);
        snprintf(unl, sizeof unl, "%s %d/%d", lang_or(m, 49071u, "Cars Unlocked:"), open, HP2_CARS);
    }
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        const char *str = NULL;
        if (w->x >= 640.0f || w->y >= 480.0f) continue;                /* parked off screen */
        if (!strcmp(w->name, "CAR_FADE_OVERLAY") || (!strcmp(w->name, "GRID_SHADOW") && w->sy >= 29.0f)) continue;
        if (has_prefix(w->name, "THUMBNAIL_") && strcmp(w->name, "THUMBNAIL_FRAME") && strcmp(w->name, "THUMBNAIL_RIGHT_FILM")) continue;
        if (has_prefix(w->name, "SPEC") || !strcmp(w->name, "CLASS")) continue;      /* the game shows no spec lines */
        if (has_prefix(w->name, "LOCKED") || has_prefix(w->name, "SWATCH") || !strcmp(w->name, "MENUITEM_1")
            || !strcmp(w->name, "POINTS") || !strcmp(w->name, "CAR_LOGO") || !strcmp(w->name, "SCROLLING_TEXT")
            || !strcmp(w->name, "ANIMATING_MENUITEM") || !strcmp(w->name, "ANIMATE_SELECTION")
            || !strcmp(w->name, "UP_ARROW") || !strcmp(w->name, "DOWN_ARROW")) continue;
        if (w->kind == HP2_FE_TEXT) {
            str = hp2_femenu_text(m, w);
            if (!strcmp(w->name, "CARSUNLOCKED")) {
                if (m->ev_mode) continue;          /* an event's own cars: no count */
                str = unl;
            }
        }
        it = push(l, w, str);
        if (!it) continue;
        if (!strcmp(w->name, "CARSUNLOCKED")) it->w.x = 233.0f;
    }
    for (i = -2; i <= 4; i++) {   /* the strip; a short list shows each car once */
        char nm[32];
        it = NULL;
        if (nr < 7 && (i < -((nr - 1) / 2) || i > nr / 2)) continue;
        snprintf(nm, sizeof nm, "THUMBNAIL_%s", roster_thumb(m, (m->car + i + 2 * nr) % nr));
        it = push(l, named(s, nm), NULL);
        if (it) {
            it->w.x = i <= 0 ? STRIP_X + STRIP_STEP * (float)i : STRIP_X + 44.0f + STRIP_STEP * (float)(i - 1);
            it->w.y = STRIP_Y;
        }
    }
    for (i = 0; i < CAR_SWATCHES && !m->car_popup; i++) {   /* colours, the chosen one framed */
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
                for (ch = 0; ch < 3; ch++) it->w.vcol[k][ch] = (uint8_t)((m->swatch_set ? m->swatch[i][ch] : SWATCH_RGB[i][ch]) * 128 / 255);
                it->w.vcol[k][3] = 0x80;
            }
        }
        if (i == m->color && (it = push(l, named(s, "SWATCH_FRAME"), NULL)) != NULL) {
            it->w.x = SWATCH_X - 2.0f;
            it->w.y = SWATCH_Y + SWATCH_STEP * (float)i - 2.0f;
        }
    }
    if (!m->car_popup && (it = push(l, named(s, "UP_ARROW"), NULL)) != NULL) { it->w.x = 587.0f; it->w.y = 93.0f; }
    if (!m->car_popup && (it = push(l, named(s, "DOWN_ARROW"), NULL)) != NULL) { it->w.x = 587.0f; it->w.y = 266.0f; }
    if (m->car_popup) {   /* the Transmission / Handling panel, built from the LOCKED_* pieces */
        static const float opt_x3[3] = { 90.0f, 266.0f, 411.0f }, opt_x2[2] = { 150.0f, 340.0f };
        static const uint32_t tr_id[3] = { 49020u, 49030u, 49010u }, hd_id[2] = { 49050u, 49060u };
        int n = m->car_popup == 1 ? 3 : 2, sel = m->car_popup == 1 ? m->trans : m->handling;
        it = push(l, named(s, "CAR_FADE_OVERLAY"), NULL);   /* the dim: its picture is not in the pack, a flat black stands in */
        if (it) {
            int k, ch;
            it->w.x = 0.0f; it->w.y = 0.0f; it->w.sx = 40.0f; it->w.sy = 30.0f;
            it->w.texture = SOLID_TEXTURE; it->w.visible = 1; it->w.layer = (uint8_t)(named(s, "LOCKED_BG1") ? named(s, "LOCKED_BG1")->layer + 1 : 1);
            for (k = 0; k < 4; k++) { for (ch = 0; ch < 3; ch++) it->w.vcol[k][ch] = 0; it->w.vcol[k][3] = 0x50; }
        }
        it = push(l, named(s, "LOCKED_BG1"), NULL);
        if (it) { it->w.x = POP_X; it->w.y = 242.0f; it->w.sx = (640.0f - POP_X) / 16.0f; it->w.sy = 81.0f / 16.0f; it->w.visible = 1; }
        it = push(l, named(s, "LOCKEDHEAD_BGBLUE"), NULL);
        if (it) { it->w.x = POP_X; it->w.y = 197.0f; it->w.sx = 0.879f * (640.0f - POP_X) / 450.0f; it->w.visible = 1; }
        it = push(l, named(s, "LOCKED_MIDLINE"), NULL);
        if (it) { it->w.x = POP_X; it->w.y = 242.0f; it->w.sx = (640.0f - POP_X) / 8.0f; it->w.visible = 1; }
        it = push(l, named(s, "LOCKED_BOTTOMLINE"), NULL);
        if (it) { it->w.x = POP_X; it->w.y = 323.0f; it->w.visible = 1; }
        it = push(l, named(s, "LOCKED_HEADING"), lang_or(m, m->car_popup == 1 ? 49000u : 49040u, m->car_popup == 1 ? "TRANSMISSION" : "HANDLING"));
        if (it) { it->w.x = POP_X + 24.0f; it->w.y = 203.0f; it->w.visible = 1; }
        for (i = 0; i < n; i++) {
            it = push(l, named(s, "LOCKED_TEXTTEST"), lang_or(m, m->car_popup == 1 ? tr_id[i] : hd_id[i], "?"));
            if (it) {
                it->w.x = n == 3 ? opt_x3[i] : opt_x2[i]; it->w.y = 273.0f; it->w.visible = 1;
                if (i == sel) memcpy(it->w.rgba, ORANGE, 4);
            }
        }
    }
    {   /* the car's name logo, a picture per car (CARLOGO_<name>) */
        char nm[40];
        snprintf(nm, sizeof nm, "CARLOGO_%s", roster_thumb(m, m->car));
        it = push(l, named(s, "CAR_LOGO"), NULL);
        if (it) { it->w.texture = hp2_fe_hash(nm); it->w.visible = 1; }
    }
    return l->n;
}

/* SELECT_TRACK. The course's picture (TRACK<n>), logo (LOGOTRACK<n>) and
 * thumbnails (TN_TRACK<n>, TN_TRACK<n>R for the other way round) are in the
 * front-end pack under those names. The layout stores the lists of one
 * mode on top of another (rows and the lap popup), so this places what it
 * shows by itself. */
static void fmt_laps(char *out, size_t cap, const char *fmt, int laps)
{
    const char *d = strstr(fmt, "%d");
    if (!d) { snprintf(out, cap, "%s", fmt); return; }
    snprintf(out, cap, "%.*s%d%s", (int)(d - fmt), fmt, laps, d + 2);
}

static int track_items(hp2_femenu *m, list *l)
{
    if (!track_open(m, m->track)) m->track = step_track(m, m->track, 1);
    const hp2_fe_screen *s = m->tracksel;
    const track_entry *t = &TRACKS[m->track];
    static char len[48], diff[40], total[64], laps[8];
    char nm[32];
    const char *dn[3];
    int i, k, loc_diff = t->num % 10 - 1;
    hp2_fe_item *it;
    dn[0] = lang_or(m, 55004u, "Beginner"); dn[1] = lang_or(m, 55005u, "Advanced"); dn[2] = lang_or(m, 55006u, "Expert");
    snprintf(len, sizeof len, "%.3f mi %s", (double)t->length,
             t->num % 10 == 3 ? lang_or(m, 55701u, "(Point-to-Point)") : lang_or(m, 55700u, "(Circuit)"));
    snprintf(diff, sizeof diff, "%s", dn[loc_diff < 0 ? 0 : loc_diff > 2 ? 2 : loc_diff]);
    fmt_laps(total, sizeof total, lang_or(m, 55025u, "Best Total Time (%d laps)"), m->laps);
    snprintf(laps, sizeof laps, "%d", m->laps);
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        const char *str = NULL;
        if (w->x >= 640.0f || w->y >= 480.0f || w->x < -40.0f) continue;     /* parked off screen */
        if (has_prefix(w->name, "TN_") || has_prefix(w->name, "TOD_") || has_prefix(w->name, "LOCKED") || has_prefix(w->name, "TRACK_IMAGE_XFADE")
            || !strcmp(w->name, "ANIMATING_MENUITEM") || !strcmp(w->name, "TRACK_ELEVATION") || !strcmp(w->name, "MENUITEM_1")
            || !strcmp(w->name, "MENUITEM_2") || !strcmp(w->name, "DIFFICULTY_NUMBER") || !strcmp(w->name, "LEVEL")
            || !strcmp(w->name, "UP_ARROW") || !strcmp(w->name, "DOWN_ARROW") || !strcmp(w->name, "LAP_LEFT_ARROW")
            || !strcmp(w->name, "LAP_RIGHT_ARROW") || has_prefix(w->name, "SELECTLAP_") || !strcmp(w->name, "POPUP_LINE_1")
            || !strcmp(w->name, "LAP_COUNT") || !strcmp(w->name, "BEST_TOTAL_TIME_STATS") || !strcmp(w->name, "BEST_LAP_TIME_STATS")
            || !strcmp(w->name, "LEFT_ARROW_SELECT") || !strcmp(w->name, "RIGHT_ARROW_SELECT")) continue;
        if (w->string_id == 55015u) continue;                                    /* "Elevation:": not known */
        if (w->kind == HP2_FE_TEXT) {
            str = hp2_femenu_text(m, w);
            if (!strcmp(w->name, "TRACK_LENGTH")) str = len;
            else if (!strcmp(w->name, "TRACK_DIFFICULTY")) str = diff;
            else if (!strcmp(w->name, "BEST_TOTAL_TIME")) str = total;
        }
        it = push(l, w, str);
        if (!it) continue;
        if (!strcmp(w->name, "TRACK_IMAGE_BOO")) {
            snprintf(nm, sizeof nm, "TRACK%d", t->num);
            it->w.texture = hp2_fe_hash(nm);
        } else if (!strcmp(w->name, "TRACK_LOGO")) {
            snprintf(nm, sizeof nm, "LOGOTRACK%d", t->num);
            it->w.texture = hp2_fe_hash(nm);
        }
    }
    it = push(l, named(s, "RIGHT_ARROW_SELECT"), NULL);   /* the strip has only the right arrow */
    if (it) it->w.visible = 1;
    for (k = 0; k < 8; k++) {   /* the strip: the selected course in the frame, the next seven after it */
        int idx = (m->track + k) % HP2_TRACKS;
        snprintf(nm, sizeof nm, "TN_TRACK%d%s", TRACKS[idx].num, k == 0 && m->alt ? "R" : "");
        it = push(l, named(s, nm), NULL);
        if (it) {
            it->w.x = k == 0 ? 242.0f : 285.0f + 40.0f * (float)(k - 1);
            it->w.y = 399.0f;
            it->w.visible = 1;
        }
    }
    {   /* the Time of Day column: the two lightings the course has, the chosen one framed and orange */
        static const float slot_y[2] = { 142.0f, 187.0f };
        const char *icon[2], *lab[2];
        icon[0] = "TOD_3PM";
        icon[1] = overcast_course(m->track) ? "TOD_OVERCAST" : "TOD_SUNSET";
        lab[0] = lang_or(m, 55008u, "Sunny");
        lab[1] = lang_or(m, overcast_course(m->track) ? 55009u : 55010u, "Overcast");
        for (k = 0; k < 2; k++) {
            it = push(l, named(s, icon[k]), NULL);
            if (it) { it->w.x = 530.0f; it->w.y = slot_y[k]; it->w.visible = 1; }
            it = push(l, named(s, "MENUITEM_1"), lab[k]);
            if (it) {
                it->w.y = slot_y[k] - 5.0f;
                it->w.align = HP2_FE_ALIGN_RIGHT;
                if (m->tod == k) memcpy(it->w.rgba, ORANGE, 4); else grey(it, 0x50);
            }
        }
        it = push(l, named(s, "TOD_FRAME"), NULL);
        if (it) { it->w.x = 526.0f; it->w.y = slot_y[m->tod] - 4.0f; it->w.visible = 1; }
        it = push(l, named(s, "UP_ARROW"), NULL);
        if (it) it->w.visible = 1;
        it = push(l, named(s, "DOWN_ARROW"), NULL);
        if (it) it->w.visible = 1;
    }
    if (!m->lap_popup && m->result[0][0] && m->go.race_type == HP2_RT_KNOCKOUT && m->go.event == 0) {   /* the Knockout's last race */
        const hp2_fe_widget *w = named(s, "POPUP_LINE_1");
        it = push(l, w, m->result[0]);
        if (it) { it->w.x = 320.0f; it->w.y = 70.0f; it->w.align = HP2_FE_ALIGN_CENTRE; it->w.visible = 1; memcpy(it->w.rgba, ORANGE, 4); }
    }
    if (m->lap_popup) {
        static const char *pop[] = { "SELECTLAP_TOP", "SELECTLAP_BG", "SELECTLAP_TOPBAR", "SELECTLAP_LOWERBAR", "POPUP_LINE_1", "LAP_COUNT",
                                     "LAP_LEFT_ARROW", "LAP_RIGHT_ARROW" };
        for (k = 0; k < 8; k++) {
            const hp2_fe_widget *w = named(s, pop[k]);
            it = push(l, w, w && w->kind == HP2_FE_TEXT ? (!strcmp(pop[k], "LAP_COUNT") ? laps : hp2_femenu_text(m, w)) : NULL);
            if (it) it->w.visible = 1;
        }
    }
    return l->n;
}

/* The title screen. Its splash (SPLASH_SCREEN) is a black placeholder the
 * game fills at run time, and its other picture is a movie texture, so the
 * port shows Main Menu's backdrop (grid, blueprints, borders) with the NFS
 * logo in the middle, and the title's own PRESS START. */
static const char *track_num_name(const hp2_femenu *m, int num, int ii);

/* An event's badge by its state in the progress record: locked, won or open
 * (every event is open without one). */
static uint32_t panel_texture(const hp2_femenu *m, int ev)
{
    static const char *NAMES[2][3] = { { "SPEED_EVENT_PANEL_LOCKED", "SPEED_EVENT_PANEL_UNLOCKED", "SPEED_EVENT_PANEL_WON" },
                                       { "PURSUIT_EVENT_PANEL_LOCKED", "PURSUIT_EVENT_PANEL_UNLOCKED", "PURSUIT_EVENT_PANEL_WON" } };
    const hp2_tree *t = m->trees ? &m->trees[m->tree_kind] : NULL;
    int st = 1;
    if (m->prog && t && ev >= 1 && ev <= t->count) {
        if (!hp2_progress_open(m->prog, m->tree_kind, t, ev)) st = 0;
        else if (hp2_progress_complete(m->prog, m->tree_kind, &t->ev[ev - 1])) st = 2;
    }
    return hp2_fe_hash(NAMES[m->tree_kind ? 1 : 0][st]);
}

/* A flat rectangle (the solid stand-in texture, tinted) from any image widget. */
/* The backdrop and header of Challenge, titled with the mode. */
static void chal_backdrop(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *c = m->chal;
    int i;
    if (!c) return;
    for (i = 0; i < c->count; i++) {
        const hp2_fe_widget *w = &c->widgets[i];
        hp2_fe_item *bi;
        if (w->x >= 640.0f || w->y >= 480.0f) continue;
        if (!strcmp(w->name, "CHALLENGE_TITLE")) push(l, w, m->tree_kind ? "Ultimate Fugitive" : "Ultimate Racer");
        else if (w->kind == HP2_FE_TEXT && !strcmp(w->name, "NONAME")) push(l, w, hp2_femenu_text(m, w));
        else if (w->kind == HP2_FE_IMAGE && (!strcmp(w->name, "GRID") || !strcmp(w->name, "MASTER_BP_OBJECT")
                 || !strcmp(w->name, "FADING_BP_OBJECT") || (!strcmp(w->name, "NONAME") && w->sx > 1.0f && w->y < 70.0f)
                 || (!strcmp(w->name, "GRID_SHADOW") && w->x == 0.0f))) { bi = push(l, w, NULL); if (bi) bi->w.layer = (uint8_t)(20 + (w->layer > 30 ? 30 : w->layer)); }
    }
}

/* A flat rectangle (the solid stand-in texture, tinted) from any image widget. */
static hp2_fe_item *rect(list *l, const hp2_fe_widget *tmpl, float x, float y, float w, float h,
                         int r, int g, int b, int a, int layer)
{
    hp2_fe_item *it = push(l, tmpl, NULL);
    int k;
    if (!it) return NULL;
    it->w.x = x; it->w.y = y; it->w.sx = w / 16.0f; it->w.sy = h / 16.0f;
    it->w.texture = SOLID_TEXTURE; it->w.visible = 1; it->w.layer = (uint8_t)layer;
    for (k = 0; k < 4; k++) {
        it->w.vcol[k][0] = (uint8_t)(r * 128 / 255); it->w.vcol[k][1] = (uint8_t)(g * 128 / 255);
        it->w.vcol[k][2] = (uint8_t)(b * 128 / 255); it->w.vcol[k][3] = (uint8_t)a;
    }
    return it;
}

/* The tree view: the map panel, the map's scale, where the event on view
 * sits (its badge's centre), the connector width, and each badge's top
 * left against its centre and its size ([0] World Racing's card, [1] Hot
 * Pursuit's star). Tree units are canvas units (hp2_tree.h). */
#define TREE_PANEL_X 141.0f
#define TREE_PANEL_Y 71.0f
#define TREE_PANEL_W 499.0f
#define TREE_PANEL_H 259.0f
#define TREE_BG_SCALE 1.25f
#define TREE_CX 320.0f
#define TREE_CY 282.0f
#define TREE_LINE 6.0f
/* The badge textures are 216 x 312 (card) and 272 x 272 (star) but the tree
 * draws them at half size: at full size the star covers the Status and
 * Reward text, and neighbouring events 130 apart would overlap. */
#define TREE_BADGE_S 0.5f
static const float BADGE_OFF[2][2] = { { -108.0f * TREE_BADGE_S, -150.0f * TREE_BADGE_S }, { -136.0f * TREE_BADGE_S, -130.0f * TREE_BADGE_S } };
static const float BADGE_SIZE[2][2] = { { 216.0f * TREE_BADGE_S, 312.0f * TREE_BADGE_S }, { 272.0f * TREE_BADGE_S, 272.0f * TREE_BADGE_S } };
#define TREE_NUM_HALF 66.0f     /* half the number's height at full size: its top is this far above its centre */

static int panel_hits(float x, float y, float w, float h)
{
    return x < TREE_PANEL_X + TREE_PANEL_W && x + w > TREE_PANEL_X && y < TREE_PANEL_Y + TREE_PANEL_H && y + h > TREE_PANEL_Y;
}

static void panel_clip(hp2_fe_item *it)
{
    it->clip = 1;
    it->clip_rect[0] = TREE_PANEL_X; it->clip_rect[1] = TREE_PANEL_Y;
    it->clip_rect[2] = TREE_PANEL_W; it->clip_rect[3] = TREE_PANEL_H;
}

static void set_vcol(hp2_fe_item *it, int r, int g, int b, int a)
{
    int k;
    for (k = 0; k < 4; k++) {
        it->w.vcol[k][0] = (uint8_t)r; it->w.vcol[k][1] = (uint8_t)g;
        it->w.vcol[k][2] = (uint8_t)b; it->w.vcol[k][3] = (uint8_t)a;
    }
}

/* The event tree with one event on view, placed as on a PCSX2 screenshot of
 * the game (the layout is stored scrolled: its title sits at the bottom and
 * its details at the top): the header of Challenge, a map panel, the event's
 * star at the centre with its number, "Event n / Status / Reward" at its
 * right and the event's title in a bar below. Every event is open. The map
 * (PURSUIT_TREE_BG / SPEED_TREE_BG) sits in the panel and the badge (a star
 * for Hot Pursuit, a card for World Racing, green tab = unlocked) behind the
 * number. With the trees' layout (hp2_tree, from the executable) the
 * neighbouring events and the red connectors show round it at the game's
 * spacing (130 x 350), clipped to the panel, and the map scrolls with the
 * event's place in the tree. The ghosted car logo is not in the port yet. */
static int tree_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->tree, *c = m->chal;
    const hp2_fe_widget *tmpl = named(s, "FLAG");
    static char num[24], evn[40], title[96], status[32], reward[64], tl[40], nnum[HP2_TREE_MAX_EVENTS][12];
    int i, ev = m->tree_ev;
    hp2_fe_item *it;
    snprintf(num, sizeof num, "%d", ev + 1);
    snprintf(tl, sizeof tl, "%s", lang_or(m, 70063u, "Event %d"));
    { char *d = strstr(tl, "%d"); if (d) { snprintf(evn, sizeof evn, "%.*s%d%s", (int)(d - tl), tl, ev + 1, d + 2); } else snprintf(evn, sizeof evn, "Event %d", ev + 1); }
    snprintf(title, sizeof title, "%s", lang_or(m, TREES[m->tree_kind].title + (uint32_t)ev, ""));
    {   const hp2_tree *tt = m->trees ? &m->trees[m->tree_kind] : NULL;
        const hp2_tree_event *te = tt && ev < tt->count ? &tt->ev[ev] : NULL;
        int locked = m->prog && tt && ev < tt->count && !hp2_progress_open(m->prog, m->tree_kind, tt, ev + 1);
        int done = !locked && m->prog && te && hp2_progress_complete(m->prog, m->tree_kind, te);
        snprintf(status, sizeof status, "%s", done ? lang_or(m, 300203u, "Complete") : locked ? lang_or(m, 62000u, "Locked") : lang_or(m, 62001u, "Unlocked"));
        reward[0] = 0;
        for (i = 0; te && i < te->nrewards; i++) {
            const hp2_tree_reward *r = &te->rewards[i];
            size_t len = strlen(reward);
            const char *nm = r->kind == 0 ? db_car_name(m, r->id >= 0 && r->id < CARDB_N ? CARDB[r->id] : "") : track_num_name(m, r->id, r->ii);
            snprintf(reward + len, sizeof reward - len, "%s%s", i ? ", " : "", nm);
        }
        if (!reward[0]) snprintf(reward, sizeof reward, "%s", lang_or(m, 67001u, "No reward"));
    }
    chal_backdrop(m, l);
    if (tmpl) {
        const hp2_tree *tr = (m->trees && m->trees[m->tree_kind].has_grid) ? &m->trees[m->tree_kind] : NULL;
        float wx0 = 0.0f, wy0 = 0.0f, fx = 0.5f, fy = 0.2f;
        if (tr && hp2_tree_pos(tr, ev + 1, &wx0, &wy0)) {
            fx = (float)tr->ev[ev].col / 19.0f;
            fy = (float)tr->ev[ev].row / 9.0f;
        }
        /* the map (8-bit 512 x 448) at 1.25, scrolled with the event's place
         * in the tree, clipped to the panel */
        it = push(l, tmpl, NULL);
        if (it) {
            it->w.texture = hp2_fe_hash(m->tree_kind ? "PURSUIT_TREE_BG" : "SPEED_TREE_BG");
            it->w.sx = it->w.sy = TREE_BG_SCALE;
            it->w.x = TREE_PANEL_X - fx * (512.0f * TREE_BG_SCALE - TREE_PANEL_W);
            it->w.y = TREE_PANEL_Y - fy * (448.0f * TREE_BG_SCALE - TREE_PANEL_H);
            it->w.visible = 1; it->w.layer = 17;
            set_vcol(it, 0x80, 0x80, 0x80, 0x80);
            panel_clip(it);
        }
        rect(l, tmpl, TREE_PANEL_X, TREE_PANEL_Y, TREE_PANEL_W, TREE_PANEL_H, 120, 130, 140, 0x30, 16);   /* the map panel */
        if (tr) {      /* the connectors and the other events, in tree units round this one */
            hp2_tree_seg seg[200];
            int k, ns = hp2_tree_segments(tr, seg, 200);
            for (k = 0; k < ns; k++) {
                float x0 = TREE_CX + seg[k].x0 - wx0, y0 = TREE_CY + seg[k].y0 - wy0;
                float x1 = TREE_CX + seg[k].x1 - wx0, y1 = TREE_CY + seg[k].y1 - wy0;
                float rx = x0 - TREE_LINE / 2, ry = y0 - TREE_LINE / 2, rw = x1 - x0 + TREE_LINE, rh = y1 - y0 + TREE_LINE;
                if (!panel_hits(rx, ry, rw, rh)) continue;
                it = rect(l, tmpl, rx, ry, rw, rh, 200, 24, 24, 0x80, 15);
                if (it) panel_clip(it);
            }
            for (k = 1; k <= tr->count; k++) {
                float x, y, bx, by;
                if (k == ev + 1 || !hp2_tree_pos(tr, k, &x, &y)) continue;
                x = TREE_CX + x - wx0; y = TREE_CY + y - wy0;
                bx = x + BADGE_OFF[m->tree_kind][0]; by = y + BADGE_OFF[m->tree_kind][1];
                if (!panel_hits(bx, by, BADGE_SIZE[m->tree_kind][0], BADGE_SIZE[m->tree_kind][1])) continue;
                it = push(l, tmpl, NULL);
                if (it) {
                    it->w.texture = panel_texture(m, k);
                    it->w.x = bx; it->w.y = by; it->w.sx = it->w.sy = TREE_BADGE_S; it->w.visible = 1; it->w.layer = 14;
                    set_vcol(it, 0x80, 0x80, 0x80, 0x80);
                    panel_clip(it);
                }
                snprintf(nnum[k - 1], sizeof nnum[k - 1], "%d", k);
                it = push(l, named(s, "EVENT_NUMBER"), nnum[k - 1]);
                if (it) {
                    it->w.x = x; it->w.y = y - TREE_NUM_HALF * TREE_BADGE_S; it->w.sx *= TREE_BADGE_S; it->w.sy *= TREE_BADGE_S; it->w.align = HP2_FE_ALIGN_CENTRE;
                    it->w.visible = 1; it->w.layer = 13;
                    panel_clip(it);
                }
            }
        }
        /* the event's badge: Hot Pursuit a 272 x 272 star, World Racing a
         * 216 x 312 card; the tab is green (unlocked), chequered (won) or
         * red (locked). Every event is open in the port. Placed as the
         * layout places EVENT_PANEL against EVENT_NUMBER (175, 135 against
         * 272, 199), moved with the number. */
        it = push(l, tmpl, NULL);
        if (it) {
            it->w.texture = panel_texture(m, ev + 1);
            it->w.x = TREE_CX + BADGE_OFF[m->tree_kind][0]; it->w.y = TREE_CY + BADGE_OFF[m->tree_kind][1];
            it->w.sx = it->w.sy = TREE_BADGE_S; it->w.visible = 1; it->w.layer = 10;
            set_vcol(it, 0x80, 0x80, 0x80, 0x80);
        }
        rect(l, tmpl, 383.0f, 205.0f, 257.0f, 35.0f, 20, 20, 20, 0x50, 8);         /* Status */
        rect(l, tmpl, 383.0f, 246.0f, 257.0f, 35.0f, 20, 20, 20, 0x50, 8);         /* Reward */
        rect(l, tmpl, 383.0f, 201.0f, 257.0f, 2.0f, 240, 240, 240, 0x80, 7);       /* under "Event n" */
    }
    it = push(l, named(s, "EVENT_NUMBER"), num);
    if (it) { it->w.x = 320.0f; it->w.y = TREE_CY - TREE_NUM_HALF * TREE_BADGE_S; it->w.sx *= TREE_BADGE_S; it->w.sy *= TREE_BADGE_S; it->w.align = HP2_FE_ALIGN_CENTRE; it->w.visible = 1; it->w.layer = 1; }
    it = push(l, named(s, "EVENT_TITLE"), evn);
    if (it) { it->w.sx = it->w.sy = 0.9f; it->w.x = 383.0f; it->w.y = 176.0f; it->w.visible = 1; it->w.layer = 0; }
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        if (w->kind != HP2_FE_TEXT || strcmp(w->name, "NONAME") || w->y < 40.0f || w->y > 80.0f) continue;
        it = push(l, w, hp2_femenu_text(m, w));      /* the "Status:" and "Reward:" labels */
        if (it) {
            it->w.x = 385.0f; it->w.y = w->y < 60.0f ? 206.0f : 247.0f; it->w.sx = it->w.sy = 0.8f; it->w.visible = 1; it->w.layer = 0;
        }
    }
    it = push(l, named(s, "OVERLAY_STATUSTITLE"), status);
    if (it) { it->w.x = 385.0f; it->w.y = 222.0f; it->w.sx = it->w.sy = 0.8f; it->w.visible = 1; it->w.layer = 0; }
    it = push(l, named(s, "OVERLAY_REWARDTITLE"), reward);
    if (it) { it->w.x = 385.0f; it->w.y = 263.0f; it->w.sx = it->w.sy = 0.8f; it->w.visible = 1; it->w.layer = 0; }
    if (tmpl) {
        it = push(l, named(s, "SCROLL_TEXT_CLIP"), NULL);     /* the title bar below the map */
        if (it) { it->w.x = 116.0f; it->w.y = 333.0f; it->w.sx = 1.03f; it->w.sy = 0.78f; it->w.visible = 1; it->w.layer = 9; }
    }
    it = push(l, named(s, "EVENT_TITLE"), title);
    if (it) { it->w.x = 127.0f; it->w.y = 336.0f; it->w.sx = it->w.sy = 0.75f; it->w.visible = 1; it->w.layer = 0; }
    return l->n;
}

/* ---- the event's details ---------------------------------------------- */

/* fmt with its first "%s" replaced by s. */
static void sub_s(char *out, size_t cap, const char *fmt, const char *s)
{
    const char *d = strstr(fmt, "%s");
    if (!d) { snprintf(out, cap, "%s", fmt); return; }
    snprintf(out, cap, "%.*s%s%s", (int)(d - fmt), fmt, s, d + 2);
}

/* fmt with "%d:%#02d" (or "%d:%02d") replaced by minutes:seconds. */
static void sub_time(char *out, size_t cap, const char *fmt, uint32_t ticks)
{
    unsigned secs = (unsigned)(ticks / HP2_TREE_TICKS);
    char t[16];
    const char *d = strstr(fmt, "%d:%#02d"), *e = d ? d + 8 : NULL;
    if (!d && (d = strstr(fmt, "%d:%02d")) != NULL) e = d + 7;
    snprintf(t, sizeof t, "%u:%02u", secs / 60u, secs % 60u);
    if (!d) { snprintf(out, cap, "%s %s", fmt, t); return; }
    snprintf(out, cap, "%.*s%s%s", (int)(d - fmt), fmt, t, e);
}

/* A track number's name: the course's (string 50000 + 10 x course, 51000 +
 * for its "II"); numbers ending 4..6 are the course in its other lighting. */
static const char *track_num_name(const hp2_femenu *m, int num, int ii)
{
    int base = num % 10 > 3 ? num - 3 : num;
    const char *s = m->lang ? hp2_lang_by_id(m->lang, (ii ? 51000u : 50000u) + 10u * (uint32_t)base) : NULL;
    return s ? s : "Track";
}

/* The objective, in the game's words (strings 64000-65001). */
static void objective(const hp2_femenu *m, const hp2_tree_event *e, char *out, size_t cap)
{
    const hp2_tree_cond *g = &e->target;
    out[0] = 0;
    if (g->kind == 2) {
        const char *fmt = e->race_type == HP2_RT_DELIVERY ? lang_or(m, 65001u, "Make your delivery in %d:%#02d or less")
                        : e->race_type == HP2_RT_PURSUIT ? "Arrest all of the speeders in %d:%#02d or less"   /* the port's: no string */
                        : lang_or(m, 65000u, "Record a Lap Time of %d:%#02d or less");
        sub_time(out, cap, fmt, g->value);
    } else if (g->kind == 1 && g->value >= 1 && g->value <= 8) {
        const char *place = lang_or(m, 61000u + g->value, "1st");
        sub_s(out, cap, g->value == 1 ? lang_or(m, 64000u, "Finish in %s place") : lang_or(m, 64001u, "Finish %s or better"), place);
    }
}

static void result_time(long ticks, char *out, size_t cap)
{
    long cs = ticks * 100 / HP2_TREE_TICKS;
    snprintf(out, cap, "%ld:%02ld.%02ld", cs / 6000, cs / 100 % 60, cs % 100);
}

static const char *ord_name(const hp2_femenu *m, int n)
{
    static const char *dflt[9] = { "", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th" };
    return lang_or(m, 61000u + (uint32_t)(n < 1 ? 1 : n > 8 ? 8 : n), dflt[n < 1 ? 1 : n > 8 ? 8 : n]);
}

/* The game's message (strings with %s for the place and "@" for a new line)
 * on one line. */
static void msg_line(const hp2_femenu *m, char *out, size_t cap, uint32_t id, const char *dflt, int place)
{
    char t[160];
    size_t k;
    sub_s(t, sizeof t, lang_or(m, id, dflt), ord_name(m, place));
    for (k = 0; t[k]; k++) if (t[k] == '@') t[k] = ' ';
    snprintf(out, cap, "%s", t);
}

/* A Tournament's points for 1st, 2nd .. 8th: the port's own table, the game's
 * scoring is not decoded. */
static const int TOURNAMENT_POINTS[8] = { 10, 8, 6, 5, 4, 3, 2, 1 };

static int series_car_id(const hp2_femenu *m, int car)
{
    return car <= 0 || car > HP2_FEM_MAX_OPP ? 0 : m->go.opp_id[car - 1];
}

/* A Tournament or Race Knockout race ended; fills the two lines and returns
 * the player's final place when the event is decided, else 0. */
static int series_result(hp2_femenu *m, int race_type, const hp2_race_result *r)
{
    int n = r->norder, k, pl = 0, sp = 0;
    hp2_fem_series *s = &m->ser;
    for (k = 0; k < n; k++) if (r->order[k] == 0) pl = k + 1;
    if (!pl) return 0;
    if (race_type == HP2_RT_KNOCKOUT) {
        int last = series_car_id(m, r->order[n - 1]), left = 0;
        s->alive[last] = 0;
        for (k = 0; k <= HP2_FEM_MAX_OPP; k++) left += s->alive[k];
        if (last == 0) {                     /* you were last: out, in the place of the cars still in */
            s->done = 1;
            sp = n;
            msg_line(m, m->result[0], sizeof m->result[0], 59019u, "You were knocked out! You finish in %s place!", sp);
        } else if (left <= 1) {              /* the rest are out: you won */
            s->done = 1;
            sp = 1;
            msg_line(m, m->result[0], sizeof m->result[0], 59010u, "You finished in %s place!", 1);
        } else {
            s->round++;
            msg_line(m, m->result[0], sizeof m->result[0], 59017u, "You finished in %s place. You advance to the next race!", pl);
        }
        return sp;
    }
    /* a Tournament */
    for (k = 0; k < n; k++) s->points[series_car_id(m, r->order[k])] += TOURNAMENT_POINTS[k < 8 ? k : 7];
    if (s->round + 1 < s->nrounds) {
        snprintf(m->result[0], sizeof m->result[0], "Race %d of %d: %s, +%d points, %d in all", s->round + 1, s->nrounds,
                 ord_name(m, pl), TOURNAMENT_POINTS[pl - 1 < 8 ? pl - 1 : 7], s->points[0]);
        snprintf(m->result[1], sizeof m->result[1], "Accept for the next race");
        s->round++;
        return 0;
    }
    {   /* the last race: most points wins, the last race's order breaks a tie */
        int key[HP2_RESULT_CARS], mine = 0;
        for (k = 0; k < HP2_RESULT_CARS; k++) key[k] = -1;
        for (k = 0; k < n; k++) {
            int id = series_car_id(m, r->order[k]);
            key[id] = s->points[id] * 100 + (8 - k);
        }
        sp = 1;
        for (k = 0; k < HP2_RESULT_CARS; k++) if (key[k] > key[0]) sp++;
        (void)mine;
    }
    s->done = 1;
    snprintf(m->result[0], sizeof m->result[0], "Tournament over: %s with %d points", ord_name(m, sp), s->points[0]);
    return sp;
}

const char *hp2_femenu_car_model(const hp2_femenu *m)
{
    if (m->ev_mode) return m->car >= 0 && m->car < m->ev_ncars ? cardb_name(m->ev_cars[m->car]) : NULL;
    return CARS[m->car % HP2_CARS].thumb;
}

int hp2_femenu_open_cars(const hp2_femenu *m) { int c, n = 0; for (c = 0; c < HP2_CARS; c++) n += car_open(m, c); return n; }
int hp2_femenu_open_tracks(const hp2_femenu *m) { int c, n = 0; for (c = 0; c < HP2_TRACKS; c++) n += track_open(m, c); return n; }

int hp2_femenu_result(hp2_femenu *m, const hp2_race_result *r)
{
    const hp2_tree *t = m->trees && m->go.event > 0 ? &m->trees[m->go.tree_kind & 1] : NULL;
    const hp2_tree_event *e = t && m->go.event <= t->count ? &t->ev[m->go.event - 1] : NULL;
    const hp2_tree_cond *g = e ? &e->target : NULL;
    char tm[24];
    int met = 0, place = r ? r->place : 0, decided = 1;
    m->result[0][0] = m->result[1][0] = 0;
    if (!r || !r->finished) return 0;
    result_time(r->ticks, tm, sizeof tm);
    if (((e && (e->race_type == HP2_RT_TOURNAMENT || e->race_type == HP2_RT_KNOCKOUT)) || (!e && m->go.race_type == HP2_RT_KNOCKOUT))
        && r->norder >= 2) {
        place = series_result(m, e ? e->race_type : HP2_RT_KNOCKOUT, r);
        decided = place > 0;
        if (!decided) return 0;
    } else if (e && e->race_type == HP2_RT_PURSUIT)
        snprintf(m->result[0], sizeof m->result[0], "All speeders arrested in %s", tm);
    else if (r->place >= 1 && r->place <= 8 && r->ticks <= 0)    /* a Lap Knockout you were out of: no time */
        snprintf(m->result[0], sizeof m->result[0], "You finished %s", ord_name(m, r->place));
    else if (r->place >= 1 && r->place <= 8)
        snprintf(m->result[0], sizeof m->result[0], "You finished %s, %s", ord_name(m, r->place), tm);
    else
        snprintf(m->result[0], sizeof m->result[0], "Race time %s", tm);
    if (g && g->kind == 1) met = place >= 1 && (uint32_t)place <= g->value;
    else if (g && g->kind == 2) {
        int whole = e->race_type == HP2_RT_DELIVERY || e->race_type == HP2_RT_PURSUIT;   /* the whole race, not a lap */
        long t2 = whole ? r->ticks : r->best;
        met = t2 > 0 && (uint32_t)t2 <= g->value;
    }
    if (g && g->kind) snprintf(m->result[1], sizeof m->result[1], "%s", met ? "Objective complete" : "Objective not met");
    if (m->prog && e && decided) {      /* kept: best place, best time (the whole race for Delivery and Pursuit, else the best lap) */
        int whole = e->race_type == HP2_RT_DELIVERY || e->race_type == HP2_RT_PURSUIT;
        if (hp2_progress_record(m->prog, m->go.tree_kind, m->go.event, place, whole ? r->ticks : r->best)) m->prog_dirty = 1;
    }
    return met;
}

/* The event's details on the game's event information panel (BLURB_BOX,
 * labels and lines in its fonts): the objective, the tracks, laps,
 * opponents, the car chosen and the reward; the bar below shows the
 * event's title. */
static int evinfo_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->evinfo;
    const hp2_tree *t = m->trees ? &m->trees[m->tree_kind] : NULL;
    const hp2_tree_event *e = t && m->tree_ev < t->count ? &t->ev[m->tree_ev] : NULL;
    const hp2_fe_widget *lab = s ? named(s, "TRACK") : NULL, *val = s ? named(s, "TRACK_LINE_1") : NULL;
    static char head[48], obj[128], tracks[200], laps[16], opp[16], reward[128];
    static const char *labels[5];
    const char *values[5];
    hp2_fe_item *it;
    int k, has_result = m->result[0][0] != 0;
    chal_backdrop(m, l);
    if (!s || !e || !lab || !val) return l->n;
    it = push(l, named(s, "BLURB_BOX"), NULL);
    if (it) it->w.visible = 1;
    { char f[48]; snprintf(f, sizeof f, "%s", lang_or(m, 63001u, "Event %d Objective:"));
      { const char *d = strstr(f, "%d"); if (d) snprintf(head, sizeof head, "%.*s%d%s", (int)(d - f), f, m->tree_ev + 1, d + 2); else snprintf(head, sizeof head, "%s", f); } }
    objective(m, e, obj, sizeof obj);
    tracks[0] = 0;
    for (k = 0; k < e->nraces && k < 2; k++) {
        size_t n = strlen(tracks);
        snprintf(tracks + n, sizeof tracks - n, "%s%s", k ? ", " : "", track_num_name(m, e->races[k].track, e->races[k].ii));
    }
    if (e->nraces > 2) { size_t n = strlen(tracks); snprintf(tracks + n, sizeof tracks - n, " +%d", e->nraces - 2); }
    {
        int lo = 99, hi = 0;
        for (k = 0; k < e->nraces; k++) { if (e->races[k].laps < lo) lo = e->races[k].laps; if (e->races[k].laps > hi) hi = e->races[k].laps; }
        if (!e->nraces) snprintf(laps, sizeof laps, "-");
        else if (lo == hi) snprintf(laps, sizeof laps, "%d", lo);
        else snprintf(laps, sizeof laps, "%d-%d", lo, hi);
    }
    snprintf(opp, sizeof opp, "%d", e->nopponents);
    reward[0] = 0;
    for (k = 0; k < e->nrewards; k++) {
        const hp2_tree_reward *r = &e->rewards[k];
        size_t n = strlen(reward);
        const char *nm = r->kind == 0 ? db_car_name(m, r->id >= 0 && r->id < CARDB_N ? CARDB[r->id] : "")
                       : track_num_name(m, r->id, r->ii);
        snprintf(reward + n, sizeof reward - n, "%s%s", k ? ", " : "", nm);
    }
    if (!reward[0]) snprintf(reward, sizeof reward, "%s", lang_or(m, 67001u, "No reward"));
    labels[0] = e->nraces > 1 ? lang_or(m, 66006u, "Tracks:") : "Track:";
    labels[1] = lang_or(m, 66002u, "Laps:");
    labels[2] = lang_or(m, 66001u, "Opponents:");
    labels[3] = lang_or(m, 66003u, "Car:");
    labels[4] = lang_or(m, 66007u, "Reward:");
    values[0] = tracks; values[1] = laps; values[2] = opp; values[3] = hp2_femenu_car_name(m, m->car); values[4] = reward;
    it = push(l, lab, head);
    if (it) { it->w.y = 74.0f; it->w.visible = 1; }
    it = push(l, val, obj);
    if (it) {
        size_t len = strlen(obj);
        float sc = len > 44 ? 44.0f / (float)len : 1.0f;
        it->w.y = 104.0f; it->w.sx *= sc; it->w.sy *= sc; it->w.visible = 1;
    }
    for (k = 0; k < 5; k++) {
        float y = has_result ? 140.0f + 28.0f * (float)k : 148.0f + 36.0f * (float)k;   /* closer together to leave room for the result */
        it = push(l, lab, labels[k]);
        if (it) { it->w.y = y; it->w.visible = 1; }
        it = push(l, val, values[k] ? values[k] : "");
        if (it) {
            size_t len = values[k] ? strlen(values[k]) : 0;
            float sc = len > 28 ? 28.0f / (float)len : 1.0f;     /* about 28 characters fit */
            it->w.x = 290.0f; it->w.y = y + 4.0f + (1.0f - sc) * 10.0f; it->w.sx *= sc; it->w.sy *= sc; it->w.visible = 1;
            it->clip = 1; it->clip_rect[0] = 140.0f; it->clip_rect[1] = y - 8.0f; it->clip_rect[2] = 478.0f; it->clip_rect[3] = 44.0f;
        }
    }
    for (k = 0; k < 2; k++) {       /* the last race of this event */
        if (!m->result[k][0]) continue;
        it = push(l, lab, m->result[k]);
        if (it) {
            size_t len = strlen(m->result[k]);
            float sc = len > 32 ? 32.0f / (float)len : 1.0f;      /* about 32 characters fit */
            it->w.y = 284.0f + 26.0f * (float)k; it->w.sx *= sc; it->w.sy *= sc; it->w.visible = 1;
        }
    }
    if (m->chal) help_bar(m, l, m->chal);
    return l->n;
}

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

/* The question over the page (the game's POPUP screen: a title, up to three
 * lines, two buttons on a faded backdrop). The wording is the port's own. */
static int popup_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->popscr;
    static char line[96];
    int i;
    snprintf(line, sizeof line, "%s?", hp2_menu_help(m->popup_row));
    if (strlen(line) > 1 && line[strlen(line) - 2] == '.') { line[strlen(line) - 2] = '?'; line[strlen(line) - 1] = 0; }
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        hp2_fe_item *it;
        const char *str = NULL;
        if (animated(w)) continue;
        if (w->kind == HP2_FE_TEXT) {
            if (!strcmp(w->name, "POPUP_TITLE")) str = hp2_menu_label(m->popup_row);
            else if (!strcmp(w->name, "POPUP_LINE_1")) str = line;
            else if (!strcmp(w->name, "POPUP_1")) str = lang_or(m, 150103u, "Yes");
            else if (!strcmp(w->name, "POPUP_2")) str = lang_or(m, 150104u, "No");
            else continue;                                   /* the other two lines stay empty */
        }
        it = push(l, w, str);
        if (it && w->kind == HP2_FE_TEXT) {
            int btn = !strcmp(w->name, "POPUP_1") ? 0 : !strcmp(w->name, "POPUP_2") ? 1 : -1;
            it->w.visible = 1;
            if (btn >= 0) { if (btn == m->popup_sel) memcpy(it->w.rgba, ORANGE, 4); else grey(it, 0x50); }
        } else if (it) it->w.visible = 1;
    }
    return l->n;
}

static int items_body(hp2_femenu *m, hp2_fe_item *items, int cap);

int hp2_femenu_items(hp2_femenu *m, hp2_fe_item *items, int cap)
{
    int n = items_body(m, items, cap);
    if (m->popup && m->popscr) {
        list l;
        l.it = items; l.n = n; l.cap = cap;
        {   int i;      /* the page sinks behind the question (a lower layer is in front) */
            for (i = 0; i < n; i++) items[i].w.layer = (uint8_t)(items[i].w.layer > 150 ? 255 : items[i].w.layer + 100);
        }
        n = popup_items(m, &l);
    }
    return n;
}

static int items_body(hp2_femenu *m, hp2_fe_item *items, int cap)
{
    list l;
    l.it = items;
    l.n = 0;
    l.cap = cap;
    if (m->screen == HP2_FEM_JUKEBOX) return jukebox_items(m, &l);
    if (m->screen == HP2_FEM_TITLE) return title_items(m, &l);
    if (m->screen == HP2_FEM_TREE) return tree_items(m, &l);
    if (m->screen == HP2_FEM_EVENT_INFO) return evinfo_items(m, &l);
    if (m->screen == HP2_FEM_SELECT_CAR) return car_items(m, &l);
    if (m->screen == HP2_FEM_SELECT_TRACK) return track_items(m, &l);
    if (list_screen(m)) return list_items(m, &l);
    return m->screen == HP2_FEM_OPTIONS ? options_items(m, &l) : settings_items(m, &l);
}

int hp2_femenu_race_args(const hp2_fem_race *g, char buf[][32], int cap)
{
    int n = 0, k;
#define ARG(...) do { if (n < cap) snprintf(buf[n++], 32, __VA_ARGS__); } while (0)
    ARG("--track");
    ARG("%d", g->track);
    if (g->reverse) ARG("--reverse");
    ARG("--laps");
    ARG("%d", g->laps);
    ARG("--car");
    ARG("%s", g->car);
    ARG("--skin");
    ARG("%d", g->skin);
    if (g->race_type == HP2_RT_LAP_KNOCKOUT) ARG("--lap-knockout");
    if (g->race_type == HP2_RT_PURSUIT) ARG("--pursuit");
    if (!g->nopp) {
        ARG("--opponents");
        ARG("0");
    }
    for (k = 0; k < g->nopp; k++) {
        ARG("--opponent");
        if (g->opp_skin[k] >= 1) ARG("%s:%d:%d", g->opp[k], g->opp_skill[k], g->opp_skin[k]);
        else ARG("%s:%d", g->opp[k], g->opp_skill[k]);
    }
#undef ARG
    return n;
}
