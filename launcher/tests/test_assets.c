/* Tests for the asset modules behind the front-end renderer: hp2_tex
 * (texture packs), hp2_font (fonts, string codes), hp2_lang (menu text),
 * hp2_zdir (the disc archive) and hp2_femenu (the Options menus). No SDL.
 *
 *   test_assets                          synthetic data only
 *   test_assets --frontb F --globalb G --lang L [--disc DIR]
 *                                        also the real files (entries 21,
 *                                        302 and 3; DIR = the disc's NFSHP2
 *                                        folder, to read FRONTB.BUN through
 *                                        ZDIR.BIN and compare)
 *   test_assets --texdump FILE...        one line per texture with a hash of
 *                                        its decoded RGBA, for check_tex.py
 */
#include "../src/hp2_fe.h"
#include "../src/hp2_femenu.h"
#include "../src/hp2_font.h"
#include "../src/hp2_lang.h"
#include "../src/hp2_tex.h"
#include "../src/hp2_zdir.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        checks++;                                                   \
        if (!(cond)) {                                              \
            failures++;                                             \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);           \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
        }                                                           \
    } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.01f)

static void set32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void set16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void setf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); set32(p, u); }

/* ---- string codes -------------------------------------------------------- */

static void test_tokens(void)
{
    const char *s = "A$RGB(134,96,48)B@$FONT(FONT_IMPACTNORMAL)$TARGET_TIME$RGB(1,2)";
    hp2_tok t;
    int n = 0;
    printf("string codes\n");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_CHAR && t.ch == 'A', "A");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_RGB && t.rgb[0] == 134 && t.rgb[1] == 96 && t.rgb[2] == 48, "$RGB");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_CHAR && t.ch == 'B', "B");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_NEWLINE, "@ breaks the line");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_FONT && t.font == 0x5F3A4A3Cu, "$FONT hashes the name");
    t = hp2_text_next(&s); CHECK(t.kind == HP2_TOK_CHAR && t.ch == '$', "$TARGET_TIME stays text");
    while ((t = hp2_text_next(&s)).kind == HP2_TOK_CHAR) n++;
    CHECK(n == 11 + 9 && t.kind == HP2_TOK_END, "an unfinished $RGB( is text too (%d chars)", n);
    s = NULL;
    CHECK(hp2_text_next(&s).kind == HP2_TOK_END, "NULL string");
}

/* ---- fonts --------------------------------------------------------------- */

/* A font chunk with ' ', 'T', 'L' and one kerning pair: "LT" -3. */
static size_t make_font(uint8_t *b, uint32_t hash, int good_kern)
{
    size_t n = 3, go = 28, ko = go + 32 * n, total = ko + 8;
    const char cs[3] = { ' ', 'L', 'T' };
    size_t i;
    memset(b, 0, total);
    set32(b, 0x00015010u);
    set32(b + 4, (uint32_t)(total - 8));
    set32(b + 8, hash);
    set32(b + 12, (uint32_t)n);
    set32(b + 16, good_kern ? 1 : 50);    /* 50 pairs cannot fit: dropped */
    set32(b + 20, (uint32_t)go);
    set32(b + 24, (uint32_t)ko);
    for (i = 0; i < n; i++) {
        uint8_t *r = b + go + 32 * i;
        set16(r, (unsigned char)cs[i]);
        setf(r + 4, 10.0f * (float)i);
        setf(r + 12, 9.0f);
        setf(r + 16, i == 2 ? 30.0f : 27.0f);
        setf(r + 20, 8.0f);
        if (cs[i] == 'T') { set16(r + 2, 1); set32(r + 28, 0); }
    }
    set32(b + ko, 'L');
    setf(b + ko + 4, -3.0f);
    return total;
}

