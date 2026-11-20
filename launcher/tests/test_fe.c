/* Tests for hp2_fe: the FRONTB.BUN screen layouts and the viewport.
 *
 *   test_fe                    synthetic data only
 *   test_fe FRONTB.BUN         also checks the real file (41 screens, the
 *                              options rows)
 *   test_fe --dump FRONTB.BUN  prints every screen and widget, one line
 *                              each, for tests/check_fe.py to compare with
 *                              tools/hp2_fescreens.py
 */
#include "../src/hp2_fe.h"

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

/* ---- a little chunk writer -------------------------------------------- */

typedef struct { uint8_t *b; size_t n, cap; } buf;

static void put(buf *o, const void *p, size_t n)
{
    if (o->n + n > o->cap) {
        o->cap = (o->n + n) * 2 + 256;
        o->b = (uint8_t *)realloc(o->b, o->cap);
    }
    memcpy(o->b + o->n, p, n);
    o->n += n;
}
static void put32(buf *o, uint32_t v)
{
    uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    put(o, t, 4);
}
static void putf(uint8_t *p, float f) { memcpy(p, &f, 4); } /* little-endian host */
static void set32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void set16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void chunk(buf *o, uint32_t id, const buf *body)
{
    put32(o, id);
    put32(o, (uint32_t)body->n);
    put(o, body->b, body->n);
}

static void header(buf *scr, const char *name, unsigned count_hint)
{
    uint8_t h[0x28] = { 0 };
    buf t = { 0 };
    set32(h + 8, hp2_fe_hash(name));
    set16(h + 0x18, 640);
    set16(h + 0x1A, 480);
    set16(h + 0x1C, count_hint);
    put(&t, h, sizeof h);
    chunk(scr, 0x00010203, &t);
    free(t.b);
}

static void text_widget(buf *scr, const char *name, float x, float y, uint32_t sid, const char *txt)
{
    uint8_t w[0x64 + 32] = { 0 };
    buf t = { 0 };
    size_t n = strlen(txt);
    set32(w + 8, hp2_fe_hash(name));
    memcpy(w + 16, name, strlen(name));
    putf(w + 0x30, x);
    putf(w + 0x34, y);
    putf(w + 0x38, 1.0f);
    putf(w + 0x3C, 0.5f);
    set32(w + 0x50, 0x5F3A4A3Cu);
    set32(w + 0x54, 32);
    w[0x5C] = 0xFF; w[0x5D] = 0x80; w[0x5E] = 0x10; w[0x5F] = 0x7F;
    set32(w + 0x60, sid);
    memcpy(w + 0x64, txt, n);
    put(&t, w, 0x64 + n + 1);
    chunk(scr, 0x00010212, &t);
    free(t.b);
}

static void image_widget(buf *scr, const char *name, float x, float y, uint32_t tex)
{
    uint8_t w[0x80] = { 0 };
    buf t = { 0 };
    set32(w + 8, hp2_fe_hash(name));
    memcpy(w + 16, name, strlen(name));
    putf(w + 0x30, x);
    putf(w + 0x34, y);
    putf(w + 0x38, 10.0f);
    putf(w + 0x3C, 6.0f);
    set32(w + 0x50, tex);
    put(&t, w, sizeof w);
    chunk(scr, 0x00010210, &t);
    free(t.b);
}

/* A small bundle: a padding chunk, then 80010200 with two screens. OPTIONS
 * has 3 rows and 20 widgets in all (more than the first allocation of 16)
 * while its header claims 5; DISPLAY_OPTIONS has none. */
static buf make_bundle(void)
{
    buf out = { 0 }, list = { 0 }, s1 = { 0 }, s2 = { 0 }, pad = { 0 }, odd = { 0 };
    int i;
    put32(&pad, 0);
    chunk(&out, 0x00000000, &pad);

    header(&s1, "OPTIONS", 5);
    text_widget(&s1, "MENUITEM_2", 100, 108, 100002, "Video");
    text_widget(&s1, "MENUITEM_1", 100, 71, 100001, "Audio");
    put32(&odd, 7);
    chunk(&s1, 0x00010213, &odd);               /* unknown: skipped */
    text_widget(&s1, "MENUITEM_3", 100, 145, 0, "Controls");
    for (i = 0; i < 17; i++) {
        char nm[24];
        sprintf(nm, "MI_%d_MASK", i);
        image_widget(&s1, nm, 50.0f + i, 60, 0x1000u + (uint32_t)i);
    }
    header(&s2, "DISPLAY_OPTIONS", 0);

    chunk(&list, 0x80010201, &s1);
    chunk(&list, 0x00000003, &odd);             /* odd chunk between screens */
    chunk(&list, 0x80010201, &s2);
    chunk(&out, 0x80010200, &list);
    free(list.b); free(s1.b); free(s2.b); free(pad.b); free(odd.b);
    return out;
}

