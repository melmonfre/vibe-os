#ifndef USERLAND_API_H
#define USERLAND_API_H

#include <stdint.h>

#define USERLAND_LAUNCH_NAME_MAX 16u
#define USERLAND_LAUNCH_ARGC_MAX 8u
#define USERLAND_LAUNCH_ARGV_BYTES 192u

enum syscall_id {
    SYSCALL_GFX_CLEAR = 1,
    SYSCALL_GFX_RECT = 2,
    SYSCALL_GFX_TEXT = 3,
    SYSCALL_INPUT_MOUSE = 4,
    SYSCALL_INPUT_KEY = 5,
    SYSCALL_SLEEP = 6,
    SYSCALL_TIME_TICKS = 7,
    SYSCALL_GFX_INFO = 8,
    SYSCALL_GETPID = 9,
    SYSCALL_YIELD = 10,
    SYSCALL_WRITE_DEBUG = 11,
    SYSCALL_GFX_FLIP = 14,
    SYSCALL_GFX_LEAVE = 15,
    SYSCALL_TEXT_MOVE_CURSOR = 16,
    SYSCALL_GFX_SET_MODE = 17,
    SYSCALL_STORAGE_LOAD = 18,
    SYSCALL_STORAGE_SAVE = 19,
    SYSCALL_STORAGE_READ_SECTORS = 20,
    SYSCALL_STORAGE_WRITE_SECTORS = 26,
    SYSCALL_STORAGE_TOTAL_SECTORS = 27,
    SYSCALL_GFX_SET_PALETTE = 28,
    SYSCALL_GFX_GET_PALETTE = 29,
    SYSCALL_GFX_BLIT8 = 30,

    SYSCALL_KEYBOARD_SET_LAYOUT = 21,
    SYSCALL_KEYBOARD_GET_LAYOUT = 22,
    SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS = 23,
    SYSCALL_GFX_CAPS = 24,
    SYSCALL_SHUTDOWN = 25,

    /* Filesystem syscalls */
    SYSCALL_OPEN = 31,
    SYSCALL_READ = 32,
    SYSCALL_WRITE = 33,
    SYSCALL_CLOSE = 34,
    SYSCALL_LSEEK = 35,
    SYSCALL_STAT = 36,
    SYSCALL_FSTAT = 37,
    SYSCALL_LAUNCH_INFO = 38,
    SYSCALL_TEXT_WRITE = 39,
    SYSCALL_SERVICE_RECV = 40,
    SYSCALL_SERVICE_SEND = 41,
    SYSCALL_SERVICE_BACKEND = 42,
    SYSCALL_TASK_SNAPSHOT = 43,
    SYSCALL_TASK_TERMINATE = 44,
    SYSCALL_GFX_BLIT8_STRETCH = 45,
    SYSCALL_GFX_SET_PRESENT_POLICY = 46,
    SYSCALL_GFX_BLIT8_STRETCH_PRESENT = 47,
    SYSCALL_GFX_BLIT8_PRESENT = 48,
    SYSCALL_GFX_BENCH = 49,
    SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE = 50,
    SYSCALL_AUDIO_GETINFO = 51,
    SYSCALL_AUDIO_MIXER_READ = 52,
    SYSCALL_AUDIO_MIXER_WRITE = 53,
    SYSCALL_AUDIO_CONTROL_INFO = 54,
    SYSCALL_NETWORK_GETINFO = 55,
    SYSCALL_NETWORK_GET_STATUS = 56,
    SYSCALL_NETWORK_SCAN = 57,
    SYSCALL_NETWORK_CONNECT_WIFI = 58,
    SYSCALL_NETWORK_DISCONNECT = 59,
    SYSCALL_AUDIO_SET_PARAMS = 60,
    SYSCALL_AUDIO_START = 61,
    SYSCALL_AUDIO_STOP = 62,
    SYSCALL_AUDIO_WRITE = 63,
    SYSCALL_AUDIO_GET_STATUS = 64,
    SYSCALL_NETWORK_SOCKET = 65,
    SYSCALL_NETWORK_BIND = 66,
    SYSCALL_NETWORK_CONNECT = 67,
    SYSCALL_NETWORK_SEND = 68,
    SYSCALL_NETWORK_RECV = 69,
    SYSCALL_NETWORK_CLOSE = 70,
    SYSCALL_NETWORK_LISTEN = 71,
    SYSCALL_NETWORK_ACCEPT = 72,
    SYSCALL_NETWORK_CONNECT_ETHERNET = 73,
    SYSCALL_NETWORK_CONFIGURE_ETHERNET = 74,
    SYSCALL_AUDIO_READ = 75,
    SYSCALL_AUDIO_WRITE_ASYNC = 76,
    SYSCALL_LAUNCH_BUILTIN_USER = 77,
    SYSCALL_INPUT_EVENT = 78,
    SYSCALL_SERVICE_SUBSCRIBE = 79,
    SYSCALL_SERVICE_EVENT_RECV = 80,
    SYSCALL_AUDIO_EVENT_SUBSCRIBE = 81,
    SYSCALL_AUDIO_EVENT_RECV = 82,
    SYSCALL_VIDEO_EVENT_SUBSCRIBE = 83,
    SYSCALL_VIDEO_EVENT_RECV = 84,
    SYSCALL_VIDEO_PRESENT_SUBMIT = 85,
    SYSCALL_NETWORK_EVENT_SUBSCRIBE = 86,
    SYSCALL_NETWORK_EVENT_RECV = 87,
    SYSCALL_TASK_EVENT_SUBSCRIBE = 88,
    SYSCALL_TASK_EVENT_RECV = 89,
    SYSCALL_SERVICE_PID = 90,
    SYSCALL_SERVICE_RESTART = 91,
    SYSCALL_LAUNCH_APP = 92,
    SYSCALL_TRANSFER_SIZE = 93,
    SYSCALL_TRANSFER_READ = 94,
    SYSCALL_TRANSFER_WRITE = 95
};

