/* hp2_zdir - the disc archive. See hp2_zdir.h and tools/hp2_extract.py. */
#if !defined(_WIN32)
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "hp2_zdir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

uint8_t *hp2_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    *len = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

uint8_t *hp2_read_file_part(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    *len = 0;
    if (!f) return NULL;
    b = (uint8_t *)malloc(max ? max : 1);
    if (b) *len = fread(b, 1, max, f);
    fclose(f);
    if (b && *len == 0) { free(b); b = NULL; }
    return b;
}

static void join(char *out, size_t cap, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == '/' || dir[n - 1] == '\\')) ? "" : "/";
    snprintf(out, cap, "%s%s%s", dir, sep, name);
}

int hp2_zdir_open(hp2_zdir *z, const char *dir)
{
    char p[1024];
    size_t len;
    FILE *f;
    memset(z, 0, sizeof *z);
    join(p, sizeof p, dir, "ZDIR.BIN");
    z->index = hp2_read_file(p, &len);
    if (!z->index || len < 12) { free(z->index); z->index = NULL; return -1; }
    z->count = (int)(len / 12);
    join(z->data, sizeof z->data, dir, "ZZDATA.BIN");
    f = fopen(z->data, "rb");
    if (!f) { hp2_zdir_close(z); return -1; }
    fclose(f);
    return 0;
}

void hp2_zdir_close(hp2_zdir *z)
{
    free(z->index);
    memset(z, 0, sizeof *z);
}

uint32_t hp2_zdir_hash(const char *s)
{
    uint32_t x = 0xFFFFFFFFu;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '/') c = '\\';
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        x = x * 33u + c;
    }
    return x;
}

static uint8_t *load(const hp2_zdir *z, uint32_t id, size_t max, size_t *len);

uint8_t *hp2_zdir_load_id(const hp2_zdir *z, uint32_t id, size_t *len)
{
    return load(z, id, (size_t)-1, len);
}

uint8_t *hp2_zdir_load_part(const hp2_zdir *z, const char *path, size_t max, size_t *len)
{
    return load(z, hp2_zdir_hash(path), max, len);
}

static uint8_t *load(const hp2_zdir *z, uint32_t id, size_t max, size_t *len)
{
    int i;
    *len = 0;
    for (i = 0; i < z->count; i++) {
        const uint8_t *r = z->index + 12 * i;
        if (rd32(r) == id) {
            uint32_t sector = rd32(r + 4), size = rd32(r + 8);
            if ((size_t)size > max) size = (uint32_t)max;
            FILE *f = fopen(z->data, "rb");
            uint8_t *b;
            if (!f) return NULL;
            b = (uint8_t *)malloc(size ? size : 1);
#if defined(_WIN32)
            if (b && _fseeki64(f, (long long)sector * 2048, SEEK_SET) != 0) { free(b); b = NULL; }
#else
            if (b && fseeko(f, (off_t)sector * 2048, SEEK_SET) != 0) { free(b); b = NULL; }
#endif
            if (b && fread(b, 1, size, f) != size) { free(b); b = NULL; }
            fclose(f);
            if (b) *len = size;
            return b;
        }
    }
    return NULL;
}

int hp2_zdir_locate(const hp2_zdir *z, const char *path, long long *offset, uint32_t *size)
{
    uint32_t id = hp2_zdir_hash(path);
    int i;
    for (i = 0; i < z->count; i++) {
        const uint8_t *r = z->index + 12 * i;
        if (rd32(r) == id) {
            *offset = (long long)rd32(r + 4) * 2048;
            *size = rd32(r + 8);
            return 0;
        }
    }
    return -1;
}

uint8_t *hp2_zdir_load(const hp2_zdir *z, const char *path, size_t *len)
{
    return hp2_zdir_load_id(z, hp2_zdir_hash(path), len);
}
