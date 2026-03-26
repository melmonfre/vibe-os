#include <kernel/bootinfo.h>
#include <kernel/kernel.h>  /* use include path */
#include <kernel/interrupt.h> /* new interrupt interfaces */
#include <kernel/scheduler.h>
#include <kernel/driver_manager.h>
#include <kernel/memory/memory_init.h>  /* kernel/memory via CFLAGS */
#include <kernel/memory/heap.h>
#include <kernel/memory/paging.h>
#include <kernel/memory/physmem.h>
#include <kernel/fs.h>
#include <kernel/hal.h>
#include <kernel/cpu/cpu.h>
#include <kernel/apic.h>
#include <kernel/smp.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/drivers/input/input.h>
#include <kernel/microkernel.h>
#include <kernel/userland.h>
#include <lang/include/vibe_app.h>
#include <stdint.h>

#define BOOTDEBUG_ADDR 0x00001000u
#define BOOTDEBUG_MAGIC 0x47444256u
#define BOOTDEBUG_DIRTY 1u
#define BOOTDEBUG_TRACE_MAX 48u

struct bootdebug_persist {
    uint32_t magic;
    uint8_t dirty;
    uint8_t len;
    uint8_t last;
    char trace[BOOTDEBUG_TRACE_MAX];
};

static volatile struct bootdebug_persist *const bootdebug_persist =
    (volatile struct bootdebug_persist *)(uintptr_t)BOOTDEBUG_ADDR;

static void kernel_bootdebug_append(uint8_t code) {
    if (bootdebug_persist->magic != BOOTDEBUG_MAGIC ||
        bootdebug_persist->dirty != BOOTDEBUG_DIRTY) {
        return;
    }

    bootdebug_persist->last = code;
    if (bootdebug_persist->len >= (BOOTDEBUG_TRACE_MAX - 1u)) {
        return;
    }

    bootdebug_persist->trace[bootdebug_persist->len] = (char)code;
    bootdebug_persist->len++;
    bootdebug_persist->trace[bootdebug_persist->len] = '\0';
}

static void kernel_bootdebug_mark_stable(void) {
    if (bootdebug_persist->magic == BOOTDEBUG_MAGIC) {
        bootdebug_persist->dirty = 0u;
    }
}

static inline void kernel_early_post(uint8_t code) {
    __asm__ volatile("outb %0, $0x80" : : "a"(code));
    __asm__ volatile("outb %0, $0xE9" : : "a"(code));
}

static void kernel_early_fill_rect(uint8_t color, uint16_t x0, uint16_t y0, uint16_t width, uint16_t height) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    uint32_t fb_addr = bootinfo->vesa.fb_addr;
    uint16_t pitch = bootinfo->vesa.pitch;
    uint16_t screen_width = bootinfo->vesa.width;
    uint16_t screen_height = bootinfo->vesa.height;
    uint8_t bpp = bootinfo->vesa.bpp;

    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        fb_addr < 0x00100000u ||
        pitch == 0u ||
        screen_width == 0u ||
        screen_height == 0u ||
        bpp != 8u ||
        x0 >= screen_width ||
        y0 >= screen_height) {
        return;
    }

    if ((uint32_t)x0 + (uint32_t)width > (uint32_t)screen_width) {
        width = (uint16_t)(screen_width - x0);
    }
    if ((uint32_t)y0 + (uint32_t)height > (uint32_t)screen_height) {
        height = (uint16_t)(screen_height - y0);
    }

    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)fb_addr;
    for (uint16_t y = 0; y < height; ++y) {
        uint32_t row = (uint32_t)(y0 + y) * (uint32_t)pitch;
        for (uint16_t x = 0; x < width; ++x) {
            fb[row + x0 + x] = color;
        }
    }
}

static void kernel_early_mark(uint8_t code, uint8_t color) {
    uint8_t stage = (uint8_t)(code & 0x0Fu);
    uint8_t stage_char;
    uint16_t x0 = (uint16_t)(16u + ((uint16_t)stage * 36u));
    uint16_t progress = (uint16_t)((stage + 1u) * 36u);
    if (stage < 10u) {
        stage_char = (uint8_t)('0' + stage);
    } else {
        stage_char = (uint8_t)('A' + (stage - 10u));
    }
    kernel_bootdebug_append(stage_char);
    kernel_early_post(code);
    kernel_early_fill_rect(color, x0, 16u, 28u, 24u);
    kernel_early_fill_rect(color, 16u, 48u, progress, 6u);
}

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return value & ~(align - 1u);
}

struct kernel_mem_range {
    uintptr_t start;
    uintptr_t end;
};

static void kernel_mem_range_clear(struct kernel_mem_range *range) {
    if (range == 0) {
        return;
    }
    range->start = 0u;
    range->end = 0u;
}