/* ---- tests --------------------------------------------------------------- */

static void test_hash(void)
{
    printf("hash\n");
    CHECK(hp2_fe_hash("") == 0xFFFFFFFFu, "empty");
    CHECK(hp2_fe_hash("A") == 0x20u, "A: -1*33 + 65");
    CHECK(hp2_fe_hash("FONT_IMPACTBIG") == 0x2D213565u, "FONT_IMPACTBIG");
    CHECK(hp2_fe_hash("FONT_IMPACTNORMAL") == 0x5F3A4A3Cu, "FONT_IMPACTNORMAL");
}

static void test_parse(void)
{
    buf b = make_bundle();
    hp2_fe_layout l;
    const hp2_fe_screen *s;
    const hp2_fe_widget *w;
    printf("parse\n");
    CHECK(hp2_fe_parse(b.b, b.n, &l) == 0, "parses");
    CHECK(l.count == 2, "2 screens, got %d", l.count);
    s = hp2_fe_find(&l, "OPTIONS");
    CHECK(s != NULL, "finds OPTIONS");
    CHECK(s == hp2_fe_find(&l, "options"), "any case");
    CHECK(s == hp2_fe_find_id(&l, hp2_fe_hash("OPTIONS")), "by id");
    CHECK(hp2_fe_find(&l, "NOPE") == NULL, "missing screen");
    CHECK(hp2_fe_find(NULL, "OPTIONS") == NULL, "NULL layout");
    if (s) {
        CHECK(s->count == 20, "count is the widgets found (20), not the header's 5: %d", s->count);
        CHECK(s->width == 640 && s->height == 480, "canvas");
        CHECK(hp2_fe_row_count(s) == 3, "3 rows, got %d", hp2_fe_row_count(s));
        w = hp2_fe_row(s, 1);
        CHECK(w && NEAR(w->y, 71) && w->string_id == 100001 && !strcmp(w->text, "Audio"), "row 1");
        w = hp2_fe_row(s, 2);
        CHECK(w && NEAR(w->y, 108) && !strcmp(w->text, "Video"), "row 2 (stored first)");
        w = hp2_fe_row(s, 3);
        CHECK(w && w->string_id == 0 && !strcmp(w->text, "Controls"), "row 3");
        CHECK(hp2_fe_row(s, 4) == NULL && hp2_fe_row(s, 0) == NULL, "no row 0 or 4");
        if (w) {
            CHECK(w->kind == HP2_FE_TEXT && w->font == 0x5F3A4A3Cu && w->capacity == 32, "text fields");
            CHECK(w->rgba[0] == 0xFF && w->rgba[1] == 0x80 && w->rgba[2] == 0x10 && w->rgba[3] == 0x7F, "colour");
            CHECK(NEAR(w->sx, 1.0f) && NEAR(w->sy, 0.5f), "scale");
            CHECK(w->name_hash == hp2_fe_hash("MENUITEM_3"), "name hash");
        }
        w = &s->widgets[19];
        CHECK(w->kind == HP2_FE_IMAGE && w->texture == 0x1010u && !strcmp(w->name, "MI_16_MASK"), "last image");
        CHECK(NEAR(w->x, 66) && NEAR(w->sx, 10) && NEAR(w->sy, 6), "image place");
    }
    s = hp2_fe_find(&l, "DISPLAY_OPTIONS");
    CHECK(s && s->count == 0 && hp2_fe_row_count(s) == 0, "empty screen");
    hp2_fe_free(&l);
    CHECK(l.count == 0 && l.screens == NULL, "free clears");
    hp2_fe_free(&l);                                /* twice is harmless */
    free(b.b);
}

static void test_nested(void)
{
    buf inner = make_bundle(), out = { 0 };
    hp2_fe_layout l;
    printf("nested\n");
    chunk(&out, 0x80030000, &inner);
    CHECK(hp2_fe_parse(out.b, out.n, &l) == 0 && l.count == 2, "found inside another container");
    hp2_fe_free(&l);
    free(inner.b); free(out.b);
}

