#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <kernel/bootinfo.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/microkernel/audio.h>

#define BOOTSTRAP_STORAGE_SMOKE_SECTOR (KERNEL_PERSIST_START_LBA + KERNEL_PERSIST_SECTOR_COUNT - 1u)
#define BOOTSTRAP_STORAGE_SMOKE_SIZE 512u
#define BOOTSTRAP_SERVICE_EVENT_TIMEOUT_TICKS 0u
#define BOOTSTRAP_TASK_EVENT_TIMEOUT_TICKS 0u

static const char *bootstrap_service_name(uint32_t service_type) {
    switch (service_type) {
    case 1u:
        return "init";
    case 2u:
        return "storage";
    case 3u:
        return "filesystem";
    case 4u:
        return "video";
    case 5u:
        return "input";
    case 6u:
        return "console";
    case 7u:
        return "network";
    case 8u:
        return "audio";
    default:
        return "unknown";
    }
}

static const char *bootstrap_service_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_SERVICE_EVENT_ONLINE:
        return "online";
    case MK_SERVICE_EVENT_OFFLINE:
        return "offline";
    case MK_SERVICE_EVENT_DEGRADED:
        return "degraded";
    case MK_SERVICE_EVENT_RECOVERED:
        return "recovered";
    case MK_SERVICE_EVENT_RESTARTED:
        return "restarted";
    default:
        return "unknown";
    }
}

