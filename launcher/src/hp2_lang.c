/* hp2_lang - FE_<LANGUAGE>.BIN lookups. See hp2_lang.h, tools/hp2_lang.py. */
#include "hp2_lang.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int hp2_lang_open(hp2_lang *l, const uint8_t *buf, size_t len)
{
    size_t n, tsize, tx;
    memset(l, 0, sizeof *l);
    if (!buf || len < 0x6C || rd32(buf) != 0x00039000u) return -1;
    n = rd32(buf + 0x54);
    tsize = rd32(buf + 0x5C);
    if (n == 0 || n > (len - 0x6C) / 16) return -1;
    tx = 0x6C + 16 * n;
    if (tsize > len - tx || tsize == 0 || buf[tx + tsize - 1] != 0) return -1;
    l->count = (int)n;
    l->keys = buf + 0x6C;
    l->ids = buf + 0x6C + 8 * n;
    l->text = (const char *)buf + tx;
    l->text_size = tsize;
    return 0;
}

static const char *at(const hp2_lang *l, uint32_t off)
{
    return off < l->text_size ? l->text + off : NULL;   /* the text ends in NUL */
}

const char *hp2_lang_by_id(const hp2_lang *l, uint32_t id)
{
    int lo = 0, hi = l->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = rd32(l->ids + 8 * mid);
        if (v == id) return at(l, rd32(l->ids + 8 * mid + 4));
        if (v < id) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

const char *hp2_lang_by_key(const hp2_lang *l, uint32_t key)
{
    int lo = 0, hi = l->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int32_t v = (int32_t)rd32(l->keys + 8 * mid);
        if (v == (int32_t)key) return at(l, rd32(l->keys + 8 * mid + 4));
        if (v < (int32_t)key) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
