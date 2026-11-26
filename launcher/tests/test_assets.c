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
 *   test_assets --scenedump SCENERY      the placed scenery, per section,
 *                                        for check_scene.py
 *   test_assets --enginedump MSH MSB      an engine bank's samples
 *   test_assets --vagdump BANK SONG FRAMES
 *                                        a hash of the first FRAMES stereo
 *                                        frames of a song, for check_vag.py
 */
#include "../src/hp2_fe.h"
#include "../src/hp2_car.h"
#include "../src/hp2_coll.h"
#include "../src/hp2_drive.h"
#include "../src/hp2_engine.h"
#include "../src/hp2_sfx.h"
#include "../src/hp2_ambient.h"
#include "../src/hp2_tires.h"
#include "../src/hp2_speech.h"
#include "../src/hp2_oneshot.h"
#include "../src/hp2_reverb.h"
#include "../src/hp2_race.h"
#include "../src/hp2_ai.h"
#include "../src/hp2_geom.h"
#include "../src/hp2_scene.h"
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
    {   /* the race's music: only the songs set to On, at the Race Music volume */
        hp2_jbplay rp;
        int saved0 = cfg.song[0], saved1 = cfg.song[1], vol = cfg.volume[HP2_VOL_RACE_MUSIC];
        cfg.song[0] = HP2_PLAY_ON; cfg.song[1] = HP2_PLAY_MENU; cfg.volume[HP2_VOL_RACE_MUSIC] = 50;
        hp2_jbplay_init(&rp, &jb, &cfg, path, 0, 777);
        hp2_jbplay_set_race(&rp, 1);
        CHECK(hp2_jbplay_fill(&rp, buf, 100) == 100 && hp2_jbplay_now(&rp) == 0 && buf[0] == 1 * 50 / 100 + (1 * 50 % 100 ? 0 : 0),
              "race mode: song 1 (On) plays, a Menu song does not, at the race volume (%d)", buf[0]);
        hp2_jbplay_close(&rp);
        cfg.song[0] = HP2_PLAY_MENU;
        hp2_jbplay_init(&rp, &jb, &cfg, path, 0, 777);
        hp2_jbplay_set_race(&rp, 1);
        CHECK(hp2_jbplay_fill(&rp, buf, 100) == 0, "race mode: no song is On, so no music");
        hp2_jbplay_close(&rp);
        cfg.song[0] = saved0; cfg.song[1] = saved1; cfg.volume[HP2_VOL_RACE_MUSIC] = vol;
    }
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
    hp2_femenu_input(&m, HP2_IN_ACCEPT);              /* the question first: Yes */
    hp2_femenu_input(&m, HP2_IN_LEFT);
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
    CHECK(m.popup == 1 && !(r & HP2_FEM_CHANGED) && cfg.volume[HP2_VOL_MENU_MUSIC] != 70, "accept asks first (the POPUP screen), nothing changed yet");
    n = hp2_femenu_items(&m, it, 160);
    CHECK(find_text(it, n, "Default Volumes") >= 1 && find_text(it, n, "Yes") == 1 && find_text(it, n, "No") == 1, "the question shows its title and two buttons");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);              /* No is on by default */
    CHECK(m.popup == 0 && cfg.volume[HP2_VOL_MENU_MUSIC] != 70, "No leaves the volumes alone");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    hp2_femenu_input(&m, HP2_IN_BACK);
    CHECK(m.popup == 0 && m.screen == HP2_FEM_SETTINGS && cfg.volume[HP2_VOL_MENU_MUSIC] != 70, "back closes the question");
    hp2_femenu_input(&m, HP2_IN_ACCEPT);
    hp2_femenu_input(&m, HP2_IN_LEFT);
    r = hp2_femenu_input(&m, HP2_IN_ACCEPT);
    CHECK((r & HP2_FEM_CHANGED) && m.popup == 0 && cfg.volume[HP2_VOL_MENU_MUSIC] == 70, "Yes resets the volumes");
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
        m.laps = 2;
        {
            char ab[48][32];
            int na, k2, same = 0;
            CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == (HP2_FEM_RACE | HP2_FEM_REDRAW) && !m.lap_popup && m.go.track == 11
                  && m.go.laps == 2 && !m.go.reverse && m.go.nopp == HP2_FEM_MAX_OPP && !m.go.event,
                  "accept on SELECT LAPS: a race on track 11, 2 laps, a full grid (%d)", m.go.nopp);
            for (k2 = 0; k2 < m.go.nopp; k2++) same += !strcmp(m.go.opp[k2], m.go.car);
            CHECK(!same && m.go.skin == m.color + 1, "Quick Race: random opponents, none in %s", m.go.car);
            na = hp2_femenu_race_args(&m.go, ab, 48);
            CHECK(na == 8 + 2 * HP2_FEM_MAX_OPP && !strcmp(ab[0], "--track") && !strcmp(ab[1], "11") && !strcmp(ab[3], "2")
                  && !strcmp(ab[8], "--opponent") && strchr(ab[9], 58), "hp2track arguments (%d): %s %s ... %s", na, ab[0], ab[1], ab[9]);
            m.lap_popup = 1;
        }
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
        {   /* a fresh save: Quick Race still has cars and courses to pick */
            hp2_progress fresh;
            hp2_progress_reset(&fresh);
            m.trees = real_trees;
            m.prog = &fresh;
            printf("fresh save: %d cars, %d courses open\n", hp2_femenu_open_cars(&m), hp2_femenu_open_tracks(&m));
            CHECK(hp2_femenu_open_cars(&m) >= 1 && hp2_femenu_open_tracks(&m) >= 1, "a fresh save has a car and a course open");
            m.prog = NULL;
        }
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
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_EVENT_INFO && find_text(it, n, "Event 4 Objective:") == 1 && find_text(it, n, "Finish 2nd or better") == 1
              && find_text(it, n, "Desert Heat") == 1 && find_text(it, n, "Opel Speedster") == 1 && find_text(it, n, "No reward") == 1
              && find_text(it, n, "3") == 1 && !strcmp(m.help, "Australia vs Germany Desert Knockout"),
              "accept on Handling: event 4's details (objective, track, car, no reward), its title in the bar");
        CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_RACE && m.screen == HP2_FEM_EVENT_INFO && m.go.event == 4
              && m.go.tree_kind == 1 && !strcmp(m.go.car, "OPEL"),
              "accept: event 4's race in the Opel (track %d%s, %d laps, %d opponents)", m.go.track, m.go.reverse ? " II" : "", m.go.laps, m.go.nopp);
        {
            const hp2_tree_event *e4 = &m.trees[1].ev[3];
            int k2, ok2 = m.go.track == e4->races[0].track && m.go.laps == e4->races[0].laps && m.go.reverse == e4->races[0].ii
                          && m.go.nopp == e4->nopponents;
            for (k2 = 0; ok2 && k2 < m.go.nopp; k2++) ok2 = m.go.opp_skill[k2] == e4->opponents[k2].skill;
            CHECK(ok2, "its track, laps, direction and opponents are the event's own");
        }
        {   /* a race of event 4 ("Finish 2nd or better") ends: place, objective */
            hp2_race_result rr = { 1, 2, 4000L * 83, 4000L * 83 };
            int met2, n2;
            hp2_fe_item it2[160];
            met2 = hp2_femenu_result(&m, &rr);
            n2 = hp2_femenu_items(&m, it2, 160);
            CHECK(met2 == 1 && find_text(it2, n2, "You finished 2nd, 1:23.00") == 1 && find_text(it2, n2, "Objective complete") == 1,
                  "2nd in event 4: the finish and the objective met are on the details");
            rr.place = 3;
            met2 = hp2_femenu_result(&m, &rr);
            n2 = hp2_femenu_items(&m, it2, 160);
            CHECK(met2 == 0 && find_text(it2, n2, "You finished 3rd, 1:23.00") == 1 && find_text(it2, n2, "Objective not met") == 1
                  && find_text(it2, n2, "Objective complete") == 0, "3rd: not met");
            rr.finished = 0;
            met2 = hp2_femenu_result(&m, &rr);
            n2 = hp2_femenu_items(&m, it2, 160);
            CHECK(met2 == 0 && !m.result[0][0] && !m.result[1][0] && find_text(it2, n2, "Objective not met") == 0,
                  "a race left unfinished leaves no result lines");
        }
        {   /* event 4 is a Race Knockout of four cars: the last place of each race is out */
            hp2_race_result rr = { 1, 2, 4000L * 83, 4000L * 83, 4, { 1, 0, 2, 3 } };
            hp2_fe_item it2[160];
            int n2, met2;
            CHECK(m.go.race_type == HP2_RT_KNOCKOUT && m.go.nopp == 3 && m.go.round == 0, "event 4: three opponents, race 1");
            met2 = hp2_femenu_result(&m, &rr);
            n2 = hp2_femenu_items(&m, it2, 160);
            CHECK(met2 == 0 && !m.ser.done && m.ser.round == 1 && !m.ser.alive[m.go.opp_id[2]] && strstr(m.result[0], "advance")
                  && find_text(it2, n2, m.result[0]) == 1 && !m.result[1][0], "2nd of four: the last car is out, you advance");
            CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_RACE && m.go.nopp == 2 && m.go.round == 1, "the next race has two opponents");
            rr.norder = 3; rr.order[0] = 0; rr.order[1] = 1; rr.order[2] = 2; rr.place = 1;
            met2 = hp2_femenu_result(&m, &rr);
            CHECK(met2 == 0 && !m.ser.done && m.ser.round == 2, "three left, one out again");
            CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_RACE && m.go.nopp == 1, "the last race: one opponent");
            rr.norder = 2; rr.order[0] = 0; rr.order[1] = 1; rr.place = 1;
            met2 = hp2_femenu_result(&m, &rr);
            CHECK(met2 == 1 && m.ser.done && strstr(m.result[0], "1st") && !strcmp(m.result[1], "Objective complete"),
                  "first of the last two: the Knockout is won, objective met");
            CHECK(hp2_femenu_input(&m, HP2_IN_ACCEPT) == HP2_FEM_RACE && m.go.nopp == 3 && m.go.round == 0 && !m.ser.done,
                  "accept after it is decided: a fresh Knockout");
            rr.norder = 4; rr.order[0] = 1; rr.order[1] = 2; rr.order[2] = 3; rr.order[3] = 0; rr.place = 4;
            met2 = hp2_femenu_result(&m, &rr);
            CHECK(met2 == 0 && m.ser.done && strstr(m.result[0], "knocked out") && strstr(m.result[0], "4th")
                  && !strcmp(m.result[1], "Objective not met"), "last in the first race: knocked out, 4th, not met");
            hp2_femenu_input(&m, HP2_IN_ACCEPT);
        }
        {   /* the course the other way round: Up/Down in the lap popup, once it is open */
            static hp2_femenu ta;
            ta = m;
            ta.prog = NULL; ta.quick_mode = HP2_QUICK_NONE; ta.alt = 0; ta.lap_popup = 1; ta.screen = HP2_FEM_SELECT_TRACK;
            CHECK(hp2_femenu_input(&ta, HP2_IN_DOWN) && ta.alt == 1, "lap popup: Down runs the course the other way round");
            hp2_femenu_input(&ta, HP2_IN_ACCEPT); ta.lap_popup = 1;
            CHECK(ta.go.reverse == 1, "and the race asks for it");
            hp2_femenu_input(&ta, HP2_IN_UP);
            CHECK(ta.alt == 0, "Up turns it off again");
            ta.lap_popup = 0; ta.alt = 1; hp2_femenu_input(&ta, HP2_IN_RIGHT);
            CHECK(ta.alt == 0, "another course starts in its usual direction");
        }
        {   /* the Challenge's Race Knockout: a series, the last car out each race */
            static hp2_femenu tk;
            hp2_race_result rr;
            tk = m;
            tk.prog = NULL; tk.quick_mode = 0; tk.race_type = 2; tk.setup_val[1] = 3; tk.car = 0; tk.ser.done = 1; tk.ser.nrounds = 0;
            tk.screen = HP2_FEM_SELECT_TRACK; tk.lap_popup = 0; tk.laps = 2; tk.setup_val[7] = 1;
            hp2_femenu_input(&tk, HP2_IN_ACCEPT);
            CHECK(hp2_femenu_input(&tk, HP2_IN_ACCEPT) == (HP2_FEM_RACE | HP2_FEM_REDRAW) && tk.go.race_type == HP2_RT_KNOCKOUT && tk.go.nopp == 4
                  && tk.go.opp_id[0] == 1 && tk.go.opp_id[3] == 4 && tk.go.event == 0, "Challenge Knockout: a series of four opponents");
            memset(&rr, 0, sizeof rr);
            rr.finished = 1; rr.place = 2; rr.norder = 5; rr.ticks = 1; rr.best = 1;
            rr.order[0] = 1; rr.order[1] = 0; rr.order[2] = 2; rr.order[3] = 3; rr.order[4] = 4;
            hp2_femenu_result(&tk, &rr);
            CHECK(tk.ser.round == 1 && !tk.ser.done && !tk.ser.alive[4] && tk.ser.alive[3] && strstr(tk.result[0], "advance"), "the last car is out, you advance");
            hp2_femenu_input(&tk, HP2_IN_ACCEPT);
            hp2_femenu_input(&tk, HP2_IN_ACCEPT);
            CHECK(tk.go.nopp == 3 && tk.go.opp_id[2] == 3, "the next race has the cars still in");
            rr.norder = 4; rr.place = 4;
            rr.order[0] = 1; rr.order[1] = 2; rr.order[2] = 3; rr.order[3] = 0;
            hp2_femenu_result(&tk, &rr);
            CHECK(tk.ser.done && strstr(tk.result[0], "knocked out"), "last yourself: knocked out, the series is over");
            hp2_femenu_input(&tk, HP2_IN_ACCEPT);
            hp2_femenu_input(&tk, HP2_IN_ACCEPT);
            CHECK(tk.go.nopp == 4 && tk.ser.round == 0, "and the next one starts afresh");
        }
        {   /* a Lap Knockout event asks hp2track for the knockout; a plain one does not */
            static hp2_femenu t3;
            char ab3[48][32];
            const hp2_tree_event *e3 = &m.trees[0].ev[2];
            int na3, k3, has3 = 0;
            t3 = m;
            t3.tree_kind = 0; t3.tree_ev = 2; t3.ev_ncars = 1; t3.ev_cars[0] = e3->cars[0]; t3.car = 0; t3.ser.done = 1;
            t3.screen = HP2_FEM_EVENT_INFO;
            CHECK(e3->race_type == HP2_RT_LAP_KNOCKOUT && hp2_femenu_input(&t3, HP2_IN_ACCEPT) == HP2_FEM_RACE, "Championship event 3: a Lap Knockout");
            na3 = hp2_femenu_race_args(&t3.go, ab3, 48);
            for (k3 = 0; k3 < na3; k3++) has3 |= !strcmp(ab3[k3], "--lap-knockout");
            CHECK(has3, "its hp2track arguments include --lap-knockout");
            t3.tree_ev = 0; t3.ev_cars[0] = m.trees[0].ev[0].cars[0];
            hp2_femenu_input(&t3, HP2_IN_ACCEPT);
            na3 = hp2_femenu_race_args(&t3.go, ab3, 48);
            has3 = 0;
            for (k3 = 0; k3 < na3; k3++) has3 |= !strcmp(ab3[k3], "--lap-knockout");
            CHECK(!has3, "a Single Race does not");
            {   /* a Pursuit event (Hot Pursuit 5): the cop car, --pursuit, and the time against the goal */
                static hp2_femenu t4;
                const hp2_tree_event *e5 = &m.trees[1].ev[4];
                hp2_race_result pr = { 1, 0, 4000L * 100, 0, 0, { 0 } };
                int na4, k4, has4 = 0;
                t4 = m;
                t4.tree_kind = 1; t4.tree_ev = 4; t4.ev_ncars = 1; t4.ev_cars[0] = e5->cars[0]; t4.car = 0; t4.ser.done = 1;
                t4.screen = HP2_FEM_EVENT_INFO;
                CHECK(e5->race_type == HP2_RT_PURSUIT && hp2_femenu_input(&t4, HP2_IN_ACCEPT) == HP2_FEM_RACE, "Hot Pursuit event 5: a Pursuit");
                na4 = hp2_femenu_race_args(&t4.go, ab3, 48);
                for (k4 = 0; k4 < na4; k4++) has4 |= !strcmp(ab3[k4], "--pursuit");
                CHECK(has4, "its hp2track arguments include --pursuit");
                CHECK(e5->target.kind == 2 && hp2_femenu_result(&t4, &pr) == 1 && !strcmp(t4.result[0], "All speeders arrested in 1:40.00")
                      && !strcmp(t4.result[1], "Objective complete"), "arrested all in 1:40: within the goal");
                pr.ticks = 4000L * 200;
                CHECK(hp2_femenu_result(&t4, &pr) == 0 && !strcmp(t4.result[1], "Objective not met"), "in 3:20: not met");
            }
            {   hp2_race_result ko = { 1, 3, 0, 0, 5, { 1, 2, 0, 3, 4 } };
                hp2_femenu_result(&t3, &ko);
                CHECK(!strcmp(t3.result[0], "You finished 3rd"), "knocked out of a Lap Knockout: place, no time");
            }
        }
        {   /* Championship event 6 is a Tournament of three races and eight cars: points */
            static hp2_femenu t2;
            const hp2_tree_event *e6 = &m.trees[0].ev[5];
            hp2_race_result rr = { 1, 2, 4000L * 83, 4000L * 83, 8, { 1, 0, 2, 3, 4, 5, 6, 7 } };
            int ok6, k6;
            t2 = m;
            t2.tree_kind = 0; t2.tree_ev = 5; t2.ev_ncars = 1; t2.ev_cars[0] = e6->cars[0]; t2.car = 0; t2.ser.done = 1;
            t2.screen = HP2_FEM_EVENT_INFO;
            ok6 = e6->race_type == HP2_RT_TOURNAMENT && e6->nraces == 3 && e6->nopponents == 7;
            CHECK(ok6 && hp2_femenu_input(&t2, HP2_IN_ACCEPT) == HP2_FEM_RACE && t2.go.nopp == 7 && t2.go.round == 0
                  && t2.go.track == e6->races[0].track, "event 6: a Tournament, race 1 on its first course, all seven opponents");
            CHECK(ok6 && hp2_femenu_result(&t2, &rr) == 0 && !t2.ser.done && t2.ser.points[0] == 8 && t2.ser.points[1] == 10
                  && strstr(t2.result[0], "Race 1 of 3") && strstr(t2.result[0], "2nd"), "2nd in race 1: 8 points, the winner 10");
            CHECK(ok6 && hp2_femenu_input(&t2, HP2_IN_ACCEPT) == HP2_FEM_RACE && t2.go.nopp == 7 && t2.go.round == 1
                  && t2.go.track == e6->races[1].track && t2.go.reverse == e6->races[1].ii, "race 2 on the second course, nobody is out");
            for (k6 = 0; k6 < 2 && ok6; k6++) {
                rr.order[0] = 0; rr.order[1] = 1; rr.order[2] = 2; rr.place = 1;
                hp2_femenu_result(&t2, &rr);
                if (k6 == 0) hp2_femenu_input(&t2, HP2_IN_ACCEPT);
            }
            CHECK(ok6 && t2.ser.done && t2.ser.points[0] == 28 && t2.ser.points[1] == 26 && strstr(t2.result[0], "Tournament over: 1st with 28 points"),
                  "two wins after a 2nd: 28 points to 26, first overall");
        }
        hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_SELECT_CAR && m.car_popup == 2 && m.ev_mode, "back: Handling again");
        hp2_femenu_input(&m, HP2_IN_BACK); hp2_femenu_input(&m, HP2_IN_BACK); hp2_femenu_input(&m, HP2_IN_BACK);
        CHECK(m.screen == HP2_FEM_TREE && m.tree_ev == 3 && !m.ev_mode && !m.car_popup, "back, back, back: the event list on event 4");
        hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(!strcmp(hp2_femenu_car_name(&m, 0), "Pursuit Crown Victoria"), "event 5: the Pursuit Crown Victoria");
        for (k = 0; k < 3; k++) hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(m.screen == HP2_FEM_EVENT_INFO && find_text(it, n, "Arrest all of the speeders in 2:00 or less") == 1
              && find_text(it, n, "Coastal Parklands") == 1, "event 5: arrest the speeders in 2:00");
        for (k = 0; k < 3; k++) hp2_femenu_input(&m, HP2_IN_BACK);
        hp2_femenu_input(&m, HP2_IN_BACK);
        for (k = 0; k < 12; k++) hp2_femenu_input(&m, HP2_IN_DOWN);
        hp2_femenu_input(&m, HP2_IN_ACCEPT);
        CHECK(m.tree_ev == 16 && !strcmp(hp2_femenu_car_name(&m, 0), "Ferrari 360 Modena"), "event 17: the 360 Modena Challenge, named by the port");
        for (k = 0; k < 3; k++) hp2_femenu_input(&m, HP2_IN_ACCEPT);
        n = hp2_femenu_items(&m, it, 160);
        CHECK(find_text(it, n, "Make your delivery in 6:10 or less") == 1 && find_text(it, n, "Autumn Crossing") == 2,
              "event 17: deliver within 6:10 at Autumn Crossing, which is also the reward");
        for (k = 0; k < 3; k++) hp2_femenu_input(&m, HP2_IN_BACK);
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


