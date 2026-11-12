#include <kernel/kernel.h>
#include <kernel/cpu/cpu.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>

/* Lock-free serial path (kernel/lock.c stuck detector). Panic must reach
 * the wire even when the deadlocked lock IS the serial lock: the nested
 * owner guard covers same-CPU reentry, but a foreign CPU holding the
 * serial lock would swallow a printf-based panic forever. */
extern void kernel_serial_puts_raw(const char *s);

/*
 * KNOWN_BUGS #27 (12/11): text-slot stack scan on EVERY panic. The ksh
 * grandchild dies with kernel-mode GP `eip=<neto window>-0x5e cs=8` when
 * its LAUNCH_APP fails; ESP/frame-drift probes were zero (refuted
 * displaced-iret), and the GP handler itself resists new instrumentation
 * in this build (see git notes), so the scan lives in the one path every
 * kernel death reaches -- kernel_panic. The tail-jmp from the exception
 * handler leaves the caller chain in range below the current ESP.
 */
extern char __kernel_text_start[];
extern char __kernel_text_end[];

static void panic_stack_backtrace(void) {
    volatile const uint32_t *sp;
    uint32_t base;
    uint32_t printed = 0u;
    uint32_t i;

    __asm__ volatile("mov %%esp, %0" : "=r"(base));
    base = (base + 3u) & ~3u;
    sp = (volatile const uint32_t *)(uintptr_t)base;
    kernel_debug_printf("panic-bt: sp=%x text=[%x,%x)\n",
                        (unsigned int)base,
                        (unsigned int)(uintptr_t)__kernel_text_start,
                        (unsigned int)(uintptr_t)__kernel_text_end);
    for (i = 0u; i < 96u && printed < 16u; ++i) {
        uint32_t w = sp[i];
        if (w >= (uint32_t)(uintptr_t)__kernel_text_start &&
            w <  (uint32_t)(uintptr_t)__kernel_text_end) {
            kernel_debug_printf("panic-bt: [%u] 0x%x\n", (unsigned int)i, w);
            printed += 1u;
        }
    }
}

void kernel_panic(const char *msg) {
    /* Raw first (survives any lock state), then the formatted paths. */
    kernel_serial_puts_raw("kernel_panic: ");
    kernel_serial_puts_raw(msg != 0 ? msg : "(null)");
    kernel_serial_puts_raw("\n");
    /* Serial next: the VGA text path can itself be dead (video state
     * corrupt); a silent panic in a QEMU headless run is undebuggable. */
    kernel_debug_printf("kernel_panic: %s\n", msg != 0 ? msg : "(null)");
    /* #27 caller-chain scan BEFORE the other dumps may relocate ESP. */
    panic_stack_backtrace();
    /* Face-1: which esp0 installations armed this death? (class #5, P4) */
    kernel_tss_install_ring_dump("panic");
    smp_tick_counts_dump();
    scheduler_frame_counts_dump();
    scheduler_save_ledger_dump();
    kernel_text_clear();
    kernel_text_puts("KERNEL PANIC:\n");
    if (msg) {
        kernel_text_puts(msg);
        kernel_text_putc('\n');
    }
    for (;;)
        __asm__ volatile("hlt");
}
