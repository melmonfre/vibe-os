#include <stdint.h>
#include <kernel/bootinfo.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/shell.h>

#ifdef VIBE_USERLAND_APP
static void userland_app_boot_debug(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

int vibe_app_main(int argc, char **argv) {
    struct userland_launch_info info;
    char *startx_argv[2] = {"startx", 0};

    (void)argc;
    (void)argv;
    userland_app_boot_debug("userland.app: shell start\n");
    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & BOOTINFO_FLAG_BOOT_TO_DESKTOP) != 0u &&
        (info.boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) == 0u) {
        userland_app_boot_debug("userland.app: autostart startx\n");
        (void)busybox_main(1, startx_argv);
    }
    shell_start();
    return 0;
}
#else
__attribute__((section(".entry"))) void userland_entry(void) {
    shell_start(); /* blocks until exit */
    for (;;)
        __asm__ volatile("hlt");
}
#endif
