#include <kernel/kernel.h>
#include <kernel/microkernel.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/userland.h>

extern void userland_entry(void);

static const struct mk_launch_descriptor g_init_launch = {
    .abi_version = MK_LAUNCH_ABI_VERSION,
    .kind = MK_LAUNCH_KIND_SERVICE,
    .service_type = MK_SERVICE_INIT,
    .flags = MK_LAUNCH_FLAG_BOOTSTRAP | MK_LAUNCH_FLAG_CRITICAL | MK_LAUNCH_FLAG_BUILTIN,
    .stack_size = 65536u,
    .name = "init",
    .entry = userland_entry,
};

__attribute__((noreturn)) void userland_run(void) {
    int pid;

    extern void kernel_text_puts(const char *);
    extern void kernel_debug_puts(const char *);
    kernel_text_puts("UL bootstrap...\n");
    kernel_debug_puts("userland_run: bootstrap init service\n");

    pid = mk_launch_bootstrap(&g_init_launch);
    if (pid < 0) {
        kernel_panic("userland bootstrap failed");
    }

    kernel_text_puts("UL schedule...\n");
    scheduler_set_preemption_ready(1);
    smp_scheduler_enable();
    schedule();
    kernel_panic("scheduler returned after init bootstrap");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
