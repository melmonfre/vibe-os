#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <include/userland_api.h>

int sys_poll_mouse(struct mouse_state *state);
int sys_poll_key(void);
void sys_clear(uint8_t color);
void sys_rect(int x, int y, int w, int h, uint8_t color);
void sys_text(int x, int y, uint8_t color, const char *text);
void sys_present(void);
void sys_leave_graphics(void);
int sys_gfx_set_mode(uint32_t width, uint32_t height);
int sys_storage_load(void *dst, uint32_t size);
int sys_storage_save(const void *src, uint32_t size);
int sys_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
void sys_sleep(void);
uint32_t sys_ticks(void);
int sys_gfx_info(struct video_mode *mode);
int sys_getpid(void);
void sys_yield(void);
void sys_write_debug(const char *msg);

#endif // SYSCALLS_H
