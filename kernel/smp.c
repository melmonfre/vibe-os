#include <kernel/smp.h>
#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/interrupt.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/paging.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/scheduler.h>
#include <stdint.h>
#include <string.h>

#define AP_TRAMPOLINE_PHYS_ADDR 0x7000u
#define AP_TRAMPOLINE_VECTOR (AP_TRAMPOLINE_PHYS_ADDR >> 12)
#define AP_TRAMPOLINE_GDT_START_OFFSET 0x60u
#define AP_TRAMPOLINE_GDT_DESC_OFFSET 0x78u
#define AP_TRAMPOLINE_PMODE_PTR_OFFSET 0x7Eu
#define AP_TRAMPOLINE_ENTRY_PTR_OFFSET 0x84u
#define AP_TRAMPOLINE_STACK_PTR_OFFSET 0x88u
#define AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET 0x8Cu
#define AP_TRAMPOLINE_PMODE_OFFSET 0x33u
#define AP_TRAMPOLINE_DEBUG_STAGE_ADDR 0x6FF0u
#define AP_STACK_SIZE 4096u
#define AP_START_TIMEOUT 2000000u

enum {
    SMP_CPU_STAGE_NONE = 0u,
    SMP_CPU_STAGE_PREPARED = 1u,
    SMP_CPU_STAGE_INIT_SENT = 2u,
    SMP_CPU_STAGE_SIPI1_SENT = 3u,
    SMP_CPU_STAGE_SIPI2_SENT = 4u,
    SMP_CPU_STAGE_AP_ENTER = 5u,
    SMP_CPU_STAGE_AP_LAPIC = 6u,
    SMP_CPU_STAGE_AP_MARKED = 7u,
    SMP_CPU_STAGE_AP_SCHED = 8u
};

extern const uint8_t _binary_build_ap_trampoline_bin_start[];
extern const uint8_t _binary_build_ap_trampoline_bin_end[];

static volatile uint32_t g_smp_started_cpus = 1u;
static volatile uint32_t g_smp_cpu_stage[32];
static spinlock_t g_smp_boot_lock;
static int g_smp_initialized = 0;
static volatile uint32_t g_smp_scheduler_enabled = 0u;

static uint32_t smp_trampoline_size(void) {
    return (uint32_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
}

static void smp_prepare_trampoline(uint32_t entry, uint32_t stack_top) {
    uint8_t *dst = (uint8_t *)(uintptr_t)AP_TRAMPOLINE_PHYS_ADDR;
    uint32_t gdt_base = AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_GDT_START_OFFSET;
    uint32_t pmode_entry = AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PMODE_OFFSET;
    uint32_t page_dir = paging_is_enabled() ? (uint32_t)paging_page_directory_phys() : 0u;
    uint32_t size = smp_trampoline_size();

    memcpy(dst, _binary_build_ap_trampoline_bin_start, size);
    *(volatile uint8_t *)(uintptr_t)AP_TRAMPOLINE_DEBUG_STAGE_ADDR = 0u;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_GDT_DESC_OFFSET + 2u) = gdt_base;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PMODE_PTR_OFFSET) = pmode_entry;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_ENTRY_PTR_OFFSET) = entry;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_STACK_PTR_OFFSET) = stack_top;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET) = page_dir;
}

static int smp_wait_for_cpu(uint32_t expected_count) {
    uint32_t spins = AP_START_TIMEOUT;
    while (spins-- > 0u) {
        if (g_smp_started_cpus >= expected_count) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static void smp_busy_wait(uint32_t iterations) {
    while (iterations-- > 0u) {
        __asm__ volatile("pause");
    }
}

static void smp_dump_cpu_stages(void) {
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (!cpu) {
            continue;
        }
        kernel_debug_printf("smp: cpu idx=%d apic=%x started=%d stage=%d\n",
                            (int)i,
                            cpu->apic_id,
                            cpu->started,
                            (int)g_smp_cpu_stage[i]);
    }
    kernel_debug_printf("smp: trampoline breadcrumb=%x\n", smp_trampoline_debug_stage());
}

void smp_ap_entry(void) {
    uint32_t apic_id;
    int idx;

    kernel_idt_init();
    local_apic_init();
    apic_id = local_apic_id();
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (cpu && cpu->apic_id == apic_id) {
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_AP_ENTER;
            break;
        }
    }
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (cpu && cpu->apic_id == apic_id) {
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_AP_LAPIC;
            break;
        }
    }
    idx = kernel_cpu_mark_started(apic_id);
    if (idx >= 0 && (uint32_t)idx < 32u) {
        g_smp_cpu_stage[idx] = SMP_CPU_STAGE_AP_MARKED;
    }
    g_smp_started_cpus += 1u;

    kernel_debug_printf("smp: ap online apic=%x idx=%d cpu_index=%d\n",
                        apic_id,
                        idx,
                        (int)kernel_cpu_index());

    if (idx >= 0 && (uint32_t)idx < 32u) {
        g_smp_cpu_stage[idx] = SMP_CPU_STAGE_AP_SCHED;
    }
    for (;;) {
        if (g_smp_scheduler_enabled != 0u) {
            schedule();
        } else {
            __asm__ volatile("cli");
            __asm__ volatile("hlt");
        }
        __asm__ volatile("pause");
    }
}

