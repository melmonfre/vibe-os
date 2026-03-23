#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/shell.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <kernel/drivers/storage/ata.h>

#define BOOTSTRAP_STORAGE_SMOKE_SECTOR (KERNEL_PERSIST_START_LBA + KERNEL_PERSIST_SECTOR_COUNT - 1u)
#define BOOTSTRAP_STORAGE_SMOKE_SIZE 512u

static void bootstrap_print_banner(void) {
    console_write("VibeOS bootstrap init\n");
    console_write("kernel pequeno, apps externas via AppFS\n");
    console_write("userland.app carregada automaticamente no boot\n");
    console_write("comandos: help, apps, clear, shutdown, run <app> [args]\n");
}

static int bootstrap_run_startup_apps(void) {
    char *argv[2];
    int rc;
    extern void kernel_debug_puts(const char *);

    argv[0] = "userland";
    argv[1] = 0;

    kernel_debug_puts("init: startup app begin\n");
    rc = lang_try_run(1, argv);
    if (rc == 0) {
        kernel_debug_puts("init: userland app returned\n");
    } else {
        kernel_debug_puts("init: userland app missing\n");
    }
    return rc;
}

static void bootstrap_storage_smoke_test(void) {
    uint8_t original[BOOTSTRAP_STORAGE_SMOKE_SIZE];
    uint8_t verify[BOOTSTRAP_STORAGE_SMOKE_SIZE];
    extern void kernel_debug_puts(const char *);

    kernel_debug_puts("init: storage smoke begin\n");
    if (sys_storage_read_sectors(BOOTSTRAP_STORAGE_SMOKE_SECTOR, original, 1u) != 0) {
        kernel_debug_puts("init: storage smoke read failed\n");
        return;
    }
    if (sys_storage_write_sectors(BOOTSTRAP_STORAGE_SMOKE_SECTOR, original, 1u) != 0) {
        kernel_debug_puts("init: storage smoke write failed\n");
        return;
    }
    if (sys_storage_read_sectors(BOOTSTRAP_STORAGE_SMOKE_SECTOR, verify, 1u) != 0) {
        kernel_debug_puts("init: storage smoke verify read failed\n");
        return;
    }
    for (uint32_t i = 0; i < BOOTSTRAP_STORAGE_SMOKE_SIZE; ++i) {
        if (original[i] != verify[i]) {
            kernel_debug_puts("init: storage smoke verify mismatch\n");
            return;
        }
    }
    kernel_debug_puts("init: storage smoke ok\n");
}

__attribute__((section(".entry"))) void userland_entry(void) {
    extern void kernel_debug_puts(const char *);
    int rc;

    kernel_debug_puts("init: entered builtin entry\n");
    sys_write_debug("init: builtin bootstrap launching external AppFS apps\n");
    kernel_debug_puts("init: sys_write_debug returned\n");

    console_init();
    kernel_debug_puts("init: console_init returned\n");

    fs_init();
    kernel_debug_puts("init: fs_init returned\n");
    bootstrap_storage_smoke_test();

    sys_write_debug("init: appfs launcher ready\n");
    kernel_debug_puts("init: appfs launcher ready\n");

    bootstrap_print_banner();
    kernel_debug_puts("init: banner returned\n");

    rc = bootstrap_run_startup_apps();
    if (rc != 0) {
        console_write("init: fallback para shell embutido userland/modules\n");
    }
    kernel_debug_puts("init: bootstrap shell active\n");
    shell_start_ready();
    kernel_debug_puts("init: bootstrap shell returned, halting\n");
    for (;;)
        __asm__ volatile("hlt");
}