#define G_TEST 9.81f

/* A 3,840 m square of road, one polygon listed in every 32 m cell. */
static uint8_t *big_floor(size_t *len)
{
    const int N = 120;
    size_t cells = 12 + 12 * (size_t)N * N, n = 8 + cells + 8 + 32 + 8 + 2 * (size_t)N * N;
    uint8_t *b = (uint8_t *)calloc(1, n), *q = b, *p;
    static const int X[4] = { -1920, 1920, 1920, -1920 }, Y[4] = { -1920, -1920, 1920, 1920 };
    int i, j, k;
    if (!b) return NULL;
    set32(q, 0x34131);
    set32(q + 4, (uint32_t)cells);
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            uint8_t *r = q + 8 + 12 + 12 * (i * N + j);
            setf(r, -1920 + 16 + 32 * i);
            setf(r + 4, -1920 + 16 + 32 * j);
            set16(r + 8, (unsigned)(i * N + j));
            set16(r + 10, 1);
        }
    q += 8 + cells;
    set32(q, 0x34132);
    set32(q + 4, 32);
    p = q + 8;
    set16(p + 2, 0x1001);
    for (k = 0; k < 4; k++) { set16(p + 8 + 2 * k, (unsigned)(X[k] * 8) & 0xFFFF); set16(p + 16 + 2 * k, (unsigned)(Y[k] * 8) & 0xFFFF); }
    q += 8 + 32;
    set32(q, 0x34133);
    set32(q + 4, 2 * (uint32_t)N * N);
    *len = n;
    return b;
}

