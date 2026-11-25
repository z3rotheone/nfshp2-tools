/* hp2_femenu - the front end's menus, drawn from the original layouts;
 * the settings are driven by hp2_menu.
 *
 *   TITLE            the title screen: the splash (letterboxed) and PRESS
 *                    START; accept goes on to the Main Menu.
 *   MAIN             Main Menu: Hot Pursuit, World Racing, Quick Race,
 *                    Options. Quick Race is the port's own row (the game
 *                    has one in each mode menu): accept picks Hot Pursuit
 *                    or World Racing settings at random (quick_mode).
 *   HOT_PURSUIT      the port's order and names: Ultimate Fugitive (the
 *                    game's Ultimate Racer), Racing But With Pigs
 *                    (Challenge), You're The Pig (You're The Cop).
 *   WORLD_RACING     Ultimate Racer (the game's Championship), Just Race
 *                    (Challenge).
 *                    (Picking a mode does nothing yet: the screens after
 *                    it are not in the port so far.) These lists draw
 *                    entry k in row k's place; their row bands (MI_k_MASK,
 *                    the BOTTOM_MASK lines and the filler below) are laid
 *                    out again for the entry count, 55 px a row.
 *   SELECT_CAR       the car list of Quick Race (SELECT_CAR layout): the 23
 *                    cars, a strip of thumbnails with the selected one in
 *                    the frame and its name logo (CARLOGO_<name>).
 *                    Left/right pick a car, up/down a colour swatch; accept
 *                    asks the Transmission, then the Handling (left/right,
 *                    accept, back); back returns to Main Menu or to
 *                    Challenge. (The 3D car is not in the port.)
 *   CHALLENGE        Players, Race Type, Skill Level (left/right) and
 *                    Setup (Customize), opened by the Challenge rows; You're
 *                    The Pig opens the same screen without Race Type, titled
 *                    with the port's name for the row.
 *   SELECT_TRACK     after the car (accept): the 15 courses, as a strip of
 *                    thumbnails starting at the selected one, with its
 *                    picture, logo, length and difficulty. Left/right pick a
 *                    course; up/down pick the Time of Day (sunny or the
 *                    course's other lighting); accept asks SELECT LAPS
 *                    (left/right, back closes; accept starts the race:
 *                    HP2_FEM_RACE). Every course is open: no points. Back
 *                    returns to the car.
 *   EVENT_INFO       an event's details after its car; accept starts the
 *                    event's race (HP2_FEM_RACE), back returns to Handling.
 *   OPTIONS          the game's own screen with the rows the port keeps:
 *                    Audio, Controller, Display, Save/Load (player
 *                    progress; does nothing yet). Player Names, Credits
 *                    and Languages are hidden. Text and help lines from
 *                    FE_<LANGUAGE>.BIN. Audio, Display and Controller
 *                    open a settings page.
 *   settings pages   one per hp2_menu section: Audio (its rows on the
 *                    AUDIO_OPTIONS layout, volumes as the game's sliders),
 *                    Video (Display) and Controls (Controller) on the
 *                    DISPLAY_OPTIONS layout. Label, value or slider, row
 *                    mask and separator are cloned from the layout, each
 *                    placed relative to its own stored row; 7 rows show at
 *                    a time inside SCROLLING_MENU_CLIP_REG, with up/down
 *                    arrows when there is more. Left/right change the
 *                    selected setting; accept presses a button row
 *                    (Jukebox, Default Volumes, Default Jukebox) or flips
 *                    an on/off row.
 *   Jukebox          the JUKEBOX_INGAME layout: a Genre row (left/right
 *                    picks the genre), then the genre's songs five at a
 *                    time, title above artist, with their play status;
 *                    left/right on a song steps where it plays: Off,
 *                    Menu (menus only), On (menus and races). Needs
 *                    hp2_femenu_set_jukebox (the music bank's table).
 *
 * The selected row looks as in the game: orange label, the white
 * MI_OVERSCORE line above it and the orange MI_UNDERSCORE below, its dark
 * row mask lifted, and its help line in the ticker. Unlike the game, which
 * shows the left/right arrows only on rows with more than two values, the
 * port shows them on every settings row.
 *
 * Produces a list of hp2_fe_item for hp2_fedraw; holds no SDL and does not
 * allocate, so it is tested without a window.
 */
