#include <kernel/bootinfo.h>
#include <string.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/shell.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

static void host_debug(const char *prefix, const char *suffix) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    if (suffix != 0) {
        str_append(msg, suffix, (int)sizeof(msg));
    }
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void host_debug_argv(int argc, char **argv) {
    char msg[160];

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return;
    }

    msg[0] = '\0';
    str_append(msg, "host: argv ", (int)sizeof(msg));
    str_append(msg, argv[0], (int)sizeof(msg));
    if (argc > 1 && argv[1] != 0) {
        str_append(msg, " ", (int)sizeof(msg));
        str_append(msg, argv[1], (int)sizeof(msg));
    }
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static int host_try_external_argv(int argc, char **argv) {
    lang_invalidate_directory_cache();
    return lang_try_run(argc, argv);
}

static int host_wait_for_task_exit(uint32_t pid) {
    struct mk_task_event event;

    if (pid == 0u) {
        return -1;
    }

    for (;;) {
        if (sys_task_event_receive(&event, MK_TASK_EVENT_WAIT_FOREVER) != 0) {
            return -1;
        }
        if (event.pid != pid) {
            continue;
        }
        if (event.event_type == MK_TASK_EVENT_TERMINATED) {
            return 0;
        }
    }
}

static int host_launch_external_argv_and_wait(int argc, char **argv) {
    int pid;

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return -1;
    }

    lang_invalidate_directory_cache();
    if (!lang_can_run(argv[0])) {
        return -1;
    }
    if (sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_TERMINATED,
                                      MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME)) != 0) {
        return -1;
    }

    host_debug_argv(argc, argv);
    pid = argc == 1 ? sys_launch_app(argv[0]) : sys_launch_app_argv(argc, argv);
    if (pid <= 0) {
        return -1;
    }
    return host_wait_for_task_exit((uint32_t)pid);
}

static int host_decode_launch_argv(const struct userland_launch_info *info,
                                   char *storage,
                                   size_t storage_size,
                                   char **argv_out,
                                   int argv_max) {
    uint32_t argc;
    uint32_t offset = 0u;

    if (info == 0 || storage == 0 || argv_out == 0 || argv_max <= 1) {
        return -1;
    }

    argc = info->argc;
    if (argc == 0u || argc >= (uint32_t)argv_max) {
        return -1;
    }

    memcpy(storage, info->argv_data, storage_size);
    storage[storage_size - 1u] = '\0';
    for (uint32_t arg_index = 0; arg_index < argc; ++arg_index) {
        size_t len;

        if (offset >= storage_size || storage[offset] == '\0') {
            return -1;
        }
        argv_out[arg_index] = &storage[offset];
        len = strlen(argv_out[arg_index]);
        offset += (uint32_t)len + 1u;
    }
    argv_out[argc] = 0;
    return (int)argc;
}

void userland_app_host_entry(void) {
    struct userland_launch_info info;
    char argv_storage[USERLAND_LAUNCH_ARGV_BYTES];
    char name[sizeof(info.name)];
    char *argv[USERLAND_LAUNCH_ARGC_MAX + 1];
    int argc;

    console_init();
    fs_init();
    host_debug("host: app start", 0);

    if (sys_launch_info(&info) != 0 || info.name[0] == '\0') {
        host_debug("host: app missing launch info", 0);
        return;
    }

    memcpy(name, info.name, sizeof(name));
    name[sizeof(name) - 1u] = '\0';
    argc = host_decode_launch_argv(&info, argv_storage, sizeof(argv_storage), argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc <= 0) {
        argv[0] = name;
        argv[1] = 0;
        argc = 1;
    }
    host_debug_argv(argc, argv);

    if (host_try_external_argv(argc, argv) == 0) {
        host_debug("host: app returned ", name);
        return;
    }

    host_debug("host: app launch failed ", name);
}

static int desktop_host_wait_session_event(int session_pid) {
    struct mk_task_event event;

    if (session_pid <= 0) {
        return -1;
    }

    for (;;) {
        if (sys_task_event_receive(&event, MK_TASK_EVENT_WAIT_FOREVER) != 0) {
            return -1;
        }
        if ((int)event.pid != session_pid) {
            continue;
        }
        if (event.event_type == MK_TASK_EVENT_TERMINATED) {
            return 0;
        }
    }
}

static int desktop_host_launch_startx_session(void) {
    int pid = sys_launch_builtin_user(USERLAND_BUILTIN_STARTX);

    if (pid > 0) {
        host_debug("host: desktop session launched", 0);
        return pid;
    }

    host_debug("host: desktop session launch failed", 0);
    return -1;
}

void userland_shell_host_entry(void) {
    host_debug("host: shell start", 0);
    console_init();
    fs_init();
    shell_start_ready();
    host_debug("host: shell returned", 0);
    for (;;) {
        sys_yield();
    }
}

void userland_desktop_host_entry(void) {
    struct userland_launch_info info;
    int session_pid = -1;

    host_debug("host: desktop start", 0);
    console_init();
    fs_init();
    if (sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_TERMINATED,
                                      MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP)) != 0) {
        host_debug("host: desktop subscribe failed", 0);
    }

    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) == 0u) {
        session_pid = desktop_host_launch_startx_session();
        if (session_pid > 0) {
            for (;;) {
                if (desktop_host_wait_session_event(session_pid) == 0) {
                    host_debug("host: desktop session exited", 0);
                    session_pid = desktop_host_launch_startx_session();
                    if (session_pid <= 0) {
                        break;
                    }
                } else {
                    host_debug("host: desktop session wait failed", 0);
                    break;
                }
            }
        } else {
            host_debug("host: desktop fallback builtin", 0);
            desktop_main();
        }
    } else {
        host_debug("host: desktop denied by boot flags", 0);
    }

    host_debug("host: desktop -> shell fallback", 0);
    shell_start_ready();
    for (;;) {
        sys_yield();
    }
}

void userland_startx_host_entry(void) {
    char *argv[2] = {"startx", 0};

    host_debug("host: startx start", 0);
    console_init();
    fs_init();

    if (host_launch_external_argv_and_wait(1, argv) == 0) {
        host_debug("host: startx external returned", 0);
        return;
    }

    host_debug("host: startx fallback builtin", 0);
    desktop_main();
    host_debug("host: startx builtin returned", 0);
}

void userland_desktop_audio_host_entry(void) {
    char *argv[4] = {"audiosvc", "play-asset", "/assets/vibe_os_desktop.wav", 0};

    host_debug("host: desktop audio start", 0);
    console_init();
    fs_init();

    if (host_launch_external_argv_and_wait(3, argv) == 0) {
        host_debug("host: desktop audio returned", 0);
        return;
    }

    host_debug("host: desktop audio fallback", 0);
    (void)audio_play_wav_best_effort("/assets/vibe_os_desktop.wav", "desktop-session");
    host_debug("host: desktop audio done", 0);
}

void userland_boot_audio_host_entry(void) {
    char *argv[4] = {"audiosvc", "play-asset", "/assets/vibe_os_boot.wav", 0};

    host_debug("host: boot audio start", 0);
    console_init();
    fs_init();

    if (host_launch_external_argv_and_wait(3, argv) == 0) {
        host_debug("host: boot audio returned", 0);
        return;
    }

    host_debug("host: boot audio fallback", 0);
    (void)audio_play_wav_best_effort("/assets/vibe_os_boot.wav", "boot");
    host_debug("host: boot audio done", 0);
}
