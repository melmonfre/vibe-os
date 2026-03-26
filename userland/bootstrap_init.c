#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/shell.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <kernel/bootinfo.h>
#include <kernel/drivers/storage/ata.h>

#define BOOTSTRAP_STORAGE_SMOKE_SECTOR (KERNEL_PERSIST_START_LBA + KERNEL_PERSIST_SECTOR_COUNT - 1u)
#define BOOTSTRAP_STORAGE_SMOKE_SIZE 512u

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
    char *userland_argv[2];
    char *startx_argv[2];
    int rc;
    int used_direct_startx = 0;
    struct userland_launch_info info;
    extern void kernel_debug_puts(const char *);

    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & BOOTINFO_FLAG_BOOT_RESCUE_SHELL) != 0u) {
        kernel_debug_puts("init: rescue shell requested, skipping userland app\n");
        return -2;
    }

    userland_argv[0] = "userland";
    userland_argv[1] = 0;
    startx_argv[0] = "startx";
    startx_argv[1] = 0;

    kernel_debug_puts("init: startup app begin\n");
    lang_invalidate_directory_cache();
    rc = lang_try_run(1, userland_argv);
    if (rc != 0) {
        kernel_debug_puts("init: userland app first attempt failed, retrying\n");
        sys_yield();
        lang_invalidate_directory_cache();
        rc = lang_try_run(1, userland_argv);
    }
    if (rc != 0 &&
        (info.boot_flags & BOOTINFO_FLAG_BOOT_TO_DESKTOP) != 0u &&
        (info.boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) == 0u) {
        kernel_debug_puts("init: userland app unavailable, trying startx directly\n");
        used_direct_startx = 1;
        sys_yield();
        lang_invalidate_directory_cache();
        rc = lang_try_run(1, startx_argv);
        if (rc != 0) {
            kernel_debug_puts("init: direct startx failed, retrying\n");
            sys_yield();
            lang_invalidate_directory_cache();
            rc = lang_try_run(1, startx_argv);
        }
    }
    if (rc == 0) {
        if (used_direct_startx) {
            kernel_debug_puts("init: direct startx returned\n");
        } else {
            kernel_debug_puts("init: userland app returned\n");
        }
    } else {
        kernel_debug_puts("init: userland app missing\n");
    }
    return rc;
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

static void bootstrap_try_play_boot_sound(void) {
    (void)audio_play_wav_best_effort("/assets/vibe_os_boot.wav", "boot");
    sys_write_debug("init: boot sound returned\n");
}

__attribute__((section(".entry"))) void userland_entry(void) {
    extern void kernel_debug_puts(const char *);
    int rc;
    struct userland_launch_info info;

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
    bootstrap_try_play_boot_sound();

    sys_write_debug("init: appfs launcher ready\n");
    kernel_debug_puts("init: appfs launcher ready\n");

    bootstrap_print_banner();
    kernel_debug_puts("init: banner returned\n");

    rc = bootstrap_run_startup_apps();
    if (rc != 0) {
        if (rc == -2) {
            console_write("init: rescue shell builtin ativa\n");
        } else {
            console_write("init: fallback para shell embutido userland/modules\n");
        }
    }
    kernel_debug_puts("init: bootstrap shell active\n");
    shell_start_ready();
    kernel_debug_puts("init: bootstrap shell returned, halting\n");
    for (;;)
        __asm__ volatile("hlt");
}
