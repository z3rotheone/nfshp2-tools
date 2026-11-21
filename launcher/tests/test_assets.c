/* Tests for the asset modules behind the front-end renderer: hp2_tex
 * (texture packs), hp2_font (fonts, string codes), hp2_lang (menu text),
 * hp2_zdir (the disc archive) and hp2_femenu (the Options menus). No SDL.
 *
 *   test_assets                          synthetic data only
 *   test_assets --frontb F --globalb G --lang L [--jukebox BANK] [--disc DIR] [--exe SLUS_203.62]
 *                                        also the real files (entries 21,
 *                                        302 and 3; DIR = the disc's NFSHP2
 *                                        folder, to read FRONTB.BUN through
 *                                        ZDIR.BIN and compare; the executable
 *                                        for the event trees' layout)
 *   test_assets --texdump FILE...        one line per texture with a hash of
 *                                        its decoded RGBA, for check_tex.py
 *   test_assets --treedump FRONTB        every event of both career trees,
 *                                        for check_tree.py
 *   test_assets --vagdump BANK SONG FRAMES
 *                                        a hash of the first FRAMES stereo
 *                                        frames of a song, for check_vag.py
 */
#include "../src/hp2_fe.h"
#include "../src/hp2_jukebox.h"
#include "../src/hp2_jbplay.h"
#include "../src/hp2_vag.h"
#include "../src/hp2_femenu.h"
#include "../src/hp2_font.h"
#include "../src/hp2_lang.h"
#include "../src/hp2_tex.h"
#include "../src/hp2_tree.h"
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

/* ---- jukebox ------------------------------------------------------------ */

static size_t make_bank(uint8_t *b, const char **lines, int n)
{
    int i;
    size_t len = 0x60 + 0x60 * (size_t)n;
    memset(b, 0xCC, len);
    memset(b, 0, 0x24);
    set32(b, 0x10); set32(b + 4, 2); set32(b + 8, 32000); set32(b + 0x20, (uint32_t)n);
    memcpy(b + 0x24, "InGame", 7);
    for (i = 0; i < n; i++) {
        uint8_t *r = b + 0x60 + 0x60 * i;
        memset(r, 0, 12);
        set32(r, 0x17D0u + 0x1000u * (uint32_t)i);
        memcpy(r + 12, lines[i], strlen(lines[i]) + 1);
    }
    return len;
}

static void test_jukebox(void)
{
    static const char *good[] = { "Bush-The People That We Love(Inst)-Golden State-Rock-2 ",
                                  "Hot Action Cop-Fever for the Flava (Rmx)--Rock-4",
                                  "Rom di Prisco-Cylkoid--Techno-0",
                                  "A-Title-With-Dashes-Album-Rock-4" };
    static const char *bad[] = { "No fields at all" };
    uint8_t b[0x60 * 8];
    hp2_jukebox jb;
    int idx[8], n;
    size_t len, cut;
    printf("jukebox table\n");
    len = make_bank(b, good, 4);
    CHECK(hp2_jukebox_parse(&jb, b, len) == 0 && jb.count == 4 && !strcmp(jb.name, "InGame"), "parses");
    CHECK(!strcmp(jb.track[0].artist, "Bush") && !strcmp(jb.track[0].title, "The People That We Love(Inst)")
          && !strcmp(jb.track[0].album, "Golden State") && !strcmp(jb.track[0].genre, "Rock") && jb.track[0].bank_status == 2,
          "fields, trailing space dropped");
    CHECK(jb.track[1].album[0] == 0 && jb.track[1].bank_status == 4, "empty album");
    CHECK(!strcmp(jb.track[3].title, "Title-With-Dashes") && !strcmp(jb.track[3].album, "Album"), "a title may hold dashes");
    CHECK(jb.genres == 2 && !strcmp(jb.genre[0], "Rock") && !strcmp(jb.genre[1], "Techno"), "genres in order of first use");
    n = hp2_jukebox_songs(&jb, 0, idx, 8);
    CHECK(n == 3 && idx[0] == 0 && idx[1] == 1 && idx[2] == 3, "Rock: songs 0, 1, 3");
    CHECK(hp2_jukebox_songs(&jb, 1, idx, 8) == 1 && idx[0] == 2, "Techno: song 2");
    CHECK(hp2_jukebox_songs(&jb, 5, idx, 8) == 0, "no such genre");
    for (cut = 0; cut < len; cut++) {
        uint8_t *c = (uint8_t *)malloc(cut ? cut : 1);
        memcpy(c, b, cut);
        CHECK(hp2_jukebox_parse(&jb, c, cut) == -1, "cut at %zu fails cleanly", cut);
        free(c);
    }
    len = make_bank(b, bad, 1);
    CHECK(hp2_jukebox_parse(&jb, b, len) == -1 && jb.count == 0, "a record that is not Artist-Title-Album-Genre-N");
    set32(b + 0x20, 1000);
    CHECK(hp2_jukebox_parse(&jb, b, len) == -1, "absurd count");
    CHECK(hp2_jukebox_parse(&jb, NULL, 0) == -1, "NULL");
}

/* ---- audio --------------------------------------------------------------- */

static void test_adpcm(void)
{
    uint8_t b[16];
    int16_t o[28];
    hp2_adpcm st = { 0, 0 };
    printf("PS-ADPCM\n");
    memset(b, 0, 16);
    b[0] = 0x0C;                           /* predictor 0, shift 12 */
    b[2] = 0x71;                           /* low nibble first: 1, then 7 */
    b[3] = 0x8F;                           /* -1, then -8 */
    CHECK(hp2_adpcm_block(&st, b, o) == 28, "28 samples");
    CHECK(o[0] == 1 && o[1] == 7 && o[2] == -1 && o[3] == -8 && o[4] == 0, "shift 12: the nibble itself (%d %d %d %d)", o[0], o[1], o[2], o[3]);
    b[0] = 0x00; b[2] = 0x07; b[3] = 0x08;
    hp2_adpcm_block(&st, b, o);
    CHECK(o[0] == 28672 && o[1] == 0 && o[2] == -32768, "shift 0: nibble << 12 (%d %d)", o[0], o[2]);
    st.h1 = 1000; st.h2 = 0;
    b[0] = 0x1C; b[2] = 0; b[3] = 0;       /* predictor 1: + 60/64 of the last sample */
    hp2_adpcm_block(&st, b, o);
    CHECK(o[0] == (60 * 1000 + 32) / 64 && o[1] == (60 * o[0] + 32) / 64, "predictor 1 (%d %d)", o[0], o[1]);
    st.h1 = -1001; st.h2 = 0;              /* (60 * -1001 + 32) / 64 = -937.94 */
    hp2_adpcm_block(&st, b, o);
    CHECK(o[0] == -938, "negative history rounds down like Python's >> (%d)", o[0]);
    st.h1 = 1000; st.h2 = 500;
    b[0] = 0x9F;                           /* predictor 9 -> 0, shift 15 -> 9 */
    b[2] = 0x01;
    hp2_adpcm_block(&st, b, o);
    CHECK(o[0] == 8, "hardware clamps: predictor > 4 is 0, shift > 12 is 9 (%d)", o[0]);
    st.h1 = 32000; st.h2 = 0;
    b[0] = 0x40; b[2] = 0x07;
    hp2_adpcm_block(&st, b, o);
    CHECK(o[0] == 32767, "clamped to int16");
    b[1] = 7;
    CHECK(hp2_adpcm_block(&st, b, o) == 0, "flag 7 ends the sound");
}

/* A bank file: song table, then each song's VAG header and stereo ADPCM in
 * 32 KB chunks. Song k's left blocks decode to the constant k+1 and right
 * blocks to -(k+1) (predictor 0, shift 12). */
static const char *write_bank(const char *path, const char **lines, int n, uint32_t adpcm_bytes)
{
    FILE *f = fopen(path, "wb");
    uint8_t head[0x60 * 8];
    uint32_t off = 0x60 + 0x60 * (uint32_t)n, k;
    int i;
    make_bank(head, lines, n);
    for (i = 0; i < n; i++) {
        set32(head + 0x60 + 0x60 * i, off + (uint32_t)i * (48 + adpcm_bytes));
        set32(head + 0x60 + 0x60 * i + 4, 48 + adpcm_bytes);
    }
    fwrite(head, 1, off, f);
    for (i = 0; i < n; i++) {
        uint8_t h[48] = { 'V', 'A', 'G', 'p' };
        h[12] = (uint8_t)(adpcm_bytes >> 24); h[13] = (uint8_t)(adpcm_bytes >> 16);
        h[14] = (uint8_t)(adpcm_bytes >> 8); h[15] = (uint8_t)adpcm_bytes;
        h[17] = 0; h[18] = 0x7D; h[19] = 0x00;              /* 32000 */
        fwrite(h, 1, 48, f);
        for (k = 0; k < adpcm_bytes; k += 16) {
            uint8_t blk[16];
            int left = (k % (2 * HP2_MUSIC_CHUNK)) < HP2_MUSIC_CHUNK;
            int v = left ? i + 1 : 16 - (i + 1);            /* 4-bit two's complement */
            memset(blk, 0, 16);
            blk[0] = 0x0C;
            memset(blk + 2, v | v << 4, 14);
            fwrite(blk, 1, 16, f);
        }
    }
    fclose(f);
    return path;
}

