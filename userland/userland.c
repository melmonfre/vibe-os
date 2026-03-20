#include <userland/modules/include/shell.h>

__attribute__((section(".entry"))) void userland_entry(void) {
    shell_start(); /* blocks until exit */
    for (;;)
        __asm__ volatile("hlt");
}
