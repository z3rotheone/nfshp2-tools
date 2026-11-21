/* hp2_geom - models to triangles. See hp2_geom.h and tools/hp2_geom.py. */
#include "hp2_geom.h"

#include <stdlib.h>
#include <string.h>

#define CH_MODEL   0x80034000u
#define CH_PART    0x80034002u
#define CH_PARTHDR 0x00034003u
#define CH_REC     0x00034004u
#define CH_GEOM    0x00034005u
#define CH_TEX     0x00034006u
#define CH_MAT     0x0003401Du

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p)
{
    uint32_t w = rd32(p);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

uint32_t hp2_geom_hash(const char *s)
{
    uint32_t h = 0xFFFFFFFFu;
    while (*s) h = h * 33 + (uint8_t)*s++;
    return h;
}

/* Start of a chunk's body past any 0x11111111 filler words. */
static const uint8_t *skip_fill(const uint8_t *p, const uint8_t *e)
{
    while (p + 4 <= e && rd32(p) == 0x11111111u) p += 4;
    return p;
}

static int add_part(hp2_partset *set, const uint8_t *b, const uint8_t *e)
{
    hp2_part pt;
    const uint8_t *p = b;
    memset(&pt, 0, sizeof pt);
    while (p + 8 <= e) {
        uint32_t id = rd32(p), sz = rd32(p + 4);
        const uint8_t *o = p + 8;
        if (sz > (size_t)(e - o)) break;
        if (id == CH_PARTHDR) {
            /* name hash, then 0x0000000b, then the name (see hp2_geom.py parse) */
            size_t i, at = (size_t)-1;
            for (i = 0; i + 4 <= sz; i++)
                if (o[i] == 0x0B && !o[i + 1] && !o[i + 2] && !o[i + 3]) { at = i; break; }
            if (at == (size_t)-1) at = 12;
            pt.hash = rd32(o + (at >= 4 ? at - 4 : 0));
            for (i = 0; i + 1 < sizeof pt.name && at + 4 + i < sz && o[at + 4 + i]; i++)
                pt.name[i] = (char)o[at + 4 + i];
        } else if (id == CH_TEX || id == CH_MAT) {
            const uint8_t *q = skip_fill(o, o + sz);
            int n = (int)((o + sz - q) / 8);
            if (id == CH_TEX) { pt.tex = q; pt.ntex = n; }
            else { pt.mat = q; pt.nmat = n; }
        } else if (id == CH_REC) {
            pt.rec = o + sz % 64;
            pt.nrec = (int)(sz / 64);
        } else if (id == CH_GEOM && sz >= 8) {
            pt.geom = o + 8;
            pt.geom_len = sz - 8;
        }
        p = o + sz;
    }
    if (set->count == set->cap) {
        int cap = set->cap ? set->cap * 2 : 256;
        hp2_part *n = (hp2_part *)realloc(set->part, (size_t)cap * sizeof *n);
        if (!n) return -1;
        set->part = n;
        set->cap = cap;
    }
    set->part[set->count++] = pt;
    return 0;
}

static const hp2_partset *sort_set;
static int by_hash(const void *a, const void *b)
{
    uint32_t x = sort_set->part[*(const int *)a].hash, y = sort_set->part[*(const int *)b].hash;
    if (x != y) return x < y ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

int hp2_geom_scan(hp2_partset *set, const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf, *e = buf + len;
    int before = set->count, i;
    while (p + 8 <= e) {
        uint32_t id = rd32(p), sz = rd32(p + 4);
        const uint8_t *o = p + 8;
        if (sz > (size_t)(e - o)) break;
        if (id == CH_MODEL) {
            const uint8_t *q = o, *qe = o + sz;
            while (q + 8 <= qe) {
                uint32_t id2 = rd32(q), sz2 = rd32(q + 4);
                if (sz2 > (size_t)(qe - q - 8)) break;
                if (id2 == CH_PART && add_part(set, q + 8, q + 8 + sz2) < 0) return -1;
                q += 8 + sz2;
            }
        }
        p = o + sz;
    }
    free(set->order);
    set->order = (int *)malloc((size_t)(set->count ? set->count : 1) * sizeof(int));
    if (!set->order) return -1;
    for (i = 0; i < set->count; i++) set->order[i] = i;
    sort_set = set;
    qsort(set->order, (size_t)set->count, sizeof(int), by_hash);
    return set->count - before;
}

void hp2_geom_free(hp2_partset *set)
{
    free(set->part);
    free(set->order);
    memset(set, 0, sizeof *set);
}

const hp2_part *hp2_geom_find(const hp2_partset *set, uint32_t hash)
{
    int lo = 0, hi = set->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t h = set->part[set->order[mid]].hash;
        if (h < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    if (lo < set->count && set->part[set->order[lo]].hash == hash) return &set->part[set->order[lo]];
    return NULL;
}

/* ---- the VIF stream ---------------------------------------------------- */

/* Payload bytes of an UNPACK, padded to 4 as the VIF requires. */
static size_t unpack_size(int cmd, int num)
{
    int vn = (cmd >> 2) & 3, vl = cmd & 3;
    size_t n;
    if (vl == 3) n = (size_t)num * 2;
    else n = (size_t)num * (size_t)(vn + 1) * (size_t)(vl == 0 ? 4 : vl == 1 ? 2 : 1);
    return (n + 3) & ~(size_t)3;
}

typedef struct {
    int            addr, cmd;
    const uint8_t *pay;
    size_t         len;
} unp;

#define MAXUNP 32

typedef struct {
    int n;
    unp u[MAXUNP];
} vbatch;

/* The k-th unpack at an address in [lo, hi), in address order (stable). */
static int gather(const vbatch *b, int lo, int hi, const unp **out, int max)
{
    int n = 0, a, i;
    for (a = lo; a < hi; a++)
        for (i = 0; i < b->n; i++)
            if (b->u[i].addr == a && n < max) out[n++] = &b->u[i];
    return n;
}

static const unp *first_at(const vbatch *b, int addr)
{
    int i;
    for (i = 0; i < b->n; i++)
        if (b->u[i].addr == addr) return &b->u[i];
    return NULL;
}

/* Per vertex, 1 where it completes no triangle (the first two of a strip).
 * docs/formats.md 7, "Strips". */
static void strip_flags(const uint8_t *q1, int n, uint8_t *f)
{
    uint32_t w[2];
    int nw = 0, k, bit;
    memset(f, 0, 80);
    for (k = 0; k < 4; k++) {
        uint32_t x = rd32(q1 + 4 * k);
        if (x && nw < 2) w[nw++] = x;
    }
    if (!nw) { f[0] = f[1] = 1; return; }
    for (bit = 0; bit < 15; bit++)
        if ((w[0] >> bit) & 1) f[14 - bit] = 1;
    if (nw > 1) {
        int padded = (n + 3) / 4 * 4;
        for (bit = 0; bit < 18; bit++) {
            int v = padded + 1 - bit;
            if (v >= 15 && v < 80 && ((w[1] >> bit) & 1)) f[v] = 1;
        }
        if (f[16] && !f[17]) f[15] = 1;
    }
    if (f[14] && !f[13]) f[15] = 1;
}

typedef struct {
    int      n, ntri, normals;
    float    pos[64][3];
    float    uv[64][2];
    uint8_t  col[64][4];
    int8_t   nrm[64][3];
    uint8_t  tri[64][3];
} dbatch;

/* One batch, as decode_batch in hp2_geom.py. 0 or -1. */
static int decode_batch(const vbatch *b, dbatch *o)
{
    const unp *hdr = first_at(b, 0), *pos[8], *uvs[8], *u;
    int n, np, nu, rem, i, j, k, nuv = 0;
    uint8_t f[80];
    if (!hdr || hdr->len < 4) return -1;
    n = hdr->pay[0];
    if (n > 60) return -1;
    o->n = n;
    o->normals = 0;
    np = gather(b, 2, 32, pos, 8);
    if (!np) return -1;
    rem = n % 4;
    {
        const uint8_t *vals = pos[0]->pay;
        int nvals = (int)(pos[0]->len / 4);
        if (rem && (n < 4 || np > 1)) {
            int full = n >= 4 ? n / 4 : 0;
            const unp *tail = pos[n >= 4 ? 1 : 0];
            if (full * 12 > nvals) return -1;
            for (k = 0; k < full; k++)
                for (j = 0; j < 4; j++)
                    for (i = 0; i < 3; i++) o->pos[k * 4 + j][i] = rdf(vals + 4 * (k * 12 + i * 4 + j));
            if (tail->len < (size_t)(12 * rem)) return -1;
            for (j = 0; j < rem; j++)
                for (i = 0; i < 3; i++) o->pos[full * 4 + j][i] = rdf(tail->pay + 4 * (i * rem + j));
        } else if (nvals >= (n + 3) / 4 * 12) {
            for (k = 0; k * 4 < n; k++)
                for (j = 0; j < 4 && k * 4 + j < n; j++)
                    for (i = 0; i < 3; i++) o->pos[k * 4 + j][i] = rdf(vals + 4 * (k * 12 + i * 4 + j));
        } else {
            return -1;
        }
    }
    nu = gather(b, 32, 52, uvs, 8);
    for (k = 0; k < nu; k++) {
        u = uvs[k];
        if (((u->cmd >> 2) & 3) == 3) {
            int nf = (int)(u->len / 4);
            for (i = 0; i + 3 < nf; i += 4) {
                if (nuv < 64) { o->uv[nuv][0] = rdf(u->pay + 4 * i); o->uv[nuv][1] = rdf(u->pay + 4 * (i + 1)); nuv++; }
                if (nuv < 64) { o->uv[nuv][0] = rdf(u->pay + 4 * (i + 3)); o->uv[nuv][1] = rdf(u->pay + 4 * (i + 2)); nuv++; }
            }
        } else if (u->len >= 8 && nuv < 64) {
            o->uv[nuv][0] = rdf(u->pay);
            o->uv[nuv][1] = rdf(u->pay + 4);
            nuv++;
        }
    }
    for (i = nuv; i < n; i++) o->uv[i][0] = o->uv[i][1] = 0;
    u = first_at(b, 52);
    for (i = 0; i < n; i++) {
        if (u && (size_t)(i * 2 + 2) <= u->len) {
            uint16_t c = rd16(u->pay + 2 * i);
            o->col[i][0] = (uint8_t)((c & 31) << 3);
            o->col[i][1] = (uint8_t)(((c >> 5) & 31) << 3);
            o->col[i][2] = (uint8_t)(((c >> 10) & 31) << 3);
            o->col[i][3] = (c & 0x8000) ? 128 : 0;
        } else {
            o->col[i][0] = o->col[i][1] = o->col[i][2] = o->col[i][3] = 128;
        }
    }
    u = first_at(b, 92);
    memset(o->nrm, 0, sizeof o->nrm);
    if (u) {
        int groups = (int)(u->len / 12);
        o->normals = 1;
        for (k = 0; k < groups; k++)
            for (j = 0; j < 4 && k * 4 + j < n; j++)
                for (i = 0; i < 3; i++) o->nrm[k * 4 + j][i] = (int8_t)u->pay[k * 12 + i * 4 + j];
    }
    u = first_at(b, 1);
    if (u && u->len >= 16) strip_flags(u->pay, n, f);
    else { memset(f, 0, sizeof f); f[0] = f[1] = 1; }
    o->ntri = 0;
    for (i = 0, k = 0; i < n; i++) {
        if (f[i]) { k = 0; continue; }
        if (i < 2) continue;
        if (k % 2 == 0) { o->tri[o->ntri][0] = (uint8_t)(i - 2); o->tri[o->ntri][1] = (uint8_t)(i - 1); }
        else { o->tri[o->ntri][0] = (uint8_t)(i - 1); o->tri[o->ntri][1] = (uint8_t)(i - 2); }
        o->tri[o->ntri][2] = (uint8_t)i;
        o->ntri++;
        k++;
    }
    return 0;
}

static int grow(hp2_mesh *m, int *capv, int *capb, int addv)
{
    if (m->nv + addv > *capv) {
        int c = *capv ? *capv : 256;
        hp2_vert *n;
        while (c < m->nv + addv) c *= 2;
        n = (hp2_vert *)realloc(m->v, (size_t)c * sizeof *n);
        if (!n) return -1;
        m->v = n;
        *capv = c;
    }
    if (m->nb + 1 > *capb) {
        int c = *capb ? *capb * 2 : 16;
        hp2_mbatch *n = (hp2_mbatch *)realloc(m->b, (size_t)c * sizeof *n);
        if (!n) return -1;
        m->b = n;
        *capb = c;
    }
    return 0;
}

static int emit(hp2_mesh *m, int *capv, int *capb, const hp2_part *p, int index, const vbatch *vb)
{
    dbatch d;
    hp2_mbatch *mb;
    int t, k;
    uint32_t tex = 0, mat = 0;
    if (!first_at(vb, 0) || !first_at(vb, 2)) return 0;     /* not a draw */
    if (decode_batch(vb, &d) < 0) { m->bad++; return 0; }
    if (index < p->nrec) {
        const uint8_t *r = p->rec + 64 * index;
        uint32_t ti = rd32(r);
        int mi = r[31];
        if (r[29] != d.n || r[30] != d.ntri) m->wrong++;
        if (ti < (uint32_t)p->ntex) tex = rd32(p->tex + 8 * ti);
        if (mi != 0xFF && mi < p->nmat) mat = rd32(p->mat + 8 * mi);
    }
    if (grow(m, capv, capb, d.ntri * 3) < 0) return -1;
    mb = &m->b[m->nb++];
    mb->tex = tex;
    mb->mat = mat;
    mb->first = m->nv;
    mb->count = d.ntri * 3;
    for (t = 0; t < d.ntri; t++)
        for (k = 0; k < 3; k++) {
            int i = d.tri[t][k];
            hp2_vert *v = &m->v[m->nv++];
            v->x = d.pos[i][0];
            v->y = d.pos[i][1];
            v->z = d.pos[i][2];
            v->u = d.uv[i][0];
            v->v = d.uv[i][1];
            memcpy(v->c, d.col[i], 4);
            memcpy(v->n, d.nrm[i], 3);
            v->pad = 0;
        }
    return 0;
}

int hp2_geom_decode(const hp2_part *p, hp2_mesh *m)
{
    const uint8_t *q, *e;
    vbatch vb;
    int capv = 0, capb = 0, index = 0;
    memset(m, 0, sizeof *m);
    if (!p->geom) return 0;
    q = p->geom;
    e = p->geom + p->geom_len;
    vb.n = 0;
    while (q + 4 <= e) {
        uint32_t w = rd32(q);
        int cmd = (int)(w >> 24) & 0xFF, num = (int)(w >> 16) & 0xFF;
        if (cmd >= 0x60 && cmd <= 0x7F) {
            size_t n = unpack_size(cmd, num ? num : 256);
            if (n > (size_t)(e - q - 4)) break;
            if (vb.n < MAXUNP) {
                unp *u = &vb.u[vb.n++];
                u->addr = (int)(w & 0x3FF);
                u->cmd = cmd;
                u->pay = q + 4;
                u->len = n;
            }
            q += 4 + n;
        } else {
            if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {
                if (vb.n) {
                    if (emit(m, &capv, &capb, p, index, &vb) < 0) return -1;
                    index++;
                }
                vb.n = 0;
            }
            q += 4;
        }
    }
    if (vb.n && emit(m, &capv, &capb, p, index, &vb) < 0) return -1;
    return 0;
}

void hp2_mesh_free(hp2_mesh *m)
{
    free(m->v);
    free(m->b);
    memset(m, 0, sizeof *m);
}
