/* hp2_femenu - the Options menus of the port, drawn from the original
 * layouts and driven by hp2_menu.
 *
 *   OPTIONS          the game's own screen and rows (Audio, Controller,
 *                    Display, Save/Load, Player Names, Credits; Languages
 *                    is hidden, as on the US disc), text and help lines
 *                    from FE_<LANGUAGE>.BIN. "Display" and "Controller"
 *                    open the settings list.
 *   settings list    the DISPLAY_OPTIONS layout with the hp2_menu rows in
 *                    place of the game's: label, value between the arrows,
 *                    row mask and separator cloned from the layout's first
 *                    row, scrolling 7 rows at a time inside the layout's
 *                    SCROLLING_MENU_CLIP_REG, up/down arrows when there is
 *                    more. Left/right change the selected setting.
 *
 * The selected row looks as in the game: orange label, the white
 * MI_OVERSCORE line above it and the orange MI_UNDERSCORE below, its dark
 * row mask lifted, arrows only on rows with more than two values, and its
 * help line in the ticker.
 *
 * Produces a list of hp2_fe_item for hp2_fedraw; holds no SDL and does not
 * allocate, so it is tested without a window.
 */
#ifndef HP2_FEMENU_H
#define HP2_FEMENU_H

#include "hp2_fe.h"
#include "hp2_lang.h"
#include "hp2_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HP2_FEM_OPTIONS, HP2_FEM_SETTINGS } hp2_fem_screen;
typedef enum { HP2_IN_UP, HP2_IN_DOWN, HP2_IN_LEFT, HP2_IN_RIGHT, HP2_IN_ACCEPT, HP2_IN_BACK } hp2_fem_input;

/* hp2_femenu_input result bits */
#define HP2_FEM_REDRAW   1    /* something on screen changed */
#define HP2_FEM_CHANGED  2    /* a setting changed (save it at some point) */
#define HP2_FEM_QUIT     4    /* back out of OPTIONS: leave the menus */

#define HP2_FEM_VISIBLE  7    /* settings rows shown at once */

typedef struct {
    const hp2_fe_layout *layout;
    const hp2_fe_screen *options, *display;
    const hp2_lang      *lang;      /* may be NULL: placeholder text is used */
    hp2_settings        *cfg;
    hp2_fem_screen       screen;
    int                  sel;       /* OPTIONS: row 1..n; settings: hp2_row */
    int                  options_sel;
    int                  orows;     /* OPTIONS rows shown */
    int                  orow[16];  /* their MENUITEM numbers, in order */
    int                  scroll;    /* settings: first row shown */
    char                 value[HP2_ROW_COUNT][HP2_MENU_VALUE];
    char                 help[96];
} hp2_femenu;

/* -1 when the layout lacks OPTIONS or DISPLAY_OPTIONS. */
int hp2_femenu_init(hp2_femenu *m, const hp2_fe_layout *layout, const hp2_lang *lang, hp2_settings *cfg);

int hp2_femenu_input(hp2_femenu *m, hp2_fem_input in);

/* Fills up to cap items for the current screen; returns how many. 128 is
 * always enough for the shipped layouts. Item strings point into m, the
 * language buffer or hp2_menu, so they stay valid until the next input. */
int hp2_femenu_items(hp2_femenu *m, hp2_fe_item *items, int cap);

/* For tests and front ends: the OPTIONS row count, the string a row shows. */
int hp2_femenu_options_rows(const hp2_femenu *m);
const char *hp2_femenu_text(const hp2_femenu *m, const hp2_fe_widget *w);

#ifdef __cplusplus
}
#endif
#endif
