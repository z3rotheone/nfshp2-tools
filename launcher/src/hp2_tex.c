/* hp2_tex - texture packs to RGBA. See hp2_tex.h and tools/hp2_tpk.py. */
#include "hp2_tex.h"

#include <stdlib.h>
#include <string.h>

#define CH_PACK   0xB0300001u
#define CH_HEAD   0x30300002u
#define CH_INDEX  0x30300003u
#define CH_DATA   0x30300004u
#define REC       164

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* ---- GS memory tables (GSdx GSTables.cpp), as in hp2_tpk.py ------------ */

static const uint8_t BT32[4][8] = { { 0, 1, 4, 5, 16, 17, 20, 21 }, { 2, 3, 6, 7, 18, 19, 22, 23 },
                                    { 8, 9, 12, 13, 24, 25, 28, 29 }, { 10, 11, 14, 15, 26, 27, 30, 31 } };
static const uint8_t CT32[8][8] = { { 0, 1, 4, 5, 8, 9, 12, 13 }, { 2, 3, 6, 7, 10, 11, 14, 15 },
                                    { 16, 17, 20, 21, 24, 25, 28, 29 }, { 18, 19, 22, 23, 26, 27, 30, 31 },
                                    { 32, 33, 36, 37, 40, 41, 44, 45 }, { 34, 35, 38, 39, 42, 43, 46, 47 },
                                    { 48, 49, 52, 53, 56, 57, 60, 61 }, { 50, 51, 54, 55, 58, 59, 62, 63 } };
static const uint8_t BT4[8][4] = { { 0, 2, 8, 10 }, { 1, 3, 9, 11 }, { 4, 6, 12, 14 }, { 5, 7, 13, 15 },
                                   { 16, 18, 24, 26 }, { 17, 19, 25, 27 }, { 20, 22, 28, 30 }, { 21, 23, 29, 31 } };
static const uint8_t R4[2][32] = {
    { 0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
      4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110 },
    { 16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
      20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126 } };

static int ct4(int y, int x)
{
    int v = R4[y & 1][x];
    if (((y + 2) >> 2) & 1) v = (v + 64) & 127;
    return 128 * (y >> 2) + v + ((y >> 1) & 1);
}

static uint8_t inv_by[32], inv_bx[32], inv_cy[64], inv_cx[64];
static int tables_ready;

static void init_tables(void)
{
    int r, c;
    if (tables_ready) return;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 8; c++) { inv_by[BT32[r][c]] = (uint8_t)r; inv_bx[BT32[r][c]] = (uint8_t)c; }
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) { inv_cy[CT32[r][c]] = (uint8_t)r; inv_cx[CT32[r][c]] = (uint8_t)c; }
    tables_ready = 1;
}

/* Byte offset of pixel (x, y) in a swizzled 8-bit texture. */
static size_t pos8(int x, int y, int w)
{
    size_t block = (size_t)(y & ~0xF) * w + (size_t)(x & ~0xF) * 2;
    int swap = (((y + 2) >> 2) & 1) * 4;
    int posy = (((y & ~3) >> 1) + (y & 1)) & 7;
    size_t col = (size_t)posy * w * 2 + (size_t)((x + swap) & 7) * 4;
    int bn = ((y >> 1) & 1) + ((x >> 2) & 2);
    return block + col + bn;
}

/* Byte offset and nibble of pixel (x, y) in a swizzled 4-bit texture made
 * of whole 128 x 128 pages: the stored data is the w/2 x h/4 raster of
 * 32-bit words the texture fills in GS memory. */
static size_t pos4(int x, int y, int w, int *odd)
{
    int pw = w / 128;
    long page = (long)(y >> 7) * pw + (x >> 7);
    long n = page * 16384 + BT4[(y >> 4) & 7][(x >> 5) & 3] * 512 + ct4(y & 15, x & 31);
    long word = n >> 3, pg = word / 2048, wip = word % 2048;
    int b = (int)(wip / 64), c = (int)(wip % 64);
    long wx = (pg % pw) * 64 + inv_bx[b] * 8 + inv_cx[c];
    long wy = (pg / pw) * 32 + inv_by[b] * 8 + inv_cy[c];
    *odd = (int)(n & 1);
    return (size_t)((wy * (w / 2) + wx) * 4 + ((n & 7) >> 1));
}

/* Number of mip levels if size is exactly w x h plus halved levels, else 0. */
static int mip_levels(int w, int h, uint32_t size, int div)
{
    uint32_t total = 0;
    int k;
    for (k = 0; k < 8; k++) {
        int lw = w >> k, lh = h >> k;
        if (lw < 8 || lh < 8) break;
        total += (uint32_t)(lw * lh / div);
        if (total == size) return k + 1;
    }
    return 0;
}

/* ---- index --------------------------------------------------------------- */

static int chunk_at(const uint8_t *b, size_t o, size_t end, uint32_t *id, size_t *sz)
{
    if (o + 8 > end) return 0;
    *id = rd32(b + o);
    *sz = rd32(b + o + 4);
    return *sz <= end - o - 8;
}

const hp2_tex *hp2_tex_find(const hp2_texset *set, uint32_t hash)
{
    int i;
    if (!set) return NULL;
    for (i = 0; i < set->count; i++)
        if (set->tex[i].hash == hash) return &set->tex[i];
    return NULL;
}

