#include <lang/include/vibe_app.h>
#include <lang/include/vibe_appfs.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

#include <stdint.h>

static struct vibe_app_context g_app_ctx;
static struct vibe_appfs_directory g_cached_directory;
static int g_cached_directory_valid = 0;
static unsigned char *g_host_read_file_buf = 0;
static int g_host_read_file_buf_capacity = 0;
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
#define LANG_STORAGE_RETRY_COUNT 4
#define LANG_DIRECTORY_RETRY_COUNT 16
#define LANG_SECTOR_SIZE 512u

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

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return value & ~(align - 1u);
}

static void *lang_memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dst;
}

static int lang_memcmp(const void *a, const void *b, uint32_t size) {
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;

    for (uint32_t i = 0; i < size; ++i) {
        if (lhs[i] != rhs[i]) {
            return (int)lhs[i] - (int)rhs[i];
        }
    }
    return 0;
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

static int lang_storage_read_retry(uint32_t lba, void *dst, uint32_t sector_count) {
    for (int attempt = 0; attempt < LANG_STORAGE_RETRY_COUNT; ++attempt) {
        if (sys_storage_read_sectors(lba, dst, sector_count) == 0) {
            return 0;
        }
        sys_sleep();
        sys_yield();
    }
    return -1;
}

static int lang_storage_read_sector_verified(uint32_t lba, void *dst) {
    uint8_t verify[LANG_SECTOR_SIZE];

    for (int attempt = 0; attempt < LANG_STORAGE_RETRY_COUNT; ++attempt) {
        if (lang_storage_read_retry(lba, dst, 1u) != 0) {
            continue;
        }
        if (lang_storage_read_retry(lba, verify, 1u) != 0) {
            continue;
        }
        if (lang_memcmp(dst, verify, LANG_SECTOR_SIZE) == 0) {
            return 0;
        }
        sys_write_debug("lang: sector unstable read\n");
        sys_sleep();
        sys_yield();
    }

    return -1;
}

static int lang_storage_read_bytes(uint32_t lba_start, void *dst, uint32_t sector_count) {
    uint8_t *out = (uint8_t *)dst;

    for (uint32_t i = 0; i < sector_count; ++i) {
        if (lang_storage_read_sector_verified(lba_start + i,
                                              out + (i * LANG_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int host_read_file(const char *path, const char **data_out, int *size_out) {
    int node;
    unsigned char *new_buf;

    if (!path || !data_out || !size_out) {
        return -1;
    }

    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        return -1;
    }

    if (g_fs_nodes[node].storage_kind == FS_NODE_STORAGE_IMAGE) {
        if (g_fs_nodes[node].size < 0) {
            return -1;
        }
        if (g_fs_nodes[node].size > g_host_read_file_buf_capacity) {
            new_buf = (unsigned char *)realloc(g_host_read_file_buf, (size_t)g_fs_nodes[node].size);
            if (!new_buf) {
                return -1;
            }
            g_host_read_file_buf = new_buf;
            g_host_read_file_buf_capacity = g_fs_nodes[node].size;
        }
        if (g_fs_nodes[node].size > 0 &&
            fs_read_node_bytes(node, 0, g_host_read_file_buf, g_fs_nodes[node].size) != g_fs_nodes[node].size) {
            return -1;
        }
        *data_out = (const char *)g_host_read_file_buf;
        *size_out = g_fs_nodes[node].size;
        return 0;
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

static int host_sync_filesystem(void) {
    fs_flush();
    return 0;
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
    host_getenv_value,
    host_sync_filesystem
};

static int lang_load_address_valid(uint32_t load_address) {
    return load_address == VIBE_APP_LOAD_ADDR ||
           load_address == VIBE_APP_DESKTOP_LOAD_ADDR ||
           load_address == VIBE_APP_BOOT_LOAD_ADDR;
}

void lang_invalidate_directory_cache(void) {
    g_cached_directory_valid = 0;
    lang_memset(&g_cached_directory, 0, (uint32_t)sizeof(g_cached_directory));
}

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
    static uint8_t verify_directory[VIBE_APPFS_DIRECTORY_SECTORS * 512u];
    uint32_t checksum;
    uint32_t computed_checksum;

    if (!directory) {
        return -1;
    }

    if (g_cached_directory_valid) {
        lang_memcpy(directory, &g_cached_directory, (uint32_t)sizeof(*directory));
        return 0;
    }

    for (int attempt = 0; attempt < LANG_DIRECTORY_RETRY_COUNT; ++attempt) {
        if (lang_storage_read_bytes(VIBE_APPFS_DIRECTORY_LBA,
                                    raw_directory,
                                    VIBE_APPFS_DIRECTORY_SECTORS) != 0) {
            sys_write_debug("lang: directory read failed\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        if (lang_storage_read_bytes(VIBE_APPFS_DIRECTORY_LBA,
                                    verify_directory,
                                    VIBE_APPFS_DIRECTORY_SECTORS) != 0) {
            sys_write_debug("lang: directory verify read failed\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        if (lang_memcmp(raw_directory,
                        verify_directory,
                        (uint32_t)sizeof(raw_directory)) != 0) {
            sys_write_debug("lang: directory unstable read\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        lang_memcpy(directory, raw_directory, (uint32_t)sizeof(*directory));

        if (directory->magic != VIBE_APPFS_MAGIC ||
            directory->version != VIBE_APPFS_VERSION ||
            directory->entry_count > VIBE_APPFS_ENTRY_MAX) {
            if (raw_directory[510] == 0x55u && raw_directory[511] == 0xAAu) {
                sys_write_debug("lang: directory looks like boot sector\n");
            } else if (raw_directory[0] == 0u &&
                       raw_directory[1] == 0u &&
                       raw_directory[2] == 0u &&
                       raw_directory[3] == 0u) {
                sys_write_debug("lang: directory looks zeroed\n");
            }
            sys_write_debug("lang: directory header invalid\n");
            sys_sleep();
            sys_yield();
            continue;
        }

        checksum = directory->checksum;
        directory->checksum = 0u;
        computed_checksum = lang_checksum_bytes((const uint8_t *)directory,
                                                (uint32_t)sizeof(*directory));
        if (checksum != computed_checksum) {
            directory->checksum = checksum;
            sys_write_debug("lang: directory checksum mismatch\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        directory->checksum = checksum;
        lang_memcpy(&g_cached_directory, directory, (uint32_t)sizeof(*directory));
        g_cached_directory_valid = 1;
        return 0;
    }
    return -1;
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
    struct vibe_app_header header_copy;
    struct vibe_app_header *header;
    uint8_t first_sector[LANG_SECTOR_SIZE];
    uint8_t *load_base;
    uintptr_t load_address;
    uintptr_t heap_base;
    uintptr_t heap_limit;

    if (!entry || !header_out || !ctx_out || entry->sector_count == 0u) {
        return -1;
    }

    lang_debug_vga(17, "lang: prep");
    if (entry->sector_count > VIBE_APPFS_APP_AREA_SECTORS) {
        sys_write_debug("lang: entry exceeds app area\n");
        return -1;
    }
    if (entry->sector_count > (VIBE_APP_ARENA_SIZE / LANG_SECTOR_SIZE)) {
        sys_write_debug("lang: entry exceeds arena\n");
        return -1;
    }

    if (lang_storage_read_retry(entry->lba_start, first_sector, 1u) != 0) {
        sys_write_debug("lang: header read failed\n");
        return -1;
    }

    lang_memcpy(&header_copy, first_sector, (uint32_t)sizeof(header_copy));
    if (header_copy.magic != VIBE_APP_MAGIC ||
        header_copy.abi_version != VIBE_APP_ABI_VERSION ||
        header_copy.header_size < sizeof(struct vibe_app_header)) {
        sys_write_debug("lang: header invalid before read\n");
        return -1;
    }

    load_address = (uintptr_t)header_copy.load_address;
    if (!lang_load_address_valid((uint32_t)load_address)) {
        sys_write_debug("lang: load address invalid\n");
        return -1;
    }
    load_base = (uint8_t *)load_address;

    lang_debug_vga(18, "lang: read app");
    if (lang_storage_read_bytes(entry->lba_start, load_base, entry->sector_count) != 0) {
        return -1;
    }
    lang_debug_vga(19, "lang: app read ok");

    header = (struct vibe_app_header *)load_base;
    if (header->magic != VIBE_APP_MAGIC ||
        header->abi_version != VIBE_APP_ABI_VERSION ||
        header->header_size < sizeof(struct vibe_app_header)) {
        sys_write_debug("lang: header invalid after read\n");
        return -1;
    }
    lang_debug_vga(20, "lang: hdr ok");

    if (header->load_address != (uint32_t)load_address) {
        sys_write_debug("lang: header load mismatch\n");
        return -1;
    }

    if (header->image_size == 0u ||
        header->image_size > entry->image_size ||
        header->memory_size < header->image_size ||
        header->memory_size > VIBE_APP_ARENA_SIZE ||
        header->entry_offset >= header->memory_size ||
        header->entry_offset >= header->image_size) {
        sys_write_debug("lang: header sizing invalid\n");
        return -1;
    }

    if (header->name[0] != '\0' && !str_eq(header->name, entry->name)) {
        sys_write_debug("lang: header name mismatch\n");
        return -1;
    }

    lang_memset(load_base + header->image_size, 0, header->memory_size - header->image_size);

    heap_base = align_up_uintptr(load_address + header->memory_size, 16u);
    heap_limit = load_address + VIBE_APP_ARENA_SIZE - VIBE_APP_STACK_SIZE;
    if (heap_base >= heap_limit) {
        sys_write_debug("lang: heap base beyond limit\n");
        return -1;
    }

    ctx_out->host = &g_host_api;
    ctx_out->heap_base = (void *)heap_base;
    ctx_out->heap_size = (uint32_t)(heap_limit - heap_base);
    if (ctx_out->heap_size < header->required_heap_size) {
        sys_write_debug("lang: required heap exceeds available\n");
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

static uintptr_t lang_app_stack_top(uint32_t load_address) {
    return align_down_uintptr((uintptr_t)load_address + VIBE_APP_ARENA_SIZE, 16u);
}

__attribute__((noinline, optimize("O0")))
static int lang_call_app(vibe_app_entry_t entry,
                         const struct vibe_app_context *ctx,
                         int argc,
                         char **argv,
                         uintptr_t stack_top) {
    uintptr_t saved_esp;
    int rc;

    sys_write_debug("lang: calling app entry\n");
    __asm__ volatile(
        "mov %%esp, %[saved_esp]\n\t"
        "mov %[stack_top], %%esp\n\t"
        "push %[argv]\n\t"
        "push %[argc]\n\t"
        "push %[ctx]\n\t"
        "call *%[entry]\n\t"
        "add $12, %%esp\n\t"
        "mov %[saved_esp], %%esp\n\t"
        : [saved_esp] "=&r"(saved_esp),
          "=a"(rc)
        : [stack_top] "r"(stack_top),
          [entry] "r"(entry),
          [ctx] "g"(ctx),
          [argc] "g"(argc),
          [argv] "g"(argv)
        : "ecx", "edx", "memory", "cc");
    return rc;
}

int lang_try_run(int argc, char **argv) {
    struct vibe_appfs_directory directory;
    const struct vibe_appfs_entry *entry;
    struct vibe_app_header *header;
    vibe_app_entry_t app_entry;
    uintptr_t entry_addr;

    if (argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    if (sys_getpid() == 0) {
        sys_write_debug("lang: pid zero before reset\n");
    } else {
        sys_write_debug("lang: pid ok before reset\n");
    }
    lang_reset_host_fds();
    if (sys_getpid() == 0) {
        sys_write_debug("lang: pid zero after reset\n");
    } else {
        sys_write_debug("lang: pid ok after reset\n");
    }
    sys_write_debug("lang: try_run begin\n");

    lang_debug_vga(14, "lang: start");
    if (lang_load_directory(&directory) != 0) {
        sys_write_debug("lang: directory load failed\n");
        if (lang_has_runtime_stub(argv[0])) {
            lang_write_load_error("catalog");
            return 0;
        }
        return -1;
    }
    sys_write_debug("lang: directory loaded\n");
    lang_debug_vga(15, "lang: dir ok");
    lang_debug_vga(11, directory.entries[0].name);

    entry = lang_find_entry(&directory, argv[0]);
    if (!entry) {
        sys_write_debug("lang: entry missing\n");
        lang_debug_vga(13, "lang: no entry");
        if (lang_has_runtime_stub(argv[0])) {
            lang_write_missing_runtime(argv[0]);
            return 0;
        }
        return -1;
    }
    sys_write_debug("lang: entry found\n");
    lang_debug_vga(16, "lang: entry ok");

    if (lang_prepare_context(entry, &header, &g_app_ctx) != 0) {
        sys_write_debug("lang: prepare failed\n");
        lang_write_load_error(entry->name);
        return 0;
    }
    sys_write_debug("lang: prepare ok\n");
    entry_addr = (uintptr_t)header->load_address + header->entry_offset;
    app_entry = (vibe_app_entry_t)entry_addr;
    sys_write_debug("lang: app entry resolved\n");
    (void)lang_call_app(app_entry,
                        &g_app_ctx,
                        argc,
                        argv,
                        lang_app_stack_top(header->load_address));
    sys_write_debug("lang: app returned\n");
    lang_reset_host_fds();
    lang_memcpy((void *)(uintptr_t)header->load_address,
                (const void *)(uintptr_t)header->load_address,
                0u);
    return 0;
}