#ifndef HP2_FEMENU_H
#define HP2_FEMENU_H

#include "hp2_fe.h"
#include "hp2_jukebox.h"
#include "hp2_lang.h"
#include "hp2_menu.h"
#include "hp2_tree.h"
#include "hp2_race.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP2_FEM_OPTIONS, HP2_FEM_SETTINGS, HP2_FEM_JUKEBOX,
    HP2_FEM_TITLE, HP2_FEM_MAIN, HP2_FEM_HOT_PURSUIT, HP2_FEM_WORLD_RACING,
    HP2_FEM_SELECT_CAR, HP2_FEM_SELECT_TRACK, HP2_FEM_CHALLENGE, HP2_FEM_RACE_SETUP, HP2_FEM_TREE,
    HP2_FEM_EVENT_INFO
} hp2_fem_screen;
/* HP2_IN_HELP (Triangle) switches the ticker between the row's help line
 * and the menu buttons (accept, back, help), on every screen. */
typedef enum { HP2_IN_UP, HP2_IN_DOWN, HP2_IN_LEFT, HP2_IN_RIGHT, HP2_IN_ACCEPT, HP2_IN_BACK, HP2_IN_HELP } hp2_fem_input;

/* quick_mode: the settings Quick Race picked */
#define HP2_QUICK_NONE          0
#define HP2_QUICK_HOT_PURSUIT   1
#define HP2_QUICK_WORLD_RACING  2

/* hp2_femenu_input result bits */
#define HP2_FEM_REDRAW   1    /* something on screen changed */
#define HP2_FEM_CHANGED  2    /* a setting changed (save it at some point) */
#define HP2_FEM_QUIT     4    /* back out of the title screen (or of OPTIONS
                                 when it was the first screen): leave */

#define HP2_FEM_RACE     8    /* start a race: its set-up is in m->go
                                 (accept on SELECT LAPS, or on an event's
                                 details) */

/* A race to start (the port drives it with hp2track). Car names are the
 * car database's (GLOBALB chunk 0x34600). */
#define HP2_FEM_MAX_OPP 7
typedef struct {
    int  track;                      /* TRACKS\TRACKB<n>.LZC */
    int  reverse;                    /* 1: the course's II, the other way round */
    int  laps;
    char car[16];
    int  skin;                       /* 1.. */
    int  nopp;
    char opp[HP2_FEM_MAX_OPP][16];
    int  opp_skill[HP2_FEM_MAX_OPP]; /* 0..9 */
    int  opp_skin[HP2_FEM_MAX_OPP];  /* 1.., or -1: any */
    int  event;                      /* the event number, 0 for a quick or single race */
    int  tree_kind;                  /* with event: 0 Championship, 1 Hot Pursuit */
    int  race_type;                  /* with event: HP2_RT_* */
} hp2_fem_race;

#define HP2_FEM_VISIBLE  7    /* settings rows shown at once */
#define HP2_FEM_SONGS    5    /* jukebox songs shown at once */
#define HP2_FEM_EVENT_ROWS 5  /* event list: events shown at once */

