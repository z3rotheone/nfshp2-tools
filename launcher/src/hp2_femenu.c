/* hp2_femenu - Options menus from the original layouts. See hp2_femenu.h. */
#include "hp2_femenu.h"

#include <stdio.h>
#include <string.h>

/* string ids of OPTIONS rows: two open the settings list, Languages is not
 * shown on the US disc. Row n's help line is string 100019 + n (Audio,
 * row 1: 100020 "Set the volume and the Jukebox."). */
#define SID_CONTROLLER 100002u
#define SID_DISPLAY    100004u
#define SID_LANGUAGES  100007u
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

static const hp2_fe_widget *options_row(const hp2_femenu *m, int sel)
{
    return sel >= 1 && sel <= m->orows ? hp2_fe_row(m->options, m->orow[sel - 1]) : NULL;
}

static void refresh(hp2_femenu *m)
{
    int i;
    for (i = 0; i < HP2_ROW_COUNT; i++) hp2_menu_value(m->cfg, i, m->value[i], sizeof m->value[i]);
    m->help[0] = 0;
    if (m->screen == HP2_FEM_SETTINGS) {
        snprintf(m->help, sizeof m->help, "%s", hp2_menu_help(m->sel));
    } else if (m->lang && m->sel >= 1 && m->sel <= m->orows) {
        const char *h = hp2_lang_by_id(m->lang, SID_HELP_BASE + (uint32_t)m->orow[m->sel - 1]);
        if (h) snprintf(m->help, sizeof m->help, "%s", h);
    }
}

int hp2_femenu_init(hp2_femenu *m, const hp2_fe_layout *layout, const hp2_lang *lang, hp2_settings *cfg)
{
    memset(m, 0, sizeof *m);
    m->layout = layout;
    m->lang = lang;
    m->cfg = cfg;
    m->options = hp2_fe_find(layout, "OPTIONS");
    m->display = hp2_fe_find(layout, "DISPLAY_OPTIONS");
    if (!m->options || !m->display || !hp2_fe_row(m->display, 1)) return -1;
    {
        int i, n = hp2_fe_row_count(m->options);
        for (i = 1; i <= n && m->orows < (int)(sizeof m->orow / sizeof m->orow[0]); i++)
            if (hp2_fe_row(m->options, i)->string_id != SID_LANGUAGES) m->orow[m->orows++] = i;
    }
    if (m->orows < 1) return -1;
    m->screen = HP2_FEM_OPTIONS;
    m->sel = m->options_sel = 1;
    refresh(m);
    return 0;
}

static void open_settings(hp2_femenu *m, int row)
{
    m->options_sel = m->sel;
    m->screen = HP2_FEM_SETTINGS;
    m->sel = row;
    m->scroll = row > HP2_ROW_COUNT - HP2_FEM_VISIBLE ? HP2_ROW_COUNT - HP2_FEM_VISIBLE : row;
}

