#ifndef USERLAND_API_H
#define USERLAND_API_H

#include <stdint.h>

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
    SYSCALL_AUDIO_READ = 75
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
    uint8_t buttons;
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
    uint32_t boot_flags;
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
    char name[16];
};

#define TASK_SNAPSHOT_ABI_VERSION 1u
#define TASK_SNAPSHOT_NAME_MAX 16u
#define TASK_SNAPSHOT_MAX 32u

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
    uint32_t flags;
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
};

typedef void (*userland_entry_t)(void);

#endif