/* The game's tyres on real cars: steady turns, braking, a light car. */
static void test_tyres_real(const hp2_carrec *recs, int n)
{
    size_t len = 0;
    uint8_t *fl = big_floor(&len);
    hp2_coll c;
    hp2_drive d;
    hp2_drive_in in;
    const hp2_carrec *p911 = hp2_cardb_find(recs, n, "911TURBO"), *f50 = hp2_cardb_find(recs, n, "F50");
    float t, x0, ay = 0, slip;
    if (!fl || !p911 || !f50 || hp2_coll_parse(&c, fl, len) < 0) { CHECK(0, "tyre test set-up"); free(fl); return; }
    CHECK(NEAR(p911->tyre[0][0], 12) && NEAR(p911->tyre[0][4], 1.47f) && NEAR(p911->tyre[1][4], 1.59f)
          && NEAR(p911->brakes[1], 1.18f) && NEAR(p911->inertia[2], 6.318f), "911TURBO's tyre, brake and yaw fields");
    /* 60-0 mph */
    hp2_drive_init(&d, p911, -1500, 0, 0, 0);
    memset(&in, 0, sizeof in);
    in.throttle = 1;
    for (t = 0; t < 10 && d.speed < 26.82f; t += 0.01f) hp2_drive_step(&d, &c, &in, 0.01f);
    x0 = d.pos[0];
    in.throttle = 0;
    in.brake = 1;
    for (t = 0; t < 10 && d.speed > 0.3f; t += 0.01f) hp2_drive_step(&d, &c, &in, 0.01f);
    CHECK(d.pos[0] - x0 > 15 && d.pos[0] - x0 < 40 && fabsf(d.pos[1]) < 0.5f, "911: 60-0 mph in %.1f m, straight", d.pos[0] - x0);
    /* a steady turn at 25 m/s: held to the sliding friction, not spinning */
    hp2_drive_init(&d, p911, -1500, 0, 0, 0);
    memset(&in, 0, sizeof in);
    for (t = 0; t < 12; t += 0.01f) {
        in.throttle = d.speed < 25 ? 1.0f : 0.0f;
        in.steer = t > 4 ? 0.7f : 0;
        hp2_drive_step(&d, &c, &in, 0.01f);
        if (t > 11) ay = fabsf(d.yawrate * d.speed) / G_TEST;
    }
    slip = fabsf(-d.vel[0] * sinf(d.heading) + d.vel[1] * cosf(d.heading));
    CHECK(ay > 1.2f && ay < 1.7f && slip < 2 && d.yawrate < 0, "911 turning right at %.1f g, sliding sideways %.1f m/s", ay, slip);
    hp2_drive_init(&d, f50, -1500, 0, 0, 0);
    memset(&in, 0, sizeof in);
    for (t = 0; t < 12; t += 0.01f) {
        in.throttle = d.speed < 25 ? 1.0f : 0.0f;
        in.steer = t > 4 ? -1.0f : 0;
        hp2_drive_step(&d, &c, &in, 0.01f);
    }
    slip = fabsf(-d.vel[0] * sinf(d.heading) + d.vel[1] * cosf(d.heading));
    CHECK(d.speed > 15 && slip < 10 && d.yawrate > 0, "F50 at full lock left: %.0f m/s, drifting %.1f m/s, not spun", d.speed, slip);
    /* an opponent round a 2 km square: two laps, never stuck */
    {
        static const float sq[] = { 0, 0, 0, 500, 0, 500, 500, 500, 1000, 0, 500, 1500 };
        hp2_race r;
        hp2_ai ai;
        hp2_ai_line l;
        int k, stuck = 0;
        hp2_race_init(&r, sq, 4, 1, 2000, 2);
        r.countdown = 0;
        CHECK(hp2_ai_line_build(&l, &r, &c) == 0 && l.len > 1700 && l.len < 2300, "line round the square: %.0f m", l.len);
        memset(&ai, 0, sizeof ai);
        ai.skill = 0.95f;
        ai.at = -1;
        hp2_drive_init(&d, p911, 0, 0, 0, 0);
        hp2_race_step(&r, 0.01f, 0, 0, 0, 0);
        for (k = 0, t = 0; t < 200 && r.state != HP2_RACE_FINISHED; t += 0.02f, k++) {
            hp2_ai_drive(&ai, &r, &l, &d, 0.02f, &in);
            if (ai.reverse > 1.19f) stuck++;
            hp2_drive_step(&d, &c, &in, 0.02f);
            hp2_race_step(&r, 0.02f, d.pos[0], d.pos[1], d.vel[0], d.vel[1]);
        }
        CHECK(r.state == HP2_RACE_FINISHED && r.clock < 180 && !stuck, "911 opponent: two laps in %.1f s, stuck %d times", r.clock, stuck);
        hp2_ai_line_free(&l);
    }
    hp2_coll_free(&c);
    free(fl);
}

static void test_cardb_real(const uint8_t *G, size_t lgb)
{
    static hp2_carrec recs[HP2_CARDB_MAX];
    int n = hp2_cardb_parse(G, lgb, recs, HP2_CARDB_MAX);
    const hp2_carrec *c = n > 0 ? hp2_cardb_find(recs, n, "911TURBO") : NULL;
    CHECK(n == 64, "car database: %d records", n);
    CHECK(c && !strcmp(c->model, "911TURBO") && !strcmp(c->path, "CARS\\911TURBO\\GEOMETRY.BIN"), "911TURBO's model");
    /* the real 996 Turbo: gears 3.82 .. 0.75, final drive 3.44 (docs/formats.md 12) */
    CHECK(c && c->gears == 6 && NEAR(c->ratio[0], 3.82f) && NEAR(c->ratio[5], 0.75f) && NEAR(c->final_drive, 3.44f)
          && NEAR(c->top_speed, 191) && NEAR(c->wheel[0].x, 1.259f) && NEAR(c->wheel[0].radius, 0.319f), "911TURBO's record");
    /* the drivetrain fields (docs/formats.md 12) */
    CHECK(c && NEAR(c->mass, 1.65f) && NEAR(c->efficiency, 0.8f) && NEAR(c->final_front, 3.44f) && NEAR(c->rear_share, 0.6f)
          && fabsf(c->drag - 0.000487f) < 1e-6f && NEAR(c->torque[0], 0.57f) && NEAR(c->torque[8], 0.4f), "911TURBO's drivetrain");
    if (c) {
        hp2_drive dv;
        float a1, a6;
        hp2_drive_init(&dv, c, 0, 0, 0, 0);
        a1 = hp2_drive_accel(&dv, 10, 1);
        a6 = hp2_drive_accel(&dv, 85, 6);
        /* first gear pulls harder than the tyres could take; at 190 mph in
         * sixth the drag has caught up with the engine */
        CHECK(a1 > 12 && a1 < 20 && fabsf(a6) < 0.6f, "911TURBO pulls %.1f m/s2 in 1st at 10 m/s, %.2f in 6th at 85", a1, a6);
        CHECK(NEAR(hp2_drive_torque(&dv, 950), 0.57f) && NEAR(hp2_drive_torque(&dv, 7400), 0.4f) && hp2_drive_torque(&dv, 9000) == 0,
              "torque curve from idle to redline");
    }
    test_tyres_real(recs, n);
    c = hp2_cardb_find(recs, n, "SPIDERHP");
    CHECK(c && !strcmp(c->model, "CHALLENGE"), "SPIDERHP uses the CHALLENGE model");
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
    test_cardb_real(G, lgb);
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
    {   /* progress: an event opens when a link's condition has been met */
        static hp2_progress pg;
        char pbuf[2048];
        hp2_progress q;
        int ev, locked = 0, opened = 0, first = 0;
        hp2_progress_reset(&pg);
        CHECK(hp2_progress_open(&pg, 0, &real_trees[0], 1), "progress: event 1 is open at the start");
        for (ev = 2; ev <= 32; ev++) if (!hp2_progress_open(&pg, 0, &real_trees[0], ev)) locked++;
        CHECK(locked > 20, "progress: most Championship events start locked");
        for (ev = 2; ev <= 32 && !first; ev++) {
            const hp2_tree_event *e = &real_trees[0].ev[ev - 1];
            if (e->nlinks == 1 && e->links[0] == 1) first = ev;
        }
        CHECK(first > 0, "progress: some event hangs off event 1");
        if (first) {
            const hp2_tree_cond *c = &real_trees[0].ev[first - 1].need[0];
            int pl = c->kind == 1 ? (int)c->value : 8;
            long tk = c->kind == 2 ? (long)c->value : 0;
            CHECK(!hp2_progress_open(&pg, 0, &real_trees[0], first), "progress: it is locked before event 1");
            hp2_progress_record(&pg, 0, 1, pl + 1 > 8 ? 8 : pl + 1, tk ? tk + 1 : 0);
            if (c->kind) CHECK(!hp2_progress_open(&pg, 0, &real_trees[0], first), "progress: a worse result leaves it locked");
            hp2_progress_record(&pg, 0, 1, pl, tk);
            opened = hp2_progress_open(&pg, 0, &real_trees[0], first);
            CHECK(opened, "progress: meeting the link's condition opens it");
            CHECK(!hp2_progress_open(&pg, 1, &real_trees[1], first) || real_trees[1].ev[first - 1].nlinks != 1,
                  "progress: the two careers are kept apart");
        }
        {   /* rewards: earned by the reward's own place or time */
            int t, n = 0, got0 = 0;
            for (t = 0; t < 2; t++) for (ev = 1; ev <= real_trees[t].count; ev++) {
                const hp2_tree_event *e = &real_trees[t].ev[ev - 1];
                int j;
                for (j = 0; j < e->nrewards; j++) { n++; got0 += hp2_progress_reward_got(&pg, t, e, &e->rewards[j]); }
            }
            CHECK(n > 10, "progress: the trees hand out rewards");
            {   const hp2_tree_event *e = &real_trees[0].ev[0];
                hp2_progress fresh;
                hp2_progress_reset(&fresh);
                CHECK(e->nrewards == 0 || !hp2_progress_reward_got(&fresh, 0, e, &e->rewards[0]), "progress: a reward is not earned before the race");
                if (e->nrewards > 0) {
                    hp2_progress_record(&fresh, 0, 1, 1, 1);
                    CHECK(hp2_progress_reward_got(&fresh, 0, e, &e->rewards[0]) || e->rewards[0].value > 8, "progress: a win earns the event's place reward");
                }
            }
            (void)got0;
        }
        CHECK(hp2_progress_write(&pg, pbuf, sizeof pbuf) < sizeof pbuf && hp2_progress_parse(&q, pbuf) == 1 &&
              q.played[0][0] && q.place[0][0] == pg.place[0][0] && q.ticks[0][0] == pg.ticks[0][0], "progress: the save text round trips");
    }
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


/* ---- models and scenery -------------------------------------------------- */

static uint8_t *put_chunk(uint8_t *p, uint32_t id, const uint8_t *body, uint32_t n)
{
    set32(p, id);
    set32(p + 4, n);
    if (body) memcpy(p + 8, body, n);
    return p + 8 + n;
}

/* One part, "BOX_A", with one batch: a 5-vertex strip (3 triangles). */
static size_t make_model(uint8_t *out)
{
    uint8_t part[1024], vif[512], hdr[64], tex[8], rec[64], *q;
    float pos[5][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 2, 1 } };
    int i, k;
    size_t n;
    memset(hdr, 0, sizeof hdr);
    set32(hdr + 8, hp2_geom_hash("BOX_A"));
    set32(hdr + 12, 0x0B);
    memcpy(hdr + 16, "BOX_A", 5);
    memset(tex, 0, sizeof tex);
    set32(tex, hp2_geom_hash("T_BOX"));
    memset(rec, 0, sizeof rec);
    rec[29] = 5;
    rec[30] = 3;
    rec[31] = 0xFF;
    memset(vif, 0, sizeof vif);
    q = vif + 8;                                        /* the 8-byte lead */
    set32(q, 0x6E010000); q += 4;                       /* V4_8 x1 at 0 */
    q[0] = 5; q[1] = 1; q[2] = 15; q[3] = 0xFC; q += 4;
    set32(q, 0x6C010001); q += 4;                       /* V4_32 x1 at 1 */
    set32(q, 0x6000); q += 16;                          /* vertices 0, 1 start the strip */
    set32(q, 0x6C030002); q += 4;                       /* V4_32 x3 at 2: x0..3 y0..3 z0..3 */
    for (k = 0; k < 3; k++)
        for (i = 0; i < 4; i++) { setf(q, pos[i][k]); q += 4; }
    set32(q, 0x68010005); q += 4;                       /* V3_32 x1 at 5: the leftover */
    for (k = 0; k < 3; k++) { setf(q, pos[4][k]); q += 4; }
    set32(q, 0x14000000); q += 4;                       /* MSCAL */
    n = 0;
    q = put_chunk(part, 0x34003, hdr, sizeof hdr);
    q = put_chunk(q, 0x34006, tex, sizeof tex);
    q = put_chunk(q, 0x34004, rec, sizeof rec);
    q = put_chunk(q, 0x34005, vif, (uint32_t)((vif + 8 + 4 + 4 + 4 + 16 + 4 + 48 + 4 + 12 + 4) - vif));
    n = (size_t)(q - part);
    q = put_chunk(out + 8, 0x80034002, part, (uint32_t)n);
    set32(out, 0x80034000);
    set32(out + 4, (uint32_t)(q - out - 8));
    return (size_t)(q - out);
}