typedef struct {
    const hp2_fe_layout *layout;
    const hp2_fe_screen *options, *display, *audio;
    const hp2_fe_screen *title, *mainm, *hotp, *world, *carsel, *tracksel, *chal, *cop, *rsetup, *tree;   /* NULL if missing */
    const hp2_fe_screen *evinfo;    /* the event information panel (screen a744ba7b), or NULL */
    int                  track;     /* SELECT_TRACK: index into the course table */
    int                  tod;       /* 0 sunny, 1 the course's other lighting */
    int                  alt;       /* 1: the course the other way round */
    int                  focus;     /* 0 the strip, 1 Time of Day, 2 Alternate */
    int                  laps;      /* the lap count the popup shows */
    int                  lap_popup; /* SELECT LAPS is open */
    int                  chal_cop;  /* 1: the You're The Pig setup (no Race Type row) */
    int                  chal_from; /* the screen Challenge was opened from */
    int                  chal_sel;  /* that screen's row */
    int                  players;   /* Challenge: 0 One Player, 1 Two Players */
    int                  race_type; /* 0 Single Race, 1 Free Run, 2 Knockout, 3 Lap Knockout */
    int                  skill;     /* 0 Beginner, 1 Advanced, 2 Expert */
    int                  setup_val[8]; /* RACE_SETUP: the value of each setting (SU_*) */
    int                  setup_sel; /* RACE_SETUP's row when car select was opened */
    int                  tree_kind; /* event list: 0 Ultimate Racer (Championship), 1 Ultimate Fugitive (Hot Pursuit) */
    int                  tree_ev;   /* event list: the chosen event, 0-based */
    int                  tree_from; /* the screen the event list was opened from */
    int                  tree_scroll;   /* event list: first event on view, 0-based */
    const hp2_tree      *trees;     /* [0] Championship, [1] Hot Pursuit (hp2_tree_parse),
                                       or NULL: accept on an event then does nothing */
    int                  ev_mode;   /* car select shows an event's own cars */
    int                  ev_ncars, ev_cars[HP2_TREE_MAX_CARS];   /* car database indices */
    hp2_fem_race         go;        /* the race HP2_FEM_RACE asks for */
    int                  car_popup; /* car select: 0 none, 1 Transmission, 2 Handling */
    int                  trans;     /* 0 Automatic, 1 Manual, 2 Semi-Auto */
    int                  handling;  /* 0 Classic, 1 Extreme */
    int                  car;       /* SELECT_CAR: index into the car table */
    int                  color;     /* SELECT_CAR: the swatch chosen, 0-7 */
    int                  main_sel;  /* where Main Menu was left */
    int                  options_from_main;   /* back from OPTIONS: Main Menu */
    int                  quick_mode;    /* HP2_QUICK_*: Quick Race's last pick */
    uint32_t             seed;          /* Quick Race's dice (any value; set it
                                           from the clock) */
    int                  show_buttons;  /* HELP: the ticker shows the buttons */
    const hp2_fe_screen *page;      /* settings: the layout in use */
    int                  first, rows;   /* settings: the page's hp2_menu rows */
    const hp2_lang      *lang;      /* may be NULL: placeholder text is used */
    hp2_settings        *cfg;
    hp2_fem_screen       screen;
    int                  sel;       /* OPTIONS: row 1..n; settings: hp2_row */
    int                  options_sel;
    int                  orows;     /* OPTIONS rows shown */
    int                  orow[16];  /* their MENUITEM numbers, in order; 0 is the
                                       port's Jukebox entry (after Audio). Entry k
                                       is drawn in row k's place. */
    int                  scroll;    /* settings: first row shown */
    char                 value[HP2_ROW_COUNT][HP2_MENU_VALUE];
    const hp2_jukebox   *jb;        /* NULL: the Jukebox row does nothing */
    const hp2_fe_screen *jbscreen;
    int                  genre;     /* jukebox: genre shown */
    int                  jsel;      /* jukebox: 0 = the Genre row, 1.. = song */
    int                  jscroll;   /* jukebox: first song shown (0-based) */
    int                  nsongs, songs[HP2_JB_MAX];  /* the genre's songs */
    int                  capture;   /* bind row waiting for a button press, or -1 */
    int                  changed_song;  /* song whose status the last input changed, or -1 */
    char                 help[96];
    char                 result[2][96];  /* the last race, on the event's details: how you finished, the objective */
} hp2_femenu;

/* -1 when the layout lacks OPTIONS or DISPLAY_OPTIONS. Starts on OPTIONS;
 * hp2_femenu_start moves to another screen (the front end starts on TITLE). */
int hp2_femenu_init(hp2_femenu *m, const hp2_fe_layout *layout, const hp2_lang *lang, hp2_settings *cfg);

/* Opens TITLE, MAIN, HOT_PURSUIT, WORLD_RACING or OPTIONS on its first row.
 * 0 if that screen is not in the layout. */
int hp2_femenu_start(hp2_femenu *m, hp2_fem_screen s);

int hp2_femenu_input(hp2_femenu *m, hp2_fem_input in);