static void bootstrap_append_u32(char *buffer, int size, uint32_t value) {
    char digits[16];
    int length = 0;

    if (buffer == 0 || size <= 0) {
        return;
    }

    if (value == 0u) {
        str_append(buffer, "0", size);
        return;
    }

    while (value != 0u && length < (int)sizeof(digits)) {
        digits[length++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (length > 0) {
        char text[2];

        text[0] = digits[length - 1];
        text[1] = '\0';
        str_append(buffer, text, size);
        length -= 1;
    }
}

static void bootstrap_log_service_event(const struct mk_service_event *event) {
    char buffer[160];

    if (event == 0 || event->event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    buffer[0] = '\0';
    str_append(buffer, "init: service-event ", (int)sizeof(buffer));
    str_append(buffer, bootstrap_service_name(event->service_type), (int)sizeof(buffer));
    str_append(buffer, " ", (int)sizeof(buffer));
    str_append(buffer, bootstrap_service_event_name(event->event_type), (int)sizeof(buffer));
    str_append(buffer, " pid=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->pid);
    str_append(buffer, " restarts=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->restart_count);
    str_append(buffer, " tick=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->tick);
    str_append(buffer, "\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static const char *bootstrap_task_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_TASK_EVENT_LAUNCHED:
        return "launched";
    case MK_TASK_EVENT_TERMINATED:
        return "terminated";
    case MK_TASK_EVENT_BLOCKED:
        return "blocked";
    case MK_TASK_EVENT_WOKE:
        return "woke";
    case MK_TASK_EVENT_RESTART_REQUESTED:
        return "restart-requested";
    default:
        return "unknown";
    }
}

static const char *bootstrap_task_priority_name(uint32_t priority_tier) {
    switch (priority_tier) {
    case 0u:
        return "desktop";
    case 1u:
        return "input";
    case 2u:
        return "video";
    case 3u:
        return "storage";
    case 4u:
        return "audio";
    case 5u:
        return "network";
    case 6u:
        return "app";
    default:
        return "background";
    }
}

static const char *bootstrap_task_class_name(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_SUPERVISION:
        return "supervision";
    case MK_TASK_CLASS_DESKTOP:
        return "desktop";
    case MK_TASK_CLASS_SHELL:
        return "shell";
    case MK_TASK_CLASS_APP_RUNTIME:
        return "app";
    case MK_TASK_CLASS_INPUT:
        return "input";
    case MK_TASK_CLASS_VIDEO_PRESENT:
        return "video-present";
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return "video-control";
    case MK_TASK_CLASS_STORAGE_IO:
        return "storage";
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return "filesystem";
    case MK_TASK_CLASS_AUDIO_IO:
        return "audio";
    case MK_TASK_CLASS_NETWORK_IO:
        return "network";
    case MK_TASK_CLASS_CONSOLE_IO:
        return "console";
    default:
        return "unknown";
    }
}

static void bootstrap_log_task_event(const struct mk_task_event *event) {
    char buffer[224];

    if (event == 0 || event->event_type == MK_TASK_EVENT_NONE) {
        return;
    }

    buffer[0] = '\0';
    str_append(buffer, "init: task-event ", (int)sizeof(buffer));
    str_append(buffer, bootstrap_task_event_name(event->event_type), (int)sizeof(buffer));
    str_append(buffer, " pid=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->pid);
    str_append(buffer, " prio=", (int)sizeof(buffer));
    str_append(buffer, bootstrap_task_priority_name(event->priority_tier), (int)sizeof(buffer));
    str_append(buffer, " class=", (int)sizeof(buffer));
    str_append(buffer, bootstrap_task_class_name(event->task_class), (int)sizeof(buffer));
    str_append(buffer, " service=", (int)sizeof(buffer));
    str_append(buffer, bootstrap_service_name(event->service_type), (int)sizeof(buffer));
    str_append(buffer, " seq=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->sequence);
    str_append(buffer, " q=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->class_pending_depth);
    str_append(buffer, " drop=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->class_dropped_events);
    str_append(buffer, " tick=", (int)sizeof(buffer));
    bootstrap_append_u32(buffer, (int)sizeof(buffer), event->tick);
    str_append(buffer, "\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static void bootstrap_subscribe_supervision_events(void) {
    uint32_t service_type;

    for (service_type = 1u; service_type <= 8u; ++service_type) {
        if (service_type == 1u) {
            continue;
        }
        (void)sys_service_subscribe(service_type);
    }
}

static void bootstrap_print_banner(void) {
    struct userland_launch_info info;

    sys_write_debug("VibeOS bootstrap init\n");
    sys_write_debug("kernel pequeno, apps externas via AppFS\n");
    sys_write_debug("userland.app carregada automaticamente no boot\n");
    sys_write_debug("use 'help' para listar comandos e apps modulares\n");
    sys_write_debug("atalhos graficos: startx, edit, nano\n");
    if (sys_launch_info(&info) == 0) {
        if ((info.boot_flags & BOOTINFO_FLAG_BOOT_SAFE_MODE) != 0u) {
            sys_write_debug("boot mode: safe mode\n");
        } else if ((info.boot_flags & BOOTINFO_FLAG_BOOT_RESCUE_SHELL) != 0u) {
            sys_write_debug("boot mode: rescue shell\n");
        }
        if ((info.boot_flags & BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT) != 0u) {
            sys_write_debug("video: i915 experimental commit enabled\n");
        }
        if ((info.boot_flags & BOOTINFO_FLAG_FORCE_LEGACY_VIDEO) != 0u) {
            sys_write_debug("video: legacy video driver forced\n");
        }
    }
}

static int bootstrap_run_startup_apps(void) {
    if (sys_launch_app("userland") > 0) {
        sys_write_debug("init: userland.app launched\n");
        return 0;
    }
    sys_write_debug("init: userland.app launch failed\n");
    return -1;
}

static void bootstrap_storage_smoke_test(void) {
    uint8_t verify[BOOTSTRAP_STORAGE_SMOKE_SIZE];
    uint8_t verify_again[BOOTSTRAP_STORAGE_SMOKE_SIZE];
    extern void kernel_debug_puts(const char *);

    kernel_debug_puts("init: storage smoke begin\n");
    if (sys_storage_read_sectors(BOOTSTRAP_STORAGE_SMOKE_SECTOR, verify, 1u) != 0) {
        kernel_debug_puts("init: storage smoke read failed\n");
        return;
    }
    if (sys_storage_read_sectors(BOOTSTRAP_STORAGE_SMOKE_SECTOR, verify_again, 1u) != 0) {
        kernel_debug_puts("init: storage smoke verify read failed\n");
        return;
    }
    for (uint32_t i = 0; i < BOOTSTRAP_STORAGE_SMOKE_SIZE; ++i) {
        if (verify[i] != verify_again[i]) {
            kernel_debug_puts("init: storage smoke verify mismatch\n");
            return;
        }
    }
    kernel_debug_puts("init: storage smoke ok\n");
}

static void bootstrap_prime_kernel_service_stack(void) {
    struct mk_audio_info audio_info = {0};

    /*
     * Early video GET_CAPS probing regressed desktop boot by waking the video
     * service before the new bootstrap path had fully settled. Keep the audio
     * prime, which is cheap and directly useful for backend diagnostics, and
     * defer video probing to the desktop/runtime path.
     */
    sys_write_debug("init: video service prime deferred\n");

    if (sys_audio_get_info(&audio_info) == 0) {
        sys_write_debug("init: audio service primed\n");
    } else {
        sys_write_debug("init: audio service prime failed\n");
    }
}

static void bootstrap_try_play_boot_sound(void) {
    if (sys_launch_builtin_user(USERLAND_BUILTIN_BOOT_AUDIO) > 0) {
        sys_write_debug("init: boot audio host launched\n");
        sys_write_debug("init: boot sound returned\n");
        return;
    }
    sys_write_debug("init: boot audio host launch failed\n");
}

static void bootstrap_launch_runtime_service_apps(uint32_t boot_flags) {
    if ((boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) != 0u) {
        sys_write_debug("init: runtime service apps skipped by boot flags\n");
        return;
    }

    {
        char *audio_apply_argv[4] = {"audiosvc", "apply-settings", "/config/audio.cfg", 0};
        if (sys_launch_app_argv(3, audio_apply_argv) > 0) {
            sys_write_debug("init: launched boot audio state sync\n");
        } else {
            sys_write_debug("init: boot audio state sync launch failed\n");
        }
    }

    if ((boot_flags & BOOTINFO_FLAG_BOOT_TO_DESKTOP) != 0u) {
        sys_write_debug("init: boot network reconcile deferred to desktop session\n");
        return;
    }

    {
        char *network_reconcile_argv[3] = {"netmgrd", "reconcile", 0};
        if (sys_launch_app_argv(2, network_reconcile_argv) > 0) {
            sys_write_debug("init: launched boot network reconcile\n");
        } else {
            sys_write_debug("init: boot network reconcile launch failed\n");
        }
    }
}

__attribute__((section(".entry"))) void userland_entry(void) {
    extern void kernel_debug_puts(const char *);
    int rc;
    struct userland_launch_info info = {0};
    struct mk_service_event event;

    kernel_debug_puts("init: entered builtin entry\n");
    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT) != 0u) {
        kernel_debug_puts("init: boot flag enabled for i915 experimental commit\n");
    }
    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & BOOTINFO_FLAG_FORCE_LEGACY_VIDEO) != 0u) {
        kernel_debug_puts("init: boot flag forcing legacy video driver\n");
    }
    sys_write_debug("init: builtin bootstrap launching external AppFS apps\n");
    kernel_debug_puts("init: sys_write_debug returned\n");

    console_init();
    kernel_debug_puts("init: console_init returned\n");

    fs_init();
    kernel_debug_puts("init: fs_init returned\n");
    bootstrap_storage_smoke_test();
    bootstrap_prime_kernel_service_stack();
    bootstrap_try_play_boot_sound();

    sys_write_debug("init: appfs launcher ready\n");
    kernel_debug_puts("init: appfs launcher ready\n");

    bootstrap_print_banner();
    kernel_debug_puts("init: banner returned\n");
    bootstrap_launch_runtime_service_apps(info.boot_flags);

    rc = bootstrap_run_startup_apps();
    if (rc != 0) {
        int shell_pid = sys_launch_builtin_user(USERLAND_BUILTIN_SHELL);

        if (shell_pid > 0) {
            if (rc == -2) {
                console_write("init: rescue shell host ativa\n");
            } else {
                console_write("init: fallback para shell host embutida\n");
            }
            kernel_debug_puts("init: shell host launched\n");
        } else {
            console_write("init: shell host launch failed\n");
            kernel_debug_puts("init: shell host launch failed\n");
        }
    }
    kernel_debug_puts("init: supervisor idle\n");
    bootstrap_subscribe_supervision_events();
    (void)sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_LIFECYCLE,
                                        MK_TASK_CLASS_MASK_ALL);
    for (;;) {
        uint32_t service_type;
        int handled = 0;
        struct mk_task_event task_event;

        for (service_type = 1u; service_type <= 8u; ++service_type) {
            if (service_type == 1u) {
                continue;
            }
            if (sys_service_event_receive(service_type,
                                          &event,
                                          BOOTSTRAP_SERVICE_EVENT_TIMEOUT_TICKS) == 0) {
                bootstrap_log_service_event(&event);
                handled = 1;
            }
        }
        while (sys_task_event_receive(&task_event, BOOTSTRAP_TASK_EVENT_TIMEOUT_TICKS) == 0) {
            bootstrap_log_task_event(&task_event);
            handled = 1;
        }
        /*
         * Supervision must stay observational: once we drained the current
         * burst of lifecycle/service events, hand control back so freshly woke
         * desktop or service tasks can consume their pending IPC immediately.
         */
        (void)handled;
        sys_yield();
    }
}
