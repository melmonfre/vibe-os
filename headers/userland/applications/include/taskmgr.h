#ifndef TASKMGR_H
#define TASKMGR_H

#include <userland/applications/include/apps.h>  /* for struct window */
#include <userland/modules/include/utils.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/service.h>
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

struct taskmgr_netmgrd_status {
    int valid;
    int saved_profiles;
    char state[24];
    char active_if[16];
    char active_kind[16];
    char backend[24];
    char transport[24];
    char ownership[24];
    char fallback[24];
    char datapath_executor[24];
    char event_stream[24];
    char backend_events[24];
    char dns_mode[24];
    char lease_state[24];
    char lease_source[24];
    char manager[32];
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char ip[32];
    char gateway[32];
    char dns[32];
    char autoconnect[MK_NETWORK_SSID_MAX + 1];
};

#define TASKMGR_SERVICE_EVENT_HISTORY 12u

struct taskmgr_service_event_entry {
    struct mk_service_event event;
};

struct taskmgr_state {
    struct rect window;
    int selected_tab;
    int performance_scroll_offset;
    int processes_scroll_offset;
    int details_scroll_offset;
    uint32_t last_refresh_ticks;
    uint32_t last_video_refresh_ticks;
    uint32_t last_audio_refresh_ticks;
    uint32_t last_network_refresh_ticks;
    uint32_t last_netmgrd_refresh_ticks;
    uint32_t selected_pid;
    int task_count;
    int video_bench_valid;
    int audio_info_valid;
    int audio_status_valid;
    int network_info_valid;
    int network_status_valid;
    int audio_event_subscription;
    int video_event_subscription;
    int network_event_subscription;
    int audio_event_valid;
    int video_event_valid;
    int network_event_valid;
    uint32_t service_event_subscriptions;
    uint32_t service_event_head;
    uint32_t service_event_count;
    struct task_snapshot_summary summary;
    struct task_snapshot_entry tasks[TASK_SNAPSHOT_MAX];
    struct taskmgr_service_event_entry service_events[TASKMGR_SERVICE_EVENT_HISTORY];
    struct video_bench_info video_bench;
    struct mk_audio_info audio_info;
    struct audio_status audio_status;
    struct mk_audio_event audio_event;
    struct mk_video_event video_event;
    struct mk_network_event network_event;
    struct mk_network_info network_info;
    struct mk_network_status network_status;
    struct taskmgr_netmgrd_status netmgrd_status;
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
int taskmgr_scroll_by(struct taskmgr_state *tm,
                      const struct window *wins,
                      int win_count,
                      int x,
                      int y,
                      int delta,
                      uint32_t ticks);

#endif // TASKMGR_H
