#include <lang/include/vibe_app.h>
#include <lang/include/vibe_appfs.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

#include <stdint.h>

static struct vibe_app_context g_app_ctx;
static void lang_debug_vga(int row, const char *text);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
static int host_getcwd(char *buf, int max_len);

#define LANG_HOST_MAX_FDS 16
#define LANG_HOST_O_RDONLY 0
#define LANG_HOST_O_WRONLY 1
#define LANG_HOST_O_RDWR 2
#define LANG_HOST_O_ACCMODE 0x0003
#define LANG_HOST_O_CREAT 0x0040
#define LANG_HOST_O_TRUNC 0x0200
#define LANG_HOST_O_APPEND 0x0400
#define LANG_HOST_SEEK_SET 0
#define LANG_HOST_SEEK_CUR 1
#define LANG_HOST_SEEK_END 2

struct lang_host_fd {
    int used;
    int flags;
    int pos;
    int size;
    int capacity;
    int dirty;
    unsigned char *buf;
    char path[256];
};

static struct lang_host_fd g_lang_host_fds[LANG_HOST_MAX_FDS];

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static void *lang_memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dst;
}

static void lang_memset(void *dst, int value, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;

    for (uint32_t i = 0; i < size; ++i) {
        out[i] = (uint8_t)value;
    }
}

static void lang_reset_host_fd(struct lang_host_fd *fd) {
    if (!fd) {
        return;
    }
    if (fd->buf) {
        free(fd->buf);
    }
    lang_memset(fd, 0, (uint32_t)sizeof(*fd));
}

static void lang_reset_host_fds(void) {
    int i;

    for (i = 0; i < LANG_HOST_MAX_FDS; ++i) {
        lang_reset_host_fd(&g_lang_host_fds[i]);
    }
}

static int lang_host_reserve(struct lang_host_fd *fd, int needed) {
    int new_capacity;
    unsigned char *new_buf;

    if (!fd || needed < 0) {
        return -1;
    }
    if (needed <= fd->capacity) {
        return 0;
    }

    new_capacity = fd->capacity > 0 ? fd->capacity : 256;
    while (new_capacity < needed) {
        if (new_capacity > (1 << 29)) {
            return -1;
        }
        new_capacity *= 2;
    }

    new_buf = (unsigned char *)realloc(fd->buf, (size_t)new_capacity);
    if (!new_buf) {
        return -1;
    }
    fd->buf = new_buf;
    fd->capacity = new_capacity;
    return 0;
}