/* Every prefix and every single damaged byte of the bundle: the parser must
 * either succeed or fail cleanly. Run under -fsanitize=address (make
 * test_fe_asan) for reads past the end; each prefix sits in its own exact
 * allocation so the sanitizer sees the end. */
static void test_damage(void)
{
    buf b = make_bundle();
    hp2_fe_layout l;
    size_t n;
    int ok = 0;
    printf("damage\n");
    for (n = 0; n < b.n; n++) {
        uint8_t *c = (uint8_t *)malloc(n ? n : 1);
        memcpy(c, b.b, n);
        if (hp2_fe_parse(c, n, &l) == 0) ok++;
        else CHECK(l.count == 0 && l.screens == NULL, "failure leaves nothing at %zu", n);
        hp2_fe_free(&l);
        free(c);
    }
    CHECK(ok == 0, "no prefix parses (the outer chunk is cut): %d did", ok);
    for (n = 0; n < b.n; n++) {
        uint8_t *c = (uint8_t *)malloc(b.n);
        static const uint8_t junk[] = { 0x00, 0xFF, 0x7F, 0x80 };
        size_t j;
        for (j = 0; j < sizeof junk; j++) {
            memcpy(c, b.b, b.n);
            c[n] = junk[j];
            if (hp2_fe_parse(c, b.n, &l) != 0)
                CHECK(l.count == 0 && l.screens == NULL, "bad byte %zu leaves nothing", n);
            hp2_fe_free(&l);
        }
        free(c);
    }
    CHECK(hp2_fe_parse(NULL, 0, &l) == -1, "NULL buffer");
    free(b.b);
}

static void test_bad_screens(void)
{
    buf out = { 0 }, list = { 0 }, s = { 0 }, w = { 0 };
    uint8_t shortw[0x40] = { 0 };
    hp2_fe_layout l;
    printf("bad screens\n");
    /* a screen with widgets but no header */
    text_widget(&s, "MENUITEM_1", 0, 0, 0, "x");
    chunk(&list, 0x80010201, &s);
    chunk(&out, 0x80010200, &list);
    CHECK(hp2_fe_parse(out.b, out.n, &l) == -1 && l.screens == NULL, "no header");
    out.n = list.n = s.n = 0;
    /* a widget body too short to hold its fields */
    header(&s, "OPTIONS", 1);
    put(&w, shortw, sizeof shortw);
    chunk(&s, 0x00010212, &w);
    chunk(&list, 0x80010201, &s);
    chunk(&out, 0x80010200, &list);
    CHECK(hp2_fe_parse(out.b, out.n, &l) == -1 && l.screens == NULL, "short widget");
    /* no screen list at all */
    CHECK(hp2_fe_parse(w.b, w.n, &l) == -1, "not a bundle");
    free(out.b); free(list.b); free(s.b); free(w.b);
}

static void test_view(void)
{
    hp2_fe_view v;
    float x, y;
    printf("viewport\n");

    hp2_fe_view_make(&v, 640, 480, 640, 480, HP2_FE_FIT);
    hp2_fe_to_window(&v, 123, 45, &x, &y);
    CHECK(NEAR(x, 123) && NEAR(y, 45) && NEAR(v.margin_l, 0), "640x480 is 1:1");

    hp2_fe_view_make(&v, 1920, 1080, 640, 480, HP2_FE_FIT);
    CHECK(NEAR(v.sx, 2.25f) && NEAR(v.sy, 2.25f), "FIT 16:9 scale %g", v.sx);
    CHECK(NEAR(v.ox, 240) && NEAR(v.oy, 0), "FIT 16:9 bars 240 px");
    CHECK(NEAR(v.margin_l, 106.667f) && NEAR(v.margin_r, 106.667f), "FIT margins in canvas units");
    hp2_fe_to_window(&v, 640, 480, &x, &y);
    CHECK(NEAR(x, 1680) && NEAR(y, 1080), "FIT far corner");

    hp2_fe_view_make(&v, 1920, 1080, 640, 480, HP2_FE_STRETCH);
    CHECK(NEAR(v.sx, 3) && NEAR(v.sy, 2.25f) && NEAR(v.ox, 0) && NEAR(v.margin_l, 0), "STRETCH");
    hp2_fe_to_window(&v, 640, 480, &x, &y);
    CHECK(NEAR(x, 1920) && NEAR(y, 1080), "STRETCH far corner");

    hp2_fe_view_make(&v, 2560, 1080, 640, 480, HP2_FE_EXPAND);
    CHECK(NEAR(v.sx, 2.25f) && NEAR(v.oy, 0) && NEAR(v.ox, 560), "EXPAND 21:9 keeps height");
    CHECK(NEAR(v.margin_l, 248.889f), "EXPAND room for a background");

    hp2_fe_view_make(&v, 800, 1000, 640, 480, HP2_FE_EXPAND);
    CHECK(NEAR(v.sx, 1.25f) && NEAR(v.ox, 0) && NEAR(v.oy, 200), "EXPAND on a tall window = FIT");
    hp2_fe_view_make(&v, 800, 1000, 640, 480, HP2_FE_FIT);
    CHECK(NEAR(v.sx, 1.25f) && NEAR(v.oy, 200) && NEAR(v.margin_l, 0), "FIT tall: bars above and below");

    hp2_fe_view_make(&v, 0, 1080, 640, 480, HP2_FE_FIT);
    CHECK(NEAR(v.sx, 1) && NEAR(v.ox, 0), "zero window: identity");
    hp2_fe_to_window(&v, 5, 6, NULL, &y);
    CHECK(NEAR(y, 6), "NULL out pointer");
}