int hp2_femenu_input(hp2_femenu *m, hp2_fem_input in)
{
    int r = 0;
    if (m->screen == HP2_FEM_OPTIONS) {
        int n = m->orows;
        const hp2_fe_widget *w = options_row(m, m->sel);
        switch (in) {
        case HP2_IN_UP:   m->sel = m->sel > 1 ? m->sel - 1 : n; r = HP2_FEM_REDRAW; break;
        case HP2_IN_DOWN: m->sel = m->sel < n ? m->sel + 1 : 1; r = HP2_FEM_REDRAW; break;
        case HP2_IN_ACCEPT:
            if (w && w->string_id == SID_DISPLAY) { open_settings(m, HP2_ROW_INTERNAL_RES); r = HP2_FEM_REDRAW; }
            else if (w && w->string_id == SID_CONTROLLER) { open_settings(m, HP2_ROW_SCHEME); r = HP2_FEM_REDRAW; }
            break;
        case HP2_IN_BACK: r = HP2_FEM_QUIT; break;
        default: break;
        }
    } else {
        switch (in) {
        case HP2_IN_UP:   m->sel = m->sel > 0 ? m->sel - 1 : HP2_ROW_COUNT - 1; r = HP2_FEM_REDRAW; break;
        case HP2_IN_DOWN: m->sel = m->sel < HP2_ROW_COUNT - 1 ? m->sel + 1 : 0; r = HP2_FEM_REDRAW; break;
        case HP2_IN_LEFT:
        case HP2_IN_RIGHT:
        case HP2_IN_ACCEPT:
            if (in == HP2_IN_ACCEPT && hp2_menu_kind(m->sel) != HP2_KIND_TOGGLE) break;
            if (hp2_menu_adjust(m->cfg, m->sel, in == HP2_IN_LEFT ? -1 : 1)) r = HP2_FEM_REDRAW | HP2_FEM_CHANGED;
            break;
        case HP2_IN_BACK:
            m->screen = HP2_FEM_OPTIONS;
            m->sel = m->options_sel;
            r = HP2_FEM_REDRAW;
            break;
        }
        if (m->sel < m->scroll) m->scroll = m->sel;
        if (m->sel >= m->scroll + HP2_FEM_VISIBLE) m->scroll = m->sel - HP2_FEM_VISIBLE + 1;
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
        if (row && w->string_id == SID_LANGUAGES) continue;
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

static int settings_items(hp2_femenu *m, list *l)
{
    const hp2_fe_screen *s = m->display;
    const hp2_fe_widget *lab = hp2_fe_row(s, 1), *lab2 = hp2_fe_row(s, 2);
    const hp2_fe_widget *mask = named(s, "MI_1_MASK"), *clipw = named(s, "SCROLLING_MENU_CLIP_REG");
    const hp2_fe_widget *la = named(s, "LEFT_ARROW"), *ra = named(s, "RIGHT_ARROW");
    const hp2_fe_widget *ua = named(s, "UP_ARROW"), *da = named(s, "DOWN_ARROW");
    const hp2_fe_widget *val = NULL, *sep = NULL;
    float pitch = lab2 ? lab2->y - lab->y : 37.0f, clip[4] = { 0, 0, 640, 480 };
    int i, k;

    if (clipw) {   /* WHITE16X16 scaled */
        clip[0] = clipw->x; clip[1] = clipw->y; clip[2] = 16.0f * clipw->sx; clip[3] = 16.0f * clipw->sy;
    }
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        int in_list = w->y >= clip[1] - 8.0f && w->y < clip[1] + clip[3];
        /* the value of row 1: the centred text nearest its line */
        if (w->kind == HP2_FE_TEXT && (w->align & HP2_FE_ALIGN_CENTRE) && in_list && !animated(w)
            && (!val || w->y - lab->y < val->y - lab->y) && w->y >= lab->y)
            val = w;
        /* a thin separator under row 1 */
        if (!strcmp(w->name, "BOTTOM_MASK") && w->sy < 1.0f && (!sep || w->y < sep->y)) sep = w;
    }

    /* the screen itself: everything that is not a row of the stored list */
    for (i = 0; i < s->count; i++) {
        const hp2_fe_widget *w = &s->widgets[i];
        hp2_fe_item *it;
        if (animated(w) || is_line(w) || row_number(w) || w == val || !strcmp(w->name, "SCROLLING_TEXT")) continue;
        if (has_prefix(w->name, "MI_") && has_suffix(w->name, "_MASK")) continue;
        if (!strcmp(w->name, "BOTTOM_MASK")) continue;
        if (w->kind == HP2_FE_TEXT && (w->align & HP2_FE_ALIGN_CENTRE) && w->y >= clip[1] - 8.0f
            && w->y < clip[1] + clip[3]) continue;                  /* other rows' values */
        it = push(l, w, w->kind == HP2_FE_TEXT ? hp2_femenu_text(m, w) : NULL);
        /* the title (the big text above the list) names the section */
        if (it && w->kind == HP2_FE_TEXT && w->y < clip[1] - 30.0f && w->string_id)
            it->str = hp2_menu_section(m->sel);
    }

    /* the rows */
    for (k = m->scroll; k < HP2_ROW_COUNT && k < m->scroll + HP2_FEM_VISIBLE; k++) {
        float dy = pitch * (float)(k - m->scroll);
        int on = k == m->sel;
        hp2_fe_item *it;
        it = push(l, lab, hp2_menu_label(k));
        if (it) {
            it->w.y += dy;
            if (on) memcpy(it->w.rgba, ORANGE, 4); else grey(it, 0x50);
            set_clip(it, clip);
        }
        if (val) {
            it = push(l, val, m->value[k]);
            if (it) { it->w.y += dy; set_clip(it, clip); }
        }
        if (mask && !on) {
            it = push(l, mask, NULL);
            if (it) { it->w.y += dy; it->w.visible = 1; set_clip(it, clip); }
        }
        if (sep) {
            it = push(l, sep, NULL);
            if (it) { it->w.y = lab->y + (sep->y - lab->y) + dy; set_clip(it, clip); }
        }
        if (on) selection_lines(m, l, s, lab->y + dy);
        if (on && la && ra && hp2_menu_kind(k) != HP2_KIND_TOGGLE) {
            it = push(l, la, NULL);
            if (it) it->w.y += dy;
            it = push(l, ra, NULL);
            if (it) it->w.y += dy;
        }
    }
    if (ua && m->scroll > 0) push(l, ua, NULL);
    if (da && m->scroll + HP2_FEM_VISIBLE < HP2_ROW_COUNT) push(l, da, NULL);
    help_bar(m, l, s);
    return l->n;
}

int hp2_femenu_items(hp2_femenu *m, hp2_fe_item *items, int cap)
{
    list l;
    l.it = items;
    l.n = 0;
    l.cap = cap;
    return m->screen == HP2_FEM_OPTIONS ? options_items(m, &l) : settings_items(m, &l);
}
