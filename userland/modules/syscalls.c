#include <userland/modules/include/syscalls.h>

static inline int syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

int sys_poll_mouse(struct mouse_state *state) {
    return syscall5(SYSCALL_INPUT_MOUSE, (int)(uintptr_t)state, 0, 0, 0, 0);
}

int sys_poll_key(void) {
    return syscall5(SYSCALL_INPUT_KEY, 0, 0, 0, 0, 0);
}

void sys_clear(uint8_t color) {
    (void)syscall5(SYSCALL_GFX_CLEAR, color, 0, 0, 0, 0);
}

void sys_rect(int x, int y, int w, int h, uint8_t color) {
    (void)syscall5(SYSCALL_GFX_RECT, x, y, w, h, color);
}

void sys_text(int x, int y, uint8_t color, const char *text) {
    (void)syscall5(SYSCALL_GFX_TEXT, x, y, (int)(uintptr_t)text, color, 0);
}

void sys_present(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_AUTO, 0, 0, 0, 0);
}

void sys_present_dirty(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_DIRTY, 0, 0, 0, 0);
}

void sys_present_full(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_FULL, 0, 0, 0, 0);
}

int sys_gfx_set_present_policy(uint32_t policy) {
    return syscall5(SYSCALL_GFX_SET_PRESENT_POLICY, (int)policy, 0, 0, 0, 0);
}

int sys_gfx_set_present_copy_override(uint32_t kind) {
    return syscall5(SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE, (int)kind, 0, 0, 0, 0);
}

void sys_leave_graphics(void) {
    (void)syscall5(SYSCALL_GFX_LEAVE, 0, 0, 0, 0, 0);
}

int sys_gfx_set_mode(uint32_t width, uint32_t height) {
    return syscall5(SYSCALL_GFX_SET_MODE, (int)width, (int)height, 0, 0, 0);
}

int sys_gfx_set_palette(const uint8_t *rgb_triplets) {
    return syscall5(SYSCALL_GFX_SET_PALETTE, (int)(uintptr_t)rgb_triplets, 0, 0, 0, 0);
}

int sys_gfx_get_palette(uint8_t *rgb_triplets) {
    return syscall5(SYSCALL_GFX_GET_PALETTE, (int)(uintptr_t)rgb_triplets, 0, 0, 0, 0);
}

void sys_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    int packed_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8,
                   (int)(uintptr_t)src,
                   packed_wh,
                   dst_x,
                   dst_y,
                   scale);
}

void sys_gfx_blit8_present(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    int packed_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8_PRESENT,
                   (int)(uintptr_t)src,
                   packed_wh,
                   dst_x,
                   dst_y,
                   scale);
}

void sys_gfx_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                           int dst_x, int dst_y, int dst_w, int dst_h) {
    int packed_src_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    int packed_dst_wh = ((dst_h & 0xFFFF) << 16) | (dst_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8_STRETCH,
                   (int)(uintptr_t)src,
                   packed_src_wh,
                   dst_x,
                   dst_y,
                   packed_dst_wh);
}

void sys_gfx_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h) {
    int packed_src_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    int packed_dst_wh = ((dst_h & 0xFFFF) << 16) | (dst_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8_STRETCH_PRESENT,
                   (int)(uintptr_t)src,
                   packed_src_wh,
                   dst_x,
                   dst_y,
                   packed_dst_wh);
}

int sys_storage_load(void *dst, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_LOAD, (int)(uintptr_t)dst, (int)size, 0, 0, 0);
}

int sys_storage_save(const void *src, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_SAVE, (int)(uintptr_t)src, (int)size, 0, 0, 0);
}

int sys_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_READ_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)dst,
                    (int)sector_count,
                    0,
                    0);
}

int sys_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_WRITE_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)src,
                    (int)sector_count,
                    0,
                    0);
}

uint32_t sys_storage_total_sectors(void) {
    return (uint32_t)syscall5(SYSCALL_STORAGE_TOTAL_SECTORS, 0, 0, 0, 0, 0);
}

int sys_open(const char *path, int flags) {
    return syscall5(SYSCALL_OPEN, (int)(uintptr_t)path, flags, 0, 0, 0);
}

int sys_read(int fd, void *buf, uint32_t count) {
    return syscall5(SYSCALL_READ, fd, (int)(uintptr_t)buf, (int)count, 0, 0);
}

