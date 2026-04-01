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
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
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

#define MAX_SYSCALLS 111
typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static syscall_fn syscall_table[MAX_SYSCALLS];

extern void userland_shell_host_entry(void);
extern void userland_shell_session_entry(void);
extern void userland_desktop_host_entry(void);
extern void userland_startx_host_entry(void);
extern void userland_desktop_session_entry(void);
extern void userland_desktop_audio_host_entry(void);
extern void userland_boot_audio_host_entry(void);
extern void userland_app_host_entry(void);
extern void userland_app_runtime_entry(void);

static const char *sys_launch_path_basename(const char *path) {
    const char *last = path;

    if (path == 0) {
        return 0;
    }

    while (*path != '\0') {
        if (*path == '/' && path[1] != '\0') {
            last = path + 1;
        }
        ++path;
    }

    return last;
}

static const char *sys_launch_descriptor_arg(const struct mk_launch_descriptor *descriptor,
                                             uint32_t index) {
    uint32_t current = 0u;
    uint32_t offset = 0u;

    if (descriptor == 0 || index >= descriptor->argc) {
        return 0;
    }

    while (current < descriptor->argc && offset < MK_LAUNCH_ARGV_BYTES) {
        uint32_t len = 0u;
        const char *arg = &descriptor->argv_data[offset];

        while ((offset + len) < MK_LAUNCH_ARGV_BYTES && arg[len] != '\0') {
            len += 1u;
        }
        if ((offset + len) >= MK_LAUNCH_ARGV_BYTES) {
            return 0;
        }
        if (current == index) {
            return arg;
        }
        offset += len + 1u;
        current += 1u;
    }

    return 0;
}

static uint32_t sys_gfx_clear(uint32_t color, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_clear((uint8_t)(color & 0xFFu));
}

static uint32_t sys_gfx_rect(uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h, uint32_t color) {
    return (uint32_t)mk_video_service_rect((int)x,
                                           (int)y,
                                           (int)w,
                                           (int)h,
                                           (uint8_t)(color & 0xFFu));
}

static uint32_t sys_gfx_text(uint32_t x, uint32_t y, uint32_t text_ptr,
                             uint32_t color, uint32_t e) {
    (void)e;
    if (text_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_text((int)x,
                                           (int)y,
                                           (uint8_t)(color & 0xFFu),
                                           (const char *)(uintptr_t)text_ptr);
}

static uint32_t sys_gfx_flip(uint32_t a, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_flip_mode(a);
}

static uint32_t sys_gfx_set_present_policy(uint32_t policy, uint32_t b, uint32_t c,
                                           uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)(mk_video_service_set_present_policy(policy) == 0 ? 0 : -1);
}

static uint32_t sys_gfx_set_present_copy_override(uint32_t kind, uint32_t b, uint32_t c,
                                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)(mk_video_service_set_present_copy_override(kind) == 0 ? 0 : -1);
}

static uint32_t sys_gfx_blit8(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                              uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8((const uint8_t *)(uintptr_t)src_ptr,
                                            src_w,
                                            src_h,
                                            (int)dst_x,
                                            (int)dst_y,
                                            (int)scale);
}

static uint32_t sys_gfx_blit8_present(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                                      uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8_present((const uint8_t *)(uintptr_t)src_ptr,
                                                    src_w,
                                                    src_h,
                                                    (int)dst_x,
                                                    (int)dst_y,
                                                    (int)scale);
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
    return (uint32_t)mk_video_service_blit8_stretch((const uint8_t *)(uintptr_t)src_ptr,
                                                    src_w,
                                                    src_h,
                                                    (int)dst_x,
                                                    (int)dst_y,
                                                    dst_w,
                                                    dst_h);
}