/* A section placing model 0 once at (100, 0, 0) turned 90 degrees. */
static size_t make_section(uint8_t *out, int num, float x)
{
    uint8_t body[512], hdr[56], mod[40], inst[48], *q;
    memset(hdr, 0, sizeof hdr);
    set32(hdr + 8, (uint32_t)num);
    memset(mod, 0, sizeof mod);
    set32(mod, hp2_geom_hash("BOX_A"));
    memset(inst, 0, sizeof inst);
    setf(inst + 16, x);
    set16(inst + 28 + 2, 0x4000);                       /* row 0 = +y */
    set16(inst + 28 + 6, 0xC000);                       /* row 1 = -x */
    set16(inst + 28 + 16, 0x4000);                      /* row 2 = +z */
    q = put_chunk(body, 0x34101, hdr, sizeof hdr);
    q = put_chunk(q, 0x34102, mod, sizeof mod);
    q = put_chunk(q, 0x34103, inst, sizeof inst);
    return (size_t)(put_chunk(out, 0x80034100, body, (uint32_t)(q - body)) - out);
}

static void test_geom(void)
{
    static uint8_t f[4096];
    size_t n = make_model(f), n2;
    hp2_partset ps = { 0 };
    hp2_mesh m;
    hp2_scene s;
    const hp2_part *p;
    uint8_t comp[64], *dec;
    printf("models and scenery (synthetic)\n");
    CHECK(hp2_geom_hash("") == 0xFFFFFFFFu, "hash of nothing");
    CHECK(hp2_geom_scan(&ps, f, n) == 1, "one part");
    p = hp2_geom_find(&ps, hp2_geom_hash("BOX_A"));
    CHECK(p && !strcmp(p->name, "BOX_A") && p->ntex == 1 && p->nrec == 1, "part found by hash");
    CHECK(!hp2_geom_find(&ps, 1234), "no such part");
    if (p && hp2_geom_decode(p, &m) == 0) {
        CHECK(m.nb == 1 && m.nv == 9 && !m.bad && !m.wrong, "one batch, 3 triangles: %d %d %d %d", m.nb, m.nv, m.bad, m.wrong);
        CHECK(m.nb == 1 && m.b[0].tex == hp2_geom_hash("T_BOX") && m.b[0].mat == 0, "texture from the record");
        /* strip 0 1 2 3 4: (0 1 2) (2 1 3) (2 3 4) */
        CHECK(m.nv == 9 && NEAR(m.v[3].x, 0) && NEAR(m.v[3].y, 1) && NEAR(m.v[4].x, 1) && NEAR(m.v[4].y, 0)
              && NEAR(m.v[5].x, 1) && NEAR(m.v[5].y, 1), "second triangle wound the other way");
        CHECK(m.nv == 9 && NEAR(m.v[8].y, 2) && NEAR(m.v[8].z, 1), "leftover vertex from its own block");
        CHECK(m.nv == 9 && m.v[0].c[0] == 128, "no colours: full brightness (128)");
        hp2_mesh_free(&m);
    } else {
        CHECK(0, "decode");
    }
    hp2_geom_free(&ps);

    n += make_section(f + n, 41, 100);
    n += make_section(f + n, 42, 100);                  /* the same record again */
    n += make_section(f + n, 43, -50);
    CHECK(hp2_scene_build(&s, f, n, 0) == 0, "scene builds");
    CHECK(s.nsec == 3 && s.inst == 3 && s.placed == 2 && s.dup == 1 && s.tris == 6,
          "3 sections, 2 placed, 1 repeat: %d %d %d %d %ld", s.nsec, s.inst, s.placed, s.dup, s.tris);
    if (s.nsec == 3 && s.sec[0].ng == 1) {
        const hp2_vert *v = s.sec[0].g[0].v;
        /* (1, 0, 0) -> 100 + 1 * row 0 = (100, 1, 0); (0, 1, 0) -> (99, 0, 0) */
        CHECK(NEAR(v[1].x, 100) && NEAR(v[1].y, 1) && NEAR(v[2].x, 99) && NEAR(v[2].y, 0), "placed with the matrix");
        CHECK(s.sec[0].num == 41 && s.sec[1].ng == 0 && NEAR(s.sec[0].box[0], 98) && NEAR(s.sec[0].box[4], 1),
              "section box %g %g", s.sec[0].box[0], s.sec[0].box[4]);
        CHECK(NEAR(s.box[0], -52) && NEAR(s.box[3], 100), "track box");
    }
    hp2_scene_free(&s);
    CHECK(hp2_scene_build(&s, f, 100, 0) < 0, "no sections: fails");

    /* COMP: "abcabcabc!" as 3 literals, a match of 6 back 3, a literal */
    memset(comp, 0, sizeof comp);
    memcpy(comp, "COMP", 4);
    comp[4] = 1; comp[5] = 16;
    set32(comp + 8, 10);
    set32(comp + 12, 16 + 2 + 3 + 2 + 1);
    set16(comp + 16, 0x8);
    memcpy(comp + 18, "abc", 3);
    comp[21] = 0x03; comp[22] = 3;                      /* length 3 + 3, distance 3 */
    comp[23] = '!';
    dec = hp2_comp_decode(comp, 24, &n2);
    CHECK(dec && n2 == 10 && !memcmp(dec, "abcabcabc!", 10), "COMP decodes");
    free(dec);
    comp[22] = 9;
    CHECK(!hp2_comp_decode(comp, 24, &n2), "COMP with a bad distance fails");
}


/* ---- collision, the car database and the stand-in driving model ---------- */

/* A 64 x 64 floor at z = 0 (two triangles' worth as one quad) and an
 * invisible wall along x = 48, in four 32-unit cells. */
static size_t make_coll(uint8_t *out)
{
    uint8_t poly[64], idx[16], cells[12 + 4 * 12], *q;
    static const int fx[4] = { 0, 64, 64, 0 }, fy[4] = { 0, 0, 64, 64 };
    int k;
    memset(poly, 0, sizeof poly);
    set16(poly + 2, 0x1001);                            /* asphalt */
    for (k = 0; k < 4; k++) { set16(poly + 8 + 2 * k, (unsigned)(fx[k] * 8)); set16(poly + 16 + 2 * k, (unsigned)(fy[k] * 8)); }
    set16(poly + 32 + 2, 0x1600);                       /* the wall: base -60, 0..120 high */
    set16(poly + 32 + 4, (unsigned)-60 & 0xFFFF);
    for (k = 0; k < 4; k++) {
        set16(poly + 32 + 8 + 2 * k, 48 * 8);
        set16(poly + 32 + 16 + 2 * k, (unsigned)((k == 1 || k == 2 ? 64 : 0) * 8));
        set16(poly + 32 + 24 + 2 * k, (unsigned)((k >= 2 ? 120 : 0) * 256) & 0xFFFF);
    }
    /* cells: (16,16) (16,48) list the floor; (48,16) (48,48) the floor and the wall */
    set16(idx, 0); set16(idx + 2, 0); set16(idx + 4, 0); set16(idx + 6, 1); set16(idx + 8, 0); set16(idx + 10, 1);
    memset(cells, 0, sizeof cells);
    set32(cells, 999999); set32(cells + 4, 999999);
    q = cells + 12;
    setf(q, 16); setf(q + 4, 16); set16(q + 8, 0); set16(q + 10, 1); q += 12;
    setf(q, 16); setf(q + 4, 48); set16(q + 8, 1); set16(q + 10, 1); q += 12;
    setf(q, 48); setf(q + 4, 16); set16(q + 8, 2); set16(q + 10, 2); q += 12;
    setf(q, 48); setf(q + 4, 48); set16(q + 8, 4); set16(q + 10, 2);
    q = put_chunk(out, 0x34131, cells, sizeof cells);
    q = put_chunk(q, 0x34132, poly, sizeof poly);
    q = put_chunk(q, 0x34133, idx, 12);
    return (size_t)(q - out);
}