static void test_font(void)
{
    uint8_t b[512], two[1024];
    size_t n, m;
    hp2_fontset set = { 0 };
    const hp2_font *f;
    hp2_glyph g;
    printf("fonts\n");
    n = make_font(b, 0x1234u, 1);
    CHECK(hp2_font_scan(&set, b, n) == 1 && set.count == 1, "one font");
    f = hp2_font_find(&set, 0x1234u);
    CHECK(f && f->count == 3 && f->kern_count == 1 && NEAR(f->line_height, 30.0f), "header");
    if (f) {
        CHECK(hp2_font_glyph(f, 'T', &g) && NEAR(g.x, 20) && NEAR(g.advance, 8) && g.kern_count == 1, "glyph T");
        CHECK(!hp2_font_glyph(f, 'Q', &g), "no Q");
        CHECK(NEAR(hp2_font_kern(f, 'L', 'T'), -3.0f), "LT kerns");
        CHECK(NEAR(hp2_font_kern(f, 'T', 'L'), 0.0f), "TL does not");
        CHECK(NEAR(hp2_text_width(f, "LT"), 13.0f), "width with kerning");
        CHECK(NEAR(hp2_text_width(f, "L$RGB(1,2,3)T@TTTT"), 13.0f), "codes skipped, first line only");
    }
    /* a second bundle: the same hash is not added twice, a new one is */
    m = make_font(two, 0x1234u, 1);
    m += make_font(two + m, 0x5678u, 0);
    CHECK(hp2_font_scan(&set, two, m) == 1 && set.count == 2, "second scan adds only the new font");
    f = hp2_font_find(&set, 0x5678u);
    CHECK(f && f->kern == NULL && NEAR(hp2_font_kern(f, 'L', 'T'), 0.0f), "kerning table that does not fit is dropped");
    /* every cut of the chunk: no crash, no read past the end (ASan) */
    for (m = 0; m < n; m++) {
        hp2_fontset s2 = { 0 };
        uint8_t *c = (uint8_t *)malloc(m ? m : 1);
        memcpy(c, b, m);
        hp2_font_scan(&s2, c, m);
        CHECK(s2.count == 0, "cut at %zu: nothing", m);
        hp2_font_free(&s2);
        free(c);
    }
    hp2_font_free(&set);
}

/* ---- language file ------------------------------------------------------- */

static void test_lang(void)
{
    static uint8_t b[0x6C + 32 + 32];
    const char text[] = "Audio\0Help\0";
    hp2_lang l;
    size_t len;
    printf("language file\n");
    memset(b, 0, sizeof b);
    set32(b, 0x00039000u);
    set32(b + 0x54, 2);
    set32(b + 0x58, 2);
    set32(b + 0x5C, sizeof text - 1);
    /* keys sorted as signed: 0x80000000 (negative) first */
    set32(b + 0x6C, 0x80000000u); set32(b + 0x70, 6);
    set32(b + 0x74, 0x00000010u); set32(b + 0x78, 0);
    set32(b + 0x7C, 10000); set32(b + 0x80, 0);
    set32(b + 0x84, 12000); set32(b + 0x88, 6);
    memcpy(b + 0x8C, text, sizeof text - 1);
    len = 0x8C + sizeof text - 1;
    CHECK(hp2_lang_open(&l, b, len) == 0 && l.count == 2, "opens");
    CHECK(!strcmp(hp2_lang_by_id(&l, 10000), "Audio") && !strcmp(hp2_lang_by_id(&l, 12000), "Help"), "by id");
    CHECK(hp2_lang_by_id(&l, 11000) == NULL, "missing id");
    CHECK(!strcmp(hp2_lang_by_key(&l, 0x80000000u), "Help") && !strcmp(hp2_lang_by_key(&l, 0x10), "Audio"), "by key, signed order");
    CHECK(hp2_lang_open(&l, b, len - 1) == -1, "text not ending in NUL");
    CHECK(hp2_lang_open(&l, b + 4, len - 4) == -1, "not a language file");
    CHECK(hp2_lang_open(&l, b, 0x70) == -1, "too short");
}

/* ---- textures ------------------------------------------------------------ */