static size_t kernel_mem_range_size(const struct kernel_mem_range *range) {
    if (range == 0 || range->end <= range->start) {
        return 0u;
    }
    return (size_t)(range->end - range->start);
}

static void kernel_mem_range_normalize(struct kernel_mem_range *range) {
    if (range == 0 || range->end <= range->start) {
        kernel_mem_range_clear(range);
    }
}

static void kernel_mem_range_subtract(const struct kernel_mem_range *source,
                                      const struct kernel_mem_range *reserved,
                                      struct kernel_mem_range *left_out,
                                      struct kernel_mem_range *right_out) {
    struct kernel_mem_range left = {0u, 0u};
    struct kernel_mem_range right = {0u, 0u};
    uintptr_t overlap_start;
    uintptr_t overlap_end;

    if (source == 0 || reserved == 0) {
        kernel_mem_range_clear(left_out);
        kernel_mem_range_clear(right_out);
        return;
    }

    if (source->end <= source->start ||
        reserved->end <= reserved->start ||
        reserved->end <= source->start ||
        reserved->start >= source->end) {
        left = *source;
    } else {
        overlap_start = reserved->start > source->start ? reserved->start : source->start;
        overlap_end = reserved->end < source->end ? reserved->end : source->end;

        left.start = source->start;
        left.end = overlap_start;
        right.start = overlap_end;
        right.end = source->end;
        kernel_mem_range_normalize(&left);
        kernel_mem_range_normalize(&right);
    }

    if (left_out != 0) {
        *left_out = left;
    }
    if (right_out != 0) {
        *right_out = right;
    }
}

static struct kernel_mem_range kernel_mem_pick_larger(const struct kernel_mem_range *a,
                                                      const struct kernel_mem_range *b) {
    size_t a_size = kernel_mem_range_size(a);
    size_t b_size = kernel_mem_range_size(b);

    if (b_size > a_size) {
        return b != 0 ? *b : (struct kernel_mem_range){0u, 0u};
    }
    return a != 0 ? *a : (struct kernel_mem_range){0u, 0u};
}

static struct kernel_mem_range kernel_mem_find_largest_free_range(
    struct kernel_mem_range usable,
    const struct kernel_mem_range *reserved,
    size_t reserved_count) {
    struct kernel_mem_range best = usable;

    for (size_t i = 0; i < reserved_count; ++i) {
        struct kernel_mem_range current = reserved[i];
        struct kernel_mem_range left;
        struct kernel_mem_range right;

        if (kernel_mem_range_size(&best) == 0u || kernel_mem_range_size(&current) == 0u) {
            continue;
        }
        kernel_mem_range_subtract(&best, &current, &left, &right);
        best = kernel_mem_pick_larger(&left, &right);
    }

    return best;
}

static int kernel_boot_framebuffer_range(uintptr_t *start_out, uintptr_t *end_out) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    uintptr_t start;
    uintptr_t end;
    size_t frame_bytes;

    if (start_out != 0) {
        *start_out = 0u;
    }
    if (end_out != 0) {
        *end_out = 0u;
    }

    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        bootinfo->vesa.fb_addr < 0x00100000u ||
        bootinfo->vesa.pitch == 0u ||
        bootinfo->vesa.width < 640u ||
        bootinfo->vesa.height < 480u ||
        bootinfo->vesa.bpp != 8u) {
        return 0;
    }

    frame_bytes = (size_t)bootinfo->vesa.pitch * (size_t)bootinfo->vesa.height;
    if (frame_bytes == 0u) {
        return 0;
    }

    start = align_down_uintptr((uintptr_t)bootinfo->vesa.fb_addr, 0x1000u);
    end = align_up_uintptr((uintptr_t)bootinfo->vesa.fb_addr + frame_bytes, 0x1000u);
    if (end <= start) {
        return 0;
    }

    if (start_out != 0) {
        *start_out = start;
    }
    if (end_out != 0) {
        *end_out = end;
    }
    return 1;
}

