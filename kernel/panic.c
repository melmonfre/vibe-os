#include <kernel/kernel.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>

void kernel_panic(const char *msg) {
    /* Serial first: the VGA text path can itself be dead (video state
     * corrupt); a silent panic in a QEMU headless run is undebuggable. */
    kernel_debug_printf("kernel_panic: %s\n", msg != 0 ? msg : "(null)");
    kernel_text_clear();
    kernel_text_puts("KERNEL PANIC:\n");
    if (msg) {
        kernel_text_puts(msg);
        kernel_text_putc('\n');
    }
    for (;;)
        __asm__ volatile("hlt");
}