/* A pack with an 8x8 4-bit linear texture and a 16x8 8-bit linear one. */
static size_t make_pack(uint8_t *b)
{
    uint8_t *p = b, *data;
    size_t i, dsz = 16 + 64 + 32 + 1024 + 128;
    set32(p, 0xB0300001u); set32(p + 4, 8 + 84 + 8 + 2 * 164 + 8 + (uint32_t)dsz); p += 8;
    set32(p, 0x30300002u); set32(p + 4, 84); memset(p + 8, 0, 84); p += 8 + 84;
    set32(p, 0x30300003u); set32(p + 4, 2 * 164); memset(p + 8, 0, 2 * 164);
    memcpy(p + 8 + 8, "FOUR", 4); set32(p + 8 + 32, 0xAAAA0001u); set16(p + 8 + 36, 8); set16(p + 8 + 38, 8);
    set32(p + 8 + 48, 64); set32(p + 8 + 52, 0); set32(p + 8 + 56, 32); set32(p + 8 + 60, 64);
    memcpy(p + 8 + 164 + 8, "EIGHT", 5); set32(p + 8 + 164 + 32, 0xAAAA0002u); set16(p + 8 + 164 + 36, 16); set16(p + 8 + 164 + 38, 8);
    set32(p + 8 + 164 + 48, 64 + 32 + 1024); set32(p + 8 + 164 + 52, 64 + 32); set32(p + 8 + 164 + 56, 128); set32(p + 8 + 164 + 60, 1024);
    p += 8 + 2 * 164;
    set32(p, 0x30300004u); set32(p + 4, (uint32_t)dsz); data = p + 8;
    memset(data, 0x11, 16);
    data += 16;
    for (i = 0; i < 16; i++) { data[4 * i] = (uint8_t)(i * 16); data[4 * i + 1] = 0; data[4 * i + 2] = 0; data[4 * i + 3] = 0x80; }
    for (i = 0; i < 32; i++) data[64 + i] = (uint8_t)(0x21 + i % 2 * 0x11);   /* nibbles 1,2 / 1,3 */
    for (i = 0; i < 256; i++) { data[96 + 4 * i] = (uint8_t)i; data[96 + 4 * i + 3] = 0x40; }
    for (i = 0; i < 128; i++) data[96 + 1024 + i] = (uint8_t)i;
    return (size_t)(data + 96 + 1024 + 128 - b);
}

static void test_tex(void)
{
    uint8_t b[4096], px[16 * 8 * 4];
    hp2_texset set = { 0 };
    const hp2_tex *t;
    size_t n = make_pack(b), m;
    printf("textures\n");
    CHECK(hp2_tex_scan(&set, b, n) == 2, "two textures");
    t = hp2_tex_find(&set, 0xAAAA0001u);
    CHECK(t && t->bpp == 4 && t->w == 8 && !strcmp(t->name, "FOUR"), "4-bit found");
    if (t) {
        CHECK(hp2_tex_decode(t, px) == 0, "decodes");
        CHECK(px[0] == 16 && px[4] == 32 && px[3] == 255, "low nibble is the left pixel; alpha 0x80 -> 255");
    }
    t = hp2_tex_find(&set, 0xAAAA0002u);
    CHECK(t && t->bpp == 8 && t->w == 16, "8-bit found");
    if (t) {
        hp2_tex_decode(t, px);
        CHECK(px[4 * 7] == 7 && px[4 * 8] == 16 && px[4 * 16] == 8 && px[4 * 24] == 24, "entries 8-15 and 16-23 swap");
        CHECK(px[3] == 128, "alpha 0x40 -> 128");
    }
    CHECK(hp2_tex_find(&set, 0x1234u) == NULL, "absent");
    CHECK(hp2_tex_scan(&set, b, n) == 0 && set.count == 2, "same pack again adds nothing");
    for (m = 0; m < n; m++) {
        hp2_texset s2 = { 0 };
        uint8_t *c = (uint8_t *)malloc(m ? m : 1);
        memcpy(c, b, m);
        hp2_tex_scan(&s2, c, m);
        hp2_tex_free(&s2);
        free(c);
    }
    CHECK(1, "every cut scanned (ASan watches)");
    hp2_tex_free(&set);
    CHECK(set.count == 0 && set.tex == NULL, "free");
}

