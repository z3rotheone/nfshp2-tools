/* Event trees: Championship (the port's Ultimate Racer) and Hot Pursuit
 * (Ultimate Fugitive), as the game lays them out.
 *
 * Data (docs/formats.md, "Event trees"):
 *   FRONTB.BUN holds two 0x80034A00 chunks. Child 0x00000001 is the name
 *   ("Championship" -> kind 0, "Hot Pursuit" -> kind 1), 0x00034A03 the
 *   prerequisite links (8 bytes: u16 kind, u16 event number, u32 value) and
 *   0x00034A05 the events (188-byte records: event number at +0x28, number
 *   of links at +0x3D, s16 link indices at +0x7C).
 *   The executable SLUS_203.62 holds each tree's column layout: a 20 x 10
 *   grid of ' ' and 'x' (Championship first, Hot Pursuit 208 bytes later);
 *   the n-th 'x' is event n. Its row equals the event's depth (the steps
 *   back to event 1 along each event's first link) for all 64 events.
 *   Positions (tEventTreeView, 0x1F96B8): x = 130 x column, y = 350 x row,
 *   in units that match the 272-wide star badge at scale 1.
 *   Connectors (tEventGroup, 0x1F81B8): events joined by links form a
 *   group; its bar runs at the midpoint between the parents' row and the
 *   children's row, from the leftmost to the rightmost member, and every
 *   member has a vertical line to the bar.
 *
 * Pure C99, no SDL. Reads never run past the given lengths. */
#ifndef HP2_TREE_H
#define HP2_TREE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP2_TREE_MAX_EVENTS 40
#define HP2_TREE_MAX_LINKS 8
#define HP2_TREE_COL_UNITS 130.0f
#define HP2_TREE_ROW_UNITS 350.0f

typedef struct {
    int number;                      /* 1.. */
    int nlinks;
    int links[HP2_TREE_MAX_LINKS];   /* prerequisite event numbers */
    int depth;                       /* steps back to event 1, -1 if broken */
    int col, row;                    /* grid cell; -1 without the grid */
} hp2_tree_event;

typedef struct {
    int present;
    int kind;                        /* 0 Championship, 1 Hot Pursuit */
    int count;                       /* events 1..count are present */
    int has_grid;
    hp2_tree_event ev[HP2_TREE_MAX_EVENTS];   /* ev[n - 1] is event n */
} hp2_tree;

typedef struct { float x0, y0, x1, y1; } hp2_tree_seg;

/* Fills t[0] (Championship) and t[1] (Hot Pursuit) from FRONTB.BUN.
 * Returns how many trees were found (0..2). */
int hp2_tree_parse(const uint8_t *frontb, size_t len, hp2_tree t[2]);

/* Finds the two layout grids in the executable by their content and fills
 * col/row. Returns 1 when both were found and agree with the trees (event
 * count and every row equal to its depth), else 0 (has_grid stays 0). */
int hp2_tree_grid(hp2_tree t[2], const uint8_t *exe, size_t len);

/* Event n's position in tree units; 0 when unknown. */
int hp2_tree_pos(const hp2_tree *t, int n, float *x, float *y);

/* The connector lines (horizontal bars and verticals) in tree units.
 * Returns the number written (at most max); 0 without a grid. */
int hp2_tree_segments(const hp2_tree *t, hp2_tree_seg *out, int max);

#ifdef __cplusplus
}
#endif

#endif