/* ---- the real file ------------------------------------------------------- */

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void rows_of(const hp2_fe_layout *l, const char *name, int want)
{
    const hp2_fe_screen *s = hp2_fe_find(l, name);
    int n, i;
    CHECK(s != NULL, "%s present", name);
    if (!s) return;
    n = hp2_fe_row_count(s);
    CHECK(n == want, "%s has %d rows, got %d", name, want, n);
    /* Rows are about 37 px apart from y = 71 but placed by hand (109, 181,
     * 220, 294...: up to 2 px off), and the file stores them out of order. */
    for (i = 1; i <= n; i++) {
        const hp2_fe_widget *w = hp2_fe_row(s, i);
        CHECK(fabsf(w->y - (71.0f + 37.0f * (float)(i - 1))) <= 3.0f, "%s row %d at y %g", name, i, w->y);
        if (i > 1) CHECK(w->y > hp2_fe_row(s, i - 1)->y, "%s row %d below row %d", name, i, i - 1);
        CHECK(w->string_id != 0, "%s row %d has a string id", name, i);
    }
}

static void test_real(const char *path)
{
    size_t len = 0;
    uint8_t *b = load(path, &len);
    hp2_fe_layout l;
    int i;
    printf("real file %s\n", path);
    CHECK(b != NULL, "read %s", path);
    if (!b) return;
    CHECK(hp2_fe_parse(b, len, &l) == 0, "parses");
    CHECK(l.count == 41, "41 screens, got %d", l.count);
    for (i = 0; i < l.count; i++)
        CHECK(l.screens[i].width == 640 && l.screens[i].height == 480, "screen %d canvas", i);
    rows_of(&l, "OPTIONS", 7);
    rows_of(&l, "DISPLAY_OPTIONS", 9);
    rows_of(&l, "AUDIO_OPTIONS", 11);
    hp2_fe_free(&l);
    free(b);
}

static int dump(const char *path)
{
    size_t len = 0;
    uint8_t *b = load(path, &len);
    hp2_fe_layout l;
    int i, j;
    if (!b || hp2_fe_parse(b, len, &l) != 0) {
        fprintf(stderr, "cannot parse %s\n", path);
        free(b);
        return 1;
    }
    for (i = 0; i < l.count; i++) {
        const hp2_fe_screen *s = &l.screens[i];
        printf("S %08x %dx%d %d\n", (unsigned)s->id, s->width, s->height, s->count);
        for (j = 0; j < s->count; j++) {
            const hp2_fe_widget *w = &s->widgets[j];
            printf("W %s %s %08x %.9g %.9g %.9g %.9g", w->kind == HP2_FE_TEXT ? "text" : "image",
                   w->name, (unsigned)w->name_hash, w->x, w->y, w->sx, w->sy);
            if (w->kind == HP2_FE_TEXT)
                printf(" %08x %u %02x%02x%02x%02x %u |%s|\n", (unsigned)w->font, (unsigned)w->capacity,
                       w->rgba[0], w->rgba[1], w->rgba[2], w->rgba[3], (unsigned)w->string_id, w->text);
            else
                printf(" %08x\n", (unsigned)w->texture);
        }
    }
    hp2_fe_free(&l);
    free(b);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--dump")) return dump(argv[2]);
    test_hash();
    test_parse();
    test_nested();
    test_damage();
    test_bad_screens();
    test_view();
    if (argc > 1) test_real(argv[1]);
    printf("\nhp2_fe: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