static void test_drive(void)
{
    static uint8_t f[1024], db[0x560 * 2 + 64];
    size_t n = make_coll(f);
    hp2_coll c;
    hp2_carrec recs[4], rec;
    hp2_drive d;
    hp2_drive_in in;
    float z = 99, nn[3], x, y, t, n2[2];
    int s = 0, i;
    printf("collision and driving (synthetic)\n");
    CHECK(hp2_coll_parse(&c, f, n) == 0 && c.npoly == 2 && c.ncell == 4, "collision parses");
    CHECK(c.npoly == 2 && !c.poly[0].wall && c.poly[1].wall, "floor and wall told apart");
    CHECK(hp2_coll_ground(&c, 10, 10, 5, 1, &z, &s, nn) && NEAR(z, 0) && s == 0x1001 && NEAR(nn[2], 1), "ground %g", z);
    CHECK(hp2_coll_ground(&c, 50, 60, 5, 1, &z, &s, NULL) && NEAR(z, 0), "ground next to the wall");
    CHECK(!hp2_coll_ground(&c, 100, 10, 5, 1, &z, &s, NULL), "no ground outside the cells");
    x = 47.5f; y = 20;
    CHECK(hp2_coll_push(&c, &x, &y, 0, 1, n2) && NEAR(x, 47) && NEAR(n2[0], -1), "pushed off the wall: %g", x);
    x = 40;
    CHECK(!hp2_coll_push(&c, &x, &y, 0, 1, n2) && NEAR(x, 40), "clear of it");
    CHECK(hp2_coll_blocked(&c, 10, 32, 60, 32, 0) && !hp2_coll_blocked(&c, 10, 32, 40, 20, 0), "a line through the wall is blocked");
    CHECK(!hp2_coll_blocked(&c, 10, 32, 60, 32, 100), "not above it");

    /* two cars side by side, overlapping: pushed apart */
    {
        hp2_carrec cr;
        hp2_drive a2, b2;
        memset(&cr, 0, sizeof cr);
        for (i = 0; i < 4; i++) {
            cr.wheel[i].x = i < 2 ? 1.3f : -1.3f;
            cr.wheel[i].y = i == 0 || i == 3 ? 0.75f : -0.75f;
            cr.wheel[i].radius = 0.32f;
        }
        cr.length = 4.4f;
        cr.width = 1.8f;
        cr.mass = 1.5f;
        hp2_drive_init(&a2, &cr, 10, 10, 0, 0);
        hp2_drive_init(&b2, &cr, 10, 11.2f, 0, 0);
        b2.vel[1] = -3;
        CHECK(hp2_drive_collide(&a2, &b2) && b2.pos[1] - a2.pos[1] > 1.25f && b2.vel[1] - a2.vel[1] > 0.5f && NEAR(a2.vel[1] + b2.vel[1], -3),
              "cars that touch are pushed apart (%.2f m) and bounce off, sharing the momentum (%.2f %.2f m/s)", b2.pos[1] - a2.pos[1], a2.vel[1], b2.vel[1]);
        b2.pos[1] = 20;
        CHECK(!hp2_drive_collide(&a2, &b2), "apart: no bump");
    }

    /* the database: one record, found by name in any case */
    memset(db, 0, sizeof db);
    set32(db, 0x34600);
    set32(db + 4, 8 + 0x560);
    {
        uint8_t *r = db + 16;
        memcpy(r, "TESTCAR", 7);
        memcpy(r + 0x20, "TESTCAR", 7);
        for (i = 0; i < 4; i++) {
            setf(r + 0x120 + 32 * i, i < 2 ? 1.3f : -1.3f);
            setf(r + 0x124 + 32 * i, i == 0 || i == 3 ? 0.75f : -0.75f);
            setf(r + 0x128 + 32 * i, 0.1f);
            setf(r + 0x12C + 32 * i, 0.32f);
            setf(r + 0x130 + 32 * i, 0.25f);
        }
        setf(r + 0x1E4, 4.4f); setf(r + 0x1E8, 1.8f); setf(r + 0x1EC, 1.3f);
        set32(r + 0x288, 6);
        setf(r + 0x298, 3.82f);
        setf(r + 0x320, 150);
    }
    CHECK(hp2_cardb_parse(db, sizeof db, recs, 4) == 1, "one record");
    CHECK(hp2_cardb_find(recs, 1, "testcar") == &recs[0] && !hp2_cardb_find(recs, 1, "TEST"), "found by name");
    rec = recs[0];
    CHECK(NEAR(rec.wheel[0].x, 1.3f) && NEAR(rec.wheel[3].y, 0.75f) && rec.gears == 6 && NEAR(rec.ratio[0], 3.82f)
          && NEAR(rec.top_speed, 150), "record read");

    /* driving: away from the wall, then into it */
    hp2_drive_init(&d, &rec, 10, 32, 0, 0);
    CHECK(NEAR(d.wheelbase, 2.6f) && NEAR(d.ride, 0.22f), "wheelbase %g ride %g", d.wheelbase, d.ride);
    memset(&in, 0, sizeof in);
    in.throttle = 1;
    for (t = 0; t < 0.99f; t += 0.05f) hp2_drive_step(&d, &c, &in, 0.05f);
    CHECK(d.speed > 5 && d.speed < 12 && d.pos[0] > 12 && NEAR(d.pos[1], 32) && NEAR(d.pos[2], 0),
          "1 s of throttle: %g m/s at %g", d.speed, d.pos[0]);
    for (t = 0; t < 4; t += 0.05f) hp2_drive_step(&d, &c, &in, 0.05f);
    CHECK(d.pos[0] + d.half_len <= 48.01f && d.pos[0] + d.half_len > 47 && d.hit < 1000, "stopped by the wall at %g", d.pos[0]);
    hp2_drive_init(&d, &rec, 10, 32, 0, 0);
    in.steer = 1;
    for (t = 0; t < 1; t += 0.05f) hp2_drive_step(&d, &c, &in, 0.05f);
    CHECK(d.heading < -0.1f && d.pos[1] < 32, "steering right turns clockwise: %g", d.heading);
    in.throttle = 0;
    in.brake = 1;
    in.steer = 0;
    for (t = 0; t < 3; t += 0.05f) hp2_drive_step(&d, &c, &in, 0.05f);
    CHECK(d.speed < -1, "holding the brake when stopped reverses: %g", d.speed);
    hp2_coll_free(&c);
}


/* ---- races and engine sound ---------------------------------------------- */