/* Fills up to cap items for the current screen; returns how many. 128 is
 * always enough for the shipped layouts. Item strings point into m, the
 * language buffer or hp2_menu, so they stay valid until the next input. */
int hp2_femenu_items(hp2_femenu *m, hp2_fe_item *items, int cap);

/* Binding by pressing: accept on an action's row makes the menu wait
 * (hp2_femenu_capturing gives the row); the front end then passes the next
 * controller button to hp2_femenu_capture, or cancels (Esc / back). */
int hp2_femenu_capturing(const hp2_femenu *m);
int hp2_femenu_capture(hp2_femenu *m, hp2_button b);
void hp2_femenu_cancel_capture(hp2_femenu *m);

/* Re-reads every row's value (after the pad or its button names change). */
void hp2_femenu_refresh(hp2_femenu *m);

/* The music bank's song table, for the Jukebox screen (may be NULL). */
void hp2_femenu_set_jukebox(hp2_femenu *m, const hp2_jukebox *jb);

/* A song's play status: the one set in hp2_settings, else the bank's. */
hp2_play hp2_femenu_play(const hp2_femenu *m, int song);

/* The song box shown when a song starts: GLOBALB.BUN's screen 0x54987EB2
 * (the game's "EA GAMES TRAX" chyron, bottom left): title in quotes, artist,
 * album if any, and the port's own line, "NFS:HP2 Remixx", in place of the
 * EA GAMES TRAX logo (whose TM is dropped). The strings point into t and
 * quoted (64 bytes or more), which must outlive the items. Returns the item
 * count (draw them after the menu: the box sits on top). */
#define HP2_SONGBOX_ID    0x54987EB2u
#define HP2_SONGBOX_LINE  "NFS:HP2 Remixx"
int hp2_femenu_songbox(const hp2_fe_screen *box, const hp2_jb_track *t, char *quoted, size_t qcap,
                       hp2_fe_item *items, int cap);

/* Takes how the race m->go started ended (hp2track's --result file, parsed
 * by hp2_race_result_parse). An event's objective is checked: place <= value,
 * or a time (a Delivery counts the whole race, other events the best lap;
 * a Pursuit has no cops here, so its goal is never met). The two lines
 * show on the event's details until the next race. Returns 1 when the
 * objective was met, 0 when not or when the event has none or the race was
 * not finished. */
int hp2_femenu_result(hp2_femenu *m, const hp2_race_result *r);

/* The command-line arguments that start go's race in hp2track (after the
 * program name and --disc DIR): --track, --reverse, --laps, --car, --skin,
 * --opponents 0 or --opponent NAME:SKILL[:SKIN] each. Writes up to cap
 * strings of 32 bytes into buf; returns how many. */
int hp2_femenu_race_args(const hp2_fem_race *go, char buf[][32], int cap);

/* The open settings page's section ("Video", "Controls", "Audio"), or "". */
const char *hp2_femenu_page(const hp2_femenu *m);

/* For tests and front ends: the OPTIONS row count, the string a row shows. */
int hp2_femenu_options_rows(const hp2_femenu *m);
/* Entries of MAIN, HOT_PURSUIT or WORLD_RACING (0 elsewhere), and entry k's
 * label (1-based; NULL out of range). */
#define HP2_CARS 23
#define HP2_TRACKS 15
int hp2_femenu_list_count(const hp2_femenu *m);
/* Car select's cars: car k's full name (NULL out of range). The port's 23,
 * or, opened from an event, that event's own (pursuit cars included). */
const char *hp2_femenu_car_name(const hp2_femenu *m, int k);
/* Course k's name, as the game's strings give it (NULL out of range), and
 * the track number it opens: TRACKS\\TRACKB<n>.LZC, n+3 for the other lighting. */
const char *hp2_femenu_track_name(const hp2_femenu *m, int k, int alternate);
int hp2_femenu_track_number(const hp2_femenu *m);
const char *hp2_femenu_list_label(const hp2_femenu *m, int k);
const char *hp2_femenu_text(const hp2_femenu *m, const hp2_fe_widget *w);

#ifdef __cplusplus
}
#endif
#endif