enum userland_builtin_target {
    USERLAND_BUILTIN_NONE = 0,
    USERLAND_BUILTIN_SHELL = 1,
    USERLAND_BUILTIN_DESKTOP = 2,
    USERLAND_BUILTIN_STARTX = 3,
    USERLAND_BUILTIN_DESKTOP_AUDIO = 4,
    USERLAND_BUILTIN_BOOT_AUDIO = 5,
    USERLAND_BUILTIN_DESKTOP_SESSION = 6,
    USERLAND_BUILTIN_SHELL_SESSION = 7
};

enum input_keycode {
    KEY_ARROW_UP = 0x100,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_DELETE
};

struct mouse_state {
    int x;
    int y;
    int dx;
    int dy;
    int wheel;
    uint8_t buttons;
};

enum input_event_type {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY = 1,
    INPUT_EVENT_MOUSE = 2
};

struct input_event {
    uint32_t type;
    int32_t value;
    struct mouse_state mouse;
};

struct video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

#define VIDEO_MODE_LIST_MAX 16u

enum video_capability_flags {
    VIDEO_CAPS_TEXT_ONLY = 1u << 0,
    VIDEO_CAPS_BOOT_LFB = 1u << 1,
    VIDEO_CAPS_BGA = 1u << 2,
    VIDEO_CAPS_CAN_SET_MODE = 1u << 3
};

enum video_resolution_bits {
    VIDEO_RES_640X480 = 1u << 0,
    VIDEO_RES_800X600 = 1u << 1,
    VIDEO_RES_1024X768 = 1u << 2,
    VIDEO_RES_1360X768 = 1u << 3,
    VIDEO_RES_1366X768 = 1u << 4,
    VIDEO_RES_1920X1080 = 1u << 5
};

enum video_present_mode {
    VIDEO_PRESENT_AUTO = 0,
    VIDEO_PRESENT_DIRTY = 1,
    VIDEO_PRESENT_FULL = 2
};

enum video_present_policy {
    VIDEO_PRESENT_POLICY_DEFAULT = 0,
    VIDEO_PRESENT_POLICY_DESKTOP = 1,
    VIDEO_PRESENT_POLICY_FULLSCREEN = 2
};

enum video_backend_kind {
    VIDEO_BACKEND_NONE = 0,
    VIDEO_BACKEND_LEGACY_LFB = 1,
    VIDEO_BACKEND_FAST_LFB = 2
};

