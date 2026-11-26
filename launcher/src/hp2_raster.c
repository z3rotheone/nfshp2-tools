#include "hp2_raster.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int hp2_rtex_make(hp2_rtex *t, const uint8_t *rgba, int w, int h)
{
    int l, x, y, k;
    memset(t, 0, sizeof *t);
    if (w < 1 || h < 1) return -1;
    t->w = w; t->h = h;
    t->lw[0] = w; t->lh[0] = h;
    t->lev[0] = (uint8_t *)malloc((size_t)w * h * 4);
    if (!t->lev[0]) return -1;
    memcpy(t->lev[0], rgba, (size_t)w * h * 4);
    t->n = 1;
    for (l = 1; l < HP2_RTEX_LEVELS && (t->lw[l - 1] > 1 || t->lh[l - 1] > 1); l++) {
        int pw = t->lw[l - 1], ph = t->lh[l - 1], nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
        uint8_t *src = t->lev[l - 1], *dst = (uint8_t *)malloc((size_t)nw * nh * 4);
        if (!dst) break;
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++) {
                int sx0 = x * pw / nw, sx1 = (x + 1) * pw / nw, sy0 = y * ph / nh, sy1 = (y + 1) * ph / nh, n = 0, sx, sy;
                float acc[4] = { 0, 0, 0, 0 };
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                for (sy = sy0; sy < sy1; sy++)
                    for (sx = sx0; sx < sx1; sx++) {
                        const uint8_t *p = src + 4 * ((size_t)sy * pw + sx);
                        float a = p[3] / 255.0f;
                        for (k = 0; k < 3; k++) acc[k] += p[k] * a;      /* alpha-weighted colour */
                        acc[3] += a;
                        n++;
                    }
                {   uint8_t *o = dst + 4 * ((size_t)y * nw + x);
                    for (k = 0; k < 3; k++) o[k] = acc[3] > 0 ? (uint8_t)(acc[k] / acc[3] + 0.5f) : 0;
                    o[3] = (uint8_t)(acc[3] / (float)n * 255.0f + 0.5f);
                }
            }
        t->lev[l] = dst; t->lw[l] = nw; t->lh[l] = nh;
        t->n = l + 1;
    }
    return 0;
}

void hp2_rtex_free(hp2_rtex *t)
{
    int l;
    for (l = 0; l < HP2_RTEX_LEVELS; l++) free(t->lev[l]);
    memset(t, 0, sizeof *t);
}

int hp2_rast_init(hp2_rast *r, int w, int h)
{
    memset(r, 0, sizeof *r);
    if (w < 1 || h < 1) return -1;
    r->px = (uint8_t *)malloc((size_t)w * h * 4);
    r->z = (float *)malloc((size_t)w * h * sizeof *r->z);
    if (!r->px || !r->z) { hp2_rast_free(r); return -1; }
    r->w = w; r->h = h;
    hp2_rast_clear(r);
    return 0;
}

void hp2_rast_free(hp2_rast *r) { free(r->px); free(r->z); memset(r, 0, sizeof *r); }

void hp2_rast_clear(hp2_rast *r)
{
    memset(r->px, 0, (size_t)r->w * r->h * 4);
    memset(r->z, 0, (size_t)r->w * r->h * sizeof *r->z);
}

void hp2_rast_clear_z(hp2_rast *r) { memset(r->z, 0, (size_t)r->w * r->h * sizeof *r->z); }

void hp2_rast_glow(hp2_rast *r, float cx, float cy, float rx, float ry, float cr, float cg, float cb, float a0)
{
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx), y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry), x, y;
    if (rx < 1 || ry < 1) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > r->w - 1) x1 = r->w - 1;
    if (y1 > r->h - 1) y1 = r->h - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry, d2 = dx * dx + dy * dy, a, ia;
            uint8_t *o;
            if (d2 >= 1.0f) continue;
            a = a0 * (1.0f - d2);
            ia = 1.0f - a;
            o = r->px + 4 * ((size_t)y * r->w + x);
            o[0] = (uint8_t)(cr * a * 255.0f + o[0] * ia + 0.5f);
            o[1] = (uint8_t)(cg * a * 255.0f + o[1] * ia + 0.5f);
            o[2] = (uint8_t)(cb * a * 255.0f + o[2] * ia + 0.5f);
            o[3] = (uint8_t)(a * 255.0f + o[3] * ia + 0.5f);
        }
}