static uint32_t sys_gfx_blit8_stretch_present(uint32_t src_ptr, uint32_t packed_src_wh,
                                              uint32_t dst_x, uint32_t dst_y,
                                              uint32_t packed_dst_wh) {
    int src_w = (int)(packed_src_wh & 0xFFFFu);
    int src_h = (int)((packed_src_wh >> 16) & 0xFFFFu);
    int dst_w = (int)(packed_dst_wh & 0xFFFFu);
    int dst_h = (int)((packed_dst_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8_stretch_present((const uint8_t *)(uintptr_t)src_ptr,
                                                            src_w,
                                                            src_h,
                                                            (int)dst_x,
                                                            (int)dst_y,
                                                            dst_w,
                                                            dst_h);
}

static uint32_t sys_gfx_leave(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_leave_graphics();
}

static uint32_t sys_gfx_set_mode(uint32_t width, uint32_t height, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_set_mode(width, height);
}

static uint32_t sys_gfx_set_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_set_palette((const uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_gfx_get_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_palette((uint8_t *)(uintptr_t)ptr);
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

static uint32_t sys_storage_partition_start_lba(uint32_t a, uint32_t b, uint32_t c,
                                                uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return mk_storage_service_partition_start_lba();
}

static uint32_t sys_storage_backend_load(uint32_t ptr, uint32_t size, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_load((void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_backend_save(uint32_t ptr, uint32_t size, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_save((const void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_backend_read_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_read_sectors(lba, (void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_backend_write_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                                  uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_write_sectors(lba, (const void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_backend_total_sectors(uint32_t a, uint32_t b, uint32_t c,
                                                  uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return kernel_storage_total_sectors();
}

static uint32_t sys_storage_backend_partition_start_lba(uint32_t a, uint32_t b, uint32_t c,
                                                        uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return kernel_storage_partition_start_lba();
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

static uint32_t sys_fs_backend_open(uint32_t path_ptr, uint32_t flags, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)open((const char *)(uintptr_t)path_ptr, (int)flags);
}

static uint32_t sys_fs_backend_read(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                                    uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)read((int)fd, (void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_backend_write(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                                     uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)write((int)fd, (const void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_backend_close(uint32_t fd, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)close((int)fd);
}

static uint32_t sys_fs_backend_lseek(uint32_t fd, uint32_t offset, uint32_t whence,
                                     uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)lseek((int)fd, (off_t)(int32_t)offset, (int)whence);
}

static uint32_t sys_fs_backend_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)stat((const char *)(uintptr_t)path_ptr,
                          (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_fs_backend_fstat(uint32_t fd, uint32_t stat_ptr, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)fstat((int)fd, (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_input_mouse(uint32_t state_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    struct mouse_state *state;

    (void)b; (void)c; (void)d; (void)e;
    if (state_ptr == 0u) {
        return 0;
    }

    state = (struct mouse_state *)(uintptr_t)state_ptr;
    if (!mk_input_service_poll_mouse(state)) {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    return 1u;
}

static uint32_t sys_input_event(uint32_t event_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    struct input_event *event;

    (void)b; (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return 0u;
    }

    event = (struct input_event *)(uintptr_t)event_ptr;
    return (uint32_t)mk_input_service_next_event(event);
}

static uint32_t sys_network_listen(uint32_t handle, uint32_t backlog, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_listen((int)handle, (int)backlog);
}

static uint32_t sys_network_accept(uint32_t handle, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_accept((int)handle);
}

static uint32_t sys_getpid(uint32_t a, uint32_t b, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return scheduler_current_pid();
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
    mk_audio_service_pump_async();
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
    return (uint32_t)mk_input_service_read_key();
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
    mk_audio_service_pump_async();
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
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_info((struct video_mode *)(uintptr_t)out_ptr);
}

static uint32_t sys_gfx_caps(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_caps((struct video_capabilities *)(uintptr_t)out_ptr);
}

static uint32_t sys_gfx_bench(uint32_t out_ptr, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    struct video_bench_info *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    out = (struct video_bench_info *)(uintptr_t)out_ptr;
    kernel_video_get_benchmarks(out);
    return 0u;
}

static uint32_t sys_audio_get_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_info((struct mk_audio_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_get_status(uint32_t out_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_status((struct audio_status *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_set_params(uint32_t params_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (params_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_set_params((const struct audio_swpar *)(uintptr_t)params_ptr);
}

static uint32_t sys_audio_start(uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_audio_service_start();
}

static uint32_t sys_audio_stop(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_audio_service_stop();
}

static uint32_t sys_audio_write(uint32_t data_ptr, uint32_t size, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_write((const void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_write_async(uint32_t data_ptr, uint32_t size, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_write_async((const void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_play_asset(uint32_t path_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    
    /* Always return success immediately to avoid UI blocking */
    /* The actual playback will be handled by audiosvc launched in background */
    return 0u;
}

static uint32_t sys_audio_read(uint32_t data_ptr, uint32_t size, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_read((void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_control_info(uint32_t index, uint32_t out_ptr, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_control_info(index,
                                                       (struct mk_audio_control_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_mixer_read(uint32_t control_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (control_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_mixer_read((mixer_ctrl_t *)(uintptr_t)control_ptr);
}

static uint32_t sys_audio_mixer_write(uint32_t control_ptr, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (control_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_mixer_write((const mixer_ctrl_t *)(uintptr_t)control_ptr);
}

static uint32_t sys_network_get_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_info((struct mk_network_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_get_status(uint32_t out_ptr, uint32_t b, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_status((struct mk_network_status *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_scan(uint32_t index, uint32_t out_ptr, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_scan(index,
                                                 (struct mk_network_scan_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_connect_wifi(uint32_t request_ptr, uint32_t b, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (request_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_connect_wifi(
        (const struct mk_network_connect_request *)(uintptr_t)request_ptr);
}

static uint32_t sys_network_disconnect(uint32_t if_name_ptr, uint32_t b, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (if_name_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_disconnect((const char *)(uintptr_t)if_name_ptr);
}

static uint32_t sys_network_connect_ethernet(uint32_t if_name_ptr, uint32_t b, uint32_t c,
                                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (if_name_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_connect_ethernet((const char *)(uintptr_t)if_name_ptr);
}

static uint32_t sys_network_configure_ethernet(uint32_t config_ptr, uint32_t b, uint32_t c,
                                               uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (config_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_configure_ethernet(
        (const struct mk_network_ethernet_config *)(uintptr_t)config_ptr);
}

static uint32_t sys_network_socket(uint32_t domain, uint32_t type, uint32_t protocol,
                                   uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_network_service_socket(domain, type, protocol);
}

static uint32_t sys_network_bind(uint32_t handle, uint32_t address_ptr, uint32_t address_length,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (address_ptr == 0u || address_length == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_bind((int)handle,
                                             (const struct sockaddr *)(uintptr_t)address_ptr,
                                             address_length);
}

static uint32_t sys_network_socket_connect(uint32_t handle, uint32_t address_ptr, uint32_t address_length,
                                           uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (address_ptr == 0u || address_length == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_socket_connect((int)handle,
                                                       (const struct sockaddr *)(uintptr_t)address_ptr,
                                                       address_length);
}

static uint32_t sys_network_send(uint32_t handle, uint32_t data_ptr, uint32_t size,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_send((int)handle,
                                             (const void *)(uintptr_t)data_ptr,
                                             size);
}

static uint32_t sys_network_recv(uint32_t handle, uint32_t buffer_ptr, uint32_t size,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (buffer_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_recv((int)handle,
                                             (void *)(uintptr_t)buffer_ptr,
                                             size);
}

static uint32_t sys_network_close(uint32_t handle, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_close((int)handle);
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

static uint32_t sys_transfer_size(uint32_t transfer_id, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return mk_transfer_size(transfer_id);
}

static uint32_t sys_transfer_read(uint32_t transfer_id, uint32_t dst_ptr, uint32_t size,
                                  uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (dst_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return mk_transfer_copy_to(transfer_id, (void *)(uintptr_t)dst_ptr, size) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_transfer_write(uint32_t transfer_id, uint32_t src_ptr, uint32_t size,
                                   uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (src_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    return mk_transfer_copy_from(transfer_id, (const void *)(uintptr_t)src_ptr, size) == 0 ? 0u : (uint32_t)-1;
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

    return (uint32_t)ipc_receive_wait(current,
                                      (void *)(uintptr_t)message_ptr,
                                      sizeof(struct mk_message));
}

static uint32_t sys_service_send(uint32_t message_ptr, uint32_t b, uint32_t c,
                                 uint32_t d, uint32_t e) {
    const struct mk_message *message;
    process_t *destination;
    int rc;

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

    rc = ipc_send(destination, message, sizeof(*message));
    return (uint32_t)rc;
}

static uint32_t sys_service_subscribe(uint32_t service_type, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || service_type == MK_SERVICE_NONE) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_service_subscribe(service_type, current);
}

static uint32_t sys_service_event_receive(uint32_t service_type, uint32_t event_ptr,
                                          uint32_t timeout_ticks, uint32_t d, uint32_t e) {
    process_t *current;

    (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE || event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_service_event_receive(service_type,
                                              current,
                                              (struct mk_service_event *)(uintptr_t)event_ptr,
                                              timeout_ticks);
}

static uint32_t sys_service_pid(uint32_t service_type, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    const struct mk_service_record *service;

    (void)b; (void)c; (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE) {
        return 0u;
    }

    service = mk_service_find_by_type(service_type);
    if (service == 0 || service->pid <= 0) {
        return 0u;
    }
    return (uint32_t)service->pid;
}

static uint32_t sys_service_restart(uint32_t service_type, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE) {
        return (uint32_t)-1;
    }
    return mk_service_restart(service_type) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_audio_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    process_t *current;

    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_audio_service_subscribe(current);
}

static uint32_t sys_audio_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                        uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_audio_service_event_receive(current,
                                                    (struct mk_audio_event *)(uintptr_t)event_ptr,
                                                    timeout_ticks);
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
            mk_service_fill_task_snapshot(&entries[i]);
        }
    }

    return count;
}

static uint32_t sys_launch_builtin_user(uint32_t target, uint32_t b, uint32_t c,
                                        uint32_t d, uint32_t e) {
    struct mk_launch_descriptor descriptor;
    uint32_t stack_size = 65536u;

    (void)b; (void)c; (void)d; (void)e;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_USER;
    descriptor.flags = MK_LAUNCH_FLAG_BUILTIN;

    switch (target) {
    case USERLAND_BUILTIN_SHELL:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_SHELL;
        descriptor.task_class = MK_TASK_CLASS_SHELL;
        memcpy(descriptor.name, "shell-host", 11u);
        descriptor.entry = userland_shell_host_entry;
        break;
    case USERLAND_BUILTIN_SHELL_SESSION:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_SHELL;
        descriptor.task_class = MK_TASK_CLASS_SHELL;
        memcpy(descriptor.name, "shell", 6u);
        descriptor.entry = userland_shell_session_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "desktop-host", 13u);
        descriptor.entry = userland_desktop_host_entry;
        break;
    case USERLAND_BUILTIN_STARTX:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "startx-host", 12u);
        descriptor.entry = userland_startx_host_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP_AUDIO:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_APP;
        descriptor.task_class = MK_TASK_CLASS_AUDIO_IO;
        memcpy(descriptor.name, "audio-host", 11u);
        descriptor.entry = userland_desktop_audio_host_entry;
        break;
    case USERLAND_BUILTIN_BOOT_AUDIO:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_APP;
        descriptor.task_class = MK_TASK_CLASS_AUDIO_IO;
        memcpy(descriptor.name, "boot-audio", 11u);
        descriptor.entry = userland_boot_audio_host_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP_SESSION:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "desktop", 8u);
        descriptor.entry = userland_desktop_session_entry;
        break;
    default:
        return (uint32_t)-1;
    }

    descriptor.stack_size = stack_size;
    return (uint32_t)mk_launch_bootstrap(&descriptor);
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

static int sys_launch_app_copy_name(const char *name, struct mk_launch_descriptor *descriptor) {
    const char *label;
    uint32_t len = 0u;
    uint32_t label_len = 0u;

    if (name == 0 || descriptor == 0) {
        return -1;
    }

    while (len < (MK_LAUNCH_ARGV_BYTES - 1u) && name[len] != '\0') {
        len += 1u;
    }
    if (len == 0u || name[len] != '\0') {
        return -1;
    }

    label = sys_launch_path_basename(name);
    if (label == 0 || label[0] == '\0') {
        label = name;
    }
    while (label[label_len] != '\0' && label_len < (MK_LAUNCH_NAME_MAX - 1u)) {
        descriptor->name[label_len] = label[label_len];
        label_len += 1u;
    }
    if (label_len == 0u) {
        return -1;
    }
    descriptor->name[label_len] = '\0';
    descriptor->argc = 1u;
    memcpy(descriptor->argv_data, name, len);
    descriptor->argv_data[len] = '\0';
    return 0;
}

static int sys_launch_app_copy_argv(const char *const *argv,
                                    uint32_t argc,
                                    struct mk_launch_descriptor *descriptor) {
    uint32_t arg_index;
    uint32_t used = 0u;

    if (argv == 0 || descriptor == 0 || argc == 0u || argc > MK_LAUNCH_ARGC_MAX) {
        return -1;
    }

    for (arg_index = 0; arg_index < argc; ++arg_index) {
        const char *arg = argv[arg_index];
        uint32_t len = 0u;

        if (arg == 0) {
            return -1;
        }
        while (used + len < (MK_LAUNCH_ARGV_BYTES - 1u) && arg[len] != '\0') {
            len += 1u;
        }
        if (len == 0u || arg[len] != '\0' || (used + len + 1u) > MK_LAUNCH_ARGV_BYTES) {
            return -1;
        }

        memcpy(&descriptor->argv_data[used], arg, len);
        descriptor->argv_data[used + len] = '\0';
        if (arg_index == 0u) {
            const char *label = sys_launch_path_basename(arg);
            uint32_t label_len = 0u;

            if (label == 0 || label[0] == '\0') {
                label = arg;
            }
            while (label[label_len] != '\0' && label_len < (MK_LAUNCH_NAME_MAX - 1u)) {
                descriptor->name[label_len] = label[label_len];
                label_len += 1u;
            }
            if (label_len == 0u) {
                return -1;
            }
            descriptor->name[label_len] = '\0';
        }
        used += len + 1u;
    }

    descriptor->argc = argc;
    return 0;
}

static void sys_launch_app_apply_role(struct mk_launch_descriptor *descriptor) {
    process_t *current_process;
    const struct mk_launch_context *current_context;
    const char *subcommand;

    if (descriptor == 0) {
        return;
    }

    if (strcmp(descriptor->name, "startx") == 0) {
        descriptor->flags &= ~MK_LAUNCH_FLAG_USER_APP;
        descriptor->flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor->task_class = MK_TASK_CLASS_DESKTOP;
        if (descriptor->stack_size < 262144u) {
            descriptor->stack_size = 262144u;
        }
    }

    current_process = scheduler_current();
    current_context = mk_launch_context_current();
    if ((current_process == 0 ||
         (current_process->task_class != MK_TASK_CLASS_DESKTOP &&
          current_process->task_class != MK_TASK_CLASS_SUPERVISION)) &&
        (current_context == 0 ||
         (current_context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) == 0u)) {
        return;
    }

    subcommand = sys_launch_descriptor_arg(descriptor, 1u);

    if (strcmp(descriptor->name, "audiosvc") == 0) {
        if (subcommand != 0 && strcmp(subcommand, "play-asset") == 0) {
            descriptor->task_class = MK_TASK_CLASS_AUDIO_IO;
        }
        return;
    }
}

static uint32_t sys_launch_app(uint32_t name_ptr, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    struct mk_launch_descriptor descriptor;

    (void)c; (void)d; (void)e;
    if (name_ptr == 0u) {
        return (uint32_t)-1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_USER;
    descriptor.stack_size = 65536u;
    descriptor.flags = MK_LAUNCH_FLAG_USER_APP;
    descriptor.task_class = MK_TASK_CLASS_APP_RUNTIME;
    if (b == 0u) {
        if (sys_launch_app_copy_name((const char *)(uintptr_t)name_ptr, &descriptor) != 0) {
            return (uint32_t)-1;
        }
    } else {
        if (sys_launch_app_copy_argv((const char *const *)(uintptr_t)name_ptr, b, &descriptor) != 0) {
            return (uint32_t)-1;
        }
    }
    sys_launch_app_apply_role(&descriptor);
    descriptor.entry = userland_app_runtime_entry;
    return (uint32_t)mk_launch_bootstrap(&descriptor);
}

static uint32_t sys_task_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                         uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)(scheduler_task_event_subscribe(current, a, b) == 0 ? 0 : (uint32_t)-1);
}

static uint32_t sys_task_event_receive(uint32_t event_ptr, uint32_t timeout_ticks, uint32_t c,
                                       uint32_t d, uint32_t e) {
    process_t *current;
    struct mk_task_event *event;

    (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    event = (struct mk_task_event *)(uintptr_t)event_ptr;
    return (uint32_t)(scheduler_task_event_receive(current, event, timeout_ticks) == 0 ? 0 : (uint32_t)-1);
}

static uint32_t sys_video_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;

    if (current == 0) {
        return (uint32_t)-1;
    }
    return mk_video_service_subscribe(current) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_video_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                        uint32_t c, uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    struct mk_video_event event;

    (void)c;
    (void)d;
    (void)e;
    if (current == 0 || event_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (mk_video_service_event_receive(current,
                                       &event,
                                       timeout_ticks) != 0) {
        return (uint32_t)-1;
    }
    memcpy((void *)(uintptr_t)event_ptr, &event, sizeof(event));
    return 0u;
}

static uint32_t sys_video_present_submit(uint32_t mode, uint32_t sequence_ptr,
                                         uint32_t c, uint32_t d, uint32_t e) {
    uint32_t sequence = 0u;

    (void)c;
    (void)d;
    (void)e;
    if (mk_video_service_present_submit(mode, &sequence) != 0) {
        return (uint32_t)-1;
    }
    if (sequence_ptr != 0u) {
        *(uint32_t *)(uintptr_t)sequence_ptr = sequence;
    }
    return 0u;
}

static uint32_t sys_network_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                            uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();

    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    if (current == 0) {
        return (uint32_t)-1;
    }
    return mk_network_service_subscribe(current) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_network_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                          uint32_t c, uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    struct mk_network_event event;

    (void)c;
    (void)d;
    (void)e;
    if (current == 0 || event_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (mk_network_service_event_receive(current, &event, timeout_ticks) != 0) {
        return (uint32_t)-1;
    }
    memcpy((void *)(uintptr_t)event_ptr, &event, sizeof(event));
    return 0u;
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GFX_CLEAR] = sys_gfx_clear;
    syscall_table[SYSCALL_GFX_RECT] = sys_gfx_rect;
    syscall_table[SYSCALL_GFX_TEXT] = sys_gfx_text;
    syscall_table[SYSCALL_GFX_FLIP] = sys_gfx_flip;
    syscall_table[SYSCALL_GFX_SET_PRESENT_POLICY] = sys_gfx_set_present_policy;
    syscall_table[SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE] = sys_gfx_set_present_copy_override;
    syscall_table[SYSCALL_GFX_BLIT8] = sys_gfx_blit8;
    syscall_table[SYSCALL_GFX_BLIT8_PRESENT] = sys_gfx_blit8_present;
    syscall_table[SYSCALL_GFX_BLIT8_STRETCH] = sys_gfx_blit8_stretch;
    syscall_table[SYSCALL_GFX_BLIT8_STRETCH_PRESENT] = sys_gfx_blit8_stretch_present;
    syscall_table[SYSCALL_GFX_LEAVE] = sys_gfx_leave;
    syscall_table[SYSCALL_GFX_SET_MODE] = sys_gfx_set_mode;
    syscall_table[SYSCALL_GFX_SET_PALETTE] = sys_gfx_set_palette;
    syscall_table[SYSCALL_GFX_GET_PALETTE] = sys_gfx_get_palette;
    syscall_table[SYSCALL_STORAGE_LOAD] = sys_storage_load;
    syscall_table[SYSCALL_STORAGE_SAVE] = sys_storage_save;
    syscall_table[SYSCALL_STORAGE_READ_SECTORS] = sys_storage_read_sectors;
    syscall_table[SYSCALL_STORAGE_WRITE_SECTORS] = sys_storage_write_sectors;
    syscall_table[SYSCALL_STORAGE_TOTAL_SECTORS] = sys_storage_total_sectors;
    syscall_table[SYSCALL_STORAGE_PARTITION_START_LBA] = sys_storage_partition_start_lba;
    syscall_table[SYSCALL_OPEN] = sys_fs_open;
    syscall_table[SYSCALL_READ] = sys_fs_read;
    syscall_table[SYSCALL_WRITE] = sys_fs_write;
    syscall_table[SYSCALL_CLOSE] = sys_fs_close;
    syscall_table[SYSCALL_LSEEK] = sys_fs_lseek;
    syscall_table[SYSCALL_STAT] = sys_fs_stat;
    syscall_table[SYSCALL_FSTAT] = sys_fs_fstat;
    syscall_table[SYSCALL_INPUT_MOUSE] = sys_input_mouse;
    syscall_table[SYSCALL_INPUT_KEY] = sys_input_key;
    syscall_table[SYSCALL_INPUT_EVENT] = sys_input_event;
    syscall_table[SYSCALL_TEXT_PUTC] = sys_text_putc;     /* legacy text mode */
    syscall_table[SYSCALL_TEXT_CLEAR] = sys_text_clear;   /* legacy text mode */
    syscall_table[SYSCALL_TEXT_MOVE_CURSOR] = sys_text_move_cursor;
    syscall_table[SYSCALL_TEXT_WRITE] = sys_text_write;
    syscall_table[SYSCALL_SLEEP] = sys_sleep;
    syscall_table[SYSCALL_TIME_TICKS] = sys_time_ticks;
    syscall_table[SYSCALL_GFX_INFO] = sys_gfx_info;
    syscall_table[SYSCALL_GFX_CAPS] = sys_gfx_caps;
    syscall_table[SYSCALL_GFX_BENCH] = sys_gfx_bench;
    syscall_table[SYSCALL_AUDIO_GETINFO] = sys_audio_get_info;
    syscall_table[SYSCALL_AUDIO_GET_STATUS] = sys_audio_get_status;
    syscall_table[SYSCALL_AUDIO_SET_PARAMS] = sys_audio_set_params;
    syscall_table[SYSCALL_AUDIO_START] = sys_audio_start;
    syscall_table[SYSCALL_AUDIO_STOP] = sys_audio_stop;
    syscall_table[SYSCALL_AUDIO_WRITE] = sys_audio_write;
    syscall_table[SYSCALL_AUDIO_WRITE_ASYNC] = sys_audio_write_async;
    syscall_table[SYSCALL_AUDIO_PLAY_ASSET] = sys_audio_play_asset;
    syscall_table[SYSCALL_AUDIO_READ] = sys_audio_read;
    syscall_table[SYSCALL_AUDIO_CONTROL_INFO] = sys_audio_control_info;
    syscall_table[SYSCALL_AUDIO_MIXER_READ] = sys_audio_mixer_read;
    syscall_table[SYSCALL_AUDIO_MIXER_WRITE] = sys_audio_mixer_write;
    syscall_table[SYSCALL_NETWORK_GETINFO] = sys_network_get_info;
    syscall_table[SYSCALL_NETWORK_GET_STATUS] = sys_network_get_status;
    syscall_table[SYSCALL_NETWORK_SCAN] = sys_network_scan;
    syscall_table[SYSCALL_NETWORK_CONNECT_WIFI] = sys_network_connect_wifi;
    syscall_table[SYSCALL_NETWORK_DISCONNECT] = sys_network_disconnect;
    syscall_table[SYSCALL_NETWORK_CONNECT_ETHERNET] = sys_network_connect_ethernet;
    syscall_table[SYSCALL_NETWORK_CONFIGURE_ETHERNET] = sys_network_configure_ethernet;
    syscall_table[SYSCALL_NETWORK_SOCKET] = sys_network_socket;
    syscall_table[SYSCALL_NETWORK_BIND] = sys_network_bind;
    syscall_table[SYSCALL_NETWORK_CONNECT] = sys_network_socket_connect;
    syscall_table[SYSCALL_NETWORK_SEND] = sys_network_send;
    syscall_table[SYSCALL_NETWORK_RECV] = sys_network_recv;
    syscall_table[SYSCALL_NETWORK_CLOSE] = sys_network_close;
    syscall_table[SYSCALL_NETWORK_LISTEN] = sys_network_listen;
    syscall_table[SYSCALL_NETWORK_ACCEPT] = sys_network_accept;
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
    syscall_table[SYSCALL_SERVICE_SUBSCRIBE] = sys_service_subscribe;
    syscall_table[SYSCALL_SERVICE_PID] = sys_service_pid;
    syscall_table[SYSCALL_SERVICE_RESTART] = sys_service_restart;
    syscall_table[SYSCALL_SERVICE_EVENT_RECV] = sys_service_event_receive;
    syscall_table[SYSCALL_TRANSFER_SIZE] = sys_transfer_size;
    syscall_table[SYSCALL_TRANSFER_READ] = sys_transfer_read;
    syscall_table[SYSCALL_TRANSFER_WRITE] = sys_transfer_write;
    syscall_table[SYSCALL_STORAGE_BACKEND_LOAD] = sys_storage_backend_load;
    syscall_table[SYSCALL_STORAGE_BACKEND_SAVE] = sys_storage_backend_save;
    syscall_table[SYSCALL_STORAGE_BACKEND_READ_SECTORS] = sys_storage_backend_read_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_WRITE_SECTORS] = sys_storage_backend_write_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_TOTAL_SECTORS] = sys_storage_backend_total_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_PARTITION_START_LBA] =
        sys_storage_backend_partition_start_lba;
    syscall_table[SYSCALL_FS_BACKEND_OPEN] = sys_fs_backend_open;
    syscall_table[SYSCALL_FS_BACKEND_READ] = sys_fs_backend_read;
    syscall_table[SYSCALL_FS_BACKEND_WRITE] = sys_fs_backend_write;
    syscall_table[SYSCALL_FS_BACKEND_CLOSE] = sys_fs_backend_close;
    syscall_table[SYSCALL_FS_BACKEND_LSEEK] = sys_fs_backend_lseek;
    syscall_table[SYSCALL_FS_BACKEND_STAT] = sys_fs_backend_stat;
    syscall_table[SYSCALL_FS_BACKEND_FSTAT] = sys_fs_backend_fstat;
    syscall_table[SYSCALL_AUDIO_EVENT_SUBSCRIBE] = sys_audio_event_subscribe;
    syscall_table[SYSCALL_AUDIO_EVENT_RECV] = sys_audio_event_receive;
    syscall_table[SYSCALL_VIDEO_EVENT_SUBSCRIBE] = sys_video_event_subscribe;
    syscall_table[SYSCALL_VIDEO_EVENT_RECV] = sys_video_event_receive;
    syscall_table[SYSCALL_VIDEO_PRESENT_SUBMIT] = sys_video_present_submit;
    syscall_table[SYSCALL_NETWORK_EVENT_SUBSCRIBE] = sys_network_event_subscribe;
    syscall_table[SYSCALL_NETWORK_EVENT_RECV] = sys_network_event_receive;
    syscall_table[SYSCALL_TASK_SNAPSHOT] = sys_task_snapshot;
    syscall_table[SYSCALL_LAUNCH_BUILTIN_USER] = sys_launch_builtin_user;
    syscall_table[SYSCALL_TASK_TERMINATE] = sys_task_terminate;
    syscall_table[SYSCALL_LAUNCH_APP] = sys_launch_app;
    syscall_table[SYSCALL_TASK_EVENT_SUBSCRIBE] = sys_task_event_subscribe;
    syscall_table[SYSCALL_TASK_EVENT_RECV] = sys_task_event_receive;
}

/* dispatch routine called by ISR */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