static void test_race(void)
{
    /* a 400 m square circuit, anticlockwise */
    static const float sq[] = { 0, 0, 0, 100, 0, 100, 100, 100, 200, 0, 100, 300 };
    static const float line[] = { 0, 0, 0, 500, 0, 500 };
    hp2_race r;
    float d[2], x, y, t;
    int laps = 0, k;
    char buf[16];
    printf("races (synthetic)\n");
    hp2_race_init(&r, sq, 4, 1, 400, 2);
    CHECK(NEAR(hp2_race_progress(&r, 50, -3, d), 50) && NEAR(d[0], 1), "progress on the first side");
    CHECK(NEAR(hp2_race_progress(&r, -2, 50, d), 350) && NEAR(d[1], -1), "progress on the closing side");
    hp2_race_step(&r, 1.0f, 0, 0, 0, 0);
    hp2_race_step(&r, 1.0f, 0, 0, 0, 0);
    CHECK(r.state == HP2_RACE_COUNTDOWN, "counting down");
    hp2_race_step(&r, 1.5f, 0, 0, 0, 0);
    CHECK(r.state == HP2_RACE_RUNNING && r.lap == 1, "GO after 3 s");
    /* round twice at 20 m/s, a step a quarter second (5 m) */
    for (k = 0, t = 0; k < 170 && r.state == HP2_RACE_RUNNING; k++) {
        float p = fmodf(5.0f * (k + 1), 400.0f);
        if (p < 100) { x = p; y = 0; }
        else if (p < 200) { x = 100; y = p - 100; }
        else if (p < 300) { x = 300 - p; y = 100; }
        else { x = 0; y = 400 - p; }
        laps += hp2_race_step(&r, 0.25f, x, y, 0, 0);
    }
    CHECK(laps == 2 && r.state == HP2_RACE_FINISHED, "two laps, finished: %d %d", laps, r.state);
    CHECK(NEAR(r.lap_time[0], 20) && NEAR(r.lap_time[1], 20) && NEAR(r.best, 20), "laps of 20 s: %g %g", r.lap_time[0], r.lap_time[1]);
    /* back and forth over the line counts no lap */
    hp2_race_init(&r, sq, 4, 1, 400, 3);
    r.countdown = 0;
    for (k = 0; k < 40; k++) laps = hp2_race_step(&r, 0.1f, k % 2 ? 3.0f : 0.0f, k % 4 < 2 ? 3.0f : 0.0f, 0, 0);
    CHECK(r.lap == 1 && !laps, "no lap for wiggling at the line");
    hp2_race_step(&r, 0.1f, 50, 0, 0, 0);
    for (k = 0; k < 10; k++) hp2_race_step(&r, 0.1f, 50 - k, 0, -10, 0);
    CHECK(r.wrong_way, "wrong way");
    /* point to point: one run, finished at the end */
    hp2_race_init(&r, line, 2, 0, 500, 3);
    r.countdown = 0;
    CHECK(r.laps == 1, "point to point: one run");
    hp2_race_step(&r, 0.1f, 0, 0, 0, 0);
    for (t = 0; t < 30 && r.state != HP2_RACE_FINISHED; t += 0.5f) hp2_race_step(&r, 0.5f, t * 20, 0, 20, 0);
    CHECK(r.state == HP2_RACE_FINISHED && r.clock > 24 && r.clock < 26, "finished at %g s", r.clock);
    hp2_race_time(83.456f, buf, sizeof buf);
    CHECK(!strcmp(buf, "1:23.46"), "time %s", buf);
    {   /* Pursuit: stay near a speeder to arrest it */
        hp2_pursuit pu;
        float px[3] = { 0, 8, 100 }, py[3] = { 0, 0, 0 };
        int g1, k;
        memset(&pu, 0, sizeof pu);
        pu.on = 1;
        for (k = 0; k < 20; k++) hp2_pursuit_step(&pu, 0.1f, (float)k * 0.1f, 3, px, py);        /* 2 s near car 1 */
        CHECK(!pu.arrested[1] && pu.narrested == 0, "2 s within range is not enough");
        g1 = 0;
        for (k = 0; k < 8; k++) g1 += hp2_pursuit_step(&pu, 0.1f, 2.0f + (float)k * 0.1f, 3, px, py);
        CHECK(pu.arrested[1] && g1 == 1 && !pu.arrested[2] && !pu.done, "after 2.5 s speeder 1 is arrested, speeder 2 (100 m) is not");
        px[2] = 5;
        for (k = 0; k < 30; k++) hp2_pursuit_step(&pu, 0.1f, 3.0f + (float)k * 0.1f, 3, px, py);
        CHECK(pu.done && pu.narrested == 2 && pu.time > 5.0f && pu.time < 5.6f, "both arrested: done, with the clock: %g", pu.time);
        memset(&pu, 0, sizeof pu);
        pu.on = 1;
        px[2] = 100;
        for (k = 0; k < 20; k++) hp2_pursuit_step(&pu, 0.1f, 0, 3, px, py);
        px[1] = 50;
        for (k = 0; k < 40; k++) hp2_pursuit_step(&pu, 0.1f, 0, 3, px, py);
        CHECK(pu.hold[1] == 0 && !pu.arrested[1], "falling away runs the hold down to nothing");
        hp2_pursuit_reset(&pu);
        CHECK(pu.on && pu.narrested == 0 && !pu.done, "reset keeps it on");
    }
    {   /* Lap Knockout: the car furthest behind goes each time the leader finishes a lap */
        hp2_race rs[4];
        hp2_knockout ko;
        int ordk[4], ik;
        memset(rs, 0, sizeof rs);
        memset(&ko, 0, sizeof ko);
        ko.on = 1;
        for (ik = 0; ik < 4; ik++) { rs[ik].laps = 3; rs[ik].lap = 1; rs[ik].travelled = 100.0f - 10.0f * (float)ik; }  /* 0 leads, 3 last */
        hp2_knockout_update(&ko, rs, 4);
        CHECK(ko.nout == 0, "lap 1 not done: nobody out");
        rs[0].lap = 2; rs[0].travelled = 400;                 /* the leader completes lap 1 */
        hp2_knockout_update(&ko, rs, 4);
        CHECK(ko.nout == 1 && ko.out[3] && !ko.done, "lap 1 done: the last car (3) is out");
        hp2_knockout_update(&ko, rs, 4);
        CHECK(ko.nout == 1, "the same lap does not drop a second car");
        rs[1].lap = 2; rs[1].travelled = 390; rs[0].lap = 3; rs[0].travelled = 800;   /* the leader completes lap 2 */
        rs[2].travelled = 90; rs[1].travelled = 700;
        hp2_knockout_update(&ko, rs, 4);
        CHECK(ko.nout == 2 && ko.out[2] && !ko.done, "lap 2 done: car 2 is out");
        hp2_knockout_order(&ko, rs, 4, ordk);
        CHECK(ordk[0] == 0 && ordk[1] == 1 && ordk[2] == 2 && ordk[3] == 3, "order: 0, 1, then the knocked out, last to go first");
        rs[0].lap = 4; rs[0].travelled = 1200;                   /* lap 3: the player (car 0) is the one behind */
        rs[1].lap = 4; rs[1].travelled = 1300;
        hp2_knockout_update(&ko, rs, 4);
        CHECK(ko.done && ko.out[0] && ko.nout == 3, "lap 3: the player is last of two and out, the Knockout is decided");
        hp2_knockout_order(&ko, rs, 4, ordk);
        CHECK(ordk[0] == 1 && ordk[1] == 0 && ordk[2] == 2 && ordk[3] == 3, "final order 1, 0, 2, 3");
        hp2_knockout_reset(&ko);
        CHECK(ko.on && !ko.done && ko.nout == 0 && !ko.out[0], "reset keeps it on and clears the rest");
    }
    {   /* how a race ended: written by hp2track, read back by the menus */
        char txt[160];
        hp2_race_result rr;
        static const int ord3[3] = { 2, 0, 1 };
        int n = hp2_race_result_format(&r, 3, ord3, 3, txt, sizeof txt);
        CHECK(n > 0 && hp2_race_result_parse(txt, &rr) && rr.finished == 1 && rr.place == 3
              && rr.ticks == (long)(r.lap_time[0] * 4000.0f + 0.5f) && rr.best == rr.ticks,
              "result text: finished, 3rd, %ld ticks", rr.ticks);
        CHECK(rr.norder == 3 && rr.order[0] == 2 && rr.order[1] == 0 && rr.order[2] == 1, "the order of the cars comes back");
        hp2_race_result_parse("hp2result 1\norder 0 9 1\n", &rr);
        CHECK(rr.norder == 0, "an order naming a car that cannot exist is dropped");
        r.state = HP2_RACE_RUNNING;
        hp2_race_result_format(&r, 0, NULL, 0, txt, sizeof txt);
        CHECK(hp2_race_result_parse(txt, &rr) && !rr.finished && !rr.place && !rr.ticks && !rr.best, "unfinished: no times");
        CHECK(!hp2_race_result_parse("place 2\nticks 5\n", &rr) && !rr.place && !rr.ticks, "no header: not a result");
        CHECK(!hp2_race_result_parse(NULL, &rr) && hp2_race_result_format(&r, 1, NULL, 0, txt, 8) < 0, "NULL and a short buffer are safe");
        hp2_race_result_parse("hp2result 1\nfinished 1\nplace 500\nticks -4\nbest zz\nstuff\n", &rr);
        CHECK(rr.finished == 1 && rr.place == 0 && rr.ticks == 0 && rr.best == 0, "out-of-range values are dropped");
    }

    /* the route as a curve through its points: on the square it bulges
     * 12.5 m out between corners (Catmull-Rom) */
    hp2_race_init(&r, sq, 4, 1, 400, 1);
    {
        float p[2], dd[2];
        hp2_race_point(&r, 0, p, dd);
        CHECK(NEAR(p[0], 0) && NEAR(p[1], 0), "route point at 0: the first point");
        hp2_race_point(&r, 50, p, dd);
        CHECK(NEAR(p[0], 50) && NEAR(p[1], -12.5f) && NEAR(dd[0], 1), "half way along a side: %.1f %.1f", p[0], p[1]);
        hp2_race_point(&r, 450, p, NULL);
        CHECK(NEAR(p[0], 50) && NEAR(p[1], -12.5f), "a circuit wraps");
    }
    /* places: finished cars first by finishing time, then by distance */
    {
        hp2_race c4[4];
        const hp2_race *cp[4];
        int ord[4];
        for (k = 0; k < 4; k++) {
            memset(&c4[k], 0, sizeof c4[k]);
            c4[k].state = HP2_RACE_RUNNING;
            cp[k] = &c4[k];
        }
        c4[0].travelled = 300;
        c4[1].travelled = 900;
        c4[2].state = HP2_RACE_FINISHED;
        c4[2].finish_time = 80;
        c4[3].state = HP2_RACE_FINISHED;
        c4[3].finish_time = 75;
        hp2_race_order(cp, 4, ord);
        CHECK(ord[0] == 3 && ord[1] == 2 && ord[2] == 1 && ord[3] == 0, "places %d %d %d %d", ord[0], ord[1], ord[2], ord[3]);
    }
    /* a course's II: the route the other way round */
    {
        hp2_scene sc;
        float pts[12];
        memset(&sc, 0, sizeof sc);
        memcpy(pts, sq, sizeof pts);
        sc.route = pts;
        sc.route_n = 4;
        sc.route_loop = 1;
        sc.route_len = 400;
        hp2_scene_reverse_route(&sc);
        CHECK(NEAR(pts[0], 0) && NEAR(pts[1], 0) && NEAR(pts[3], 0) && NEAR(pts[4], 100) && NEAR(pts[5], 100)
              && NEAR(pts[9], 100) && NEAR(pts[10], 0) && NEAR(pts[11], 300), "a circuit reversed: same line, the other way, progress counted anew");
        {
            float op[9] = { 0, 0, 0, 100, 0, 100, 100, 150, 250 };
            sc.route = op;
            sc.route_n = 3;
            sc.route_loop = 0;
            sc.route_len = 250;
            hp2_scene_reverse_route(&sc);
            CHECK(NEAR(op[0], 100) && NEAR(op[1], 150) && NEAR(op[2], 0) && NEAR(op[5], 150) && NEAR(op[8], 250) && NEAR(op[6], 0),
                  "a point-to-point reversed: from the old finish");
        }
    }
    /* progress stays on its own stretch of route where another passes close */
    {
        static const float hair[] = { 0, 0, 0, 1000, 0, 1000, 1000, 10, 2000, 0, 10, 3000 };
        hp2_race rh;
        hp2_race_init(&rh, hair, 4, 0, 3000, 1);
        rh.countdown = 0;
        hp2_race_step(&rh, 0.01f, 500, 1, 0, 0);
        hp2_race_step(&rh, 0.01f, 500, 1, 0, 0);
        hp2_race_step(&rh, 0.1f, 500, 6, 30, 0);
        CHECK(rh.progress < 600, "nearer the stretch coming back, still on the way out (%.0f)", rh.progress);
        CHECK(hp2_race_progress(&rh, 500, 6, NULL) > 2000, "(the nearest point alone is on the way back)");
    }
    /* the opponents' line with no collision: the route, every 4 m */
    {
        hp2_ai_line l;
        float p[2], dd[2], half;
        CHECK(hp2_ai_line_build(&l, &r, NULL) == 0 && l.loop && l.n > 90 && l.n < 120, "line from the route alone: %d points", l.n);
        if (l.n) {
            hp2_ai_line_point(&l, 2 * HP2_AI_STEP, p, dd, &half);
            CHECK(NEAR(p[0], l.pt[8]) && NEAR(p[1], l.pt[9]) && half > 1, "line point");
            CHECK(fabsf(hp2_ai_line_find(&l, p[0], p[1], 0, -1) - 2 * HP2_AI_STEP) < 0.01f, "found again");
            CHECK(fabsf(hp2_ai_line_find(&l, p[0], p[1], 0, l.len - 10) - 2 * HP2_AI_STEP) < 0.01f, "found across the line");
        }
        hp2_ai_line_free(&l);
    }
}

/* A menu sound bank: a sound stored twice (left, right) and a single one. */
static void test_sfx(void)
{
    static uint8_t msh[12 + 3 * 52], msb[3 * 48];
    static const char *names[3] = { "MenuTest", "MenuTest", "Solo" };
    hp2_sfx x;
    const hp2_sfx_sample *a, *b;
    int i;
    printf("menu sounds (synthetic)\n");
    memset(msh, 0, sizeof msh);
    memset(msb, 0, sizeof msb);
    memset(&x, 0, sizeof x);
    set32(msh, sizeof msh);
    set32(msh + 4, 16);
    set32(msh + 8, 3);
    for (i = 0; i < 3; i++) {
        uint8_t *r = msh + 12 + 52 * i;
        memcpy(r, names[i], strlen(names[i]));
        r[33] = 1;
        set16(r + 34, i == 2 ? 44100 : 22050);
        set32(r + 40, (uint32_t)(48 * i));
        msb[48 * i + 1] = 0; msb[48 * i + 17] = 0; msb[48 * i + 33] = 1;      /* three blocks, the last flagged */
        msb[48 * i + 2] = (uint8_t)(i == 1 ? 0x77 : 0x00);                  /* the right channel has a sound in it */
    }
    CHECK(hp2_sfx_load(&x, msh, sizeof msh, msb, sizeof msb) == 2 && x.count == 2, "sfx: a stereo pair is one sound, a single another");
    a = hp2_sfx_find(&x, "MenuTest");
    b = hp2_sfx_find(&x, "Solo");
    CHECK(a && a->frames == 3 * 28 * 2 && b && b->frames == 3 * 28 && !hp2_sfx_find(&x, "Nope"), "sfx: 22,050 Hz doubled, 44,100 Hz kept");
    {   int l = 0, r = 0;
        for (i = 0; a && i < a->frames; i++) { if (a->pcm[2 * i]) l = 1; if (a->pcm[2 * i + 1]) r = 1; }
        CHECK(!l && r, "sfx: left and right stay apart");
    }
    CHECK(hp2_sfx_load(&x, msb, 20, msb, sizeof msb) == -1, "sfx: not a bank");
    hp2_sfx_free(&x);
    set32(msh + 4, 28);                                 /* COMMON and FRONTEND: a longer trailer */
    set16(msh + 12 + 34, 32000);                        /* the first sample at another rate */
    CHECK(hp2_sfx_load(&x, msh, sizeof msh, msb, sizeof msb) == 2, "sfx: any trailer length up to 64 is a bank");
    a = hp2_sfx_find(&x, "MenuTest");
    CHECK(a && a->frames == (int)((long long)3 * 28 * 44100 / 32000), "sfx: 32,000 Hz resampled to 44,100");
    hp2_sfx_free(&x);
}