static void test_zdir_hash(void)
{
    printf("archive names\n");
    CHECK(hp2_zdir_hash("FRONTEND\\FRONTB.BUN") == 0x14C57A19u, "FRONTB.BUN id (entry 21)");
    CHECK(hp2_zdir_hash("frontend/frontb.bun") == 0x14C57A19u, "any case, either slash");
    CHECK(hp2_zdir_hash("GLOBAL\\GLOBALB.BUN") == 0x4C937012u, "GLOBALB.BUN id (entry 302)");
    CHECK(hp2_zdir_hash("LANGUAGES\\FE_ENGLISH.BIN") == 0x0E1049EDu, "FE_ENGLISH.BIN id (entry 3)");
}

/* ---- real files ---------------------------------------------------------- */

static int find_text(const hp2_fe_item *it, int n, const char *s)
{
    int i, c = 0;
    for (i = 0; i < n; i++)
        if (it[i].w.kind == HP2_FE_TEXT && it[i].str && !strcmp(it[i].str, s)) c++;
    return c;
}

static void test_menu(const hp2_fe_layout *L, const hp2_lang *lang)
{
    static hp2_fe_item it[160];
    hp2_settings cfg;
    hp2_femenu m;
    int n, i, r;
    hp2_scheme before;
    printf("options menus\n");
    hp2_settings_defaults(&cfg);
    CHECK(hp2_femenu_init(&m, L, lang, &cfg) == 0, "init");
    CHECK(hp2_femenu_options_rows(&m) == 6, "OPTIONS shows 6 rows (Languages hidden, as on the US disc)");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(n > 20 && n < 160, "%d items", n);
    CHECK(find_text(it, n, "Audio") == 1 && find_text(it, n, "Credits") == 1, "row text from FE_ENGLISH");
    CHECK(find_text(it, n, "Languages") == 0, "no Languages row");
    CHECK(!strcmp(m.help, "Set the volume and the Jukebox."), "Audio's help line, got '%s'", m.help);
    for (i = 0; i < n; i++)
        if (it[i].w.kind == HP2_FE_TEXT && it[i].str && !strcmp(it[i].str, "Audio"))
            CHECK(it[i].w.rgba[0] == 124 && it[i].w.rgba[1] == 64, "selected row is orange");
    for (i = 0, r = 0; i < n; i++)
        if (!strcmp(it[i].w.name, "MI_OVERSCORE") || !strcmp(it[i].w.name, "MI_UNDERSCORE")) r++;
    CHECK(r == 2, "the two selection lines");
    CHECK(hp2_femenu_input(&m, HP2_IN_UP) == HP2_FEM_REDRAW && m.sel == 6, "up from the top wraps to Credits");
    CHECK(!strcmp(m.help, "See who made this game."), "Credits' help line, got '%s'", m.help);
    hp2_femenu_input(&m, HP2_IN_DOWN);
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == 2, "Controller");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(m.screen == HP2_FEM_SETTINGS && m.sel == HP2_ROW_SCHEME, "Controller opens the list at the controls");
    CHECK(m.scroll <= m.sel && m.sel < m.scroll + HP2_FEM_VISIBLE, "selection visible");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Controls") == 1, "title = section");
    CHECK(find_text(it, n, hp2_menu_label(HP2_ROW_SCHEME)) == 1, "row label from hp2_menu");
    CHECK(!strcmp(m.help, hp2_menu_help(HP2_ROW_SCHEME)), "help line from hp2_menu");
    before = cfg.scheme;
    r = hp2_femenu_input(&m, HP2_IN_RIGHT);
    CHECK((r & HP2_FEM_CHANGED) && cfg.scheme != before, "right changes the setting");
    for (i = 0; i < 3 * HP2_ROW_COUNT; i++) {
        int k, rows = 0;
        hp2_femenu_input(&m, i < HP2_ROW_COUNT + 2 ? HP2_IN_DOWN : HP2_IN_UP);
        CHECK(m.scroll <= m.sel && m.sel < m.scroll + HP2_FEM_VISIBLE && m.scroll >= 0
              && m.scroll <= HP2_ROW_COUNT - HP2_FEM_VISIBLE, "step %d: row %d visible (scroll %d)", i, m.sel, m.scroll);
        n = hp2_femenu_items(&m, it, 160);
        for (k = 0; k < HP2_ROW_COUNT; k++) rows += find_text(it, n, hp2_menu_label(k));
        CHECK(rows == HP2_FEM_VISIBLE, "step %d: %d rows drawn", i, rows);
    }
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_OPTIONS && m.sel == 2, "back returns to Controller");
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) & HP2_FEM_QUIT, "back from OPTIONS quits");
    CHECK(hp2_femenu_items(&m, it, 3) == 3, "never more than cap");
}