enum video_native_backend_kind {
    VIDEO_NATIVE_BACKEND_NONE = 0,
    VIDEO_NATIVE_BACKEND_BGA = 1,
    VIDEO_NATIVE_BACKEND_I915 = 2,
    VIDEO_NATIVE_BACKEND_RADEON = 3,
    VIDEO_NATIVE_BACKEND_NOUVEAU = 4,
    VIDEO_NATIVE_BACKEND_UNKNOWN = 5
};

enum video_present_copy_kind {
    VIDEO_PRESENT_COPY_BYTE_LOOP = 0,
    VIDEO_PRESENT_COPY_REP_MOVSD = 1,
    VIDEO_PRESENT_COPY_MOVNTDQ = 2
};

enum video_present_copy_override_kind {
    VIDEO_PRESENT_COPY_OVERRIDE_AUTO = 0,
    VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP = 1,
    VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD = 2,
    VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ = 3
};

enum mk_video_event_type {
    MK_VIDEO_EVENT_NONE = 0,
    MK_VIDEO_EVENT_PRESENT = 1,
    MK_VIDEO_EVENT_MODE_SET = 2,
    MK_VIDEO_EVENT_LEAVE = 3,
    MK_VIDEO_EVENT_OVERFLOW = 4,
    MK_VIDEO_EVENT_PRESENT_SUBMITTED = 5,
    MK_VIDEO_EVENT_MODE_SET_BEGIN = 6,
    MK_VIDEO_EVENT_MODE_SET_DONE = 7,
    MK_VIDEO_EVENT_BACKEND_FAILED = 8,
    MK_VIDEO_EVENT_BACKEND_RECOVERED = 9
};

struct mk_video_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t present_mode;
    uint32_t sequence;
    uint32_t completed_sequence;
    uint32_t pending_depth;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t dropped_events;
    uint32_t tick;
};

struct video_capabilities {
    uint32_t flags;
    uint32_t supported_modes;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_bpp;
    uint32_t mode_count;
    uint16_t mode_width[VIDEO_MODE_LIST_MAX];
    uint16_t mode_height[VIDEO_MODE_LIST_MAX];
};

struct video_bench_info {
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_pitch;
    uint32_t gpu_vendor_id;
    uint32_t gpu_device_id;
    uint32_t gpu_revision;
    uint32_t detected_gpu_vendor_id;
    uint32_t detected_gpu_device_id;
    uint32_t detected_gpu_revision;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint32_t cpu_stepping;
    uint32_t fill_ticks;
    uint32_t present_ticks;
    uint32_t frame_ticks;
    uint32_t fullscreen_direct_ticks;
    uint32_t fullscreen_blit_present_ticks;
    uint32_t microkernel_frame_ticks;
    uint32_t microkernel_flip_ticks;
    uint32_t microkernel_blit_ticks;
    uint32_t microkernel_stretch_ticks;
    uint32_t frame_bytes;
    uint32_t backbuffer_bytes;
    uint32_t heap_free_before;
    uint32_t heap_free_after;
    uint32_t cpu_has_pat;
    uint32_t cpu_has_sse2;
    uint32_t wc_enabled;
    uint32_t backend_kind;
    uint32_t present_copy_kind;
    uint32_t present_copy_override_kind;
    uint32_t native_backend_kind;
    uint32_t detected_native_backend_kind;
    char cpu_vendor[13];
};

struct userland_launch_info {
    uint32_t abi_version;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t task_class;
    uint32_t boot_flags;
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
    uint32_t argc;
    char name[USERLAND_LAUNCH_NAME_MAX];
    char argv_data[USERLAND_LAUNCH_ARGV_BYTES];
};

enum mk_service_event_type {
    MK_SERVICE_EVENT_NONE = 0,
    MK_SERVICE_EVENT_ONLINE = 1,
    MK_SERVICE_EVENT_OFFLINE = 2,
    MK_SERVICE_EVENT_DEGRADED = 3,
    MK_SERVICE_EVENT_RECOVERED = 4,
    MK_SERVICE_EVENT_RESTARTED = 5
};

