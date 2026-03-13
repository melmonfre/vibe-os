#ifndef VIBE_LANG_VIBE_APP_RUNTIME_H
#define VIBE_LANG_VIBE_APP_RUNTIME_H

#include <stddef.h>

#include <lang/include/vibe_app.h>

const struct vibe_app_context *vibe_app_get_context(void);
void vibe_app_console_putc(char c);
void vibe_app_console_write(const char *text);
int vibe_app_poll_key(void);
void vibe_app_yield(void);
int vibe_app_read_file(const char *path, const char **data_out, int *size_out);
int vibe_app_read_line(char *buf, int max_len, const char *prompt);

void *vibe_app_malloc(size_t size);
void vibe_app_free(void *ptr);
void *vibe_app_realloc(void *ptr, size_t size);
void vibe_app_runtime_init(const struct vibe_app_context *ctx);

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
char *strcpy(char *dst, const char *src);
char *strchr(const char *text, int c);

int vibe_app_main(int argc, char **argv);

#endif