static int lang_host_load_file(struct lang_host_fd *fd, int node) {
    if (!fd || node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (g_fs_nodes[node].size <= 0) {
        fd->size = 0;
        return 0;
    }
    if (lang_host_reserve(fd, g_fs_nodes[node].size) != 0) {
        return -1;
    }
    if (fs_read_node_bytes(node, 0, fd->buf, g_fs_nodes[node].size) != g_fs_nodes[node].size) {
        return -1;
    }
    fd->size = g_fs_nodes[node].size;
    return 0;
}

static int lang_host_flush_fd(struct lang_host_fd *fd) {
    if (!fd || !fd->used) {
        return -1;
    }
    if ((fd->flags & LANG_HOST_O_ACCMODE) == LANG_HOST_O_RDONLY || !fd->dirty) {
        return 0;
    }
    if (fs_write_bytes(fd->path, fd->buf, fd->size) != 0) {
        return -1;
    }
    fd->dirty = 0;
    return 0;
}

static int lang_alloc_host_fd(void) {
    int i;

    for (i = 3; i < LANG_HOST_MAX_FDS; ++i) {
        if (!g_lang_host_fds[i].used) {
            return i;
        }
    }
    return -1;
}

static uint32_t lang_checksum_bytes(const uint8_t *data, uint32_t size) {
    uint32_t hash = 2166136261u;

    for (uint32_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int host_read_file(const char *path, const char **data_out, int *size_out) {
    int node;

    if (!path || !data_out || !size_out) {
        return -1;
    }

    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        return -1;
    }

    *data_out = g_fs_nodes[node].data;
    *size_out = g_fs_nodes[node].size;
    return 0;
}

static int host_write_file(const char *path, const void *data, int size) {
    if (!path || (!data && size > 0) || size < 0) {
        return -1;
    }
    return fs_write_bytes(path, (const uint8_t *)data, size);
}

static int host_create_dir(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    return fs_create(path, 1);
}

static int host_open_file(const char *path, int flags) {
    int fd_index;
    int node;
    int accmode;
    struct lang_host_fd *fd;

    if (!path || path[0] == '\0') {
        return -1;
    }

    fd_index = lang_alloc_host_fd();
    if (fd_index < 0) {
        return -1;
    }

    fd = &g_lang_host_fds[fd_index];
    lang_memset(fd, 0, (uint32_t)sizeof(*fd));
    fd->used = 1;
    fd->flags = flags;
    str_copy_limited(fd->path, path, (int)sizeof(fd->path));

    accmode = flags & LANG_HOST_O_ACCMODE;
    node = fs_resolve(path);
    if (node >= 0) {
        if (g_fs_nodes[node].is_dir) {
            lang_reset_host_fd(fd);
            return -1;
        }
        if (lang_host_load_file(fd, node) != 0) {
            lang_reset_host_fd(fd);
            return -1;
        }
    } else if ((flags & LANG_HOST_O_CREAT) == 0) {
        lang_reset_host_fd(fd);
        return -1;
    }

    if ((flags & LANG_HOST_O_TRUNC) && accmode != LANG_HOST_O_RDONLY) {
        fd->size = 0;
        fd->pos = 0;
        fd->dirty = 1;
    }
    if (flags & LANG_HOST_O_APPEND) {
        fd->pos = fd->size;
    }
    return fd_index;
}

static int host_read_fd(int fd_index, void *buf, int size) {
    struct lang_host_fd *fd;

    if (!buf || size < 0 || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used || ((fd->flags & LANG_HOST_O_ACCMODE) == LANG_HOST_O_WRONLY)) {
        return -1;
    }
    if (fd->pos >= fd->size) {
        return 0;
    }
    if (fd->pos + size > fd->size) {
        size = fd->size - fd->pos;
    }
    if (size <= 0) {
        return 0;
    }
    lang_memcpy(buf, fd->buf + fd->pos, (uint32_t)size);
    fd->pos += size;
    return size;
}

static int host_write_fd(int fd_index, const void *buf, int size) {
    struct lang_host_fd *fd;
    int accmode;

    if ((!buf && size > 0) || size < 0 || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    accmode = fd->flags & LANG_HOST_O_ACCMODE;
    if (accmode == LANG_HOST_O_RDONLY) {
        return -1;
    }
    if (fd->flags & LANG_HOST_O_APPEND) {
        fd->pos = fd->size;
    }
    if (size == 0) {
        return 0;
    }
    if (lang_host_reserve(fd, fd->pos + size) != 0) {
        return -1;
    }
    lang_memcpy(fd->buf + fd->pos, buf, (uint32_t)size);
    fd->pos += size;
    if (fd->pos > fd->size) {
        fd->size = fd->pos;
    }
    fd->dirty = 1;
    return size;
}

static int host_close_fd(int fd_index) {
    struct lang_host_fd *fd;

    if (fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    if (lang_host_flush_fd(fd) != 0) {
        return -1;
    }
    lang_reset_host_fd(fd);
    return 0;
}

static int host_seek_fd(int fd_index, int offset, int whence) {
    struct lang_host_fd *fd;
    int base;
    int next;

    if (fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }

    if (whence == LANG_HOST_SEEK_SET) {
        base = 0;
    } else if (whence == LANG_HOST_SEEK_CUR) {
        base = fd->pos;
    } else if (whence == LANG_HOST_SEEK_END) {
        base = fd->size;
    } else {
        return -1;
    }

    next = base + offset;
    if (next < 0) {
        return -1;
    }
    fd->pos = next;
    return fd->pos;
}

static int host_stat_path(const char *path, struct vibe_app_stat *stat_out) {
    int node;

    if (!path || !stat_out) {
        return -1;
    }
    node = fs_resolve(path);
    if (node < 0) {
        return -1;
    }
    stat_out->size = g_fs_nodes[node].size;
    stat_out->is_dir = g_fs_nodes[node].is_dir;
    return 0;
}

static int host_fstat_fd(int fd_index, struct vibe_app_stat *stat_out) {
    struct lang_host_fd *fd;

    if (!stat_out || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    stat_out->size = fd->size;
    stat_out->is_dir = 0;
    return 0;
}

static const char *host_getenv_value(const char *name) {
    static char cwd_buf[80];

    if (!name || name[0] == '\0') {
        return 0;
    }
    if (str_eq(name, "HOME")) {
        return "/home/user";
    }
    if (str_eq(name, "USER")) {
        return "user";
    }
    if (str_eq(name, "TMPDIR")) {
        return "/tmp";
    }
    if (str_eq(name, "PATH")) {
        return "/bin:/usr/bin:/compat/bin";
    }
    if (str_eq(name, "JAVA_HOME")) {
        return "/lang/vendor/jdk8u";
    }
    if (str_eq(name, "CLASSPATH")) {
        return ".";
    }
    if (str_eq(name, "PWD")) {
        if (host_getcwd(cwd_buf, (int)sizeof(cwd_buf)) == 0) {
            return cwd_buf;
        }
        return "/";
    }
    return 0;
}

static int host_getcwd(char *buf, int max_len) {
    int i = 0;
    char cwd[80];

    if (!buf || max_len <= 0) {
        return -1;
    }

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    while (cwd[i] != '\0' && i < (max_len - 1)) {
        buf[i] = cwd[i];
        ++i;
    }
    buf[i] = '\0';
    return 0;
}

static int host_remove_dir(const char *path) {
    int node;
    int rc;

    if (!path || path[0] == '\0') {
        return -1;
    }

    node = fs_resolve(path);
    if (node < 0) {
        return -1;
    }
    if (!g_fs_nodes[node].is_dir) {
        return -3;
    }

    rc = fs_remove(path);
    if (rc == -2) {
        return -2;
    }
    if (rc != 0) {
        return -1;
    }
    return 0;
}

static int host_keyboard_set_layout(const char *name) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    return sys_keyboard_set_layout(name);
}

static int host_keyboard_get_layout(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    return sys_keyboard_get_layout(buf, max_len);
}

static int host_keyboard_get_available_layouts(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    return sys_keyboard_get_available_layouts(buf, max_len);
}

static const struct vibe_app_host_api g_host_api = {
    VIBE_APP_ABI_VERSION,
    console_putc,
    console_write,
    sys_poll_key,
    sys_yield,
    host_read_file,
    host_write_file,
    host_create_dir,
    sys_write_debug,
    host_getcwd,
    host_remove_dir,
    host_keyboard_set_layout,
    host_keyboard_get_layout,
    host_keyboard_get_available_layouts,
    host_open_file,
    host_read_fd,
    host_write_fd,
    host_close_fd,
    host_seek_fd,
    host_stat_path,
    host_fstat_fd,
    host_getenv_value
};

static int lang_has_runtime_stub(const char *name) {
    static const char *prefixes[] = {
        "/bin/",
        "/usr/bin/",
        "/compat/bin/"
    };
    char path[64];
    int prefix_i;

    if (!name || name[0] == '\0') {
        return 0;
    }

    for (prefix_i = 0; prefix_i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); ++prefix_i) {
        int pos = 0;
        const char *prefix = prefixes[prefix_i];
        const char *cursor = name;

        while (prefix[pos] != '\0' && pos < (int)sizeof(path) - 1) {
            path[pos] = prefix[pos];
            ++pos;
        }
        while (*cursor != '\0' && pos < (int)sizeof(path) - 1) {
            path[pos++] = *cursor++;
        }
        path[pos] = '\0';

        if (fs_resolve(path) >= 0) {
            return 1;
        }
    }

    return 0;
}

static int lang_load_directory(struct vibe_appfs_directory *directory) {
    static uint8_t raw_directory[VIBE_APPFS_DIRECTORY_SECTORS * 512u];
    uint32_t checksum;

    if (!directory) {
        return -1;
    }

    if (sys_storage_read_sectors(VIBE_APPFS_DIRECTORY_LBA,
                                 raw_directory,
                                 VIBE_APPFS_DIRECTORY_SECTORS) != 0) {
        return -1;
    }

    lang_memcpy(directory, raw_directory, (uint32_t)sizeof(*directory));

    if (directory->magic != VIBE_APPFS_MAGIC ||
        directory->version != VIBE_APPFS_VERSION ||
        directory->entry_count > VIBE_APPFS_ENTRY_MAX) {
        return -1;
    }

    checksum = directory->checksum;
    directory->checksum = 0u;
    if (checksum != lang_checksum_bytes((const uint8_t *)directory,
                                        (uint32_t)sizeof(*directory))) {
        return -1;
    }
    directory->checksum = checksum;
    return 0;
}

static const struct vibe_appfs_entry *lang_find_entry(const struct vibe_appfs_directory *directory,
                                                      const char *name) {
    if (!directory || !name) {
        return 0;
    }

    for (uint32_t i = 0; i < VIBE_APPFS_ENTRY_MAX; ++i) {
        if (directory->entries[i].name[0] == '\0') {
            continue;
        }
        lang_debug_vga(12, "lang: cmp");
        if (str_eq(name, directory->entries[i].name)) {
            return &directory->entries[i];
        }
    }
    return 0;
}

static int lang_prepare_context(const struct vibe_appfs_entry *entry,
                                struct vibe_app_header **header_out,
                                struct vibe_app_context *ctx_out) {
    struct vibe_app_header *header;
    uint8_t *load_base = (uint8_t *)(uintptr_t)VIBE_APP_LOAD_ADDR;
    uintptr_t heap_base;
    uintptr_t heap_limit;

    if (!entry || !header_out || !ctx_out || entry->sector_count == 0u) {
        return -1;
    }

    lang_debug_vga(17, "lang: prep");
    if (entry->sector_count > VIBE_APPFS_APP_AREA_SECTORS) {
        return -1;
    }

    lang_debug_vga(18, "lang: read app");
    if (sys_storage_read_sectors(entry->lba_start, load_base, entry->sector_count) != 0) {
        return -1;
    }
    lang_debug_vga(19, "lang: app read ok");

    header = (struct vibe_app_header *)load_base;
    if (header->magic != VIBE_APP_MAGIC ||
        header->abi_version != VIBE_APP_ABI_VERSION ||
        header->header_size < sizeof(struct vibe_app_header)) {
        return -1;
    }
    lang_debug_vga(20, "lang: hdr ok");

    if (header->image_size == 0u ||
        header->image_size > entry->image_size ||
        header->memory_size < header->image_size ||
        header->memory_size > VIBE_APP_ARENA_SIZE ||
        header->entry_offset >= header->memory_size) {
        return -1;
    }

    if (header->name[0] != '\0' && !str_eq(header->name, entry->name)) {
        return -1;
    }

    lang_memset(load_base + header->image_size, 0, header->memory_size - header->image_size);

    heap_base = align_up_uintptr((uintptr_t)load_base + header->memory_size, 16u);
    heap_limit = (uintptr_t)VIBE_APP_STACK_TOP - VIBE_APP_STACK_SIZE;
    if (heap_base >= heap_limit) {
        return -1;
    }

    ctx_out->host = &g_host_api;
    ctx_out->heap_base = (void *)heap_base;
    ctx_out->heap_size = (uint32_t)(heap_limit - heap_base);
    if (ctx_out->heap_size < header->required_heap_size) {
        return -1;
    }

    *header_out = header;
    return 0;
}

static void lang_write_load_error(const char *name) {
    console_write("erro: falha ao carregar app externo");
    if (name && name[0] != '\0') {
        console_write(" ");
        console_write(name);
    }
    console_putc('\n');
}

static void lang_debug_vga(int row, const char *text) {
    (void)row;
    (void)text;
}

static void lang_write_missing_runtime(const char *name) {
    console_write("erro: runtime externo nao instalado: ");
    console_write(name);
    console_putc('\n');
}

static int lang_call_app(vibe_app_entry_t entry,
                         const struct vibe_app_context *ctx,
                         int argc,
                         char **argv) {
    return entry(ctx, argc, argv);
}

int lang_try_run(int argc, char **argv) {
    struct vibe_appfs_directory directory;
    const struct vibe_appfs_entry *entry;
    struct vibe_app_header *header;
    vibe_app_entry_t app_entry;

    if (argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    lang_reset_host_fds();

    lang_debug_vga(14, "lang: start");
    if (lang_load_directory(&directory) != 0) {
        if (lang_has_runtime_stub(argv[0])) {
            lang_write_load_error("catalog");
            return 0;
        }
        return -1;
    }
    lang_debug_vga(15, "lang: dir ok");
    lang_debug_vga(11, directory.entries[0].name);

    entry = lang_find_entry(&directory, argv[0]);
    if (!entry) {
        lang_debug_vga(13, "lang: no entry");
        if (lang_has_runtime_stub(argv[0])) {
            lang_write_missing_runtime(argv[0]);
            return 0;
        }
        return -1;
    }
    lang_debug_vga(16, "lang: entry ok");

    if (lang_prepare_context(entry, &header, &g_app_ctx) != 0) {
        lang_write_load_error(entry->name);
        return 0;
    }
    app_entry = (vibe_app_entry_t)(uintptr_t)(VIBE_APP_LOAD_ADDR + header->entry_offset);
    (void)lang_call_app(app_entry, &g_app_ctx, argc, argv);
    lang_reset_host_fds();
    lang_memcpy((void *)(uintptr_t)VIBE_APP_LOAD_ADDR,
                (const void *)(uintptr_t)VIBE_APP_LOAD_ADDR,
                0u);
    return 0;
}
