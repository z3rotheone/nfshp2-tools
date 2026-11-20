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

const char *hp2_femenu_text(const hp2_femenu *m, const hp2_fe_widget *w)
{
    const char *s = NULL;
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

void hp2_femenu_set_jukebox(hp2_femenu *m, const hp2_jukebox *jb)
{
    m->jb = jb && jb->count > 0 ? jb : NULL;
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
        m->screen = HP2_FEM_SETTINGS;           /* back to the Audio page, on Jukebox */
        m->sel = HP2_ROW_JUKEBOX;
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

static const hp2_fe_widget *options_row(const hp2_femenu *m, int sel)
{
    return sel >= 1 && sel <= m->orows ? hp2_fe_row(m->options, m->orow[sel - 1]) : NULL;
}

static void refresh(hp2_femenu *m)
{
    int i;
    for (i = 0; i < HP2_ROW_COUNT; i++) hp2_menu_value(m->cfg, i, m->value[i], sizeof m->value[i]);
    m->help[0] = 0;
    if (m->capture >= 0) {
        snprintf(m->value[m->capture], sizeof m->value[m->capture], "%s", "Press a button");
        snprintf(m->help, sizeof m->help, "Press the button for %s. Esc cancels.", hp2_menu_label(m->capture));
    } else if (m->screen == HP2_FEM_SETTINGS) {
        snprintf(m->help, sizeof m->help, "%s", hp2_menu_help(m->sel));
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
    if (!m->options || !m->display || !hp2_fe_row(m->display, 1)) return -1;
    if (m->audio && !hp2_fe_row(m->audio, 1)) m->audio = NULL;
    {
        int i, n = hp2_fe_row_count(m->options);
        for (i = 1; i <= n && m->orows < (int)(sizeof m->orow / sizeof m->orow[0]); i++)
            if (!hidden_row(hp2_fe_row(m->options, i))) m->orow[m->orows++] = i;
    }
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
    if (m->screen == HP2_FEM_JUKEBOX) {
        r = jukebox_input(m, in);
    } else if (m->screen == HP2_FEM_OPTIONS) {
        int n = m->orows;
        const hp2_fe_widget *w = options_row(m, m->sel);
        switch (in) {
        case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : n; r = HP2_FEM_REDRAW; break;
        case HP2_IN_DOWN: m->sel = m->sel < n ? m->sel + 1 : 1; r = HP2_FEM_REDRAW; break;
        case HP2_IN_ACCEPT:
            if (!w) break;
            if (w->string_id == SID_DISPLAY) open_page(m, "Video");
            else if (w->string_id == SID_CONTROLLER) open_page(m, "Controls");
            else if (w->string_id == SID_AUDIO) open_page(m, "Audio");
            if (m->screen == HP2_FEM_SETTINGS) r = HP2_FEM_REDRAW;
            break;
        case HP2_IN_BACK: r = HP2_FEM_QUIT; break;
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
            if (m->sel == HP2_ROW_JUKEBOX) {
                if (m->jb && m->jbscreen) {
                    m->screen = HP2_FEM_JUKEBOX;
                    m->jsel = 0;
                    pick_genre(m, 0);
                    r = HP2_FEM_REDRAW;
                }
            } else if (hp2_menu_bind_action(m->sel) >= 0) {
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
    const hp2_fe_widget *sel = options_row(m, m->sel);
    char mask[24];
    int i;
    snprintf(mask, sizeof mask, "MI_%d_MASK", sel ? row_number(sel) : 0);
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        int row = row_number(w);
        hp2_fe_item *it;
        if (animated(w) || is_line(w) || !strcmp(w->name, "SCROLLING_TEXT")) continue;
        if (row && hidden_row(w)) continue;
        it = push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
        if (!it) continue;
        if (row) {
            if (w == sel) memcpy(it->w.rgba, ORANGE, 4);
            else grey(it, 0x50);
        }
        if (!strcmp(w->name, mask)) it->w.visible = 0;    /* the selected row's band lifts */
    }
    if (sel) selection_lines(m, l, s, sel->y);
    help_bar(m, l, s);
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
    return m->screen == HP2_FEM_OPTIONS ? options_items(m, &l) : settings_items(m, &l);
}
