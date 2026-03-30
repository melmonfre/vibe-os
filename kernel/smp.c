#include <kernel/smp.h>
#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/memory/paging.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/scheduler.h>
#include <stdint.h>
#include <string.h>

#define BOOTDEBUG_ADDR 0x00001000u
#define BOOTDEBUG_MAGIC 0x47444256u
#define BOOTDEBUG_DIRTY 1u
#define BOOTDEBUG_TRACE_MAX 48u
#define AP_TRAMPOLINE_PHYS_ADDR 0x7000u
#define SMP_WAKE_VECTOR 0x82u
#define AP_TRAMPOLINE_VECTOR (AP_TRAMPOLINE_PHYS_ADDR >> 12)
#define AP_TRAMPOLINE_GDT_START_OFFSET 0x80u
#define AP_TRAMPOLINE_GDT_DESC_OFFSET 0x98u
#define AP_TRAMPOLINE_PMODE_PTR_OFFSET 0x9Eu
#define AP_TRAMPOLINE_ENTRY_PTR_OFFSET 0xA4u
#define AP_TRAMPOLINE_STACK_PTR_OFFSET 0xA8u
#define AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET 0xACu
#define AP_TRAMPOLINE_PMODE_OFFSET 0x33u
#define AP_TRAMPOLINE_DEBUG_STAGE_ADDR 0x7FF0u
#define AP_STACK_SIZE 4096u
#define AP_START_TIMEOUT_TICKS 20u
#define BIOS_WARM_RESET_VECTOR_OFFSET_ADDR 0x467u
#define BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR 0x469u
#define CMOS_INDEX_PORT 0x70u
#define CMOS_DATA_PORT 0x71u
#define CMOS_SHUTDOWN_STATUS_REG 0x0Fu
#define CMOS_SHUTDOWN_JUMP_VECTOR 0x0Au

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

struct smp_bootdebug_persist {
    uint32_t magic;
    uint8_t dirty;
    uint8_t len;
    uint8_t last;
    char trace[BOOTDEBUG_TRACE_MAX];
};

static volatile uint32_t g_smp_started_cpus = 1u;
static volatile uint32_t g_smp_cpu_stage[32];
static spinlock_t g_smp_boot_lock;
static int g_smp_initialized = 0;
static volatile uint32_t g_smp_scheduler_enabled = 0u;
static volatile struct smp_bootdebug_persist *const g_smp_bootdebug =
    (volatile struct smp_bootdebug_persist *)(uintptr_t)BOOTDEBUG_ADDR;
static uint8_t g_smp_saved_cmos_shutdown = 0u;
static uint16_t g_smp_saved_warm_reset_offset = 0u;
static uint16_t g_smp_saved_warm_reset_segment = 0u;
static int g_smp_ap_reset_vector_armed = 0;

void smp_persist_trace_arm(void) {
    g_smp_bootdebug->magic = BOOTDEBUG_MAGIC;
    g_smp_bootdebug->dirty = BOOTDEBUG_DIRTY;
    g_smp_bootdebug->len = 0u;
    g_smp_bootdebug->last = 0u;
    g_smp_bootdebug->trace[0] = '\0';
}

void smp_persist_trace_mark(uint8_t code) {
    if (g_smp_bootdebug->magic != BOOTDEBUG_MAGIC ||
        g_smp_bootdebug->dirty != BOOTDEBUG_DIRTY) {
        return;
    }

    g_smp_bootdebug->last = code;
    if (g_smp_bootdebug->len >= (BOOTDEBUG_TRACE_MAX - 1u)) {
        return;
    }

    g_smp_bootdebug->trace[g_smp_bootdebug->len] = (char)code;
    g_smp_bootdebug->len++;
    g_smp_bootdebug->trace[g_smp_bootdebug->len] = '\0';
}

void smp_persist_trace_disarm(void) {
    if (g_smp_bootdebug->magic == BOOTDEBUG_MAGIC) {
        g_smp_bootdebug->dirty = 0u;
    }
}