struct mk_service_event {
    uint32_t abi_version;
    uint32_t service_type;
    uint32_t event_type;
    uint32_t pid;
    uint32_t restart_count;
    uint32_t transport_degraded;
    uint32_t tick;
};

enum mk_audio_event_type {
    MK_AUDIO_EVENT_NONE = 0,
    MK_AUDIO_EVENT_QUEUED = 1,
    MK_AUDIO_EVENT_IDLE = 2,
    MK_AUDIO_EVENT_UNDERRUN = 3,
    MK_AUDIO_EVENT_OVERFLOW = 4
};

struct mk_audio_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t backend_kind;
    uint32_t queued_bytes;
    uint32_t underruns;
    uint32_t dropped_events;
    uint32_t tick;
};

enum mk_network_event_type {
    MK_NETWORK_EVENT_NONE = 0,
    MK_NETWORK_EVENT_STATUS = 1,
    MK_NETWORK_EVENT_SOCKET_RECV = 2,
    MK_NETWORK_EVENT_SOCKET_ACCEPT = 3,
    MK_NETWORK_EVENT_SOCKET_SEND = 4,
    MK_NETWORK_EVENT_SOCKET_CLOSED = 5,
    MK_NETWORK_EVENT_BACKEND_RX = 6,
    MK_NETWORK_EVENT_BACKEND_TX = 7,
    MK_NETWORK_EVENT_OVERFLOW = 8
};

struct mk_network_event {
    uint32_t abi_version;
    uint32_t event_type;
    int32_t handle;
    int32_t peer_handle;
    uint32_t sequence;
    uint32_t link_state;
    uint32_t byte_count;
    uint32_t dropped_events;
    uint32_t tick;
};

enum mk_task_class {
    MK_TASK_CLASS_NONE = 0,
    MK_TASK_CLASS_SUPERVISION = 1,
    MK_TASK_CLASS_DESKTOP = 2,
    MK_TASK_CLASS_SHELL = 3,
    MK_TASK_CLASS_APP_RUNTIME = 4,
    MK_TASK_CLASS_INPUT = 5,
    MK_TASK_CLASS_VIDEO_PRESENT = 6,
    MK_TASK_CLASS_STORAGE_IO = 7,
    MK_TASK_CLASS_FILESYSTEM_IO = 8,
    MK_TASK_CLASS_AUDIO_IO = 9,
    MK_TASK_CLASS_NETWORK_IO = 10,
    MK_TASK_CLASS_CONSOLE_IO = 11,
    MK_TASK_CLASS_VIDEO_CONTROL = 12
};

#define MK_TASK_CLASS_MASK(task_class) (1u << (task_class))
#define MK_TASK_CLASS_MASK_ALL 0xffffffffu

enum mk_task_event_type {
    MK_TASK_EVENT_NONE = 0,
    MK_TASK_EVENT_LAUNCHED = 1,
    MK_TASK_EVENT_TERMINATED = 2,
    MK_TASK_EVENT_BLOCKED = 3,
    MK_TASK_EVENT_WOKE = 4,
    MK_TASK_EVENT_RESTART_REQUESTED = 5
};

struct mk_task_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t task_class;
    uint32_t priority_tier;
    uint32_t flags;
    uint32_t sequence;
    uint32_t class_pending_depth;
    uint32_t class_dropped_events;
    uint32_t tick;
};

#define MK_TASK_EVENT_MASK_LAUNCHED (1u << MK_TASK_EVENT_LAUNCHED)
#define MK_TASK_EVENT_MASK_TERMINATED (1u << MK_TASK_EVENT_TERMINATED)
#define MK_TASK_EVENT_MASK_BLOCKED (1u << MK_TASK_EVENT_BLOCKED)
#define MK_TASK_EVENT_MASK_WOKE (1u << MK_TASK_EVENT_WOKE)
#define MK_TASK_EVENT_MASK_RESTART_REQUESTED (1u << MK_TASK_EVENT_RESTART_REQUESTED)
#define MK_TASK_EVENT_MASK_LIFECYCLE \
    (MK_TASK_EVENT_MASK_LAUNCHED | \
     MK_TASK_EVENT_MASK_TERMINATED | \
     MK_TASK_EVENT_MASK_RESTART_REQUESTED)