static void sample(const hp2_rtex *t, int lv, float u, float v, float out[4])
{
    int w = t->lw[lv], h = t->lh[lv], x0, y0, x1, y1;
    float x = u * w - 0.5f, y = v * h - 0.5f, fx, fy;
    const uint8_t *d = t->lev[lv];
    int k;
    x0 = (int)floorf(x); y0 = (int)floorf(y);
    fx = x - (float)x0; fy = y - (float)y0;
    if ((w & (w - 1)) == 0) x0 &= w - 1; else { x0 %= w; if (x0 < 0) x0 += w; }
    if ((h & (h - 1)) == 0) y0 &= h - 1; else { y0 %= h; if (y0 < 0) y0 += h; }
    x1 = x0 + 1 == w ? 0 : x0 + 1;
    y1 = y0 + 1 == h ? 0 : y0 + 1;
    {   const uint8_t *a = d + 4 * ((size_t)y0 * w + x0), *b = d + 4 * ((size_t)y0 * w + x1),
                      *c = d + 4 * ((size_t)y1 * w + x0), *e = d + 4 * ((size_t)y1 * w + x1);
        for (k = 0; k < 4; k++)
            out[k] = ((float)a[k] * (1 - fx) + (float)b[k] * fx) * (1 - fy) + ((float)c[k] * (1 - fx) + (float)e[k] * fx) * fy;
    }
}