static uint32_t smp_trampoline_size(void) {
    return (uint32_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
}

static void smp_text_put_hex8(uint8_t value) {
    char text[3];
    uint8_t high = (uint8_t)((value >> 4) & 0x0Fu);
    uint8_t low = (uint8_t)(value & 0x0Fu);

    text[0] = (char)(high < 10u ? (uint8_t)('0' + high) : (uint8_t)('A' + (high - 10u)));
    text[1] = (char)(low < 10u ? (uint8_t)('0' + low) : (uint8_t)('A' + (low - 10u)));
    text[2] = '\0';
    kernel_text_puts(text);
}

static void smp_text_put_apic_line(const char *prefix, uint32_t apic_id) {
    kernel_text_puts(prefix);
    smp_text_put_hex8((uint8_t)(apic_id & 0xFFu));
    kernel_text_puts("\n");
}

static void smp_assert_trampoline_reserved(void) {
    uintptr_t tramp_begin = AP_TRAMPOLINE_PHYS_ADDR;
    uintptr_t tramp_end = AP_TRAMPOLINE_PHYS_ADDR + 0x1000u;
    uintptr_t usable_begin = physmem_usable_base();
    uintptr_t usable_end = physmem_usable_end();

    if (tramp_end <= usable_begin || tramp_begin >= usable_end) {
        return;
    }

    physmem_reserve_range(tramp_begin, tramp_end - tramp_begin);
    kernel_debug_printf("smp: trampoline explicitly reserved [%x,%x)\n",
                        (uint32_t)tramp_begin,
                        (uint32_t)tramp_end);
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
    uint32_t deadline = kernel_timer_get_ticks() + AP_START_TIMEOUT_TICKS;
    while ((int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
        if (g_smp_started_cpus >= expected_count) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static void smp_wait_ticks(uint32_t ticks) {
    uint32_t deadline = kernel_timer_get_ticks() + ticks;

    while ((int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
        io_wait();
        __asm__ volatile("pause");
    }
}

static uint16_t smp_read_bios_u16(uintptr_t addr) {
    uint16_t value;

    __asm__ volatile("movw (%1), %0"
                     : "=r"(value)
                     : "r"(addr)
                     : "memory");
    return value;
}

static void smp_write_bios_u16(uintptr_t addr, uint16_t value) {
    __asm__ volatile("movw %1, (%0)"
                     :
                     : "r"(addr), "r"(value)
                     : "memory");
}

static void smp_arm_ap_reset_vector(void) {
    uint16_t vector_segment = (uint16_t)(AP_TRAMPOLINE_PHYS_ADDR >> 4);

    if (!g_smp_ap_reset_vector_armed) {
        outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
        g_smp_saved_cmos_shutdown = inb(CMOS_DATA_PORT);
        g_smp_saved_warm_reset_offset = smp_read_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR);
        g_smp_saved_warm_reset_segment = smp_read_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR);
        g_smp_ap_reset_vector_armed = 1;
    }

    outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
    outb(CMOS_DATA_PORT, CMOS_SHUTDOWN_JUMP_VECTOR);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR, 0u);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR, vector_segment);
    kernel_debug_printf("smp: warm reset vector armed seg=%x phys=%x\n",
                        vector_segment,
                        AP_TRAMPOLINE_PHYS_ADDR);
}

static void smp_restore_ap_reset_vector(void) {
    if (!g_smp_ap_reset_vector_armed) {
        return;
    }

    outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
    outb(CMOS_DATA_PORT, g_smp_saved_cmos_shutdown);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR, g_smp_saved_warm_reset_offset);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR, g_smp_saved_warm_reset_segment);
    g_smp_ap_reset_vector_armed = 0;
    kernel_debug_puts("smp: warm reset vector restored\n");
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

    smp_persist_trace_mark('a');
    gdt_init();
    smp_persist_trace_mark('g');
    kernel_idt_init();
    smp_persist_trace_mark('i');
    kernel_cpu_enable_current_features();
    local_apic_init();
    smp_persist_trace_mark('l');
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
    smp_persist_trace_mark('m');

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
            __asm__ volatile("sti; hlt" : : : "memory");
        }
        __asm__ volatile("pause");
    }
}