static void test_real(const char *fb, const char *gb, const char *lf, const char *disc)
{
    size_t lfb = 0, lgb = 0, llf = 0;
    uint8_t *F = hp2_read_file(fb, &lfb), *G = hp2_read_file(gb, &lgb), *Lb = lf ? hp2_read_file(lf, &llf) : NULL;
    hp2_fontset fonts = { 0 };
    hp2_texset tex = { 0 };
    hp2_fe_layout L;
    hp2_lang lang;
    const hp2_font *f;
    const hp2_tex *t;
    int i, bad = 0;
    printf("real files\n");
    CHECK(F && G, "read %s and %s", fb, gb);
    if (!F || !G) return;
    CHECK(hp2_font_scan(&fonts, F, lfb) == 4 && hp2_font_scan(&fonts, G, lgb) == 4, "4 + 4 fonts");
    f = hp2_font_find(&fonts, hp2_fe_hash("FONT_IMPACTNORMAL"));
    CHECK(f && f->count == 130 && f->kern_count == 1300 && NEAR(f->line_height, 43.0f), "FONT_IMPACTNORMAL");
    if (f) {
        CHECK(NEAR(hp2_font_kern(f, 'L', 'T'), -3.0f) && NEAR(hp2_font_kern(f, 'A', 'V'), -2.0f), "LT -3, AV -2");
        CHECK(NEAR(hp2_font_kern(f, 'L', 'L'), 0.0f), "LL 0");
    }
    f = hp2_font_find(&fonts, hp2_fe_hash("FONT_EVENTNUMBERS"));
    CHECK(f && f->count == 10 && NEAR(hp2_font_kern(f, '1', '1'), 0.0f), "FONT_EVENTNUMBERS: digits; its 0xCD filler pairs are never used");
    f = hp2_font_find(&fonts, hp2_fe_hash("FONT_STATS"));
    CHECK(f && f->count == 130, "FONT_STATS from GLOBALB.BUN");

    CHECK(hp2_tex_scan(&tex, F, lfb) == 365 && hp2_tex_scan(&tex, G, lgb) == 116, "365 + 116 textures");
    for (i = 0; i < fonts.count; i++) {
        t = hp2_tex_find(&tex, fonts.font[i].hash);
        CHECK(t && t->bpp, "font %08x has its texture", (unsigned)fonts.font[i].hash);
    }
    for (i = 0; i < tex.count; i++) bad += tex.tex[i].bpp == 0;
    CHECK(bad == 4, "4 textures of a layout not handled (as hp2_tpk.py), got %d", bad);
    t = hp2_tex_find(&tex, hp2_fe_hash("WHITE16X16"));
    CHECK(t && t->w == 16 && t->h == 16, "WHITE16X16");

    if (Lb) {
        CHECK(hp2_lang_open(&lang, Lb, llf) == 0 && lang.count == 2437, "2,437 strings");
        CHECK(hp2_lang_by_id(&lang, 100001) && !strcmp(hp2_lang_by_id(&lang, 100001), "Audio"), "100001 Audio");
        CHECK(hp2_lang_by_id(&lang, 12000) && !strcmp(hp2_lang_by_id(&lang, 12000), "Help"), "12000 Help");
        CHECK(hp2_lang_by_key(&lang, hp2_fe_hash("Help")) && !strcmp(hp2_lang_by_key(&lang, hp2_fe_hash("Help")), "Help"), "key = hash of the wording");
        CHECK(hp2_lang_by_id(&lang, 1) == NULL, "no id 1");
    }
    CHECK(hp2_fe_parse(F, lfb, &L) == 0, "layouts");
    if (Lb && lang.count) test_menu(&L, &lang);
    hp2_fe_free(&L);

    if (disc) {
        hp2_zdir z;
        size_t n = 0;
        uint8_t *Z;
        CHECK(hp2_zdir_open(&z, disc) == 0 && z.count == 405, "ZDIR.BIN: 405 entries, got %d", z.count);
        Z = hp2_zdir_load(&z, "FRONTEND\\FRONTB.BUN", &n);
        CHECK(Z && n == lfb && !memcmp(Z, F, n), "FRONTB.BUN through the archive = the extracted file");
        CHECK(hp2_zdir_load(&z, "NO\\SUCH.FILE", &n) == NULL, "absent path");
        free(Z);
        hp2_zdir_close(&z);
    }
    hp2_font_free(&fonts);
    hp2_tex_free(&tex);
    free(F); free(G); free(Lb);
}

