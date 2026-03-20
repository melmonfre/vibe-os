#ifndef VIBE_LANG_VIBE_APP_H
#define VIBE_LANG_VIBE_APP_H

#include <stddef.h>
#include <stdint.h>

#define VIBE_APP_MAGIC 0x50504156u
#define VIBE_APP_ABI_VERSION 1u
#define VIBE_APP_NAME_MAX 16u

#define VIBE_APP_LOAD_ADDR 0x00300000u
#define VIBE_APP_ARENA_SIZE 0x01000000u
#define VIBE_APP_STACK_SIZE 0x00010000u
#define VIBE_APP_STACK_TOP (VIBE_APP_LOAD_ADDR + VIBE_APP_ARENA_SIZE)

struct vibe_app_stat {
    int size;
    int is_dir;
};

struct vibe_app_header {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t memory_size;
    uint32_t entry_offset;
    uint32_t required_heap_size;
    char name[VIBE_APP_NAME_MAX];
};

struct vibe_app_host_api {
    uint32_t abi_version;
    void (*console_putc)(char c);
    void (*console_write)(const char *text);
    int (*poll_key)(void);
    void (*yield)(void);
    int (*read_file)(const char *path, const char **data_out, int *size_out);
    int (*write_file)(const char *path, const void *data, int size);
    int (*create_dir)(const char *path);
    void (*write_debug)(const char *msg);
    int (*getcwd)(char *buf, int max_len);
    int (*remove_dir)(const char *path);
    int (*keyboard_set_layout)(const char *name);
    int (*keyboard_get_layout)(char *buf, int max_len);
    int (*keyboard_get_available_layouts)(char *buf, int max_len);
    int (*open_file)(const char *path, int flags);
    int (*read_fd)(int fd, void *buf, int size);
    int (*write_fd)(int fd, const void *buf, int size);
    int (*close_fd)(int fd);
    int (*seek_fd)(int fd, int offset, int whence);
    int (*stat_path)(const char *path, struct vibe_app_stat *stat_out);
    int (*fstat_fd)(int fd, struct vibe_app_stat *stat_out);
    const char *(*getenv_value)(const char *name);
};

struct vibe_app_context {
    const struct vibe_app_host_api *host;
    void *heap_base;
    uint32_t heap_size;
};

typedef int (*vibe_app_entry_t)(const struct vibe_app_context *ctx, int argc, char **argv);

#endif