#define MK_TASK_EVENT_MASK_ALL \
    (MK_TASK_EVENT_MASK_LAUNCHED | \
     MK_TASK_EVENT_MASK_TERMINATED | \
     MK_TASK_EVENT_MASK_BLOCKED | \
     MK_TASK_EVENT_MASK_WOKE | \
     MK_TASK_EVENT_MASK_RESTART_REQUESTED)

#define MK_TASK_EVENT_WAIT_FOREVER 0xffffffffu

#define TASK_SNAPSHOT_ABI_VERSION 5u
#define TASK_SNAPSHOT_NAME_MAX 16u
#define TASK_SNAPSHOT_MAX 32u

#define TASK_SNAPSHOT_FLAG_BOOTSTRAP (1u << 0)
#define TASK_SNAPSHOT_FLAG_CRITICAL (1u << 1)
#define TASK_SNAPSHOT_FLAG_BUILTIN (1u << 2)
#define TASK_SNAPSHOT_FLAG_SERVICE_ONLINE (1u << 16)
#define TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED (1u << 17)
#define TASK_SNAPSHOT_FLAG_SERVICE_RESTARTABLE (1u << 18)

enum task_wait_result {
    TASK_WAIT_RESULT_NONE = 0,
    TASK_WAIT_RESULT_SIGNALED = 1,
    TASK_WAIT_RESULT_TIMED_OUT = 2,
    TASK_WAIT_RESULT_CANCELED = 3
};

enum task_wait_event_kind {
    TASK_WAIT_EVENT_NONE = 0,
    TASK_WAIT_EVENT_WAITABLE = 1,
    TASK_WAIT_EVENT_QUEUE = 2,
    TASK_WAIT_EVENT_SIGNAL = 3,
    TASK_WAIT_EVENT_COMPLETION = 4
};

enum task_wait_event_class {
    TASK_WAIT_CLASS_NONE = 0,
    TASK_WAIT_CLASS_GENERIC = 1,
    TASK_WAIT_CLASS_IPC = 2,
    TASK_WAIT_CLASS_INPUT = 3,
    TASK_WAIT_CLASS_STORAGE = 4,
    TASK_WAIT_CLASS_FILESYSTEM = 5,
    TASK_WAIT_CLASS_VIDEO = 6,
    TASK_WAIT_CLASS_AUDIO = 7,
    TASK_WAIT_CLASS_NETWORK = 8,
    TASK_WAIT_CLASS_SUPERVISION = 9
};

struct task_snapshot_entry {
    uint32_t pid;
    uint32_t kind;
    uint32_t state;
    int32_t current_cpu;
    int32_t preferred_cpu;
    int32_t last_cpu;
    uint32_t stack_size;
    uint32_t runtime_ticks;
    uint32_t context_switches;
    uint32_t service_type;
    uint32_t priority_tier;
    uint32_t task_class;
    uint32_t service_restart_count;
    uint32_t flags;
    uint32_t wait_result;
    uint32_t wait_event_kind;
    uint32_t wait_event_class;
    uint32_t wait_owner_service;
    uint32_t wait_deadline;
    uint32_t wait_pending_signals;
    uint32_t last_task_event_sequence;
    uint32_t last_task_event_type;
    uint32_t last_task_event_tick;
    uint32_t task_class_pending_events;
    uint32_t task_class_dropped_events;
    char name[TASK_SNAPSHOT_NAME_MAX];
};

struct task_snapshot_summary {
    uint32_t abi_version;
    uint32_t uptime_ticks;
    uint32_t current_pid;
    uint32_t cpu_count;
    uint32_t started_cpu_count;
    uint32_t running_tasks;
    uint32_t ready_tasks;
    uint32_t blocked_tasks;
    uint32_t total_tasks;
    uint32_t kernel_heap_used;
    uint32_t kernel_heap_free;
    uint32_t physmem_total_kb;
    uint32_t physmem_free_kb;
    uint32_t timed_out_waits;
    uint32_t canceled_waits;
    uint32_t pending_event_signals;
    uint32_t latest_task_event_sequence;
    uint32_t task_class_pending_events;
    uint32_t task_class_dropped_events;
};

typedef void (*userland_entry_t)(void);

#endif