static void test_stream(void)
{
    static const char *lines[] = { "A-One--Rock-0", "B-Two--Rock-3", "C-Three--Rock-0" };
    const char *path = write_bank("test_vag_tmp.bin", lines, 3, 3 * HP2_MUSIC_CHUNK);  /* 1.5 chunk pairs */
    static hp2_jukebox jb;
    hp2_stream st;
    hp2_settings cfg;
    hp2_jbplay p;
    static int16_t buf[2 * 8192];
    size_t len;
    uint8_t *head = hp2_read_file_part(path, hp2_jukebox_header_size(), &len);
    int n, total, i, ok;
    printf("music stream\n");
    CHECK(head && hp2_jukebox_parse(&jb, head, len) == 0 && jb.count == 3 && jb.rate == 32000, "bank table");
    CHECK(jb.track[1].offset == 0x60 + 0x60 * 3 + 48 + 3 * HP2_MUSIC_CHUNK, "track offsets");
    CHECK(hp2_stream_open(&st, path, jb.track[1].offset) == 0 && st.rate == 32000 && st.size == 3 * HP2_MUSIC_CHUNK, "opens song 2");
    for (total = 0, ok = 1; (n = hp2_stream_read(&st, buf, 1000)) > 0; total += n)
        for (i = 0; i < n; i++) ok &= buf[2 * i] == 2 && buf[2 * i + 1] == -2;
    /* pair 1: 2048 blocks a side; pair 2 is 32 KB: all left, no right */
    CHECK(total == 2048 * 28, "frames: whole pairs only (%d)", total);
    CHECK(ok, "left and right come from their own chunks");
    CHECK(hp2_stream_read(&st, buf, 10) == 0, "then the end");
    hp2_stream_close(&st);
    CHECK(hp2_stream_open(&st, path, 5) == -1, "no VAG header there");
    CHECK(hp2_stream_open(&st, "no/such/file", 0) == -1, "no file");

    hp2_settings_defaults(&cfg);
    hp2_jbplay_init(&p, &jb, &cfg, path, 0, 12345);
    n = hp2_jbplay_fill(&p, buf, 4096);
    CHECK(n == 4096 && hp2_jbplay_now(&p) == 1 && buf[0] == 2 * 70 / 100, "only song 2 (Menu) plays, at 70%% (%d, song %d)", buf[0], hp2_jbplay_now(&p));
    cfg.volume[HP2_VOL_MENU_MUSIC] = 0;
    hp2_jbplay_fill(&p, buf, 100);
    CHECK(buf[0] == 0 && buf[1] == 0, "volume 0: silence");
    cfg.volume[HP2_VOL_MENU_MUSIC] = 100;
    cfg.song[1] = HP2_PLAY_OFF;
    CHECK(hp2_jbplay_fill(&p, buf, 100) == 100 && hp2_jbplay_now(&p) == 1, "switched off: it plays on");
    for (total = 0; total < 3 * 2048 * 28 && hp2_jbplay_now(&p) == 1; total += n)
        if ((n = hp2_jbplay_fill(&p, buf, 8192)) <= 0) break;
    CHECK(hp2_jbplay_now(&p) == -1 && hp2_jbplay_fill(&p, buf, 100) == 0, "to its end; then nothing, as no song is left for the menus");
    cfg.song[0] = HP2_PLAY_ON;
    CHECK(hp2_jbplay_fill(&p, buf, 100) == 100 && hp2_jbplay_now(&p) == 0 && buf[0] == 1, "On: song 1 plays");
    for (total = 0; total < 3 * 2048 * 28; total += n)
        if ((n = hp2_jbplay_fill(&p, buf, 8192)) <= 0) break;
    CHECK(total >= 3 * 2048 * 28 && hp2_jbplay_now(&p) == 0, "one song in the menus: it plays again after it ends");
    cfg.song[1] = HP2_PLAY_MENU;
    for (i = 0, ok = 0; i < 40; i++) {
        hp2_jbplay_fill(&p, buf, 8192);
        ok |= hp2_jbplay_now(&p) == 1;
    }
    CHECK(ok, "with two in the menus, the other one comes up");
    CHECK(hp2_jbplay_play(&p, 1) == 1 && hp2_jbplay_now(&p) == 1, "play song 2 now (as the Jukebox does when it is set to Menu)");
    hp2_jbplay_fill(&p, buf, 10);
    CHECK(buf[0] == 2 && hp2_jbplay_now(&p) == 1, "and it is what plays, from its start");
    hp2_jbplay_play(&p, 1);
    hp2_jbplay_skip(&p);
    CHECK(hp2_jbplay_now(&p) == -1, "skip ends it");
    hp2_jbplay_fill(&p, buf, 10);
    CHECK(hp2_jbplay_now(&p) == 0, "the next fill picks the other menu song (%d)", hp2_jbplay_now(&p));
    CHECK(hp2_jbplay_play(&p, 7) == 0, "no song 8");
    hp2_jbplay_play(&p, 1);
    hp2_jbplay_stop(&p);
    CHECK(hp2_jbplay_now(&p) == -1 && hp2_jbplay_stopped(&p) && hp2_jbplay_fill(&p, buf, 100) == 0,
          "stop (the playing song set to Off): silence");
    CHECK(hp2_jbplay_fill(&p, buf, 100) == 0 && hp2_jbplay_now(&p) == -1, "and it stays silent");
    CHECK(hp2_jbplay_play(&p, 1) == 1 && !hp2_jbplay_stopped(&p) && hp2_jbplay_fill(&p, buf, 10) == 10 && buf[0] == 2,
          "set to Menu again: it plays from its start");
    hp2_jbplay_stop(&p);
    hp2_jbplay_resume(&p);
    CHECK(hp2_jbplay_fill(&p, buf, 10) == 10 && hp2_jbplay_now(&p) >= 0, "resume (leaving the Jukebox): music again");
    hp2_jbplay_stop(&p);
    hp2_jbplay_skip(&p);
    CHECK(hp2_jbplay_fill(&p, buf, 10) == 10, "Next Song after a stop: music again");
    hp2_jbplay_close(&p);
    free(head);
    remove(path);
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

static int count_named(const hp2_fe_item *it, int n, const char *name)
{
    int i, c = 0;
    for (i = 0; i < n; i++) c += !strcmp(it[i].w.name, name);
    return c;
}

static int find_named(const hp2_fe_item *it, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(it[i].w.name, name)) return i;
    return 0;
}

/* Down and up through a whole page: the selection stays on the page and in
 * view, and min(rows, 7) rows are drawn every time. */
static void page_walk(hp2_femenu *m, hp2_fe_item *it, int rows)
{
    int i, k, n, drawn, want = rows < HP2_FEM_VISIBLE ? rows : HP2_FEM_VISIBLE;
    for (i = 0; i < 3 * rows; i++) {
        hp2_femenu_input(m, i < rows + 2 ? HP2_IN_DOWN : HP2_IN_UP);
        CHECK(m->sel >= m->first && m->sel < m->first + rows, "%s step %d: row %d on the page", hp2_femenu_page(m), i, m->sel);
        CHECK(m->scroll <= m->sel && m->sel < m->scroll + HP2_FEM_VISIBLE && m->scroll >= m->first
              && m->scroll <= m->first + rows - want, "%s step %d: row %d in view (scroll %d)", hp2_femenu_page(m), i, m->sel, m->scroll);
        n = hp2_femenu_items(m, it, 160);
        for (k = 0, drawn = 0; k < HP2_ROW_COUNT; k++) drawn += find_text(it, n, hp2_menu_label(k));
        CHECK(drawn == want, "%s step %d: %d rows drawn", hp2_femenu_page(m), i, drawn);
    }
}

/* ---- event trees ---- */

static hp2_tree real_trees[2];

static size_t put32(uint8_t *b, size_t o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24); return o + 4; }

/* A tree chunk: 1 -> 2, 3 -> 4 (4 needs 2 and 3). */
static size_t synth_tree(uint8_t *b, size_t o, const char *name)
{
    size_t start = o, body, c;
    int e;
    o = put32(b, o, 0x80034A00u); o = put32(b, o, 0);
    body = o;
    o = put32(b, o, 1); o = put32(b, o, 64);
    memset(b + o, 0, 64); memcpy(b + o, name, strlen(name)); o += 64;
    o = put32(b, o, 0x00034A03u); o = put32(b, o, 3 * 8);
    for (e = 0; e < 3; e++) {   /* links: event 1, event 2, event 3 */
        b[o] = 1; b[o + 1] = 0; b[o + 2] = (uint8_t)(e + 1); b[o + 3] = 0; put32(b, o + 4, 7); o += 8;
    }
    o = put32(b, o, 0x00034A01u); o = put32(b, o, 8);        /* cars: 41, 37 */
    o = put32(b, o, 0x0029FFFFu); o = put32(b, o, 0x0025FFFFu);
    o = put32(b, o, 0x00034A02u); o = put32(b, o, 4);        /* track 12, II, 5 laps */
    o = put32(b, o, 12u | 1u << 9 | 0x3000u | 5u << 14 | 25u << 26);
    o = put32(b, o, 0x00034A04u); o = put32(b, o, 8);        /* reward: track 11 II, place 1 */
    b[o] = 1; b[o + 1] = 11; b[o + 2] = 1; b[o + 3] = 0; put32(b, o + 4, 1); o += 8;
    o = put32(b, o, 0x00034A06u); o = put32(b, o, 8);        /* opponents */
    b[o] = 13; b[o + 1] = 255; b[o + 2] = 4; b[o + 3] = 0; b[o + 4] = 19; b[o + 5] = 3; b[o + 6] = 7; b[o + 7] = 0; o += 8;
    o = put32(b, o, 0x00034A05u); o = put32(b, o, 4 * 188);
    c = o;
    memset(b + o, 0, 4 * 188);
    for (e = 0; e < 4; e++) {
        uint8_t *r = b + c + (size_t)e * 188;
        r[0x28] = (uint8_t)(e + 1);
        r[0x42] = 0xFF; r[0x43] = 0xFF;      /* no goal of its own */
        if (e == 1 || e == 2) { r[0x3D] = 1; r[0x7C] = 0; }
        if (e == 3) { r[0x3D] = 2; r[0x7C] = 1; r[0x7E] = 2; }
    }
    {   /* event 1: a Lap Knockout, 2 cars, 1 race, 2 opponents, a reward, goal = link 0 */
        uint8_t *r = b + c;
        r[0x29] = 2; r[0x2A] = 5; r[0x2C] = 3;
        r[0x3B] = 2; r[0x44] = 0; r[0x46] = 1;
        r[0x3C] = 1; r[0x6C] = 0;
        r[0x3E] = 1; r[0x8C] = 0;
        r[0x3F] = 3; r[0x9C] = 0; r[0x9E] = 1; r[0xA0] = 7;   /* index 7 is past the table: skipped */
        r[0x42] = 0; r[0x43] = 0;
    }
    o += 4 * 188;
    put32(b, start + 4, (uint32_t)(o - body));
    return o;
}

