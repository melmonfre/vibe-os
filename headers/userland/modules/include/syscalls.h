#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <include/userland_api.h>

struct mk_message;

int sys_poll_mouse(struct mouse_state *state);
int sys_poll_key(void);
void sys_clear(uint8_t color);
void sys_rect(int x, int y, int w, int h, uint8_t color);
void sys_text(int x, int y, uint8_t color, const char *text);
void sys_present(void);
void sys_leave_graphics(void);
int sys_gfx_set_mode(uint32_t width, uint32_t height);
int sys_gfx_set_palette(const uint8_t *rgb_triplets);
int sys_gfx_get_palette(uint8_t *rgb_triplets);
void sys_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);
int sys_storage_load(void *dst, uint32_t size);
int sys_storage_save(const void *src, uint32_t size);
int sys_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int sys_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
uint32_t sys_storage_total_sectors(void);
int sys_open(const char *path, int flags);
int sys_read(int fd, void *buf, uint32_t count);
int sys_write(int fd, const void *buf, uint32_t count);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_stat(const char *path, struct stat *buf);
int sys_fstat(int fd, struct stat *buf);
int sys_launch_info(struct userland_launch_info *info);
void sys_sleep(void);
uint32_t sys_ticks(void);
int sys_gfx_info(struct video_mode *mode);
int sys_gfx_caps(struct video_capabilities *caps);
int sys_getpid(void);
void sys_yield(void);
void sys_write_debug(const char *msg);
int sys_text_write(const char *msg);
int sys_keyboard_set_layout(const char *name);
int sys_keyboard_get_layout(char *buffer, int size);
int sys_keyboard_get_available_layouts(char *buffer, int size);
int sys_service_receive(struct mk_message *message);
int sys_service_send(const struct mk_message *message);
int sys_service_backend(const struct mk_message *request, struct mk_message *reply);
void sys_shutdown(void);

#endif // SYSCALLS_H