__attribute__((noreturn, section(".entry"))) void kernel_entry(void) {
    enum {
        USERLAND_STACK_RESERVE = 512 * 1024,
        HEAP_GUARD_BYTES = 64 * 1024,
        APP_ARENA_TOTAL_BYTES = VIBE_APP_ARENA_SIZE * 3u,
        APP_BACKING_ALIGN = 0x00400000u
    };
    extern uint8_t __bss_end[];
    uintptr_t kernel_end;
    uintptr_t usable_base;
    uintptr_t usable_end;
    uintptr_t heap_start;
    uintptr_t heap_end;
    uintptr_t fb_start = 0u;
    uintptr_t fb_end = 0u;
    uintptr_t low_reserved_end;
    uintptr_t app_backing_start = 0u;
    uintptr_t app_backing_end = 0u;
    struct kernel_mem_range usable_range;
    struct kernel_mem_range reserved_ranges[3];
    struct kernel_mem_range heap_range;
    struct kernel_mem_range app_range;
    size_t reserved_count = 0u;
    uint32_t app_pde_snapshot[(APP_ARENA_TOTAL_BYTES / 0x00400000u)];

    /* zero kernel BSS */
    extern uint8_t __bss_start[];
    kernel_early_mark(0x10u, 0x04u);
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }
    kernel_early_mark(0x11u, 0x02u);
    kernel_debug_init(); /* registers debug driver */
    mk_launch_init();
    mk_service_init();
    kernel_early_mark(0x12u, 0x06u);
    hal_init();
    kernel_early_mark(0x13u, 0x01u);
    cpu_init();
    kernel_early_mark(0x14u, 0x03u);
    gdt_init();
    kernel_early_mark(0x15u, 0x05u);
    kernel_video_init(); /* preserve boot LFB when available */
    kernel_early_mark(0x16u, 0x07u);
    kernel_text_init();
    kernel_early_mark(0x17u, 0x0Fu);
    kernel_text_puts("VIBE OS Booting...\n");
    if (kernel_cpu_count() > 1u) {
        if (kernel_cpu_is_smp_capable()) {
            kernel_text_puts("CPU topology: multiprocessor platform verified\n");
        } else {
            kernel_text_puts("CPU topology: multiple cores detected, SMP bring-up deferred\n");
        }
    } else {
        kernel_text_puts("CPU topology: single processor\n");
    }
    if (kernel_cpu_is_smp_capable()) {
        local_apic_init();
    } else {
        kernel_text_puts("LAPIC/SMP deferred on this platform\n");
    }
    kernel_text_puts("Video OK\n");

    kernel_text_puts("Setting up interrupts...\n");
    kernel_idt_init();
    kernel_pic_init();
    kernel_text_puts("Interrupts OK\n");

    kernel_text_puts("Starting timers/input...\n");
    kernel_timer_init(100);
    kernel_keyboard_init();
    kernel_mouse_init();
    kernel_irq_enable();
    kernel_text_puts("IRQ OK\n");

    kernel_text_puts("Initializing memory...\n");
    memory_subsystem_init();
    if (kernel_boot_framebuffer_range(&fb_start, &fb_end)) {
        physmem_reserve_range(fb_start, (size_t)(fb_end - fb_start));
        kernel_debug_printf("memory: reserved framebuffer [%x, %x) kb=%u\n",
                            (uint32_t)fb_start,
                            (uint32_t)fb_end,
                            (unsigned int)((fb_end - fb_start) / 1024u));
    }
    kernel_end = align_up_uintptr((uintptr_t)__bss_end, 0x1000u);
    usable_base = physmem_usable_base();
    usable_end = physmem_usable_end();
    low_reserved_end = align_up_uintptr(kernel_end + USERLAND_STACK_RESERVE + HEAP_GUARD_BYTES, 0x1000u);

    usable_range.start = align_up_uintptr(usable_base, 0x1000u);
    usable_range.end = align_down_uintptr(usable_end, 0x1000u);
    kernel_mem_range_clear(&reserved_ranges[0]);
    kernel_mem_range_clear(&reserved_ranges[1]);
    kernel_mem_range_clear(&reserved_ranges[2]);

    reserved_ranges[0].start = usable_range.start;
    reserved_ranges[0].end = low_reserved_end;
    kernel_mem_range_normalize(&reserved_ranges[0]);
    if (kernel_mem_range_size(&reserved_ranges[0]) != 0u) {
        physmem_reserve_range(reserved_ranges[0].start,
                              (size_t)(reserved_ranges[0].end - reserved_ranges[0].start));
        kernel_debug_printf("memory: reserved boot/core [%x, %x) kb=%u\n",
                            (uint32_t)reserved_ranges[0].start,
                            (uint32_t)reserved_ranges[0].end,
                            (unsigned int)((reserved_ranges[0].end - reserved_ranges[0].start) / 1024u));
        reserved_count = 1u;
    }

    reserved_ranges[1].start = fb_start;
    reserved_ranges[1].end = fb_end;
    kernel_mem_range_normalize(&reserved_ranges[1]);
    if (kernel_mem_range_size(&reserved_ranges[1]) != 0u) {
        reserved_count = 2u;
    }

    app_range = kernel_mem_find_largest_free_range(usable_range, reserved_ranges, reserved_count);
    if (kernel_mem_range_size(&app_range) >= APP_ARENA_TOTAL_BYTES + APP_BACKING_ALIGN) {
        app_backing_end = align_down_uintptr(app_range.end, APP_BACKING_ALIGN);
        app_backing_start = align_down_uintptr(app_backing_end - APP_ARENA_TOTAL_BYTES, APP_BACKING_ALIGN);
        if (app_backing_start >= app_range.start && app_backing_end > app_backing_start) {
            reserved_ranges[2].start = app_backing_start;
            reserved_ranges[2].end = app_backing_end;
            kernel_mem_range_normalize(&reserved_ranges[2]);
        }
    }

    if (kernel_mem_range_size(&reserved_ranges[2]) != 0u) {
        uintptr_t app_phys = reserved_ranges[2].start;
        int mapped = 0;

        physmem_reserve_range(reserved_ranges[2].start,
                              (size_t)(reserved_ranges[2].end - reserved_ranges[2].start));
        kernel_debug_printf("memory: reserved app backing [%x, %x) mb=%u\n",
                            (uint32_t)reserved_ranges[2].start,
                            (uint32_t)reserved_ranges[2].end,
                            (unsigned int)((reserved_ranges[2].end - reserved_ranges[2].start) / (1024u * 1024u)));
        if (paging_snapshot_large_region(VIBE_APP_LOAD_ADDR,
                                         APP_ARENA_TOTAL_BYTES,
                                         app_pde_snapshot,
                                         sizeof(app_pde_snapshot) / sizeof(app_pde_snapshot[0])) == 0 &&
            paging_map_large_region(VIBE_APP_LOAD_ADDR, app_phys, VIBE_APP_ARENA_SIZE) == 0 &&
            paging_map_large_region(VIBE_APP_DESKTOP_LOAD_ADDR, app_phys + VIBE_APP_ARENA_SIZE, VIBE_APP_ARENA_SIZE) == 0 &&
            paging_map_large_region(VIBE_APP_BOOT_LOAD_ADDR, app_phys + (VIBE_APP_ARENA_SIZE * 2u), VIBE_APP_ARENA_SIZE) == 0) {
            kernel_debug_printf("memory: app arenas mapped v=%x/%x/%x p=%x/%x/%x\n",
                                (uint32_t)VIBE_APP_LOAD_ADDR,
                                (uint32_t)VIBE_APP_DESKTOP_LOAD_ADDR,
                                (uint32_t)VIBE_APP_BOOT_LOAD_ADDR,
                                (uint32_t)app_phys,
                                (uint32_t)(app_phys + VIBE_APP_ARENA_SIZE),
                                (uint32_t)(app_phys + (VIBE_APP_ARENA_SIZE * 2u)));
            reserved_count = 3u;
            mapped = 1;
        } else {
            kernel_debug_puts("memory: app arena remap failed, using identity mapping\n");
            (void)paging_restore_large_region(VIBE_APP_LOAD_ADDR,
                                              APP_ARENA_TOTAL_BYTES,
                                              app_pde_snapshot,
                                              sizeof(app_pde_snapshot) / sizeof(app_pde_snapshot[0]));
        }
        if (!mapped) {
            physmem_release_range(reserved_ranges[2].start,
                                  (size_t)(reserved_ranges[2].end - reserved_ranges[2].start));
            kernel_mem_range_clear(&reserved_ranges[2]);
        }
    }

    heap_range = kernel_mem_find_largest_free_range(usable_range, reserved_ranges, reserved_count);
    heap_start = align_up_uintptr(heap_range.start, 0x1000u);
    heap_end = align_down_uintptr(heap_range.end, 0x1000u);
    if (heap_end <= heap_start) {
        heap_start = 0x00500000u;
        heap_end = 0x00900000u;
    }
    kernel_mm_init(heap_start, heap_end - heap_start);
    kernel_video_refresh_backend();
    mk_transfer_init();
    kernel_text_puts("Memory OK\n");
    kernel_bootdebug_mark_stable();

    kernel_text_puts("Initializing storage...\n");
    kernel_storage_init();
    kernel_text_puts(kernel_storage_ready() ? "Storage OK\n" : "Storage unavailable\n");

    kernel_text_puts("Initializing scheduler/driver manager...\n");
    scheduler_init();
    driver_manager_init(); /* second call to debug init performs HW setup */
    kernel_usb_host_init();
    kernel_text_puts("Scheduler OK\n");

    if (kernel_cpu_is_smp_capable() && local_apic_enabled()) {
        kernel_text_puts("SMP deferred\n");
    } else {
        kernel_text_puts("SMP skipped\n");
    }

    kernel_text_puts("Initializing VFS...\n");
    vfs_init();
    mk_storage_service_init();
    mk_filesystem_service_init();
    mk_video_service_init();
    mk_input_service_init();
    mk_console_service_init();
    mk_network_service_init();
    mk_audio_service_init();
    kernel_text_puts("VFS OK\n");

    kernel_text_puts("Initializing syscalls...\n");
    syscall_init();
    kernel_text_puts("Syscalls OK\n");

    kernel_text_puts("Starting userland...\n");
    userland_run();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