void hp2_rast_tri(hp2_rast *r, const hp2_rvert v[3], const hp2_rtex *tex, int blend)
{
    float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
    float minx, maxx, miny, maxy, iz[3], inv;
    int x0, x1, y0, y1, x, y, i, lv0 = 0, lv1 = 0;
    float lodf = 0;
    if (fabsf(area) < 1e-4f) return;
    if (((blend & HP2_RAST_CULL_POS) && area > 0) || ((blend & HP2_RAST_CULL_NEG) && area < 0)) return;
    blend &= 3;
    for (i = 0; i < 3; i++) { if (v[i].z < 0.01f) return; iz[i] = 1.0f / v[i].z; }
    minx = fminf(v[0].x, fminf(v[1].x, v[2].x)); maxx = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    miny = fminf(v[0].y, fminf(v[1].y, v[2].y)); maxy = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    x0 = (int)floorf(minx); x1 = (int)ceilf(maxx); y0 = (int)floorf(miny); y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > r->w - 1) x1 = r->w - 1;
    if (y1 > r->h - 1) y1 = r->h - 1;
    if (x0 > x1 || y0 > y1) return;
    if (tex && tex->n > 0) {          /* the mip level from how many texels fall on a pixel, for the whole triangle */
        float ua = fabsf((v[1].u - v[0].u) * (v[2].v - v[0].v) - (v[2].u - v[0].u) * (v[1].v - v[0].v)) * (float)tex->w * (float)tex->h;
        float ratio = ua / fabsf(area);
        if (ratio > 1.0f) lodf = 0.5f * log2f(ratio);
        if (lodf > (float)(tex->n - 1)) lodf = (float)(tex->n - 1);
        lv0 = (int)lodf;
        lv1 = lv0 + 1 < tex->n ? lv0 + 1 : lv0;
        lodf -= (float)lv0;
    }
    inv = 1.0f / area;
    {   /* the barycentric weights are linear in x and y: w = a * x + b * y + c */
        float a0 = (v[1].y - v[2].y) * inv, b0 = (v[2].x - v[1].x) * inv, c0 = (v[1].x * v[2].y - v[2].x * v[1].y) * inv;
        float a1 = (v[2].y - v[0].y) * inv, b1 = (v[0].x - v[2].x) * inv, c1 = (v[2].x * v[0].y - v[0].x * v[2].y) * inv;
        int lvl = lodf > 0.5f ? lv1 : lv0;       /* one mip level, bilinear: with the supersampling it is smooth enough */
        for (y = y0; y <= y1; y++) {
            float py = (float)y + 0.5f;
            float base0 = b0 * py + c0, base1 = b1 * py + c1, base2 = 1.0f - base0 - base1;   /* the weights at x = 0 */
            float a2 = -a0 - a1;
            int xs = x0, xe = x1;
            float rw0, rw1;
            /* the pixels of this row inside the triangle: each weight a * (x + .5) + base >= 0 bounds x on one side */
            {   const float aa[3] = { a0, a1, a2 }, bb[3] = { base0, base1, base2 };
                int e;
                for (e = 0; e < 3; e++) {
                    if (aa[e] > 1e-7f) { int lo = (int)ceilf(-bb[e] / aa[e] - 0.5f) - 1; if (lo > xs) xs = lo; }
                    else if (aa[e] < -1e-7f) { int hi = (int)floorf(-bb[e] / aa[e] - 0.5f) + 1; if (hi < xe) xe = hi; }
                    else if (bb[e] < -1e-4f) { xs = 1; xe = 0; break; }
                }
            }
            if (xs > xe) continue;
            rw0 = a0 * ((float)xs + 0.5f) + base0; rw1 = a1 * ((float)xs + 0.5f) + base1;
            for (x = xs; x <= xe; x++, rw0 += a0, rw1 += a1) {
                float w0 = rw0, w1 = rw1, w2 = 1.0f - w0 - w1, izp, rz, p0, p1, p2, col[4];
                size_t idx;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                izp = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
                idx = (size_t)y * r->w + x;
                if (izp <= r->z[idx]) continue;
                rz = 1.0f / izp;
                p0 = w0 * iz[0] * rz; p1 = w1 * iz[1] * rz; p2 = w2 * iz[2] * rz;
                col[0] = p0 * v[0].r + p1 * v[1].r + p2 * v[2].r;
                col[1] = p0 * v[0].g + p1 * v[1].g + p2 * v[2].g;
                col[2] = p0 * v[0].b + p1 * v[1].b + p2 * v[2].b;
                col[3] = p0 * v[0].a + p1 * v[1].a + p2 * v[2].a;
                if (tex && tex->n > 0) {
                    float u = p0 * v[0].u + p1 * v[1].u + p2 * v[2].u, vv = p0 * v[0].v + p1 * v[1].v + p2 * v[2].v, a[4];
                    sample(tex, lvl, u, vv, a);
                    col[0] *= a[0] * (1.0f / 255.0f); col[1] *= a[1] * (1.0f / 255.0f); col[2] *= a[2] * (1.0f / 255.0f);
                    if (blend) col[3] *= a[3] * (1.0f / 255.0f);
                }
            if (col[3] > 1) col[3] = 1;
            for (i = 0; i < 3; i++) { if (col[i] > 1) col[i] = 1; if (col[i] < 0) col[i] = 0; }
            {   uint8_t *o = r->px + 4 * idx;
                if (!blend) {
                    o[0] = (uint8_t)(col[0] * 255.0f + 0.5f); o[1] = (uint8_t)(col[1] * 255.0f + 0.5f);
                    o[2] = (uint8_t)(col[2] * 255.0f + 0.5f); o[3] = 255;
                    r->z[idx] = izp;
                } else {
                    float a = col[3], ia = 1.0f - a;
                    for (i = 0; i < 3; i++) o[i] = (uint8_t)(col[i] * a * 255.0f + o[i] * ia + 0.5f);
                    o[3] = (uint8_t)(a * 255.0f + o[3] * ia + 0.5f);
                    if (blend == 2) r->z[idx] = izp;
                }
            }
        }
    }
    }
}
