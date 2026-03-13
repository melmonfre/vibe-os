#include <stddef.h>
#include <stdint.h>
#include <kernel/userland.h>

typedef void (*userland_entry_t)(void);

/* userland now linked into kernel; entry is a normal symbol */
extern void userland_entry(void);

__attribute__((noreturn)) void userland_run(void) {
    /* debug breadcrumbs */
    extern void kernel_text_puts(const char *);
    extern void kernel_debug_puts(const char *);
    kernel_text_puts("UL jump...\n");
    kernel_debug_puts("userland_run: jump\n");

    /*
     * Userland is linked directly inside the kernel image now, so its .bss
     * lives inside the kernel address space. Keep the userland stack far above
     * the embedded image and below the kernel bump heap at 0x500000.
     */
    uint32_t stack_top = 0x480000;
    stack_top = (stack_top + 0xF) & ~0xFu; /* align 16 */

    __asm__ volatile("cli\n\t"
                     "mov %0, %%esp\n\t"
                     "sti\n\t"
                     :
                     : "r"(stack_top)
                     : "memory");

    userland_entry_t entry = userland_entry;
    entry();

    __builtin_unreachable();
}
