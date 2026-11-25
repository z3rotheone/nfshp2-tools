/* hp2_progress - what the player has done in the two event trees (Championship
 * and Hot Pursuit): per event the best place and the best time. An event is
 * open when it has no prerequisites or when a prerequisite's condition (34A03)
 * has been met. The port opens an event when ANY of its links is met (the
 * game's own rule is not decoded). Kept in its own small text file next to
 * the settings, so the settings file stays about settings.
 *
 *   [career 0]                # 0 Championship, 1 Hot Pursuit
 *   event3 = 2 128000         # best place, best time in ticks (0 = none)
 */
#ifndef HP2_PROGRESS_H
#define HP2_PROGRESS_H

#include <stddef.h>
#include "hp2_tree.h"

typedef struct {
    int place[2][HP2_TREE_MAX_EVENTS];     /* best place, 0 = none */
    long ticks[2][HP2_TREE_MAX_EVENTS];    /* best time, 0 = none */
    int played[2][HP2_TREE_MAX_EVENTS];    /* finished at least once */
} hp2_progress;

void   hp2_progress_reset(hp2_progress *p);
/* Record a finished event (ev 1-based). Returns 1 when it improved something. */
int    hp2_progress_record(hp2_progress *p, int kind, int ev, int place, long ticks);
/* Is the condition met by what is recorded for its event? */
int    hp2_progress_met(const hp2_progress *p, int kind, const hp2_tree_cond *c);
/* Is event ev (1-based) of tree `t` open? */
int    hp2_progress_open(const hp2_progress *p, int kind, const hp2_tree *t, int ev);
/* Has the event's own objective been reached? */
int    hp2_progress_complete(const hp2_progress *p, int kind, const hp2_tree_event *e);
/* Has reward `r` of event `e` been earned? Its value is a place (1..8) or a time in ticks. */
int    hp2_progress_reward_got(const hp2_progress *p, int kind, const hp2_tree_event *e, const hp2_tree_reward *r);
size_t hp2_progress_write(const hp2_progress *p, char *buf, size_t cap);
int    hp2_progress_parse(hp2_progress *p, const char *text);
int    hp2_progress_load(hp2_progress *p, const char *path);
int    hp2_progress_save(const hp2_progress *p, const char *path);

#endif