/* The ambient zones of a track file: a bed and a one-shot, each a square. */
static void put_f(uint8_t *p, float f) { memcpy(p, &f, 4); }
static void test_ambient(void)
{
    static uint8_t buf[8 + 8 + 160 + 8 + 160], *q = buf;
    hp2_ambient a;
    hp2_sfx bank;
    hp2_sfx_sample smp[2];
    static int16_t pcm[2 * 4000], out[2 * 100];
    static const float sq[8] = { 0, 0, 100, 0, 100, 100, 0, 100 };
    int i, k, sum;
    printf("ambient zones (synthetic)\n");
    memset(buf, 0, sizeof buf);
    memset(&a, 0, sizeof a);
    set32(q, 0x80038000u); set32(q + 4, 8 + 8 * 3 + 4 + 64 + 8 + 4 * 3 + 64 + 8 + 160 - 8 * 3);   /* patched below */
    q += 8;
    /* a bed: name, sample, gain, f32, u32, 3 f32, n, points */
    set32(q, 0x38020u); set32(q + 4, 88 + 4 + 32);
    memcpy(q + 8, "BedA_21", 7); memcpy(q + 8 + 32, "Wind", 4);
    put_f(q + 8 + 64, 1.0f);
    set32(q + 8 + 88, 4);
    for (i = 0; i < 8; i++) put_f(q + 8 + 92 + 4 * i, sq[i]);
    q += 8 + 88 + 4 + 32;
    /* a one-shot: name, sample, period, spread, 3 f32, n, points */
    set32(q, 0x38030u); set32(q + 4, 84 + 4 + 32);
    memcpy(q + 8, "Bird01_21", 9); memcpy(q + 8 + 32, "Bird", 4);
    put_f(q + 8 + 64, 100.0f); put_f(q + 8 + 68, 0.0f);
    set32(q + 8 + 84, 4);
    for (i = 0; i < 8; i++) put_f(q + 8 + 88 + 4 * i, sq[i]);
    q += 8 + 84 + 4 + 32;
    set32(buf + 4, (uint32_t)(q - buf - 8));
    CHECK(hp2_ambient_parse(&a, buf, (size_t)(q - buf)) == 2 && a.z[0].bed && !a.z[1].bed && a.z[1].n == 4, "ambient: a bed and a one-shot read");
    CHECK(hp2_amb_inside(&a.z[0], 50, 50) && !hp2_amb_inside(&a.z[0], 150, 50) && !hp2_amb_inside(&a.z[0], 50, -1), "ambient: the polygon test");
    CHECK(!strcmp(hp2_ambient_bank_name(21), "DESERT") && !strcmp(hp2_ambient_bank_name(63), "TROPICS") && !hp2_ambient_bank_name(99), "ambient: course to bank");
    for (i = 0; i < 4000; i++) pcm[2 * i] = pcm[2 * i + 1] = 1000;
    memset(&bank, 0, sizeof bank);
    memset(smp, 0, sizeof smp);
    memcpy(smp[0].name, "Wind", 4); smp[0].pcm = pcm; smp[0].frames = 4000;
    memcpy(smp[1].name, "Bird", 4); smp[1].pcm = pcm; smp[1].frames = 150;
    bank.s = smp; bank.count = bank.cap = 2;
    hp2_ambient_bind(&a, &bank, 7);
    memset(out, 0, sizeof out);
    hp2_ambient_mix(&a, 500, 500, 1.0f, out, 100);
    for (sum = 0, i = 0; i < 200; i++) sum += out[i] != 0;
    CHECK(sum == 0, "ambient: nothing outside the zones");
    for (k = 0; k < 300; k++) { memset(out, 0, sizeof out); hp2_ambient_mix(&a, 50, 50, 1.0f, out, 100); }
    CHECK(out[198] == 500, "ambient: the bed is at full level (half the sample: the port's headroom) after a fade in");
    memset(out, 0, sizeof out);
    hp2_ambient_mix(&a, 50, 50, 0.5f, out, 100);
    CHECK(out[198] == 250, "ambient: follows the volume");
    memset(out, 0, sizeof out);
    hp2_ambient_mix(&a, 50, 50, 0.0f, out, 100);
    CHECK(out[10] == 0, "ambient: volume 0 is silent");
    for (k = 0; k < 400; k++) { memset(out, 0, sizeof out); hp2_ambient_mix(&a, 500, 500, 1.0f, out, 100); }
    CHECK(out[198] == 0 && a.z[0].level == 0, "ambient: the bed fades away outside");
    hp2_ambient_free(&a);
    CHECK(hp2_ambient_parse(&a, buf + 100, 16) == 0, "ambient: a file without the chunk gives none");
    hp2_ambient_free(&a);
}

/* The tyre sounds: each of the six samples a constant, so what plays shows in the sum. */
static void test_speech(void)
{
    /* a two-line vocabulary and bank, one VAG clip of one block */
    static uint8_t voc[0x40 + 72 * 2], ban[8 + 16 * 3 + 64], dat[256 + 64];
    hp2_speech sp;
    const char *path = "test_speech.tmp";
    FILE *f;
    int i;
    printf("police radio (synthetic)\n");
    memset(voc, 0, sizeof voc); memset(ban, 0, sizeof ban); memset(dat, 0, sizeof dat);
    voc[0x18] = 0x40;                                   /* line names at 0x40 */
    for (i = 0; i < 2; i++) {
        uint8_t *r = voc + 0x40 + 72 * i;
        r[0] = (uint8_t)(0x10 + i);
        memcpy(r + 40, i ? "27Arrest" : "19Arrest", 8);
    }
    ban[0] = 3;
    for (i = 0; i < 3; i++) {                           /* records: two lines, then the closing one */
        uint8_t *r = ban + 8 + 16 * i;
        r[4] = (uint8_t)(i * 0x80); r[12] = (uint8_t)(8 + 48 + 8 * i);
        ban[8 + 48 + 8 * i] = i < 2 ? (uint8_t)(0x10 + i) : 0x99; ban[8 + 48 + 8 * i + 1] = 0; ban[8 + 48 + 8 * i + 2] = 0xFF; ban[8 + 48 + 8 * i + 3] = 0xFF;
    }
    memcpy(dat, "VAGp", 4);                             /* a VAG: size (big-endian) 32, rate 24,000 */
    dat[12 + 3] = 32; dat[16 + 2] = 0x5D; dat[16 + 3] = 0xC0;
    for (i = 0; i < 16; i++) dat[48 + 1 + i] = (uint8_t)(i == 0 ? 0 : 0x12);
    dat[48 + 16 + 1] = 7;                               /* the second block ends the sound */
    f = fopen(path, "wb"); if (f) { fwrite(dat, 1, sizeof dat, f); fclose(f); }
    CHECK(hp2_speech_open(&sp, ban, sizeof ban, voc, sizeof voc, path, 0) == 0 && sp.nclips == 2 && sp.nnames == 2, "speech: opens, the closing record is skipped (%d clips)", sp.nclips);
    CHECK(hp2_speech_has(&sp, "Arrest") && !hp2_speech_has(&sp, "Crash"), "speech: lines found by name");
    CHECK(hp2_speech_say(&sp, "Arrest", 0) == 1 && hp2_speech_busy(&sp), "speech: a line starts");
    CHECK(hp2_speech_say(&sp, "Arrest", 0) == 0, "speech: a second waits for the first");
    {   static int16_t out[2 * 4000];
        hp2_speech_mix(&sp, 1.0f, out, 4000);
        CHECK(!hp2_speech_busy(&sp), "speech: the line plays out (%d frames)", sp.frames);
    }
    hp2_speech_close(&sp);
    remove(path);
}

static void test_oneshot(void)
{
    static int16_t a[2 * 10], out[2 * 8];
    hp2_sfx_sample smp;
    hp2_sfx bank;
    hp2_oneshot o;
    int i;
    printf("one-shot sounds (synthetic)\n");
    memset(&smp, 0, sizeof smp); memset(&bank, 0, sizeof bank);
    for (i = 0; i < 20; i++) a[i] = 1000;
    memcpy(smp.name, "Horn01", 6); smp.pcm = a; smp.frames = 10;
    bank.s = &smp; bank.count = bank.cap = 1;
    hp2_oneshot_bind(&o, &bank);
    CHECK(!hp2_oneshot_play(&o, "Nope", 1.0f) && hp2_oneshot_play(&o, "Horn01", 1.0f), "one-shot: found by name");
    memset(out, 0, sizeof out);
    hp2_oneshot_mix(&o, 1.0f, out, 8);
    CHECK(out[0] == 500 && out[15] == 500 && o.smp[0] != NULL, "one-shot: plays at half headroom (%d)", out[0]);
    memset(out, 0, sizeof out);
    hp2_oneshot_mix(&o, 1.0f, out, 8);
    CHECK(out[2 * 1] == 500 && out[2 * 2] == 0 && o.smp[0] == NULL, "one-shot: ends and frees its voice");
}

static void test_reverb(void)
{
    static uint8_t buf[8 + 8 + 48 + 8 * 4 + 8], two[1];
    static hp2_reverb r;
    static int16_t out[2 * 4000];
    uint8_t *c = buf + 8 + 8;
    float wet, dec;
    int i, tail_in = 0, tail_out = 0;
    float pts[8] = { 0, 0, 10, 0, 10, 10, 0, 10 };
    printf("echo regions (synthetic)\n");
    (void)two;
    memset(buf, 0, sizeof buf);
    buf[0] = 0x00; buf[1] = 0x80; buf[2] = 0x03; buf[3] = 0x80;     /* 0x80038000 */
    buf[4] = 8 + 48 + 32; buf[8] = 0x10; buf[9] = 0x80; buf[10] = 0x03; buf[11] = 0;   /* 0x38010: stored as 0x00038010 */
    buf[9] = 0x80; buf[8] = 0x10; buf[10] = 0x03; buf[9] = 0x80;
    buf[8] = 0x10; buf[9] = 0x80; buf[10] = 0x03; buf[11] = 0x00;
    buf[12] = 48 + 32;
    memcpy(c, "Tunnel_22", 9);
    c[36] = 4;
    memcpy(c + 40, pts, sizeof pts);
    hp2_reverb_init(&r);
    CHECK(hp2_reverb_parse(&r, buf, sizeof buf) == 1 && r.nz == 1 && !strcmp(r.z[0].name, "Tunnel_22"), "echo: one region read");
    hp2_reverb_kind("Overpass01_21", &wet, &dec);
    CHECK(wet < 0.3f && dec < 0.7f, "echo: an overpass is a short, light echo");
    hp2_reverb_kind("Cavern_21", &wet, &dec);
    CHECK(wet > 0.4f, "echo: a cavern is a long, strong one");
    out[0] = out[1] = 8000;
    hp2_reverb_apply(&r, 50, 50, out, 4000);
    for (i = 2000; i < 4000; i++) tail_out += abs(out[2 * i]);
    CHECK(tail_out == 0, "echo: nothing added outside a region");
    memset(out, 0, sizeof out); out[0] = out[1] = 8000;
    hp2_reverb_apply(&r, 5, 5, out, 4000);
    for (i = 2000; i < 4000; i++) tail_in += abs(out[2 * i]);
    CHECK(tail_in > 0, "echo: a tail inside a region (%d)", tail_in);
    hp2_reverb_free(&r);
}

static void test_cop(void)
{
    hp2_drive d;
    hp2_race r;
    hp2_ai ai;
    hp2_drive_in in;
    printf("police driver (synthetic)\n");
    memset(&d, 0, sizeof d); memset(&r, 0, sizeof r); memset(&ai, 0, sizeof ai);
    d.max_speed = 80.0f; d.heading = 0; ai.at = ai.tat = -1;
    r.state = HP2_RACE_COUNTDOWN;
    hp2_cop_drive(&ai, &r, NULL, &d, 100, 0, 0, 0, 0, 0.05f, &in);
    CHECK(in.brake == 1 && in.throttle == 0, "cop: held at the line before GO");
    r.state = HP2_RACE_RUNNING;
    hp2_cop_drive(&ai, &r, NULL, &d, 100, 0, 0, 0, 0, 0.05f, &in);
    CHECK(in.throttle == 1 && fabsf(in.steer) < 0.01f, "cop: full throttle, straight at a target ahead");
    hp2_cop_drive(&ai, &r, NULL, &d, 100, 30, 0, 0, 0, 0.05f, &in);
    CHECK(in.steer != 0 && in.throttle == 1, "cop: steers towards a target off to the side (%.2f)", in.steer);
    d.speed = 40; d.vel[0] = 40;
    hp2_cop_drive(&ai, &r, NULL, &d, 10, 0, 0, 5, 0, 0.05f, &in);
    CHECK(in.throttle < 1 && in.brake > 0, "cop: eases off when alongside a slower target");
}