static void synth_grid(uint8_t *g, int bad)
{
    memset(g, ' ', 200);
    g[8] = 'x'; g[20 + 4] = 'x'; g[20 + 12] = 'x'; g[bad ? 60 + 8 : 40 + 8] = 'x';
}

static void test_tree(void)
{
    static uint8_t fb[4096], ex[1024];
    static hp2_tree t[2];
    hp2_tree_seg seg[32];
    size_t n = 0, cut;
    float x, y;
    int k, ok;
    printf("event trees\n");
    n = synth_tree(fb, 16, "Hot Pursuit");
    n = synth_tree(fb, n, "Championship");
    CHECK(hp2_tree_parse(fb, n, t) == 2 && t[0].present && t[1].present && t[0].count == 4 && t[1].count == 4,
          "two trees, kind by name, 4 events each");
    CHECK(t[1].ev[3].nlinks == 2 && t[1].ev[3].links[0] == 2 && t[1].ev[3].links[1] == 3 && t[1].ev[3].depth == 2
          && t[1].ev[0].depth == 0, "links and depth");
    {
        const hp2_tree_event *e = &t[1].ev[0];
        CHECK(e->race_type == HP2_RT_LAP_KNOCKOUT && e->tier == 5 && e->cops == 3 && e->ncars == 2 && e->cars[0] == 41 && e->cars[1] == 37,
              "event 1: Lap Knockout, tier, cop level, two cars");
        CHECK(e->nraces == 1 && e->races[0].track == 12 && e->races[0].ii == 1 && e->races[0].laps == 5, "a race: track 12 II, 5 laps");
        CHECK(e->nopponents == 2 && e->opponents[0].car == 13 && e->opponents[0].colour == -1 && e->opponents[0].skill == 4
              && e->opponents[1].colour == 3 && e->opponents[1].skill == 7, "opponents; an index past the table is skipped");
        CHECK(e->nrewards == 1 && e->rewards[0].kind == 1 && e->rewards[0].id == 11 && e->rewards[0].ii == 1 && e->rewards[0].value == 1,
              "reward: track 11 II for a win");
        CHECK(e->target.kind == 1 && e->target.event == 1 && e->target.value == 7 && t[1].ev[1].target.kind == 0, "own goal from +0x42; -1 = none");
        CHECK(t[1].ev[3].need[1].kind == 1 && t[1].ev[3].need[1].event == 3 && t[1].ev[3].need[1].value == 7, "what a link asks");
    }
    CHECK(hp2_tree_segments(&t[0], seg, 32) == 0 && !hp2_tree_pos(&t[0], 1, &x, &y), "no grid: no positions, no lines");
    memset(ex, 0x55, sizeof ex);
    synth_grid(ex + 100, 0); synth_grid(ex + 308, 1);
    CHECK(hp2_tree_grid(t, ex, sizeof ex) == 0 && !t[0].has_grid && !t[1].has_grid, "a grid whose rows are not the depths is refused");
    synth_grid(ex + 308, 0);
    CHECK(hp2_tree_grid(t, ex, sizeof ex) == 1 && t[0].has_grid && t[1].has_grid, "grids found by content");
    CHECK(hp2_tree_pos(&t[1], 3, &x, &y) && x == 12 * 130.0f && y == 350.0f, "event 3 at column 12, row 1");
    k = hp2_tree_segments(&t[1], seg, 32);
    ok = k == 8;
    for (n = 0; ok && n < (size_t)k; n++) {   /* bars at 175 and 525 from 520 to 1560; verticals at 520, 1040, 1560 */
        if (seg[n].y0 == seg[n].y1) ok &= (seg[n].y0 == 175.0f || seg[n].y0 == 525.0f) && seg[n].x0 == 520.0f && seg[n].x1 == 1560.0f;
        else ok &= seg[n].x0 == seg[n].x1 && (seg[n].x0 == 520.0f || seg[n].x0 == 1040.0f || seg[n].x0 == 1560.0f) && seg[n].y0 < seg[n].y1;
    }
    CHECK(ok, "two groups: two bars and six verticals (%d)", k);
    CHECK(hp2_tree_segments(&t[1], seg, 3) == 3, "segments stop at max");
    n = synth_tree(fb, 16, "Hot Pursuit");
    n = synth_tree(fb, n, "Championship");
    ok = 1;
    for (cut = 0; cut < n; cut++) {   /* every prefix parses without reading past it */
        uint8_t *c = (uint8_t *)malloc(cut ? cut : 1);
        memcpy(c, fb, cut);
        if (hp2_tree_parse(c, cut, t) == 2 && cut < n) ok = 0;
        free(c);
    }
    CHECK(ok, "every cut-short FRONTB gives fewer than two trees");
    CHECK(hp2_tree_parse(NULL, 0, t) == 0 && hp2_tree_grid(t, NULL, 0) == 0, "no data");
}

static int count_tex(const hp2_fe_item *it, int n, uint32_t tex)
{
    int i, c = 0;
    for (i = 0; i < n; i++) if (it[i].w.kind == HP2_FE_IMAGE && it[i].w.texture == tex) c++;
    return c;
}

static int count_red(const hp2_fe_item *it, int n)
{
    int i, c = 0;
    for (i = 0; i < n; i++)
        if (it[i].w.kind == HP2_FE_IMAGE && it[i].w.texture == 0xFFFFFFFFu && it[i].w.vcol[0][0] == 200 * 128 / 255 && it[i].w.vcol[0][1] == 24 * 128 / 255) c++;
    return c;
}

