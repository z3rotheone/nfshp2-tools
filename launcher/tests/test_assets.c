/* Tests for the asset modules behind the front-end renderer: hp2_tex
 * (texture packs), hp2_font (fonts, string codes), hp2_lang (menu text),
 * hp2_zdir (the disc archive) and hp2_femenu (the Options menus). No SDL.
 *
 *   test_assets                          synthetic data only
 *   test_assets --frontb F --globalb G --lang L [--jukebox BANK] [--disc DIR]
 *                                        also the real files (entries 21,
 *                                        302 and 3; DIR = the disc's NFSHP2
 *                                        folder, to read FRONTB.BUN through
 *                                        ZDIR.BIN and compare)
 *   test_assets --texdump FILE...        one line per texture with a hash of
 *                                        its decoded RGBA, for check_tex.py
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

static void test_real(const char *fb, const char *gb, const char *lf, const char *disc, const char *bank)
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
    const char *fb = NULL, *gb = NULL, *lf = NULL, *disc = NULL, *bank = NULL;
    int i;
    if (argc > 1 && !strcmp(argv[1], "--texdump")) return texdump(argc - 2, argv + 2);
    if (argc == 5 && !strcmp(argv[1], "--vagdump")) return vagdump(argv[2], atoi(argv[3]), atoi(argv[4]));
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frontb")) fb = argv[i + 1];
        else if (!strcmp(argv[i], "--globalb")) gb = argv[i + 1];
        else if (!strcmp(argv[i], "--lang")) lf = argv[i + 1];
        else if (!strcmp(argv[i], "--disc")) disc = argv[i + 1];
        else if (!strcmp(argv[i], "--jukebox")) bank = argv[i + 1];
    }
    test_tokens();
    test_font();
    test_lang();
    test_tex();
    test_jukebox();
    test_adpcm();
    test_stream();
    test_zdir_hash();
    if (fb && gb) test_real(fb, gb, lf, disc, bank);
    printf("\nassets: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
