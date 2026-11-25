#include "hp2_progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hp2_progress_reset(hp2_progress *p) { memset(p, 0, sizeof *p); }

int hp2_progress_record(hp2_progress *p, int kind, int ev, int place, long ticks)
{
    int k = kind & 1, i = ev - 1, ch = 0;
    if (ev < 1 || ev > HP2_TREE_MAX_EVENTS) return 0;
    if (!p->played[k][i]) { p->played[k][i] = 1; ch = 1; }
    if (place >= 1 && place <= 8 && (p->place[k][i] == 0 || place < p->place[k][i])) { p->place[k][i] = place; ch = 1; }
    if (ticks > 0 && (p->ticks[k][i] == 0 || ticks < p->ticks[k][i])) { p->ticks[k][i] = ticks; ch = 1; }
    return ch;
}

int hp2_progress_met(const hp2_progress *p, int kind, const hp2_tree_cond *c)
{
    int k = kind & 1, i = c->event - 1;
    if (c->event < 1 || c->event > HP2_TREE_MAX_EVENTS) return 0;
    if (c->kind == 1) return p->place[k][i] >= 1 && (unsigned long)p->place[k][i] <= c->value;
    if (c->kind == 2) return p->ticks[k][i] > 0 && (unsigned long)p->ticks[k][i] <= c->value;
    return p->played[k][i];
}

int hp2_progress_open(const hp2_progress *p, int kind, const hp2_tree *t, int ev)
{
    const hp2_tree_event *e;
    int n;
    if (!t || ev < 1 || ev > t->count) return 1;
    e = &t->ev[ev - 1];
    if (e->nlinks < 1) return 1;
    for (n = 0; n < e->nlinks && n < HP2_TREE_MAX_LINKS; n++) {
        hp2_tree_cond c = e->need[n];
        if (!c.event) c.event = e->links[n];
        if (hp2_progress_met(p, kind, &c)) return 1;
    }
    return 0;
}

int hp2_progress_complete(const hp2_progress *p, int kind, const hp2_tree_event *e)
{
    hp2_tree_cond c = e->target;
    c.event = e->number;
    return hp2_progress_met(p, kind, &c);
}

int hp2_progress_reward_got(const hp2_progress *p, int kind, const hp2_tree_event *e, const hp2_tree_reward *r)
{
    hp2_tree_cond c;
    c.event = e->number;
    c.value = r->value;
    c.kind = r->value >= 1 && r->value <= 8 ? 1 : r->value > 8 ? 2 : 0;
    return hp2_progress_met(p, kind, &c);
}

size_t hp2_progress_write(const hp2_progress *p, char *buf, size_t cap)
{
    size_t n = 0;
    int k, i;
    for (k = 0; k < 2; k++) {
        int head = 0;
        for (i = 0; i < HP2_TREE_MAX_EVENTS; i++) {
            int w;
            if (!p->played[k][i]) continue;
            w = snprintf(buf ? buf + n : NULL, n < cap ? cap - n : 0, "%sevent%d = %d %ld\n",
                         head ? "" : (k ? "[career 1]            # Hot Pursuit: best place, best time in ticks\n" : "[career 0]            # Championship: best place, best time in ticks\n"),
                         i + 1, p->place[k][i], p->ticks[k][i]);
            if (w < 0) return 0;
            n += (size_t)w;
            head = 1;
        }
    }
    return n;
}

int hp2_progress_parse(hp2_progress *p, const char *text)
{
    int k = 0, got = 0;
    const char *s = text;
    hp2_progress_reset(p);
    while (*s) {
        char line[128];
        size_t len = strcspn(s, "\n");
        int ev, pl;
        long tk;
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, s, len); line[len] = 0;
        s += strcspn(s, "\n"); if (*s) s++;
        if (sscanf(line, "[career %d]", &ev) == 1) { k = ev & 1; continue; }
        if (sscanf(line, "event%d = %d %ld", &ev, &pl, &tk) == 3 && ev >= 1 && ev <= HP2_TREE_MAX_EVENTS) {
            p->played[k][ev - 1] = 1;
            p->place[k][ev - 1] = pl >= 1 && pl <= 8 ? pl : 0;
            p->ticks[k][ev - 1] = tk > 0 ? tk : 0;
            got++;
        }
    }
    return got;
}

int hp2_progress_load(hp2_progress *p, const char *path)
{
    char buf[4096];
    size_t n;
    FILE *f = fopen(path, "rb");
    hp2_progress_reset(p);
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    hp2_progress_parse(p, buf);
    return 1;
}

int hp2_progress_save(const hp2_progress *p, const char *path)
{
    char buf[4096];
    size_t n = hp2_progress_write(p, buf, sizeof buf);
    FILE *f;
    if (n >= sizeof buf) return 0;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(buf, 1, n, f);
    return fclose(f) == 0;
}