static int add_pack(hp2_texset *set, const uint8_t *b, size_t sz)
{
    size_t o = 0, csz, isz = 0, dsz = 0, fill = 0, i, n, total = 0;
    const uint8_t *index = NULL, *data = NULL, *base;
    uint32_t id;
    int added = 0;
    while (chunk_at(b, o, sz, &id, &csz)) {
        if (id == CH_INDEX) { index = b + o + 8; isz = csz; }
        else if (id == CH_DATA) { data = b + o + 8; dsz = csz; }
        o += 8 + csz;
    }
    if (!index || !data) return 0;
    n = isz / REC;
    for (i = 0; i < n; i++) {
        const uint8_t *r = index + i * REC;
        size_t end = (size_t)rd32(r + 48) + rd32(r + 56);
        if (end > total) total = end;
    }
    if (total > dsz) return 0;
    /* offsets count from the end of the 0x11 filler opening the data chunk */
    while (fill < dsz && data[fill] == 0x11) fill++;
    if (fill > dsz - total) fill = dsz - total;
    base = data + fill;

    for (i = 0; i < n; i++) {
        const uint8_t *r = index + i * REC;
        hp2_tex t;
        uint32_t pix_off = rd32(r + 48), pal_off = rd32(r + 52);
        uint32_t pix_sz = rd32(r + 56), pal_sz = rd32(r + 60);
        size_t room = dsz - fill;
        memset(&t, 0, sizeof t);
        memcpy(t.name, r + 8, 24);
        t.hash = rd32(r + 32);
        t.w = (int)rd16(r + 36);
        t.h = (int)rd16(r + 38);
        if (hp2_tex_find(set, t.hash)) continue;
        if ((size_t)pix_off + pix_sz <= room) t.pix = base + pix_off;
        if (pal_sz && (size_t)pal_off + pal_sz <= room) t.pal = base + pal_off;
        if (t.w > 0 && t.h > 0 && t.pix) {
            if (pal_sz == 1024 && t.pal && mip_levels(t.w, t.h, pix_sz, 1)) t.bpp = 8;
            else if (pal_sz == 64 && t.pal && t.w % 2 == 0 && mip_levels(t.w, t.h, pix_sz, 2)) t.bpp = 4;
            else if (pal_sz == 0 && pix_sz == (uint32_t)t.w * (uint32_t)t.h * 4u) t.bpp = 32;
        }
        if (set->count == set->cap) {
            int ncap = set->cap ? set->cap * 2 : 256;
            hp2_tex *nt = (hp2_tex *)realloc(set->tex, (size_t)ncap * sizeof *nt);
            if (!nt) return -1;
            set->tex = nt;
            set->cap = ncap;
        }
        set->tex[set->count++] = t;
        added++;
    }
    return added;
}

int hp2_tex_scan(hp2_texset *set, const uint8_t *buf, size_t len)
{
    size_t i;
    int added = 0;
    for (i = 0; i + 12 <= len; i += 4) {
        uint32_t sz;
        int r;
        if (rd32(buf + i) != CH_PACK || rd32(buf + i + 8) != CH_HEAD) continue;
        sz = rd32(buf + i + 4);
        if (sz == 0 || sz > len - i - 8) continue;
        r = add_pack(set, buf + i + 8, sz);
        if (r < 0) return -1;
        added += r;
        i += 8 + sz - 4;
    }
    return added;
}

void hp2_tex_free(hp2_texset *set)
{
    if (!set) return;
    free(set->tex);
    set->tex = NULL;
    set->count = set->cap = 0;
}

/* ---- decode -------------------------------------------------------------- */

static void pal_entry(const uint8_t *p, uint8_t *out)
{
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3] >= 128 ? 255 : (uint8_t)(p[3] * 2);
}

int hp2_tex_decode(const hp2_tex *t, uint8_t *rgba)
{
    uint8_t pal[256][4];
    int x, y, i;
    init_tables();
    if (!t || t->bpp == 0) return -1;
    if (t->bpp == 32) {
        size_t n = (size_t)t->w * t->h;
        for (i = 0; (size_t)i < n; i++) pal_entry(t->pix + 4 * (size_t)i, rgba + 4 * (size_t)i);
        return 0;
    }
    if (t->bpp == 8) {
        /* entries 8-15 and 16-23 of every 32 are stored swapped */
        for (i = 0; i < 256; i++) {
            int s = i, m = i & 31;
            if (m >= 8 && m < 16) s = i + 8;
            else if (m >= 16 && m < 24) s = i - 8;
            pal_entry(t->pal + 4 * s, pal[i]);
        }
        if (t->w % 128 == 0 && t->h % 64 == 0) {
            for (y = 0; y < t->h; y++)
                for (x = 0; x < t->w; x++)
                    memcpy(rgba + 4 * ((size_t)y * t->w + x), pal[t->pix[pos8(x, y, t->w)]], 4);
        } else {
            size_t n = (size_t)t->w * t->h;
            for (i = 0; (size_t)i < n; i++) memcpy(rgba + 4 * (size_t)i, pal[t->pix[i]], 4);
        }
        return 0;
    }
    for (i = 0; i < 16; i++) pal_entry(t->pal + 4 * i, pal[i]);
    if (t->w % 128 == 0 && t->h % 128 == 0) {
        for (y = 0; y < t->h; y++)
            for (x = 0; x < t->w; x++) {
                int odd;
                uint8_t b = t->pix[pos4(x, y, t->w, &odd)];
                memcpy(rgba + 4 * ((size_t)y * t->w + x), pal[odd ? b >> 4 : b & 15], 4);
            }
    } else {
        size_t n = (size_t)t->w * t->h;
        for (i = 0; (size_t)i < n; i++) {
            uint8_t b = t->pix[i >> 1];
            memcpy(rgba + 4 * (size_t)i, pal[(i & 1) ? b >> 4 : b & 15], 4);
        }
    }
    return 0;
}