void smp_init(void) {
    if (g_smp_initialized) {
        return;
    }
    smp_persist_trace_mark('S');
    g_smp_initialized = 1;
    spinlock_init(&g_smp_boot_lock);
    for (uint32_t i = 0; i < 32u; ++i) {
        g_smp_cpu_stage[i] = SMP_CPU_STAGE_NONE;
    }
    g_smp_cpu_stage[0] = SMP_CPU_STAGE_AP_SCHED;

    if (!kernel_cpu_is_smp_capable() || !local_apic_enabled() || kernel_cpu_count() <= 1u) {
        return;
    }

    smp_persist_trace_mark('T');
    kernel_text_puts("SMP: trampoline\n");
    smp_assert_trampoline_reserved();
    kernel_debug_printf("smp: preparing trampoline at %x (%d bytes)\n",
                        AP_TRAMPOLINE_PHYS_ADDR,
                        (int)smp_trampoline_size());
    kernel_debug_printf("smp: bsp cpu_index=%d apic=%x total=%d\n",
                        (int)kernel_cpu_index(),
                        local_apic_id(),
                        (int)kernel_cpu_count());
    smp_arm_ap_reset_vector();

    for (uint32_t i = 1; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        void *stack;
        uint32_t stack_top;

        if (!cpu) {
            break;
        }
        if (cpu->apic_id == local_apic_id()) {
            kernel_text_puts("SMP: skip self apic\n");
            kernel_debug_printf("smp: refusing to startup bsp apic=%x as target idx=%d\n",
                                cpu->apic_id,
                                (int)i);
            continue;
        }
        stack = kernel_malloc(AP_STACK_SIZE);
        if (!stack) {
            kernel_debug_printf("smp: no stack for cpu idx=%d\n", (int)i);
            break;
        }
        stack_top = (uint32_t)(uintptr_t)stack + AP_STACK_SIZE;

        spinlock_lock(&g_smp_boot_lock);
        smp_persist_trace_mark('P');
        kernel_text_puts("SMP: prepare ap\n");
        smp_text_put_apic_line("SMP: target apic ", cpu->apic_id);
        smp_prepare_trampoline((uint32_t)(uintptr_t)smp_ap_entry, stack_top);
        g_smp_cpu_stage[i] = SMP_CPU_STAGE_PREPARED;
        kernel_debug_printf("smp: starting ap idx=%d apic=%x stack=%x\n",
                            (int)i,
                            cpu->apic_id,
                            stack_top);
        smp_persist_trace_mark('I');
        kernel_text_puts("SMP: send INIT\n");
        if (local_apic_send_init(cpu->apic_id) == 0) {
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_INIT_SENT;
            kernel_debug_printf("smp: init sent apic=%x\n", cpu->apic_id);
            smp_wait_ticks(1u);
            smp_persist_trace_mark('1');
            kernel_text_puts("SMP: send SIPI1\n");
            kernel_debug_printf("smp: attempting sipi1 apic=%x vec=%x\n",
                                cpu->apic_id,
                                (unsigned)AP_TRAMPOLINE_VECTOR);
            if (local_apic_send_startup(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR) == 0) {
                g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI1_SENT;
                kernel_debug_printf("smp: sipi1 sent apic=%x vec=%x\n",
                                    cpu->apic_id,
                                    (unsigned)AP_TRAMPOLINE_VECTOR);
                smp_wait_ticks(1u);
                kernel_text_puts("SMP: ap stage ");
                smp_text_put_hex8(smp_trampoline_debug_stage());
                kernel_text_puts("\n");
            } else {
                kernel_debug_printf("smp: sipi1 failed apic=%x stage=%x\n",
                                    cpu->apic_id,
                                    (unsigned)smp_trampoline_debug_stage());
            }
            smp_persist_trace_mark('2');
            kernel_text_puts("SMP: send SIPI2\n");
            kernel_debug_printf("smp: attempting sipi2 apic=%x vec=%x\n",
                                cpu->apic_id,
                                (unsigned)AP_TRAMPOLINE_VECTOR);
            if (local_apic_send_startup(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR) == 0) {
                g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI2_SENT;
                kernel_debug_printf("smp: sipi2 sent apic=%x vec=%x\n",
                                    cpu->apic_id,
                                    (unsigned)AP_TRAMPOLINE_VECTOR);
            } else {
                kernel_debug_printf("smp: sipi2 failed apic=%x stage=%x\n",
                                    cpu->apic_id,
                                    (unsigned)smp_trampoline_debug_stage());
            }
        } else {
            kernel_debug_printf("smp: init failed apic=%x\n", cpu->apic_id);
        }
        spinlock_unlock(&g_smp_boot_lock);

        if (smp_wait_for_cpu(i + 1u) != 0) {
            smp_persist_trace_mark('F');
            kernel_text_puts("SMP: wait timeout\n");
            kernel_debug_printf("smp: ap idx=%d apic=%x did not respond\n",
                                (int)i,
                                cpu->apic_id);
            smp_dump_cpu_stages();
            break;
        }
        smp_persist_trace_mark('O');
        kernel_text_puts("SMP: ap online\n");
    }
    smp_restore_ap_reset_vector();

    smp_persist_trace_mark('D');
    kernel_debug_printf("smp: online cpus=%d/%d\n",
                        (int)g_smp_started_cpus,
                        (int)kernel_cpu_count());
    kernel_debug_puts("smp: aps online and parked until per-cpu scheduler bring-up is implemented\n");
}

uint32_t smp_started_cpu_count(void) {
    return g_smp_started_cpus;
}

void smp_scheduler_enable(void) {
    g_smp_scheduler_enabled = 1u;
    if (g_smp_started_cpus > 1u && local_apic_enabled()) {
        (void)local_apic_broadcast_ipi((uint8_t)SMP_WAKE_VECTOR);
    }
}

int smp_scheduler_enabled(void) {
    return g_smp_scheduler_enabled != 0u;
}

void smp_wake_sleeping_cpus(void) {
    if (g_smp_scheduler_enabled == 0u ||
        g_smp_started_cpus <= 1u ||
        !local_apic_enabled()) {
        return;
    }
    (void)local_apic_broadcast_ipi((uint8_t)SMP_WAKE_VECTOR);
}

void smp_wakeup_ipi_handler(void) {
    local_apic_eoi();
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