static void test_menu(const hp2_fe_layout *L, const hp2_lang *lang, const hp2_jukebox *jb)
{
    static hp2_fe_item it[160];
    hp2_settings cfg;
    hp2_femenu m;
    int n, i, r;
    hp2_scheme before;
    printf("options menus\n");
    hp2_settings_defaults(&cfg);
    CHECK(hp2_femenu_init(&m, L, lang, &cfg) == 0, "init");
    CHECK(hp2_femenu_options_rows(&m) == 4, "OPTIONS shows 4 rows (Player Names, Credits, Languages hidden)");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(n > 20 && n < 160, "%d items", n);
    CHECK(find_text(it, n, "Audio") == 1 && find_text(it, n, "Save/Load") == 1, "row text from FE_ENGLISH");
    CHECK(find_text(it, n, "Languages") == 0 && find_text(it, n, "Credits") == 0 && find_text(it, n, "Player Names") == 0,
          "no Languages, Credits, Player Names");
    CHECK(!strcmp(m.help, "Set the volume and the Jukebox."), "Audio's help line, got '%s'", m.help);
    for (i = 0; i < n; i++)
        if (it[i].w.kind == HP2_FE_TEXT && it[i].str && !strcmp(it[i].str, "Audio"))
            CHECK(it[i].w.rgba[0] == 124 && it[i].w.rgba[1] == 64, "selected row is orange");
    for (i = 0, r = 0; i < n; i++)
        if (!strcmp(it[i].w.name, "MI_OVERSCORE") || !strcmp(it[i].w.name, "MI_UNDERSCORE")) r++;
    CHECK(r == 2, "the two selection lines");
    CHECK(hp2_femenu_input(&m, HP2_IN_UP) == HP2_FEM_REDRAW && m.sel == 4, "up from the top wraps to Save/Load");
    CHECK(!strcmp(m.help, "Save and load your settings."), "Save/Load's help line, got '%s'", m.help);
    hp2_femenu_input(&m, HP2_IN_DOWN);
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == 2, "Controller");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(m.screen == HP2_FEM_SETTINGS && !strcmp(hp2_femenu_page(&m), "Controls") && m.sel == HP2_ROW_SCHEME, "Controller opens the Controls page");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Controls") == 1, "title = section");
    CHECK(find_text(it, n, hp2_menu_label(HP2_ROW_SCHEME)) == 1, "row label from hp2_menu");
    CHECK(find_text(it, n, hp2_menu_label(HP2_ROW_VSYNC)) == 0, "no Video rows on the Controls page");
    CHECK(count_named(it, n, "DOWN_ARROW") == 1, "15 rows: more below");
    CHECK(!strcmp(m.help, hp2_menu_help(HP2_ROW_SCHEME)), "help line from hp2_menu");
    before = cfg.scheme;
    r = hp2_femenu_input(&m, HP2_IN_RIGHT);
    CHECK((r & HP2_FEM_CHANGED) && cfg.scheme != before, "right changes the setting");
    hp2_femenu_input(&m, HP2_IN_UP);
    CHECK(m.sel == HP2_ROW_DEFAULT_CONTROLS, "up from the page's top wraps to its bottom");
    /* binding by pressing */
    while (m.sel != HP2_ROW_BIND_ACCELERATE) hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && hp2_femenu_capturing(&m) == HP2_ROW_BIND_ACCELERATE, "accept waits for a button");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Press a button") == 1 && !strcmp(m.help, "Press the button for Accelerate. Esc cancels."), "and says so");
    CHECK(hp2_femenu_input(&m, HP2_IN_DOWN) == 0 && m.sel == HP2_ROW_BIND_ACCELERATE, "menu keys do nothing meanwhile");
    r = hp2_femenu_capture(&m, HP2_BTN_DPAD_UP);
    CHECK((r & HP2_FEM_CHANGED) && hp2_femenu_capturing(&m) == -1 && hp2_bound_button(&cfg, HP2_ACT_ACCELERATE) == HP2_BTN_DPAD_UP,
          "the press binds it");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    {
        hp2_button hb = hp2_bound_button(&cfg, HP2_ACT_HANDBRAKE);
        hp2_femenu_capture(&m, hb);
        CHECK(hp2_bound_button(&cfg, HP2_ACT_ACCELERATE) == hb && hp2_bound_button(&cfg, HP2_ACT_HANDBRAKE) == HP2_BTN_DPAD_UP,
              "pressing handbrake's button swaps them");
    }
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && hp2_femenu_capturing(&m) == -1 && m.screen == HP2_FEM_SETTINGS,
          "back cancels, staying on the page");
    CHECK(hp2_bound_button(&cfg, HP2_ACT_HANDBRAKE) == HP2_BTN_DPAD_UP, "cancel changes nothing");
    while (m.sel != HP2_ROW_DEFAULT_CONTROLS) hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK((hp2_femenu_input(&m, HP2_IN_ACCEPT) & HP2_FEM_CHANGED) && cfg.bind[HP2_ACT_ACCELERATE] == -1, "Default Controls");
    page_walk(&m, it, 15);
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_OPTIONS && m.sel == 2, "back returns to Controller");

    hp2_femenu_input(&m, HP2_IN_DOWN);
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(!strcmp(hp2_femenu_page(&m), "Video") && m.sel == HP2_ROW_INTERNAL_RES, "Display opens the Video page");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(count_named(it, n, "DOWN_ARROW") == 1, "8 rows: more below");
    page_walk(&m, it, 8);
    hp2_femenu_input(&m, HP2_IN_BACK);

    hp2_femenu_input(&m, HP2_IN_UP);
    hp2_femenu_input(&m, HP2_IN_UP);
    CHECK(m.sel == 1, "Audio");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(!strcmp(hp2_femenu_page(&m), "Audio") && m.page == hp2_fe_find(L, "AUDIO_OPTIONS") && m.sel == HP2_ROW_VOL_MENU_MUSIC,
          "Audio opens its page on the AUDIO_OPTIONS layout");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Audio") == 1 && find_text(it, n, "Menu Music") == 1, "title and first row");
    CHECK(count_named(it, n, "AUDIO_BAR1") == 7 && count_named(it, n, "AUDIO_BAR1_V") == 7, "7 sliders on the 7 volume rows shown");
    CHECK(find_text(it, n, "70%") == 0, "volumes show as sliders, not text");
    for (i = 0; i < n; i++)
        if (!strcmp(it[i].w.name, "AUDIO_BAR1_V") && it[i].w.y < 90.0f)
            CHECK(it[i].w.x > 539.0f && it[i].w.x < 540.0f, "Menu Music's marker at 70%%: x %g", it[i].w.x);
    CHECK(!strcmp(m.help, "Set volume from mute to maximum."), "the game's help line");
    r = hp2_femenu_input(&m, HP2_IN_RIGHT);
    CHECK((r & HP2_FEM_CHANGED) && cfg.volume[HP2_VOL_MENU_MUSIC] == 75, "right: 75%%");
    for (i = 0; i < 7; i++) hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == HP2_ROW_AUDIO_MODE, "Audio Mode");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Stereo") == 1 && count_named(it, n, "LEFT_ARROW") == 1, "its value and arrows");
    CHECK(count_named(it, n, "UP_ARROW") == 1, "scrolled: more above");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == HP2_ROW_DEFAULT_VOLUMES, "Default Volumes: the Jukebox is not on the Audio page");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Jukebox") == 0, "no Jukebox row");
    CHECK(count_named(it, n, "LEFT_ARROW") == 0, "a button has no arrows");
    CHECK(hp2_femenu_input(&m, HP2_IN_RIGHT) == 0, "right does nothing on it");
    r = hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK((r & HP2_FEM_CHANGED) && cfg.volume[HP2_VOL_MENU_MUSIC] == 70, "accept resets the volumes");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == HP2_ROW_DEFAULT_JUKEBOX, "Default Jukebox, the last row");
    page_walk(&m, it, 10);
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_OPTIONS && m.sel == 1, "back returns to Audio");
    CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_SETTINGS, "Audio opens again");
    hp2_femenu_input(&m, HP2_IN_BACK);
    if (jb) {
        int k, first;
        hp2_femenu_set_jukebox(&m, jb);
        CHECK(hp2_femenu_options_rows(&m) == 5, "with a song table, OPTIONS has 5 entries");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Jukebox") == 1 && count_named(it, n, "MENUITEM_5") == 1, "Jukebox among them, in row 5's place at the end");
        for (k = 0; k < n; k++)
            if (it[k].str && !strcmp(it[k].str, "Jukebox"))
                CHECK(it[k].w.y > 107.0f && it[k].w.y < 109.0f, "Jukebox right under Audio (y %g)", it[k].w.y);
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.sel == 2 && !strcmp(m.help, "Set what songs you want to hear."), "down: Jukebox, its help line");
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_JUKEBOX, "accept opens the Jukebox");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Rock") == 1 && find_text(it, n, "Genre:") == 1, "Genre: Rock");
        CHECK(find_text(it, n, "The People That We Love(Inst)") == 1 && find_text(it, n, "Bush") == 2, "first songs, title and artist");
        CHECK(find_text(it, n, "On") == 5, "bank play status 2 and 4 (Hot Pursuit, Menu/Racing in the game): On");
        CHECK(count_named(it, n, "LEFT_ARROW") == 1 && count_named(it, n, "LEFT_ARROW1") == 0, "arrows on the Genre row only");
        CHECK(count_named(it, n, "DOWN_ARROW") == 1 && count_named(it, n, "UP_ARROW") == 0, "more songs below");
        for (k = 0; k < n; k++)
            if (it[k].str && !strcmp(it[k].str, "Rock")) CHECK(it[k].w.rgba[0] == 124, "the genre is orange while its row is selected");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.nsongs == 4 && find_text(it, n, "Techno") == 1 && find_text(it, n, "Cylkoid") == 1, "right: Techno, 4 songs");
        CHECK(count_named(it, n, "DOWN_ARROW") == 0, "4 songs fit");
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.genre == 0 && m.nsongs == 19, "back to Rock, 19 songs");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        first = m.songs[0];
        CHECK(m.jsel == 1 && hp2_femenu_play(&m, first) == HP2_PLAY_ON, "first song: On");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(count_named(it, n, "LEFT_ARROW1") == 1 && count_named(it, n, "LEFT_ARROW") == 0, "arrows move to the song");
        for (k = 0; k < n; k++)
            if (!strcmp(it[k].w.name, "SONG1") || !strcmp(it[k].w.name, "ARTIST1"))
                CHECK(it[k].w.rgba[0] == 124 && it[k].w.rgba[1] == 64, "%s orange: title and artist both", it[k].w.name);
        r = hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK((r & HP2_FEM_CHANGED) && cfg.song[first] == HP2_PLAY_OFF && m.changed_song == first,
              "right: On -> Off (wraps), kept in the settings, reported as changed");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(cfg.song[first] == HP2_PLAY_MENU, "right: Menu");
        hp2_femenu_input(&m, HP2_IN_LEFT);
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(cfg.song[first] == HP2_PLAY_ON && m.changed_song == first, "left twice: On");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.changed_song == -1, "moving changes no song");
        for (k = 0; k < 25; k++) hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.jsel >= 1 && m.jsel - 1 >= m.jscroll && m.jsel - 1 < m.jscroll + HP2_FEM_SONGS, "scrolling keeps the song in view");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Rock") == 1, "the genre stays on screen while scrolling");
        CHECK(hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && m.screen == HP2_FEM_OPTIONS && m.sel == 2,
              "back: OPTIONS, on Jukebox");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(!strcmp(hp2_femenu_page(&m), "Controls"), "the entry after it is Controller");
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_UP);
        hp2_femenu_input(&m, HP2_IN_UP);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        hp2_femenu_input(&m, HP2_IN_UP);
        CHECK(m.sel == HP2_ROW_DEFAULT_JUKEBOX, "Audio page: up from the top, Default Jukebox");
        r = hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK((r & HP2_FEM_CHANGED) && cfg.song[first] == -1, "accept puts every song back to the bank's");
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_set_jukebox(&m, NULL);
        CHECK(hp2_femenu_options_rows(&m) == 4 && m.sel == 1, "no song table: no Jukebox entry");
    }
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) & HP2_FEM_QUIT, "back from OPTIONS quits");
    CHECK(hp2_femenu_items(&m, it, 3) == 3, "never more than cap");
}