int sys_write(int fd, const void *buf, uint32_t count) {
    return syscall5(SYSCALL_WRITE, fd, (int)(uintptr_t)buf, (int)count, 0, 0);
}

int sys_close(int fd) {
    return syscall5(SYSCALL_CLOSE, fd, 0, 0, 0, 0);
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall5(SYSCALL_LSEEK, fd, (int)offset, whence, 0, 0);
}

int sys_stat(const char *path, struct stat *buf) {
    return syscall5(SYSCALL_STAT, (int)(uintptr_t)path, (int)(uintptr_t)buf, 0, 0, 0);
}

int sys_fstat(int fd, struct stat *buf) {
    return syscall5(SYSCALL_FSTAT, fd, (int)(uintptr_t)buf, 0, 0, 0);
}

int sys_launch_info(struct userland_launch_info *info) {
    return syscall5(SYSCALL_LAUNCH_INFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_task_snapshot(struct task_snapshot_summary *summary,
                      struct task_snapshot_entry *entries,
                      uint32_t max_entries) {
    return syscall5(SYSCALL_TASK_SNAPSHOT,
                    (int)(uintptr_t)summary,
                    (int)(uintptr_t)entries,
                    (int)max_entries,
                    0,
                    0);
}

int sys_task_terminate(uint32_t pid) {
    return syscall5(SYSCALL_TASK_TERMINATE, (int)pid, 0, 0, 0, 0);
}

void sys_sleep(void) {
    (void)syscall5(SYSCALL_SLEEP, 0, 0, 0, 0, 0);
}

uint32_t sys_ticks(void) {
    return (uint32_t)syscall5(SYSCALL_TIME_TICKS, 0, 0, 0, 0, 0);
}
int sys_gfx_info(struct video_mode *mode) {
    return syscall5(SYSCALL_GFX_INFO, (int)(uintptr_t)mode, 0, 0, 0, 0);
}

int sys_gfx_caps(struct video_capabilities *caps) {
    return syscall5(SYSCALL_GFX_CAPS, (int)(uintptr_t)caps, 0, 0, 0, 0);
}

int sys_gfx_bench(struct video_bench_info *bench) {
    return syscall5(SYSCALL_GFX_BENCH, (int)(uintptr_t)bench, 0, 0, 0, 0);
}

int sys_audio_get_info(struct mk_audio_info *info) {
    return syscall5(SYSCALL_AUDIO_GETINFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_audio_get_status(struct audio_status *status) {
    return syscall5(SYSCALL_AUDIO_GET_STATUS, (int)(uintptr_t)status, 0, 0, 0, 0);
}

int sys_audio_set_params(const struct audio_swpar *params) {
    return syscall5(SYSCALL_AUDIO_SET_PARAMS, (int)(uintptr_t)params, 0, 0, 0, 0);
}

int sys_audio_start(void) {
    return syscall5(SYSCALL_AUDIO_START, 0, 0, 0, 0, 0);
}

int sys_audio_stop(void) {
    return syscall5(SYSCALL_AUDIO_STOP, 0, 0, 0, 0, 0);
}

int sys_audio_write(const void *data, uint32_t size) {
    return syscall5(SYSCALL_AUDIO_WRITE, (int)(uintptr_t)data, (int)size, 0, 0, 0);
}

int sys_audio_read(void *data, uint32_t size) {
    return syscall5(SYSCALL_AUDIO_READ, (int)(uintptr_t)data, (int)size, 0, 0, 0);
}

int sys_audio_control_info(uint32_t index, struct mk_audio_control_info *info) {
    return syscall5(SYSCALL_AUDIO_CONTROL_INFO, (int)index, (int)(uintptr_t)info, 0, 0, 0);
}

int sys_audio_mixer_read(mixer_ctrl_t *control) {
    return syscall5(SYSCALL_AUDIO_MIXER_READ, (int)(uintptr_t)control, 0, 0, 0, 0);
}

int sys_audio_mixer_write(const mixer_ctrl_t *control) {
    return syscall5(SYSCALL_AUDIO_MIXER_WRITE, (int)(uintptr_t)control, 0, 0, 0, 0);
}

int sys_network_get_info(struct mk_network_info *info) {
    return syscall5(SYSCALL_NETWORK_GETINFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_network_get_status(struct mk_network_status *status) {
    return syscall5(SYSCALL_NETWORK_GET_STATUS, (int)(uintptr_t)status, 0, 0, 0, 0);
}

int sys_network_scan(uint32_t index, struct mk_network_scan_info *info) {
    return syscall5(SYSCALL_NETWORK_SCAN, (int)index, (int)(uintptr_t)info, 0, 0, 0);
}

int sys_network_connect_wifi(const struct mk_network_connect_request *request) {
    return syscall5(SYSCALL_NETWORK_CONNECT_WIFI, (int)(uintptr_t)request, 0, 0, 0, 0);
}

int sys_network_connect_ethernet(const char *if_name) {
    return syscall5(SYSCALL_NETWORK_CONNECT_ETHERNET, (int)(uintptr_t)if_name, 0, 0, 0, 0);
}

int sys_network_configure_ethernet(const struct mk_network_ethernet_config *config) {
    return syscall5(SYSCALL_NETWORK_CONFIGURE_ETHERNET, (int)(uintptr_t)config, 0, 0, 0, 0);
}

int sys_network_disconnect(const char *if_name) {
    return syscall5(SYSCALL_NETWORK_DISCONNECT, (int)(uintptr_t)if_name, 0, 0, 0, 0);
}

int sys_network_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    return syscall5(SYSCALL_NETWORK_SOCKET, (int)domain, (int)type, (int)protocol, 0, 0);
}

int sys_network_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    return syscall5(SYSCALL_NETWORK_BIND,
                    handle,
                    (int)(uintptr_t)address,
                    (int)address_length,
                    0,
                    0);
}

int sys_network_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    return syscall5(SYSCALL_NETWORK_CONNECT,
                    handle,
                    (int)(uintptr_t)address,
                    (int)address_length,
                    0,
                    0);
}

int sys_network_send(int handle, const void *data, uint32_t size) {
    return syscall5(SYSCALL_NETWORK_SEND,
                    handle,
                    (int)(uintptr_t)data,
                    (int)size,
                    0,
                    0);
}

int sys_network_recv(int handle, void *buffer, uint32_t size) {
    return syscall5(SYSCALL_NETWORK_RECV,
                    handle,
                    (int)(uintptr_t)buffer,
                    (int)size,
                    0,
                    0);
}

int sys_network_close(int handle) {
    return syscall5(SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
}

int sys_network_listen(int handle, int backlog) {
    return syscall5(SYSCALL_NETWORK_LISTEN, handle, backlog, 0, 0, 0);
}

int sys_network_accept(int handle) {
    return syscall5(SYSCALL_NETWORK_ACCEPT, handle, 0, 0, 0, 0);
}

int sys_getpid(void) {
    return syscall5(SYSCALL_GETPID, 0, 0, 0, 0, 0);
}

void sys_yield(void) {
    (void)syscall5(SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

void sys_write_debug(const char *msg) {
    (void)syscall5(SYSCALL_WRITE_DEBUG, (int)(uintptr_t)msg, 0, 0, 0, 0);
}

int sys_text_write(const char *msg) {
    return syscall5(SYSCALL_TEXT_WRITE, (int)(uintptr_t)msg, 0, 0, 0, 0);
}

int sys_keyboard_set_layout(const char *name) {
    return syscall5(SYSCALL_KEYBOARD_SET_LAYOUT, (int)(uintptr_t)name, 0, 0, 0, 0);
}

int sys_keyboard_get_layout(char *buffer, int size) {
    return syscall5(SYSCALL_KEYBOARD_GET_LAYOUT, (int)(uintptr_t)buffer, size, 0, 0, 0);
}

int sys_keyboard_get_available_layouts(char *buffer, int size) {
    return syscall5(SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS, (int)(uintptr_t)buffer, size, 0, 0, 0);
}

int sys_service_receive(struct mk_message *message) {
    return syscall5(SYSCALL_SERVICE_RECV, (int)(uintptr_t)message, 0, 0, 0, 0);
}

int sys_service_send(const struct mk_message *message) {
    return syscall5(SYSCALL_SERVICE_SEND, (int)(uintptr_t)message, 0, 0, 0, 0);
}

int sys_service_backend(const struct mk_message *request, struct mk_message *reply) {
    return syscall5(SYSCALL_SERVICE_BACKEND,
                    (int)(uintptr_t)request,
                    (int)(uintptr_t)reply,
                    0,
                    0,
                    0);
}

void sys_shutdown(void) {
    (void)syscall5(SYSCALL_SHUTDOWN, 0, 0, 0, 0, 0);
}
