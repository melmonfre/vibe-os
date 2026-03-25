#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/fs.h>
#include <kernel/microkernel.h>
#include <kernel/microkernel/launch.h>
#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/hal/io.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/cpu/cpu.h>
#include <stdint.h>
#include <include/userland_api.h> /* syscall IDs */
#include <string.h>
#include <kernel/drivers/input/input.h>

/* syscall table and helpers for the new kernel-side mechanism.  The
   legacy stage2 dispatch is still compiled into the image; eventually we
   will migrate completely to this table-driven approach. */

#define MAX_SYSCALLS 64
typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static syscall_fn syscall_table[MAX_SYSCALLS];

static uint32_t sys_gfx_clear(uint32_t color, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_video_clear((uint8_t)(color & 0xFFu));
    return 0u;
}

static uint32_t sys_gfx_rect(uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h, uint32_t color) {
    kernel_gfx_rect((int)x, (int)y, (int)w, (int)h, (uint8_t)(color & 0xFFu));
    return 0u;
}

static uint32_t sys_gfx_text(uint32_t x, uint32_t y, uint32_t text_ptr,
                             uint32_t color, uint32_t e) {
    (void)e;
    if (text_ptr == 0u) {
        return (uint32_t)-1;
    }
    kernel_gfx_draw_text((int)x,
                         (int)y,
                         (const char *)(uintptr_t)text_ptr,
                         (uint8_t)(color & 0xFFu));
    return 0u;
}

static uint32_t sys_gfx_flip(uint32_t a, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_video_flip();
    return 0u;
}

static uint32_t sys_gfx_blit8(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                              uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }

    kernel_gfx_blit8((const uint8_t *)(uintptr_t)src_ptr,
                     src_w,
                     src_h,
                     (int)dst_x,
                     (int)dst_y,
                     (int)scale);
    return 0u;
}