/* title -> Main Menu -> the mode menus and Options, and back */
static void test_flow(const hp2_fe_layout *L, const hp2_lang *lang)
{
    static hp2_fe_item it[160];
    hp2_settings cfg;
    hp2_femenu m;
    int n, k;
    printf("main menu flow\n");
    hp2_settings_defaults(&cfg);
    CHECK(hp2_femenu_init(&m, L, lang, &cfg) == 0 && hp2_femenu_start(&m, HP2_FEM_TITLE) && m.screen == HP2_FEM_TITLE, "starts on the title");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "PRESS START") == 1 && count_named(it, n, "TITLE_FULLSCREEN") == 0 && count_named(it, n, "TITLE_LETTERBOX") == 0,
          "PRESS START, not the placeholder splash");
    CHECK(count_named(it, n, "TITLE_SCREEN_TITLE") == 1 && count_named(it, n, "GRID") == 1, "the logo on Main Menu's backdrop");
    CHECK(hp2_femenu_input(&m, HP2_IN_UP) == 0, "only accept or back on the title");
    CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_MAIN && m.sel == 1, "accept: Main Menu");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(hp2_femenu_list_count(&m) == 4 && !strcmp(hp2_femenu_list_label(&m, 3), "Quick Race")
          && !strcmp(hp2_femenu_list_label(&m, 4), "Options"), "four entries: Quick Race above Options");
    CHECK(find_text(it, n, "Main Menu") == 1 && find_text(it, n, "Hot Pursuit") == 1 && find_text(it, n, "World Racing") == 1
          && find_text(it, n, "Quick Race") == 1 && find_text(it, n, "Options") == 1, "its title and four rows");
    {   /* the 4th row sits a row pitch under the 3rd; bands for 4 rows */
        int i, q = -1, o = -1, end = 0;
        for (i = 0; i < n; i++) {
            if (it[i].str && !strcmp(it[i].str, "Quick Race")) q = i;
            if (it[i].str && !strcmp(it[i].str, "Options")) o = i;
            if (!strcmp(it[i].w.name, "BOTTOM_MASK") && it[i].w.sy > 1.0f) end = (int)(it[i].w.y + it[i].w.sy * 16.0f + 0.5f);
        }
        CHECK(q >= 0 && o >= 0 && it[o].w.y - it[q].w.y == 55.0f && it[o].w.y == 236.0f, "Options moved down a row");
        CHECK(count_named(it, n, "MI_1_MASK") == 3 && count_named(it, n, "BOTTOM_MASK") == 4 && end == 341,
              "3 row bands (the selected one lifts), 3 lines and the filler down to 341 (%d)", end);
    }
    CHECK(find_text(it, n, "Unlocked") == 0 && find_text(it, n, "Easter Egg") == 0, "no progress line yet");
    CHECK(!strcmp(m.help, "Be the cops or beat the cops!"), "Hot Pursuit's help line, got '%s'", m.help);
    {   /* the corner prompt is Triangle (j), not the game's Circle (k) */
        int i, g = 0;
        for (i = 0; i < n; i++) if (it[i].w.kind == HP2_FE_TEXT && it[i].str && !strcmp(it[i].str, "j")) g++;
        CHECK(g == 1, "the Help prompt shows the Triangle glyph");
    }
    CHECK(hp2_femenu_input(&m, HP2_IN_HELP) == HP2_FEM_REDRAW && strstr(m.help, ": accept") && strstr(m.help, ": back")
          && strstr(m.help, ": help"), "help: the ticker shows the buttons ('%s')", m.help);
    hp2_femenu_input(&m, HP2_IN_HELP);
    CHECK(!strcmp(m.help, "Be the cops or beat the cops!"), "help again: the row's line is back");
    CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_HOT_PURSUIT, "Hot Pursuit opens its modes");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(hp2_femenu_list_count(&m) == 3 && find_text(it, n, "Ultimate Fugitive") == 1 && find_text(it, n, "Racing But With Pigs") == 1
          && find_text(it, n, "You're The Pig") == 1, "three modes, renamed");
    CHECK(find_text(it, n, "Quick Race") == 0 && find_text(it, n, "Ultimate Racer") == 0 && find_text(it, n, "Challenge") == 0
          && find_text(it, n, "You're The Cop") == 0, "no Quick Race, none of the old names");
    CHECK(!strcmp(hp2_femenu_list_label(&m, 1), "Ultimate Fugitive") && !strcmp(m.help, "Beat the heat in 30 events."),
          "Ultimate Fugitive on top, with Ultimate Racer's help line");
    CHECK(count_named(it, n, "MI_1_MASK") == 2 && count_named(it, n, "BOTTOM_MASK") == 3, "bands for 3 rows");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(!strcmp(m.help, "One-player and two-player challenges."), "Racing But With Pigs' help line");
    hp2_femenu_input(&m, HP2_IN_UP);
    hp2_femenu_input(&m, HP2_IN_UP);
    CHECK(m.sel == 3 && !strcmp(m.help, "Fill your ticket quota for big points!"), "up from the top wraps to You're The Pig");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == 1 && hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_TREE, "Ultimate Fugitive opens the event tree");
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_HOT_PURSUIT && m.sel == 1, "back: Hot Pursuit, on Ultimate Fugitive");
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && m.screen == HP2_FEM_MAIN && m.sel == 1, "back: Main Menu, on Hot Pursuit");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(!strcmp(m.help, "Take on the world's best sports cars!"), "World Racing's help line");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    n = hp2_femenu_items(&m, it, 160);
    CHECK(m.screen == HP2_FEM_WORLD_RACING && hp2_femenu_list_count(&m) == 2 && find_text(it, n, "Ultimate Racer") == 1
          && find_text(it, n, "Just Race") == 1 && find_text(it, n, "Championship") == 0 && find_text(it, n, "Quick Race") == 0,
          "World Racing: Ultimate Racer and Just Race");
    CHECK(!strcmp(m.help, "Real cars, real fast in 30 events."), "Ultimate Racer on top, with Championship's help line");
    CHECK(count_named(it, n, "MI_1_MASK") == 1 && count_named(it, n, "BOTTOM_MASK") == 2, "bands for 2 rows");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    n = hp2_femenu_items(&m, it, 160);
    CHECK(m.screen == HP2_FEM_TREE && m.tree_kind == 0 && find_text(it, n, "National Forest Challenge") == 1 && find_text(it, n, "Ultimate Racer") == 1,
          "Ultimate Racer: its event list");
    hp2_femenu_input(&m, HP2_IN_UP);
    CHECK(m.tree_ev == 30, "Ultimate Racer has 31 events");
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_WORLD_RACING && m.sel == 1, "back from the tree: World Racing");
    hp2_femenu_input(&m, HP2_IN_UP);
    CHECK(m.sel == 2 && !strcmp(m.help, "One-player and two-player challenges."), "up wraps to Just Race");
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.screen == HP2_FEM_MAIN && m.sel == 2, "back: on World Racing");
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.sel == 3 && !strcmp(m.help, "Go straight into the action!"), "Quick Race's help line");
    {
        int hp = 0, wr = 0, k, ok = 1;
        for (k = 0; k < 64; k++) {
            m.seed = 0x1234u + (uint32_t)k * 7919u;
            ok &= hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_SELECT_CAR;
            hp += m.quick_mode == HP2_QUICK_HOT_PURSUIT;
            wr += m.quick_mode == HP2_QUICK_WORLD_RACING;
            ok &= hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && m.screen == HP2_FEM_MAIN && m.sel == 3;
        }
        CHECK(ok && hp + wr == 64 && hp > 10 && wr > 10, "Quick Race picks Hot Pursuit or World Racing at random (%d / %d)", hp, wr);
    }
    {   /* car select */
        int i, thumbs = 0, ok = 1;
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.screen == HP2_FEM_SELECT_CAR && m.car == 0, "Quick Race opens car select on the first car");
        n = hp2_femenu_items(&m, it, 160);
        for (i = 0; i < n; i++) thumbs += !strncmp(it[i].w.name, "THUMBNAIL_", 10) && strcmp(it[i].w.name, "THUMBNAIL_FRAME")
                                          && strcmp(it[i].w.name, "THUMBNAIL_RIGHT_FILM");
        CHECK(thumbs == 7 && count_named(it, n, "THUMBNAIL_FRAME") == 1, "a strip of seven thumbnails (two left of the frame, four right) (%d)", thumbs);
        {
            int sw = 0, k;
            for (k = 0; k < n; k++) sw += !strncmp(it[k].w.name, "SWATCH_", 7) && strcmp(it[k].w.name, "SWATCH_FRAME");
            CHECK(sw == 8 && count_named(it, n, "SWATCH_FRAME") == 1 && count_named(it, n, "UP_ARROW") == 1
                  && count_named(it, n, "DOWN_ARROW") == 1, "eight colour swatches, a frame and the two arrows (%d)", sw);
        }
        CHECK(find_text(it, n, "Cars Unlocked: 23/23") == 1, "all cars unlocked");
        CHECK(!strcmp(hp2_femenu_car_name(&m, 0), "Chevrolet Corvette Z06") && count_named(it, n, "CAR_LOGO") == 1
              && it[find_named(it, n, "CAR_LOGO")].w.texture == hp2_fe_hash("CARLOGO_CORVETTE"), "the Corvette's name logo (CARLOGO_CORVETTE)");
        CHECK(find_text(it, n, "171 mph") == 0 && find_text(it, n, "Top Speed:") == 0 && find_text(it, n, "Power:") == 0,
              "no spec lines on car select");
        CHECK(find_text(it, n, "LOCKED") == 0 && count_named(it, n, "LOCKED_BG1") == 0, "nothing locked");
        CHECK(hp2_femenu_input(&m, HP2_IN_DOWN) == HP2_FEM_REDRAW && m.color == 1, "down: the next colour");
        hp2_femenu_input(&m, HP2_IN_UP);
        hp2_femenu_input(&m, HP2_IN_UP);
        CHECK(m.color == 7 && m.car == 0, "up from the first colour wraps to the last");
        m.color = 0;
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.car == 1 && !strcmp(hp2_femenu_car_name(&m, 1), "Dodge Viper GTS"), "right: the next car");
        hp2_femenu_input(&m, HP2_IN_LEFT);
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.car == HP2_CARS - 1, "left from the first car wraps to the last");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(!strcmp(hp2_femenu_car_name(&m, m.car), "Ferrari 550 Barchetta")
              && it[find_named(it, n, "CAR_LOGO")].w.texture == hp2_fe_hash("CARLOGO_BARCHETTA"), "the Barchetta's name logo");
        {   /* every car has a name and a logo of its own */
            uint32_t seen[HP2_CARS];
            for (i = 0; i < HP2_CARS; i++) {
                int k;
                m.car = i;
                n = hp2_femenu_items(&m, it, 160);
                ok &= hp2_femenu_car_name(&m, i) != NULL && count_named(it, n, "CAR_LOGO") == 1;
                seen[i] = it[find_named(it, n, "CAR_LOGO")].w.texture;
                for (k = 0; k < i; k++) ok &= seen[k] != seen[i];
                
            }
        }
        CHECK(ok, "all %d cars have a name and a logo of their own", HP2_CARS);
        m.car = 0;
        /* track select */
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_SELECT_CAR && m.car_popup == 1, "accept on a car: Transmission");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "TRANSMISSION") == 1 && find_text(it, n, "Automatic") == 1 && find_text(it, n, "Manual") == 1
              && find_text(it, n, "Semi-Auto") == 1 && count_named(it, n, "LOCKEDHEAD_BGBLUE") == 1, "the Transmission panel");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.trans == 1, "right: Manual");
        hp2_femenu_input(&m, HP2_IN_LEFT);
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.trans == 2, "left from Automatic wraps to Semi-Auto");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.car_popup == 2 && find_text(it, n, "HANDLING") == 1 && find_text(it, n, "Classic") == 1 && find_text(it, n, "Extreme") == 1,
              "accept: the Handling panel");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.handling == 1, "right: Extreme");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.car_popup == 1 && m.screen == HP2_FEM_SELECT_CAR, "back: Transmission again");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.car_popup == 0 && m.screen == HP2_FEM_SELECT_CAR, "back: the car list");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_SELECT_TRACK && m.track == 0 && m.laps == 2,
              "then track select");
        n = hp2_femenu_items(&m, it, 160);
        {
            int tn = 0, k;
            for (k = 0; k < n; k++) tn += !strncmp(it[k].w.name, "TN_TRACK", 8);
            CHECK(tn == 8 && count_named(it, n, "THUMBNAIL_FRAME") == 1, "a strip of eight course thumbnails (%d)", tn);
        }
        CHECK(find_text(it, n, "4.590 mi (Circuit)") == 1 && find_text(it, n, "Beginner") == 1 && find_text(it, n, "Best Total Time (2 laps)") == 1,
              "length, difficulty and the best-time heading");
        CHECK(find_text(it, n, "Sunny") == 1 && find_text(it, n, "Overcast") == 1 && count_named(it, n, "TOD_3PM") == 1
              && count_named(it, n, "TOD_OVERCAST") == 1 && count_named(it, n, "TOD_FRAME") == 1 && find_text(it, n, "Alternate") == 0,
              "the Time of Day column: two lightings and a frame");
        CHECK(count_named(it, n, "LEFT_ARROW_SELECT") == 0 && count_named(it, n, "RIGHT_ARROW_SELECT") == 1 && count_named(it, n, "UP_ARROW") == 1
              && count_named(it, n, "DOWN_ARROW") == 1, "the strip has only its right arrow");
        CHECK(count_named(it, n, "SELECTLAP_BG") == 0 && count_named(it, n, "LOCKED_BG1") == 0 && find_text(it, n, "SELECT LAPS") == 0,
              "no popup, nothing locked");
        CHECK(!strcmp(hp2_femenu_track_name(&m, 0, 0), "Coastal Parklands") && !strcmp(hp2_femenu_track_name(&m, 0, 1), "Coastal Parklands II")
              && !strcmp(hp2_femenu_track_name(&m, 14, 0), "Tropical Circuit"), "course names from the language file");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.track == 1 && hp2_femenu_track_number(&m) == 12, "right: the next course (track 12)");
        hp2_femenu_input(&m, HP2_IN_LEFT);
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.track == HP2_TRACKS - 1 && hp2_femenu_track_number(&m) == 63, "left from the first wraps to the last");
        m.track = 0;
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.tod == 1 && hp2_femenu_track_number(&m) == 14, "down: the other lighting is track + 3");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(count_named(it, n, "TOD_FRAME") == 1, "its frame");
        hp2_femenu_input(&m, HP2_IN_UP);
        CHECK(m.tod == 0, "up: back to sunny");
        m.tod = 1;
        m.track = 6;
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Sunset") == 1 && hp2_femenu_track_number(&m) == 34, "sunset in Greece (track 34)");
        m.track = 2; m.tod = 0;
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "7.750 mi (Point-to-Point)") == 1, "a point-to-point course says so");
        m.track = 0;
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.lap_popup, "accept: SELECT LAPS");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "SELECT LAPS") == 1 && find_text(it, n, "2") >= 1 && count_named(it, n, "SELECTLAP_BG") == 1, "the popup shows");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.laps == 4 && m.track == 0, "right in the popup: laps");
        m.laps = 1;
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.laps == 1, "at least one lap");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(!m.lap_popup && m.screen == HP2_FEM_SELECT_TRACK, "back closes the popup");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_SELECT_CAR, "back: the car");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_MAIN && m.sel == 3, "back: Main Menu, on Quick Race");
    }
    {   /* Challenge (Racing But With Pigs) */
        hp2_femenu_input(&m, HP2_IN_UP);
        hp2_femenu_input(&m, HP2_IN_UP);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.screen == HP2_FEM_HOT_PURSUIT && m.sel == 2, "Hot Pursuit, on Racing But With Pigs");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_CHALLENGE && find_text(it, n, "One Player") == 1 && find_text(it, n, "Single Race") == 1
              && find_text(it, n, "Setup") == 1 && !strcmp(m.help, "Solo play, full-screen."), "Challenge: Players, Race Type, Skill Level, Setup");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Two Players") == 1 && !strcmp(m.help, "Head-to-head play, split-screen.") && count_named(it, n, "LEFT_ARROW") == 1,
              "right: Two Players, with arrows");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Free Run") == 1 && !strcmp(m.help, "Run practice laps."), "Race Type: Free Run and its help");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Advanced") == 1, "Skill Level: Advanced");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.sel == 4 && count_named(it, n, "LEFT_ARROW") == 0, "Setup has no arrows");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_RACE_SETUP && find_text(it, n, "Traffic") == 1 && find_text(it, n, "Damage") == 1
              && find_text(it, n, "Opponents") == 0 && find_text(it, n, "Cops") == 0, "Setup: Customize; Free Run has only Traffic and Damage");
        m.race_type = 0;
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_RACE_SETUP && find_text(it, n, "Cops") == 1 && find_text(it, n, "Opponent Type") == 1
              && find_text(it, n, "Catch-up") == 1 && find_text(it, n, "Full Grid") == 1 && find_text(it, n, "Advanced") == 1 && !strcmp(m.help, ""),
              "Single Race Customize: opponents, type, AI (from the skill), catch-up");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(!strcmp(m.help, "Set the number of opponents."), "Opponents' help line");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "1") == 1 && find_text(it, n, "Full Grid") == 0, "right from Full Grid: 1 opponent");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.screen == HP2_FEM_SELECT_CAR, "accept: car select");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_RACE_SETUP && m.sel == 2, "back: Customize");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_CHALLENGE && m.sel == 4, "back: Challenge");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_HOT_PURSUIT && m.sel == 2, "back: Hot Pursuit");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_MAIN && m.sel == 1, "back: Main Menu");
        n = 0;      /* You're The Pig: three rows, titled with the port's name */
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_CHALLENGE && m.chal_cop && find_text(it, n, "You're The Pig") == 1 && find_text(it, n, "Race Type") == 0
              && find_text(it, n, "Skill Level") == 1 && find_text(it, n, "Setup") == 1, "You're The Pig: Players, Skill Level, Setup");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.skill == 2 && !strcmp(m.help, "Toughest cops"), "Skill Level: the cops' help line");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_RACE_SETUP && find_text(it, n, "Cops") == 0 && find_text(it, n, "Opponent Type") == 0
              && find_text(it, n, "Traffic") == 1, "You're The Pig: Customize without Cops and Opponent Type");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.screen == HP2_FEM_SELECT_CAR, "accept: car select");
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_CHALLENGE && m.sel == 3, "back: the setup, on Setup");
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_MAIN && m.sel == 1, "back: Main Menu again");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "1") == 1 && find_text(it, n, "Lotus Elise Delivery") == 1 && find_text(it, n, "5") == 1
              && find_text(it, n, "Pursuit Crown Victoria Challenge") == 1 && find_text(it, n, "6") == 0 && count_red(it, n) == 0,
              "Ultimate Fugitive: a list, events 1 to 5 on view");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.tree_ev == 1 && m.tree_scroll == 0, "down: event 2");
        for (k = 0; k < 5; k++) hp2_femenu_input(&m, HP2_IN_DOWN);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.tree_ev == 6 && m.tree_scroll == 2 && find_text(it, n, "7") == 1 && find_text(it, n, "1") == 0 && find_text(it, n, "3") == 1,
              "down past the fifth row scrolls (events 3 to 7)");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.tree_ev == 11 && m.tree_scroll == 7, "right: a page down");
        for (k = 0; k < 5; k++) hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.tree_ev == 31 && m.tree_scroll == 27, "pages stop at event 32");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        CHECK(m.tree_ev == 0 && m.tree_scroll == 0, "down from the last wraps to event 1");
        hp2_femenu_input(&m, HP2_IN_UP);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.tree_ev == 31 && find_text(it, n, "Hot Pursuit 2 Championship Duel") == 1, "up from event 1: event 32 (%d)", m.tree_ev);
        hp2_femenu_input(&m, HP2_IN_LEFT);
        CHECK(m.tree_ev == 26, "left: a page up");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_HOT_PURSUIT, "back: Hot Pursuit");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.screen == HP2_FEM_TREE && m.tree_ev == 0 && m.tree_scroll == 0, "opened again: on event 1");
        m.trees = NULL;
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == 0 && m.screen == HP2_FEM_TREE, "no event data: accept does nothing");
        m.trees = real_trees;
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_SELECT_CAR && m.ev_mode && m.ev_ncars == 1,
              "accept on event 1: car select with its one car");
        n = hp2_femenu_items(&m, it, 160);
        CHECK(!strcmp(hp2_femenu_car_name(&m, 0), "Lotus Elise") && hp2_femenu_car_name(&m, 1) == NULL
              && count_tex(it, n, hp2_fe_hash("CARLOGO_ELISE")) == 1 && count_named(it, n, "THUMBNAIL_ELISE") == 1
              && count_named(it, n, "CARSUNLOCKED") == 0, "the Lotus Elise alone: its logo, one thumbnail, no unlock count");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.car == 0, "one car: right stays on it");
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_TREE && m.tree_ev == 0 && !m.ev_mode, "back: the event list, on event 1");
        for (k = 0; k < 3; k++) hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.ev_ncars == 3 && !strcmp(hp2_femenu_car_name(&m, 0), "Opel Speedster") && count_named(it, n, "THUMBNAIL_TS50") == 1
              && count_named(it, n, "THUMBNAIL_CL55AMG") == 1 && count_named(it, n, "THUMBNAIL_OPEL") == 1,
              "event 4: Opel, TS50 and CL55 AMG, each once in the strip");
        hp2_femenu_input(&m, HP2_IN_RIGHT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.car == 1 && !strcmp(hp2_femenu_car_name(&m, 1), "Ford TS50") && count_tex(it, n, hp2_fe_hash("CARLOGO_TS50")) == 1, "right: the TS50");
        hp2_femenu_input(&m, HP2_IN_RIGHT); hp2_femenu_input(&m, HP2_IN_RIGHT);
        CHECK(m.car == 0, "wraps after the third");
        hp2_femenu_input(&m, HP2_IN_ACCEPT); hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.car_popup == 2, "accept: Transmission, then Handling");
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.screen == HP2_FEM_TREE && m.tree_ev == 3 && !m.ev_mode && !m.car_popup, "accept on Handling: back to the event list (no race yet)");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(!strcmp(hp2_femenu_car_name(&m, 0), "Pursuit Crown Victoria"), "event 5: the Pursuit Crown Victoria");
        hp2_femenu_input(&m, HP2_IN_BACK);
        for (k = 0; k < 12; k++) hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.tree_ev == 16 && !strcmp(hp2_femenu_car_name(&m, 0), "Ferrari 360 Modena"), "event 17: the 360 Modena Challenge, named by the port");
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_MAIN && m.sel == 1, "back from the tree: Main Menu");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_DOWN);
    }
    hp2_femenu_input(&m, HP2_IN_DOWN);
    CHECK(m.quick_mode == HP2_QUICK_NONE && !strcmp(m.help, "Customize your settings."), "Options' help line (the pick is dropped)");
    CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_REDRAW && m.screen == HP2_FEM_OPTIONS && m.sel == 1, "Options opens OPTIONS");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(m.screen == HP2_FEM_SETTINGS && !strcmp(hp2_femenu_page(&m), "Audio"), "and its pages work as before");
    hp2_femenu_input(&m, HP2_IN_HELP);
    CHECK(strstr(m.help, ": accept") != NULL, "help works on a settings page too");
    hp2_femenu_input(&m, HP2_IN_HELP);
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && m.screen == HP2_FEM_MAIN && m.sel == 4,
          "back from OPTIONS: Main Menu, on Options (not quit)");
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) == HP2_FEM_REDRAW && m.screen == HP2_FEM_TITLE, "back: the title");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK(m.screen == HP2_FEM_MAIN && m.sel == 4, "Main Menu remembers its row");
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(hp2_femenu_input(&m, HP2_IN_BACK) & HP2_FEM_QUIT, "back from the title quits");
}