/* FNV-1a 64 of each decoded texture, as tests/check_tex.py computes it. */
static int texdump(int argc, char **argv)
{
    hp2_texset s = { 0 };
    uint8_t **keep = (uint8_t **)calloc((size_t)argc, sizeof *keep);
    int i;
    for (i = 0; i < argc; i++) {
        size_t n;
        keep[i] = hp2_read_file(argv[i], &n);
        if (!keep[i] || hp2_tex_scan(&s, keep[i], n) < 0) { fprintf(stderr, "cannot read %s\n", argv[i]); return 1; }
    }
    for (i = 0; i < s.count; i++) {
        const hp2_tex *t = &s.tex[i];
        size_t k, n = (size_t)t->w * t->h * 4;
        uint8_t *px = (uint8_t *)malloc(n ? n : 1);
        uint64_t h = 1469598103934665603ULL;
        if (t->bpp && hp2_tex_decode(t, px) == 0) {
            for (k = 0; k < n; k++) { h ^= px[k]; h *= 1099511628211ULL; }
            printf("%08x %d %d %016llx %s\n", (unsigned)t->hash, t->w, t->h, (unsigned long long)h, t->name);
        } else {
            printf("%08x %d %d none %s\n", (unsigned)t->hash, t->w, t->h, t->name);
        }
        free(px);
    }
    for (i = 0; i < argc; i++) free(keep[i]);
    free(keep);
    hp2_tex_free(&s);
    return 0;
}

int main(int argc, char **argv)
{
    const char *fb = NULL, *gb = NULL, *lf = NULL, *disc = NULL;
    int i;
    if (argc > 1 && !strcmp(argv[1], "--texdump")) return texdump(argc - 2, argv + 2);
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frontb")) fb = argv[i + 1];
        else if (!strcmp(argv[i], "--globalb")) gb = argv[i + 1];
        else if (!strcmp(argv[i], "--lang")) lf = argv[i + 1];
        else if (!strcmp(argv[i], "--disc")) disc = argv[i + 1];
    }
    test_tokens();
    test_font();
    test_lang();
    test_tex();
    test_zdir_hash();
    if (fb && gb) test_real(fb, gb, lf, disc);
    printf("\nassets: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