static uint32_t sys_gfx_blit8_stretch(uint32_t src_ptr, uint32_t packed_src_wh,
                                      uint32_t dst_x, uint32_t dst_y,
                                      uint32_t packed_dst_wh) {
    int src_w = (int)(packed_src_wh & 0xFFFFu);
    int src_h = (int)((packed_src_wh >> 16) & 0xFFFFu);
    int dst_w = (int)(packed_dst_wh & 0xFFFFu);
    int dst_h = (int)((packed_dst_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }

    kernel_gfx_blit8_stretch((const uint8_t *)(uintptr_t)src_ptr,
                             src_w,
                             src_h,
                             (int)dst_x,
                             (int)dst_y,
                             dst_w,
                             dst_h);
    return 0u;
}

static uint32_t sys_gfx_leave(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_video_leave_graphics();
    return 0u;
}

static uint32_t sys_gfx_set_mode(uint32_t width, uint32_t height, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_set_mode(width, height);
}

static uint32_t sys_gfx_set_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_set_palette((const uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_gfx_get_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_get_palette((uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_storage_load(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_storage_service_load((void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_save(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_storage_service_save((const void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_read_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                         uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_storage_service_read_sectors(lba, (void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_write_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                          uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_storage_service_write_sectors(lba, (const void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_total_sectors(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return mk_storage_service_total_sectors();
}

static uint32_t sys_fs_open(uint32_t path_ptr, uint32_t flags, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_open((const char *)(uintptr_t)path_ptr, (int)flags);
}

static uint32_t sys_fs_read(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                            uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_read((int)fd, (void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_write(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                             uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_write((int)fd, (const void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_close(uint32_t fd, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_close((int)fd);
}

static uint32_t sys_fs_lseek(uint32_t fd, uint32_t offset, uint32_t whence,
                             uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_lseek((int)fd, (off_t)(int32_t)offset, (int)whence);
}

static uint32_t sys_fs_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_stat((const char *)(uintptr_t)path_ptr,
                                                (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_fs_fstat(uint32_t fd, uint32_t stat_ptr, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_fstat((int)fd,
                                                 (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_input_mouse(uint32_t state_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (state_ptr == 0)
        return 0;
    return (uint32_t)mk_input_service_poll_mouse((struct mouse_state *)(uintptr_t)state_ptr);
}

static uint32_t sys_getpid(uint32_t a, uint32_t b, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    process_t *cur = scheduler_current();
    return cur ? (uint32_t)cur->pid : 0u;
}

static uint32_t sys_launch_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    const struct mk_launch_context *context;
    struct userland_launch_info *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return (uint32_t)-1;
    }

    out = (struct userland_launch_info *)(uintptr_t)out_ptr;
    memcpy(out, context, sizeof(*out));
    return 0;
}

static uint32_t sys_yield(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    yield();
    return 0;
}

static uint32_t sys_write_debug(uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_debug_puts((const char *)(uintptr_t)a);
    return 0;
}

static uint32_t sys_input_key(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    /*
     * Key polling is latency-sensitive and already backed by the global
     * kernel PS/2 queue. Going through the input worker adds an avoidable
     * IPC/restart failure mode without improving arbitration.
     */
    return (uint32_t)kernel_keyboard_read();
}

static uint32_t sys_text_clear(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_text_clear();
    return 0;
}

static uint32_t sys_text_move_cursor(uint32_t a, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_move_cursor((int32_t)a);
    return 0;
}

static uint32_t sys_text_putc(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_putc((char)(a & 0xFF));
    return 0;
}

static uint32_t sys_text_write(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_puts((const char *)(uintptr_t)a);
    return 0;
}

static uint32_t sys_sleep(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    __asm__ volatile("hlt");
    return 0;
}

static uint32_t sys_time_ticks(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return kernel_timer_get_ticks();
}

static uint32_t sys_gfx_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    struct video_mode *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }

    out = (struct video_mode *)(uintptr_t)out_ptr;
    *out = *kernel_video_get_mode();
    return 0u;
}

static uint32_t sys_gfx_caps(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    struct video_capabilities *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }
    out = (struct video_capabilities *)(uintptr_t)out_ptr;
    kernel_video_get_capabilities(out);
    return 0u;
}

static uint32_t sys_keyboard_set_layout(uint32_t name_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_set_layout((const char*)(uintptr_t)name_ptr);
}

static uint32_t sys_keyboard_get_layout(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_get_layout((char*)(uintptr_t)buffer_ptr, (int)size);
}

static uint32_t sys_keyboard_get_available_layouts(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_get_available_layouts((char*)(uintptr_t)buffer_ptr, (int)size);
}

static uint32_t sys_shutdown(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;

    kernel_debug_puts("sys_shutdown: poweroff requested\n");

    /* Common ACPI/QEMU/Bochs poweroff ports. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    __asm__ volatile("cli" : : : "memory");
    for (;;) {
        __asm__ volatile("hlt");
    }

    __builtin_unreachable();
}

static uint32_t sys_service_receive(uint32_t message_ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)ipc_receive(current,
                                 (void *)(uintptr_t)message_ptr,
                                 sizeof(struct mk_message));
}

static uint32_t sys_service_send(uint32_t message_ptr, uint32_t b, uint32_t c,
                                 uint32_t d, uint32_t e) {
    const struct mk_message *message;
    process_t *destination;

    (void)b; (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    message = (const struct mk_message *)(uintptr_t)message_ptr;
    if (message->target_pid == 0u) {
        return (uint32_t)-1;
    }

    destination = scheduler_find_task_by_pid((int)message->target_pid);
    if (destination == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)ipc_send(destination, message, sizeof(*message));
}

static uint32_t sys_service_backend(uint32_t request_ptr, uint32_t reply_ptr, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (request_ptr == 0u || reply_ptr == 0u) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_service_backend_handle_current(
        (const struct mk_message *)(uintptr_t)request_ptr,
        (struct mk_message *)(uintptr_t)reply_ptr);
}

static uint32_t sys_task_snapshot(uint32_t summary_ptr, uint32_t entries_ptr, uint32_t max_entries,
                                  uint32_t d, uint32_t e) {
    struct task_snapshot_summary *summary;
    struct task_snapshot_entry *entries;
    uint32_t count;

    (void)d; (void)e;
    if (summary_ptr == 0u) {
        return (uint32_t)-1;
    }

    summary = (struct task_snapshot_summary *)(uintptr_t)summary_ptr;
    entries = entries_ptr != 0u ? (struct task_snapshot_entry *)(uintptr_t)entries_ptr : 0;
    if (max_entries > TASK_SNAPSHOT_MAX) {
        max_entries = TASK_SNAPSHOT_MAX;
    }

    count = scheduler_snapshot(entries, max_entries, summary);
    summary->kernel_heap_used = (uint32_t)kernel_heap_used();
    summary->kernel_heap_free = (uint32_t)kernel_heap_free();
    summary->physmem_total_kb = (uint32_t)(physmem_usable_size() / 1024u);
    summary->physmem_free_kb = (uint32_t)((physmem_free_pages() * PHYSMEM_PAGE_SIZE) / 1024u);

    if (entries != 0) {
        for (uint32_t i = 0u; i < count; ++i) {
            const struct mk_launch_context *context = mk_launch_context_for_pid((int)entries[i].pid);

            if (context != 0) {
                memcpy(entries[i].name, context->name, sizeof(entries[i].name));
                entries[i].flags = context->flags;
                entries[i].service_type = context->service_type;
            }
        }
    }

    return count;
}

static uint32_t sys_task_terminate(uint32_t pid, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    process_t *current;
    process_t *task;
    const struct mk_launch_context *context;

    (void)b; (void)c; (void)d; (void)e;
    if (pid == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current != 0 && (uint32_t)current->pid == pid) {
        return (uint32_t)-1;
    }

    task = scheduler_find_task_by_pid((int)pid);
    if (task == 0) {
        return (uint32_t)-1;
    }

    context = mk_launch_context_for_pid((int)pid);
    if (context != 0 && (context->flags & MK_LAUNCH_FLAG_CRITICAL) != 0u) {
        return (uint32_t)-1;
    }

    scheduler_terminate_task(task);
    return 0u;
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GFX_CLEAR] = sys_gfx_clear;
    syscall_table[SYSCALL_GFX_RECT] = sys_gfx_rect;
    syscall_table[SYSCALL_GFX_TEXT] = sys_gfx_text;
    syscall_table[SYSCALL_GFX_FLIP] = sys_gfx_flip;
    syscall_table[SYSCALL_GFX_BLIT8] = sys_gfx_blit8;
    syscall_table[SYSCALL_GFX_BLIT8_STRETCH] = sys_gfx_blit8_stretch;
    syscall_table[SYSCALL_GFX_LEAVE] = sys_gfx_leave;
    syscall_table[SYSCALL_GFX_SET_MODE] = sys_gfx_set_mode;
    syscall_table[SYSCALL_GFX_SET_PALETTE] = sys_gfx_set_palette;
    syscall_table[SYSCALL_GFX_GET_PALETTE] = sys_gfx_get_palette;
    syscall_table[SYSCALL_STORAGE_LOAD] = sys_storage_load;
    syscall_table[SYSCALL_STORAGE_SAVE] = sys_storage_save;
    syscall_table[SYSCALL_STORAGE_READ_SECTORS] = sys_storage_read_sectors;
    syscall_table[SYSCALL_STORAGE_WRITE_SECTORS] = sys_storage_write_sectors;
    syscall_table[SYSCALL_STORAGE_TOTAL_SECTORS] = sys_storage_total_sectors;
    syscall_table[SYSCALL_OPEN] = sys_fs_open;
    syscall_table[SYSCALL_READ] = sys_fs_read;
    syscall_table[SYSCALL_WRITE] = sys_fs_write;
    syscall_table[SYSCALL_CLOSE] = sys_fs_close;
    syscall_table[SYSCALL_LSEEK] = sys_fs_lseek;
    syscall_table[SYSCALL_STAT] = sys_fs_stat;
    syscall_table[SYSCALL_FSTAT] = sys_fs_fstat;
    syscall_table[SYSCALL_INPUT_MOUSE] = sys_input_mouse;
    syscall_table[SYSCALL_INPUT_KEY] = sys_input_key;
    syscall_table[12] = sys_text_putc;     /* legacy text mode */
    syscall_table[13] = sys_text_clear;    /* legacy text mode */
    syscall_table[SYSCALL_TEXT_MOVE_CURSOR] = sys_text_move_cursor;
    syscall_table[SYSCALL_TEXT_WRITE] = sys_text_write;
    syscall_table[SYSCALL_SLEEP] = sys_sleep;
    syscall_table[SYSCALL_TIME_TICKS] = sys_time_ticks;
    syscall_table[SYSCALL_GFX_INFO] = sys_gfx_info;
    syscall_table[SYSCALL_GFX_CAPS] = sys_gfx_caps;
    syscall_table[SYSCALL_GETPID] = sys_getpid;
    syscall_table[SYSCALL_LAUNCH_INFO] = sys_launch_info;
    syscall_table[SYSCALL_YIELD] = sys_yield;
    syscall_table[SYSCALL_WRITE_DEBUG] = sys_write_debug;
    syscall_table[SYSCALL_KEYBOARD_SET_LAYOUT] = sys_keyboard_set_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_LAYOUT] = sys_keyboard_get_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS] = sys_keyboard_get_available_layouts;
    syscall_table[SYSCALL_SHUTDOWN] = sys_shutdown;
    syscall_table[SYSCALL_SERVICE_RECV] = sys_service_receive;
    syscall_table[SYSCALL_SERVICE_SEND] = sys_service_send;
    syscall_table[SYSCALL_SERVICE_BACKEND] = sys_service_backend;
    syscall_table[SYSCALL_TASK_SNAPSHOT] = sys_task_snapshot;
    syscall_table[SYSCALL_TASK_TERMINATE] = sys_task_terminate;
}

/* dispatch routine called by ISR */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