static void test_ram(void)
{
    hp2_pursuit p;
    float px[3] = { 0, 100, 200 }, py[3] = { 0, 0, 0 };
    int arr = 0, k;
    printf("pursuit by ramming (synthetic)\n");
    memset(&p, 0, sizeof p);
    p.on = p.ram = 1;
    hp2_pursuit_reset(&p);
    hp2_pursuit_step(&p, 0.1f, 0.0f, 3, px, py);
    CHECK(p.speeders == 2 && p.health[1] == HP2_RAM_HEALTH, "ram: two speeders at full health");
    px[1] = 5;
    for (k = 0; k < 40; k++) hp2_pursuit_step(&p, 0.1f, 0.0f, 3, px, py);
    CHECK(p.narrested == 0, "ram: staying close arrests nobody");
    CHECK(hp2_pursuit_ram(&p, 1, 2.0f, 5, &arr) == 0, "ram: a tap below the minimum does nothing");
    CHECK(hp2_pursuit_ram(&p, 1, 10.0f, 5, &arr) > 24.0f && !arr && p.health[1] < 76.0f, "ram: a 10 m/s hit takes 24.5 off");
    CHECK(hp2_pursuit_ram(&p, 1, 20.0f, 5, &arr) == 0, "ram: a second hit in the same half second is ignored");
    hp2_pursuit_step(&p, 0.6f, 5.5f, 3, px, py);
    hp2_pursuit_ram(&p, 1, 20.0f, 6, &arr);
    hp2_pursuit_step(&p, 0.6f, 6.1f, 3, px, py);
    hp2_pursuit_ram(&p, 1, 20.0f, 7, &arr);
    CHECK(arr == 1 && p.arrested[1] && p.narrested == 1 && !p.done, "ram: three hard hits arrest it (%.0f left)", p.health[1]);
    hp2_pursuit_step(&p, 0.6f, 7.0f, 3, px, py);
    for (k = 0; k < 3; k++) { hp2_pursuit_step(&p, 0.6f, 8.0f + k, 3, px, py); hp2_pursuit_ram(&p, 2, 30.0f, 8.0f + k, &arr); }
    CHECK(p.done && p.narrested == 2 && p.time > 0, "ram: the event is done when the last is arrested");
}

static void test_tires(void)
{
    static int16_t pcm[6][2 * 200], out[2 * 100];
    static const char *names[6] = { "RoadNoise", "GravelNoise", "LongitudinalSkid", "SquallSkid", "GravelSkid", "BurnOutPeel" };
    static const int amp[6] = { 100, 200, 400, 800, 1600, 3200 };
    hp2_sfx bank;
    hp2_sfx_sample smp[6];
    hp2_tires t;
    hp2_tires_in in;
    int i, k;
    printf("tyre sounds (synthetic)\n");
    memset(&bank, 0, sizeof bank);
    memset(smp, 0, sizeof smp);
    for (i = 0; i < 6; i++) {
        for (k = 0; k < 400; k++) pcm[i][k] = (int16_t)amp[i];
        memcpy(smp[i].name, names[i], strlen(names[i]));
        smp[i].pcm = pcm[i]; smp[i].frames = 200;
    }
    bank.s = smp; bank.count = bank.cap = 6;
    hp2_tires_bind(&t, &bank);
    CHECK(t.v[HP2_TV_ROAD].smp && t.v[HP2_TV_BURN].smp, "tyres: the six samples are found by name");
    CHECK(hp2_tires_loose(3) && hp2_tires_loose(6) && !hp2_tires_loose(1) && !hp2_tires_loose(0x31), "tyres: dirt, gravel and grass are loose ground");
    memset(&in, 0, sizeof in);
    in.on_ground = 1; in.surface = 1;
    memset(out, 0, sizeof out);
    hp2_tires_mix(&t, &in, 1.0f, out, 100);
    CHECK(out[0] == 0 && out[199] == 0, "tyres: silent at a standstill");
    in.speed = 40;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] > 0 && out[10] < 100, "tyres: road noise on asphalt at speed (and quieter than the sample)");
    in.surface = 3;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] > 80 && out[10] < 100, "tyres: gravel noise on gravel, the road noise faded out");
    in.surface = 1; in.sliding = 2; in.steer = 0.3f;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] > 400 && out[10] < 700, "tyres: a squall skid when sliding with steering");
    in.steer = 0.0f;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] > 150 && out[10] < 400, "tyres: a longitudinal skid when sliding straight");
    in.speed = 3; in.throttle = 1.0f;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] >= 1600, "tyres: wheelspin at a standstill peels");
    memset(out, 0, sizeof out);
    hp2_tires_mix(&t, &in, 0.0f, out, 100);
    CHECK(out[10] == 0, "tyres: volume 0 is silent");
    in.on_ground = 0;
    for (k = 0; k < 100; k++) { memset(out, 0, sizeof out); hp2_tires_mix(&t, &in, 1.0f, out, 100); }
    CHECK(out[10] == 0, "tyres: nothing in the air");
}

/* An engine bank of three samples: coasting idle, under load low, horn. */
static void test_engine(void)
{
    static uint8_t msh[12 + 3 * 52], msb[3 * 64];
    static const char *names[3] = { "TSTCEid", "TSTLELo", "TSTHorn" };
    hp2_engine e;
    int16_t out[2 * 100];
    int i, k, loud = 0;
    printf("engine sound (synthetic)\n");
    memset(msh, 0, sizeof msh);
    memset(msb, 0, sizeof msb);
    set32(msh, sizeof msh);
    set32(msh + 4, 16);
    set32(msh + 8, 3);
    for (i = 0; i < 3; i++) {
        uint8_t *r = msh + 12 + 52 * i, *b = msb + 64 * i;
        memcpy(r, names[i], strlen(names[i]));
        r[33] = 1;
        set16(r + 34, 22050);
        set32(r + 40, (uint32_t)(64 * i));
        /* a silent block, then a loop of three: 6 at its start, 3 at its end */
        b[16 + 1] = 6;
        b[48 + 1] = 3;
        for (k = 16; k < 64; k++)
            if (k % 16 >= 2) b[k] = 0x7F;        /* loud nibbles: 7 and -1 alternate */
        b[16] = b[32] = b[48] = 0x00;            /* shift 0, no filter */
    }
    CHECK(hp2_engine_load(&e, msh, sizeof msh, msb, sizeof msb, 1000, 7000) == 0 && e.count == 3, "bank loads");
    CHECK(e.horn == 2 && e.pick[0][0][0] == 0 && e.pick[1][0][0] == 1 && e.pick[0][1][0] == -1, "samples by name");
    CHECK(e.count == 3 && e.s[0].n == 3 * 28, "the loop: %d samples", e.s[0].n);
    CHECK(NEAR(e.at[0][0], 1000) && NEAR(e.at[1][0], 2100), "idle at idle, low at 30%% of the redline");
    hp2_engine_mix(&e, 1000, 0, 1, out, 100, 44100);
    hp2_engine_mix(&e, 1000, 0, 1, out, 100, 44100);
    for (i = 0; i < 200; i++) loud |= out[i] != 0;
    CHECK(loud, "makes a sound");
    hp2_engine_mix(&e, 1000, 0, 0, out, 100, 44100);
    for (i = 0, loud = 0; i < 200; i++) loud |= out[i] != 0;
    CHECK(!loud, "silent at volume 0");
    hp2_engine_free(&e);
    msh[8] = 99;
    CHECK(hp2_engine_load(&e, msh, sizeof msh, msb, sizeof msb, 1000, 7000) < 0, "a bad count fails");
}

/* An engine bank's samples and which slot each fills (--enginedump). */
static int enginedump(const char *msh, const char *msb)
{
    size_t a = 0, b = 0;
    uint8_t *h = hp2_read_file(msh, &a), *m = hp2_read_file(msb, &b);
    hp2_engine e;
    int i, slots = 0, x, l, k;
    if (!h || !m || hp2_engine_load(&e, h, a, m, b, 1000, 7000) < 0) {
        fprintf(stderr, "not an engine bank: %s %s\n", msh, msb);
        free(h); free(m);
        return 1;
    }
    for (x = 0; x < 2; x++)
        for (l = 0; l < 2; l++)
            for (k = 0; k < 3; k++) slots += e.pick[x][l][k] >= 0;
    printf("%d samples, %d of 12 engine slots filled, horn %s:", e.count, slots, e.horn >= 0 ? "yes" : "no");
    for (i = 0; i < e.count; i++) printf(" %s(%.2fs)", e.s[i].name, (double)e.s[i].n / e.s[i].rate);
    printf("\n");
    hp2_engine_free(&e);
    free(h); free(m);
    return slots == 12 ? 0 : 1;
}

/* The placed scenery, per section, for tests/check_scene.py. */
static int scenedump(const char *path)
{
    size_t n = 0;
    uint8_t *F = hp2_read_file(path, &n), *dec = NULL;
    hp2_scene s;
    int i, j, k;
    if (F && n >= 4 && !memcmp(F, "COMP", 4)) {
        dec = hp2_comp_decode(F, n, &n);
        free(F);
        F = dec;
    }
    if (!F || hp2_scene_build(&s, F, n, 0) < 0) { fprintf(stderr, "no scenery in %s\n", path); free(F); return 1; }
    printf("total inst=%d placed=%d dup=%d missing=%d tris=%ld sky=%ld bad=%d\n", s.inst, s.placed, s.dup, s.missing, s.tris,
           s.sky_tris, s.bad);
    for (i = 0; i < s.nsec; i++) {
        const hp2_ssection *c = &s.sec[i];
        long tris = 0;
        for (j = 0; j < c->ng; j++) tris += c->g[j].nv / 3;
        printf("section %d %d %ld", c->num, c->placed, tris);
        for (k = 0; k < 6; k++) printf(" %.3f", c->ng ? c->box[k] : (k < 3 ? 1e30 : -1e30));
        printf("\n");
    }
    /* the ground on a 25-unit grid, highest surface first */
    {
        hp2_coll c;
        float x, y, z;
        int sf, walls = 0;
        if (hp2_coll_parse(&c, F, n) == 0) {
            for (i = 0; i < c.npoly; i++) walls += c.poly[i].wall;
            printf("coll %d %d %d\n", c.npoly, walls, c.ncell);
            for (x = s.box[0]; x <= s.box[3]; x += 25)
                for (y = s.box[1]; y <= s.box[4]; y += 25)
                    if (hp2_coll_ground(&c, x, y, 10000, 0, &z, &sf, NULL)) printf("ground %.3f %.3f %.4f %x\n", x, y, z, sf);
            hp2_coll_free(&c);
        }
    }
    hp2_scene_free(&s);
    free(F);
    return 0;
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
    if (argc == 3 && !strcmp(argv[1], "--scenedump")) return scenedump(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "--enginedump")) return enginedump(argv[2], argv[3]);
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
    test_geom();
    test_drive();
    test_race();
    test_engine();
    test_sfx();
    test_ambient();
    test_tires();
    test_speech();
    test_oneshot();
    test_reverb();
    test_cop();
    test_ram();
    if (fb && gb) test_real(fb, gb, lf, disc, bank, exe);
    printf("\nassets: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
