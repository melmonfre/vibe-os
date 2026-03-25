#ifndef TASKMGR_H
#define TASKMGR_H

#include <userland/applications/include/apps.h>  /* for struct window */
#include <userland/modules/include/utils.h>
#include <include/userland_api.h>
#include <stdint.h>

enum taskmgr_tab {
    TASKMGR_TAB_PROCESSES = 0,
    TASKMGR_TAB_PERFORMANCE,
    TASKMGR_TAB_DETAILS
};

enum taskmgr_action_type {
    TASKMGR_ACTION_NONE = 0,
    TASKMGR_ACTION_CLOSE_WINDOW,
    TASKMGR_ACTION_TERMINATE_PID
};

struct taskmgr_action {
    int type;
    int value;
};

struct taskmgr_state {
    struct rect window;
    int selected_tab;
    uint32_t last_refresh_ticks;
    uint32_t selected_pid;
    int task_count;
    struct task_snapshot_summary summary;
    struct task_snapshot_entry tasks[TASK_SNAPSHOT_MAX];
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
struct taskmgr_action taskmgr_handle_click(struct taskmgr_state *tm,
                                           const struct window *wins,
                                           int win_count,
                                           int x,
                                           int y,
                                           uint32_t ticks);

#endif // TASKMGR_H