/* Mean step between vertically adjacent pixels (RGB, 0..255) of a decoded
 * texture, or -1. A scrambled (wrongly swizzled) reading is far rougher. */
static double tex_vdiff(const hp2_tex *t)
{
    uint8_t *px;
    double sum = 0.0;
    long n = 0;
    int x, y, c;
    if (!t || !t->bpp) return -1.0;
    px = (uint8_t *)malloc((size_t)t->w * t->h * 4);
    if (!px || hp2_tex_decode(t, px) != 0) { free(px); return -1.0; }
    for (y = 0; y + 1 < t->h; y++)
        for (x = 0; x < t->w; x++)
            for (c = 0; c < 3; c++) {
                int a = px[4 * ((size_t)y * t->w + x) + c], b = px[4 * ((size_t)(y + 1) * t->w + x) + c];
                sum += a > b ? a - b : b - a; n++;
            }
    free(px);
    return n ? sum / n : -1.0;
}

static void test_real(const char *fb, const char *gb, const char *lf, const char *disc, const char *bank, const char *exe)
{
    static hp2_jukebox jb;
    int have_jb = 0;
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
    {   /* names cut at 23 characters in the pack: the hash is of the full name */
        static const char *full[] = { "PURSUIT_EVENT_PANEL_LOCKED", "PURSUIT_EVENT_PANEL_UNLOCKED", "PURSUIT_EVENT_PANEL_WON",
                                      "SPEED_EVENT_PANEL_LOCKED", "SPEED_EVENT_PANEL_UNLOCKED", "SPEED_EVENT_PANEL_WON" };
        int k, ok = 1;
        for (k = 0; k < 6; k++) {
            t = hp2_tex_find(&tex, hp2_fe_hash(full[k]));
            if (!t || t->bpp != 8 || t->w != (k < 3 ? 272 : 216) || t->h != (k < 3 ? 272 : 312)) ok = 0;
        }
        CHECK(ok, "event badges by their full names (272 x 272 stars, 216 x 312 cards)");
    }
    {   /* 3, 6 or 7 page grids are stored linear: the right reading is smooth */
        static const char *lin[] = { "PURSUIT_TREE_BG", "SPEED_TREE_BG", "GRID_BLUEPRINT2", "GRID_BLUEPRINT1", "LOGOTRACK33", "GRID" };
        int k;
        for (k = 0; k < 6; k++) {
            double sm = tex_vdiff(hp2_tex_find(&tex, hp2_fe_hash(lin[k])));
            CHECK(sm >= 0.0 && sm < 10.0, "%s reads linear (vertical step %.2f)", lin[k], sm);
        }
    }
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
    CHECK(hp2_tree_parse(F, lfb, real_trees) == 2 && real_trees[0].count == 32 && real_trees[1].count == 32, "two event trees, 32 events each");
    CHECK(real_trees[0].ev[27].nlinks == 4 && real_trees[0].ev[27].links[0] == 22 && real_trees[0].ev[27].links[3] == 25,
          "Championship event 28 needs events 22, 23, 24 and 25");
    CHECK(real_trees[1].ev[31].depth == 9 && real_trees[1].ev[0].depth == 0 && real_trees[1].ev[10].nlinks == 3, "Hot Pursuit: depths 0 to 9, event 11 has three links");
    {
        const hp2_tree_event *w2 = &real_trees[0].ev[1], *w9 = &real_trees[0].ev[8], *w30 = &real_trees[0].ev[29], *h1 = &real_trees[1].ev[0];
        CHECK(w2->race_type == HP2_RT_TIME_TRIAL && w2->ncars == 1 && w2->cars[0] == 43 && w2->nraces == 1 && w2->races[0].track == 32
              && w2->target.kind == 2 && w2->target.value == 185u * HP2_TREE_TICKS && w2->nrewards == 1 && w2->rewards[0].kind == 0 && w2->rewards[0].id == 43,
              "Championship 2: CL55 AMG time trial at Calypso Coast, 3:05, wins the CL55 AMG");
        CHECK(w9->race_type == HP2_RT_TOURNAMENT && w9->nraces == 5 && w9->races[4].track == 23 && w9->races[4].ii == 1 && w9->nopponents == 6,
              "Championship 9: five races ending at Rocky Canyons II, six opponents");
        CHECK(w30->nraces == 1 && w30->races[0].laps == 10 && w30->races[0].track == 65 && w30->nopponents == 7, "Championship 30: ten laps, seven McLaren F1 LMs");
        CHECK(h1->race_type == HP2_RT_DELIVERY && h1->cops == 2 && h1->target.value == 300u * HP2_TREE_TICKS && h1->races[0].track == 13,
              "Hot Pursuit 1: a Delivery at Scenic Drive within 5:00, cop level 2");
        CHECK(real_trees[1].ev[4].race_type == HP2_RT_PURSUIT && real_trees[1].ev[4].cars[0] == 4 && real_trees[1].ev[4].nopponents == 3,
              "Hot Pursuit 5: a Pursuit in the Crown Victoria after three speeders");
    }
    if (exe) {
        size_t xl = 0;
        uint8_t *X = hp2_read_file(exe, &xl);
        hp2_tree_seg seg[200];
        float x, y;
        CHECK(X && hp2_tree_grid(real_trees, X, xl) == 1, "the layout grids in %s, every row = depth", exe);
        CHECK(hp2_tree_pos(&real_trees[1], 6, &x, &y) && x == 8 * 130.0f && y == 2 * 350.0f
              && hp2_tree_pos(&real_trees[0], 6, &x, &y) && x == 9 * 130.0f, "event 6: column 8 (Hot Pursuit), 9 (Championship)");
        CHECK(hp2_tree_segments(&real_trees[0], seg, 200) == 78 && hp2_tree_segments(&real_trees[1], seg, 200) == 78,
              "connectors: 78 + 78 segments, got %d + %d", hp2_tree_segments(&real_trees[0], seg, 200), hp2_tree_segments(&real_trees[1], seg, 200));
        free(X);
    }
    if (bank) {
        size_t bl = 0;
        uint8_t *B = hp2_read_file_part(bank, hp2_jukebox_header_size(), &bl);
        CHECK(B && hp2_jukebox_parse(&jb, B, bl) == 0, "song table of %s", bank);
        CHECK(jb.count == 23 && jb.genres == 2 && !strcmp(jb.name, "InGame"), "23 songs, Rock and Techno");
        CHECK(!strcmp(jb.track[14].artist, "Rush") && !strcmp(jb.track[14].title, "One Little Victory (Inst)")
              && !strcmp(jb.track[14].album, "Vapor Trails"), "song 15: Rush");
        have_jb = jb.count == 23;
        free(B);
    }
    if (Lb && lang.count) test_menu(&L, &lang, have_jb ? &jb : NULL);
    if (Lb && lang.count) test_flow(&L, &lang);
    hp2_fe_free(&L);

    {   /* GLOBALB.BUN's layouts: the song box shown when a song starts */
        hp2_fe_layout GL;
        const hp2_fe_screen *box;
        static hp2_fe_item it[32];
        hp2_jb_track t;
        char q[80];
        int n, k, line = 0, tm = 0, label = 0;
        CHECK(hp2_fe_parse(G, lgb, &GL) == 0, "GLOBALB.BUN layouts");
        box = hp2_fe_find_id(&GL, HP2_SONGBOX_ID);
        CHECK(box && box->count == 10, "song box: 10 widgets");
        memset(&t, 0, sizeof t);
        strcpy(t.title, "Agenda Suicide");
        strcpy(t.artist, "The Faint");
        n = hp2_femenu_songbox(box, &t, q, sizeof q, it, 32);
        for (k = 0; k < n; k++) {
            if (it[k].str && !strcmp(it[k].str, HP2_SONGBOX_LINE)) line = 1;
            if (it[k].str && !strcmp(it[k].str, "TM")) tm = 1;
            if (it[k].str && it[k].str == t.album) label = 1;
        }
        CHECK(n == 8 && !strcmp(q, "\"Agenda Suicide\"") && line && !tm && !label,
              "song box: title in quotes, \"" HP2_SONGBOX_LINE "\", no TM, no album line when there is none (%d)", n);
        strcpy(t.album, "Danse Macabre");
        CHECK(hp2_femenu_songbox(box, &t, q, sizeof q, it, 32) == 9, "song box: an album adds its line");
        hp2_fe_free(&GL);
    }

    if (disc) {
        hp2_zdir z;
        size_t n = 0;
        uint8_t *Z;
        CHECK(hp2_zdir_open(&z, disc) == 0 && z.count == 405, "ZDIR.BIN: 405 entries, got %d", z.count);
        Z = hp2_zdir_load_part(&z, "SOUND\\JUKEBOXM.JBC", hp2_jukebox_header_size(), &n);
        {
            static hp2_jukebox j2;
            CHECK(Z && n == hp2_jukebox_header_size() && hp2_jukebox_parse(&j2, Z, n) == 0 && j2.count == 23,
                  "the song table straight from the archive, %zu bytes read", n);
        }
        free(Z);
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

/* Every event of both trees, one line each, for tests/check_tree.py. */
static int treedump(const char *path)
{
    static hp2_tree t[2];
    size_t n = 0;
    uint8_t *F = hp2_read_file(path, &n);
    int k, i, j;
    if (!F || hp2_tree_parse(F, n, t) != 2) { fprintf(stderr, "no trees in %s\n", path); free(F); return 1; }
    for (k = 0; k < 2; k++)
        for (i = 0; i < t[k].count; i++) {
            const hp2_tree_event *e = &t[k].ev[i];
            printf("%d %d type %d tier %d cops %d cars", k, e->number, e->race_type, e->tier, e->cops);
            for (j = 0; j < e->ncars; j++) printf(" %d", e->cars[j]);
            printf(" races");
            for (j = 0; j < e->nraces; j++) printf(" %d/%d/%d", e->races[j].track, e->races[j].ii, e->races[j].laps);
            printf(" opp");
            for (j = 0; j < e->nopponents; j++) printf(" %d/%d/%d", e->opponents[j].car, e->opponents[j].colour, e->opponents[j].skill);
            printf(" rew");
            for (j = 0; j < e->nrewards; j++) printf(" %d/%d/%d/%u", e->rewards[j].kind, e->rewards[j].id, e->rewards[j].ii, (unsigned)e->rewards[j].value);
            printf(" need");
            for (j = 0; j < e->nlinks; j++) printf(" %d/%d/%u", e->need[j].kind, e->need[j].event, (unsigned)e->need[j].value);
            printf(" goal %d/%d/%u\n", e->target.kind, e->target.event, (unsigned)e->target.value);
        }
    free(F);
    return 0;
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

/* FNV-1a 64 of the first frames stereo frames (int16 little-endian, L R)
 * of song k of a bank, as tests/check_vag.py computes it. */
static int vagdump(const char *bank, int k, int frames)
{
    static hp2_jukebox jb;
    size_t len;
    uint8_t *head = hp2_read_file_part(bank, hp2_jukebox_header_size(), &len);
    hp2_stream st;
    int16_t buf[2 * 1024];
    uint64_t h = 1469598103934665603ULL;
    int got = 0, n, i;
    if (!head || hp2_jukebox_parse(&jb, head, len) != 0 || k < 0 || k >= jb.count
        || hp2_stream_open(&st, bank, jb.track[k].offset) != 0) {
        fprintf(stderr, "cannot read song %d of %s\n", k, bank);
        free(head);
        return 1;
    }
    while (got < frames && (n = hp2_stream_read(&st, buf, frames - got < 1024 ? frames - got : 1024)) > 0) {
        for (i = 0; i < 2 * n; i++) {
            h ^= (uint8_t)buf[i]; h *= 1099511628211ULL;
            h ^= (uint8_t)((uint16_t)buf[i] >> 8); h *= 1099511628211ULL;
        }
        got += n;
    }
    printf("%d %016llx\n", got, (unsigned long long)h);
    hp2_stream_close(&st);
    free(head);
    return 0;
}

int main(int argc, char **argv)
{
    const char *fb = NULL, *gb = NULL, *lf = NULL, *disc = NULL, *bank = NULL, *exe = NULL;
    int i;
    if (argc > 1 && !strcmp(argv[1], "--texdump")) return texdump(argc - 2, argv + 2);
    if (argc == 3 && !strcmp(argv[1], "--treedump")) return treedump(argv[2]);
    if (argc == 5 && !strcmp(argv[1], "--vagdump")) return vagdump(argv[2], atoi(argv[3]), atoi(argv[4]));
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frontb")) fb = argv[i + 1];
        else if (!strcmp(argv[i], "--globalb")) gb = argv[i + 1];
        else if (!strcmp(argv[i], "--lang")) lf = argv[i + 1];
        else if (!strcmp(argv[i], "--disc")) disc = argv[i + 1];
        else if (!strcmp(argv[i], "--jukebox")) bank = argv[i + 1];
        else if (!strcmp(argv[i], "--exe")) exe = argv[i + 1];
    }
    test_tokens();
    test_font();
    test_lang();
    test_tex();
    test_jukebox();
    test_adpcm();
    test_stream();
    test_zdir_hash();
    test_tree();
    if (fb && gb) test_real(fb, gb, lf, disc, bank, exe);
    printf("\nassets: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