void smp_init(void) {
    if (g_smp_initialized) {
        return;
    }
    g_smp_initialized = 1;
    spinlock_init(&g_smp_boot_lock);
    for (uint32_t i = 0; i < 32u; ++i) {
        g_smp_cpu_stage[i] = SMP_CPU_STAGE_NONE;
    }
    g_smp_cpu_stage[0] = SMP_CPU_STAGE_AP_SCHED;

    if (!kernel_cpu_is_smp_capable() || !local_apic_enabled() || kernel_cpu_count() <= 1u) {
        return;
    }

    kernel_debug_printf("smp: preparing trampoline at %x (%d bytes)\n",
                        AP_TRAMPOLINE_PHYS_ADDR,
                        (int)smp_trampoline_size());
    kernel_debug_printf("smp: bsp cpu_index=%d apic=%x total=%d\n",
                        (int)kernel_cpu_index(),
                        local_apic_id(),
                        (int)kernel_cpu_count());

    for (uint32_t i = 1; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        void *stack;
        uint32_t stack_top;
        if (!cpu) {
            break;
        }
        stack = kernel_malloc(AP_STACK_SIZE);
        if (!stack) {
            kernel_debug_printf("smp: no stack for cpu idx=%d\n", (int)i);
            break;
        }
        stack_top = (uint32_t)(uintptr_t)stack + AP_STACK_SIZE;

        spinlock_lock(&g_smp_boot_lock);
        smp_prepare_trampoline((uint32_t)(uintptr_t)smp_ap_entry, stack_top);
        g_smp_cpu_stage[i] = SMP_CPU_STAGE_PREPARED;
        kernel_debug_printf("smp: starting ap idx=%d apic=%x stack=%x\n",
                            (int)i,
                            cpu->apic_id,
                            stack_top);
        if (local_apic_send_init(cpu->apic_id) == 0) {
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_INIT_SENT;
            kernel_debug_printf("smp: init sent apic=%x\n", cpu->apic_id);
            smp_busy_wait(200000u);
            if (local_apic_send_startup(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR) == 0) {
                g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI1_SENT;
                kernel_debug_printf("smp: sipi1 sent apic=%x vec=%x\n",
                                    cpu->apic_id,
                                    (unsigned)AP_TRAMPOLINE_VECTOR);
            }
            smp_busy_wait(50000u);
            if (local_apic_send_startup(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR) == 0) {
                g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI2_SENT;
                kernel_debug_printf("smp: sipi2 sent apic=%x vec=%x\n",
                                    cpu->apic_id,
                                    (unsigned)AP_TRAMPOLINE_VECTOR);
            }
        } else {
            kernel_debug_printf("smp: init failed apic=%x\n", cpu->apic_id);
        }
        spinlock_unlock(&g_smp_boot_lock);

        if (smp_wait_for_cpu(i + 1u) != 0) {
            kernel_debug_printf("smp: ap idx=%d apic=%x did not respond\n",
                                (int)i,
                                cpu->apic_id);
            smp_dump_cpu_stages();
            break;
        }
    }

    kernel_debug_printf("smp: online cpus=%d/%d\n",
                        (int)g_smp_started_cpus,
                        (int)kernel_cpu_count());
    kernel_debug_puts("smp: aps online and parked until per-cpu scheduler bring-up is implemented\n");
}

uint32_t smp_started_cpu_count(void) {
    return g_smp_started_cpus;
}

uint32_t smp_cpu_stage(uint32_t cpu_index) {
    if (cpu_index >= 32u) {
        return SMP_CPU_STAGE_NONE;
    }
    return g_smp_cpu_stage[cpu_index];
}

uint8_t smp_trampoline_debug_stage(void) {
    return *(volatile uint8_t *)(uintptr_t)AP_TRAMPOLINE_DEBUG_STAGE_ADDR;
}
