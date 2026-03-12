#ifndef TASKMGR_H
#define TASKMGR_H

#include <userland/applications/include/apps.h>  /* for struct window */
#include <userland/modules/include/utils.h>
#include <stdint.h>

struct taskmgr_state {
    struct rect window;
};

void taskmgr_init_state(struct taskmgr_state *tm);
void taskmgr_draw_window(struct taskmgr_state *tm,
                          struct window *wins,
                          int win_count,
                          uint32_t ticks,
                          int active,
                          int min_hover,
                          int max_hover,
                          int close_hover);
int taskmgr_hit_test_close(const struct taskmgr_state *tm,
                           const struct window *wins,
                           int win_count,
                           int x,
                           int y);

#endif // TASKMGR_H
