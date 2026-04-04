#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <kernel/drivers/storage/ata.h>
#include <userland/modules/include/utils.h> // for str_* functions
#include "app_catalog.h"
#include <string.h>

#define FS_PERSIST_MAGIC 0x56465331u
#define FS_PERSIST_VERSION 4u
#define FS_SECTOR_SIZE 512u
#define FS_STORAGE_RETRY_COUNT 4u
#define FS_STORAGE_DATA_START_LBA (KERNEL_PERSIST_START_LBA + KERNEL_PERSIST_SECTOR_COUNT)

struct fs_persist_header {
    uint32_t magic;
    uint32_t version;
    uint32_t used_bytes;
    int32_t root;
    int32_t cwd;
    uint32_t checksum;
};

struct fs_persist_node {
    uint8_t used;
    uint8_t is_dir;
    uint8_t storage_kind;
    uint8_t reserved;
    int32_t parent;
    int32_t first_child;
    int32_t next_sibling;
    int32_t size;
    uint32_t image_lba;
    uint32_t image_sector_count;
    uint32_t data_offset;
    uint32_t data_size;
    char name[FS_NAME_MAX + 1];
};

struct fs_node g_fs_nodes[FS_MAX_NODES];
int g_fs_root = -1;
int g_fs_cwd = -1;
static uint8_t g_fs_persist_buffer[KERNEL_PERSIST_MAX_BYTES];
static int g_fs_sync_suspended = 0;
static uint32_t g_fs_sync_hold_depth = 0u;
static uint32_t g_fs_total_sectors_cache = 0u;
static int g_fs_dirty = 0;
static uint32_t g_fs_dirty_generation = 0u;
static uint32_t g_fs_last_sync_tick = 0u;
static int g_fs_writeback_active = 0;
static uint32_t g_fs_writeback_generation = 0u;
static uint32_t g_fs_writeback_bytes = 0u;
static uint32_t g_fs_writeback_sector_index = 0u;
static uint32_t g_fs_writeback_progress_tick = 0xffffffffu;
static int g_fs_allow_immediate_writeback = 0;
static int g_fs_doom_assets_registered = 0;
static int g_fs_texture_assets_registered = 0;
static int g_fs_assets_registered = 0;
static int g_fs_doom_assets_registering = 0;
static int g_fs_texture_assets_registering = 0;
static int g_fs_assets_registering = 0;

#define FS_SYNC_PERIOD_TICKS 1000u
#define FS_WRITEBACK_SECTORS_PER_TICK 4u
#define FS_IMAGE_READ_CHUNK_SECTORS 8u

static void fs_ensure_doom_wad_registered(void);
static void fs_ensure_craft_textures_registered(void);
static void fs_ensure_assets_registered(void);
static uint32_t fs_storage_total_sectors(void);
static int fs_validate_loaded_tree(void);

#define DOOM_WAD_IMAGE_LBA 131728u
#define DOOM_WAD_IMAGE_SECTORS 24235u
#define DOOM_WAD_IMAGE_BYTES 12408292
/* Keep these LBAs aligned with the asset layout in the Makefile. */
#define CRAFT_TEXTURE_IMAGE_LBA 156304u
#define CRAFT_TEXTURE_IMAGE_SECTORS 70u
#define CRAFT_TEXTURE_IMAGE_BYTES 35751
#define CRAFT_FONT_IMAGE_LBA 156432u
#define CRAFT_FONT_IMAGE_SECTORS 84u
#define CRAFT_FONT_IMAGE_BYTES 42838
#define CRAFT_SKY_IMAGE_LBA 156560u
#define CRAFT_SKY_IMAGE_SECTORS 154u
#define CRAFT_SKY_IMAGE_BYTES 78482
#define CRAFT_SIGN_IMAGE_LBA 156816u
#define CRAFT_SIGN_IMAGE_SECTORS 5u
#define CRAFT_SIGN_IMAGE_BYTES 2401
#define WALLPAPER_IMAGE_LBA 156944u
#define WALLPAPER_IMAGE_SECTORS 999u
#define WALLPAPER_IMAGE_BYTES 511155
#define VIBE_BOOT_WAV_IMAGE_LBA 157968u
#define VIBE_BOOT_WAV_IMAGE_SECTORS 517u
#define VIBE_BOOT_WAV_IMAGE_BYTES 264644
#define VIBE_DESKTOP_WAV_IMAGE_LBA 158992u
#define VIBE_DESKTOP_WAV_IMAGE_SECTORS 862u
#define VIBE_DESKTOP_WAV_IMAGE_BYTES 441044
#define BOOTLOADER_BG_IMAGE_LBA 160016u
#define BOOTLOADER_BG_IMAGE_SECTORS 6312u
#define BOOTLOADER_BG_IMAGE_BYTES 3231497

static void fs_reset_node(int idx) {
    int i;

    g_fs_nodes[idx].used = 0;
    g_fs_nodes[idx].is_dir = 0;
    g_fs_nodes[idx].parent = -1;
    g_fs_nodes[idx].first_child = -1;
    g_fs_nodes[idx].next_sibling = -1;
    g_fs_nodes[idx].storage_kind = FS_NODE_STORAGE_INLINE;
    g_fs_nodes[idx].size = 0;
    g_fs_nodes[idx].image_lba = 0u;
    g_fs_nodes[idx].image_sector_count = 0u;

    for (i = 0; i <= FS_NAME_MAX; ++i) {
        g_fs_nodes[idx].name[i] = '\0';
    }

    for (i = 0; i <= FS_FILE_MAX; ++i) {
        g_fs_nodes[idx].data[i] = '\0';
    }
}

static uint32_t fs_checksum_bytes(const uint8_t *data, int size) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t fs_persist_inline_base(void) {
    return (uint32_t)(sizeof(struct fs_persist_header) +
                      (sizeof(struct fs_persist_node) * FS_MAX_NODES));
}

static uint32_t fs_persist_sector_count_for_bytes(uint32_t byte_count) {
    if (byte_count == 0u) {
        return 0u;
    }
    return (byte_count + (FS_SECTOR_SIZE - 1u)) / FS_SECTOR_SIZE;
}

static int fs_serialize_persistent_image(uint8_t *buffer,
                                         uint32_t capacity,
                                         uint32_t *used_bytes_out) {
    struct fs_persist_header *header;
    struct fs_persist_node *persist_nodes;
    uint32_t data_cursor;

    if (buffer == 0 || used_bytes_out == 0) {
        return -1;
    }

    data_cursor = fs_persist_inline_base();
    if (data_cursor > capacity) {
        return -1;
    }

    memset(buffer, 0, capacity);
    header = (struct fs_persist_header *)buffer;
    persist_nodes = (struct fs_persist_node *)(buffer + sizeof(*header));

    for (int i = 0; i < FS_MAX_NODES; ++i) {
        struct fs_persist_node *persist = &persist_nodes[i];
        const struct fs_node *node = &g_fs_nodes[i];

        persist->used = (uint8_t)(node->used != 0);
        if (!node->used) {
            continue;
        }

        persist->is_dir = (uint8_t)(node->is_dir != 0);
        persist->storage_kind = (uint8_t)node->storage_kind;
        persist->parent = (int32_t)node->parent;
        persist->first_child = (int32_t)node->first_child;
        persist->next_sibling = (int32_t)node->next_sibling;
        persist->size = (int32_t)node->size;
        persist->image_lba = node->image_lba;
        persist->image_sector_count = node->image_sector_count;
        str_copy_limited(persist->name, node->name, FS_NAME_MAX + 1);

        if (node->is_dir || node->storage_kind != FS_NODE_STORAGE_INLINE || node->size <= 0) {
            continue;
        }

        if (node->size > FS_FILE_MAX || (uint32_t)node->size > (capacity - data_cursor)) {
            return -1;
        }

        persist->data_offset = data_cursor;
        persist->data_size = (uint32_t)node->size;
        memcpy(buffer + data_cursor, node->data, persist->data_size);
        data_cursor += persist->data_size;
    }

    header->magic = FS_PERSIST_MAGIC;
    header->version = FS_PERSIST_VERSION;
    header->used_bytes = data_cursor;
    header->root = g_fs_root;
    header->cwd = g_fs_cwd;
    header->checksum = 0u;
    header->checksum = fs_checksum_bytes(buffer, (int)data_cursor);
    *used_bytes_out = data_cursor;
    return 0;
}

static int fs_deserialize_persistent_image(uint8_t *buffer, uint32_t capacity) {
    struct fs_persist_header *header;
    struct fs_persist_node *persist_nodes;
    uint32_t checksum;

    if (buffer == 0 || capacity < fs_persist_inline_base()) {
        return -1;
    }

    header = (struct fs_persist_header *)buffer;
    if (header->magic != FS_PERSIST_MAGIC || header->version != FS_PERSIST_VERSION) {
        return -1;
    }
    if (header->used_bytes < fs_persist_inline_base() || header->used_bytes > capacity) {
        return -1;
    }

    checksum = header->checksum;
    header->checksum = 0u;
    if (checksum != fs_checksum_bytes(buffer, (int)header->used_bytes)) {
        header->checksum = checksum;
        return -1;
    }
    header->checksum = checksum;
    persist_nodes = (struct fs_persist_node *)(buffer + sizeof(*header));

    for (int i = 0; i < FS_MAX_NODES; ++i) {
        const struct fs_persist_node *persist = &persist_nodes[i];
        struct fs_node *node = &g_fs_nodes[i];

        fs_reset_node(i);
        if (!persist->used) {
            continue;
        }

        node->used = 1;
        node->is_dir = persist->is_dir != 0;
        node->parent = persist->parent;
        node->first_child = persist->first_child;
        node->next_sibling = persist->next_sibling;
        node->storage_kind = persist->storage_kind;
        node->size = persist->size;
        node->image_lba = persist->image_lba;
        node->image_sector_count = persist->image_sector_count;
        str_copy_limited(node->name, persist->name, FS_NAME_MAX + 1);

        if (node->is_dir) {
            node->storage_kind = FS_NODE_STORAGE_INLINE;
            node->size = 0;
            node->image_lba = 0u;
            node->image_sector_count = 0u;
            continue;
        }

        if (node->storage_kind == FS_NODE_STORAGE_INLINE) {
            if (node->size < 0 || persist->data_size > (uint32_t)FS_FILE_MAX ||
                persist->data_size != (uint32_t)node->size ||
                persist->data_offset < fs_persist_inline_base() ||
                persist->data_offset > header->used_bytes ||
                persist->data_size > (header->used_bytes - persist->data_offset)) {
                return -1;
            }
            if (persist->data_size > 0u) {
                memcpy(node->data, buffer + persist->data_offset, persist->data_size);
            }
            node->data[persist->data_size] = '\0';
            continue;
        }

        if (node->storage_kind != FS_NODE_STORAGE_IMAGE || node->size < 0) {
            return -1;
        }
        node->data[0] = '\0';
    }

    g_fs_root = header->root;
    g_fs_cwd = header->cwd;
    return fs_validate_loaded_tree() ? 0 : -1;
}

static int fs_storage_read_sector_verified(uint32_t lba, uint8_t *dst) {
    uint8_t verify[FS_SECTOR_SIZE];

    if (!dst) {
        return -1;
    }

    for (uint32_t attempt = 0u; attempt < FS_STORAGE_RETRY_COUNT; ++attempt) {
        if (sys_storage_read_sectors(lba, dst, 1u) != 0) {
            continue;
        }
        if (sys_storage_read_sectors(lba, verify, 1u) != 0) {
            continue;
        }
        if (memcmp(dst, verify, FS_SECTOR_SIZE) == 0) {
            return 0;
        }
        sys_sleep();
        sys_yield();
    }

    return -1;
}

static int fs_read_image_bytes(uint32_t lba, uint32_t offset, void *dst, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;

    while (size > 0u) {
        uint32_t sector_index = offset / FS_SECTOR_SIZE;
        uint32_t sector_offset = offset % FS_SECTOR_SIZE;
        uint32_t sectors = ((sector_offset + size) + (FS_SECTOR_SIZE - 1u)) / FS_SECTOR_SIZE;

        while (sectors > 0u) {
            uint8_t chunk_buffer[FS_SECTOR_SIZE * FS_IMAGE_READ_CHUNK_SECTORS];
            uint32_t read_sectors = sectors;
            uint32_t available;
            uint32_t chunk;

            if (read_sectors > FS_IMAGE_READ_CHUNK_SECTORS) {
                read_sectors = FS_IMAGE_READ_CHUNK_SECTORS;
            }
            if (sys_storage_read_sectors(lba + sector_index, chunk_buffer, read_sectors) != 0) {
                uint32_t fallback_chunk = FS_SECTOR_SIZE - sector_offset;

                if (fallback_chunk > size) {
                    fallback_chunk = size;
                }
                if (fs_storage_read_sector_verified(lba + sector_index, chunk_buffer) != 0) {
                    return -1;
                }
                memcpy(out, chunk_buffer + sector_offset, fallback_chunk);
                out += fallback_chunk;
                offset += fallback_chunk;
                size -= fallback_chunk;
                sector_index += 1u;
                sector_offset = 0u;
                sectors = ((sector_offset + size) + (FS_SECTOR_SIZE - 1u)) / FS_SECTOR_SIZE;
                continue;
            }

            available = (read_sectors * FS_SECTOR_SIZE) - sector_offset;
            chunk = available;
            if (chunk > size) {
                chunk = size;
            }
            memcpy(out, chunk_buffer + sector_offset, chunk);
            out += chunk;
            offset += chunk;
            size -= chunk;
            sector_index += read_sectors;
            sector_offset = 0u;
            sectors = ((sector_offset + size) + (FS_SECTOR_SIZE - 1u)) / FS_SECTOR_SIZE;
        }
    }
    return 0;
}

static uint32_t fs_storage_total_sectors(void) {
    if (g_fs_total_sectors_cache == 0u) {
        g_fs_total_sectors_cache = sys_storage_total_sectors();
    }
    return g_fs_total_sectors_cache;
}

static uint32_t fs_sector_count_for_size(int size) {
    if (size <= 0) {
        return 0u;
    }
    return ((uint32_t)size + (FS_SECTOR_SIZE - 1u)) / FS_SECTOR_SIZE;
}

static void fs_clear_node_storage(int idx) {
    g_fs_nodes[idx].storage_kind = FS_NODE_STORAGE_INLINE;
    g_fs_nodes[idx].image_lba = 0u;
    g_fs_nodes[idx].image_sector_count = 0u;
    g_fs_nodes[idx].size = 0;
    g_fs_nodes[idx].data[0] = '\0';
}

struct fs_extent_info {
    uint32_t lba;
    uint32_t sector_count;
};

static int fs_compare_extents(const struct fs_extent_info *a, const struct fs_extent_info *b) {
    if (a->lba < b->lba) {
        return -1;
    }
    if (a->lba > b->lba) {
        return 1;
    }
    return 0;
}

static int fs_collect_sector_extents(struct fs_extent_info *extents, int max_extents, int exclude_node) {
    int count = 0;
    int j;
    struct fs_extent_info key;

    for (int i = 0; i < FS_MAX_NODES; ++i) {
        if (i == exclude_node ||
            !g_fs_nodes[i].used ||
            g_fs_nodes[i].is_dir ||
            g_fs_nodes[i].storage_kind != FS_NODE_STORAGE_IMAGE ||
            g_fs_nodes[i].image_sector_count == 0u) {
            continue;
        }
        if (count >= max_extents) {
            break;
        }
        extents[count].lba = g_fs_nodes[i].image_lba;
        extents[count].sector_count = g_fs_nodes[i].image_sector_count;
        ++count;
    }

    for (int i = 1; i < count; ++i) {
        key = extents[i];
        j = i - 1;
        while (j >= 0 && fs_compare_extents(&extents[j], &key) > 0) {
            extents[j + 1] = extents[j];
            --j;
        }
        extents[j + 1] = key;
    }

    return count;
}

static int fs_path_matches_root_prefix(const char *path, const char *prefix) {
    int i = 0;
    const char *lhs = path;
    const char *rhs = prefix;

    if (!lhs || !rhs) {
        return 0;
    }

    if (lhs[0] == '/') {
        ++lhs;
    }
    if (rhs[0] == '/') {
        ++rhs;
    }

    while (rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        ++i;
    }

    return lhs[i] == '\0' || lhs[i] == '/';
}

static void fs_debug_asset_path(const char *prefix, const char *path) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, path ? path : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static const char *fs_path_basename(const char *path) {
    const char *basename = path;

    if (path == 0) {
        return 0;
    }

    while (*path != '\0') {
        if (*path == '/') {
            basename = path + 1;
        }
        ++path;
    }

    return basename;
}

static void fs_maybe_register_boot_assets_for_path(const char *path) {
    if (fs_path_matches_root_prefix(path, "/DOOM") &&
        !g_fs_doom_assets_registered &&
        !g_fs_doom_assets_registering) {
        fs_ensure_doom_wad_registered();
    }

    if (fs_path_matches_root_prefix(path, "/textures") &&
        !g_fs_texture_assets_registered &&
        !g_fs_texture_assets_registering) {
        fs_ensure_craft_textures_registered();
    }

    if ((fs_path_matches_root_prefix(path, "/wallpaper.png") ||
         fs_path_matches_root_prefix(path, "/assets")) &&
        !g_fs_assets_registered &&
        !g_fs_assets_registering) {
        fs_ensure_assets_registered();
    }
}

static uint32_t fs_find_free_extent(uint32_t sector_count, int exclude_node) {
    struct fs_extent_info extents[FS_MAX_NODES];
    uint32_t cursor = FS_STORAGE_DATA_START_LBA;
    uint32_t total_sectors = fs_storage_total_sectors();
    int extent_count;

    if (sector_count == 0u || total_sectors <= cursor) {
        return 0u;
    }

    extent_count = fs_collect_sector_extents(extents, FS_MAX_NODES, exclude_node);
    for (int i = 0; i < extent_count; ++i) {
        uint32_t extent_end = extents[i].lba + extents[i].sector_count;

        if (cursor + sector_count <= extents[i].lba) {
            return cursor;
        }
        if (cursor < extent_end) {
            cursor = extent_end;
        }
    }

    if (cursor + sector_count <= total_sectors) {
        return cursor;
    }
    return 0u;
}

static int fs_write_sector_bytes(uint32_t start_lba, const uint8_t *data, int size) {
    uint8_t sector[FS_SECTOR_SIZE];
    uint32_t sector_count = fs_sector_count_for_size(size);

    if (sector_count == 0u) {
        return 0;
    }

    for (uint32_t i = 0; i < sector_count; ++i) {
        uint32_t offset = i * FS_SECTOR_SIZE;
        uint32_t chunk = FS_SECTOR_SIZE;

        if ((int)offset + (int)chunk > size) {
            chunk = (uint32_t)(size - (int)offset);
        }
        memset(sector, 0, sizeof(sector));
        memcpy(sector, data + offset, chunk);
        if (sys_storage_write_sectors(start_lba + i, sector, 1u) != 0) {
            sys_write_debug("fs: sector write fail\n");
            return -1;
        }
    }

    return 0;
}

static void fs_register_known_asset(const char *path,
                                    uint32_t lba,
                                    uint32_t sector_count,
                                    int size) {
    if (fs_register_image_file(path, lba, sector_count, size) == 0) {
        fs_debug_asset_path("fs: asset file ", path);
    }
}

static int fs_validate_loaded_tree(void) {
    int i;

    if (g_fs_root < 0 || g_fs_root >= FS_MAX_NODES) {
        return 0;
    }
    if (!g_fs_nodes[g_fs_root].used || !g_fs_nodes[g_fs_root].is_dir) {
        return 0;
    }
    if (g_fs_nodes[g_fs_root].parent != g_fs_root) {
        return 0;
    }

    for (i = 0; i < FS_MAX_NODES; ++i) {
        int child;
        int guard = 0;

        if (!g_fs_nodes[i].used) {
            continue;
        }

        if (i != g_fs_root) {
            int p = g_fs_nodes[i].parent;
            if (p < 0 || p >= FS_MAX_NODES || !g_fs_nodes[p].used || !g_fs_nodes[p].is_dir) {
                return 0;
            }
        }

        if (g_fs_nodes[i].is_dir) {
            if (g_fs_nodes[i].storage_kind != FS_NODE_STORAGE_INLINE) {
                return 0;
            }
        } else {
            if (g_fs_nodes[i].storage_kind != FS_NODE_STORAGE_INLINE &&
                g_fs_nodes[i].storage_kind != FS_NODE_STORAGE_IMAGE) {
                return 0;
            }
            if (g_fs_nodes[i].storage_kind == FS_NODE_STORAGE_IMAGE) {
                if (g_fs_nodes[i].size < 0 || g_fs_nodes[i].image_sector_count == 0u) {
                    return 0;
                }
            }
        }

        child = g_fs_nodes[i].first_child;
        while (child != -1) {
            if (++guard > FS_MAX_NODES) {
                return 0;
            }
            if (child < 0 || child >= FS_MAX_NODES || !g_fs_nodes[child].used) {
                return 0;
            }
            if (g_fs_nodes[child].parent != i) {
                return 0;
            }
            child = g_fs_nodes[child].next_sibling;
        }
    }

    if (g_fs_cwd < 0 || g_fs_cwd >= FS_MAX_NODES || !g_fs_nodes[g_fs_cwd].used) {
        g_fs_cwd = g_fs_root;
    }
    return 1;
}

static int fs_load_persistent_image(void) {
    extern void kernel_debug_puts(const char *);

    kernel_debug_puts("fs: sys_storage_load begin\n");
    if (sys_storage_load(g_fs_persist_buffer, (uint32_t)sizeof(g_fs_persist_buffer)) != 0) {
        kernel_debug_puts("fs: sys_storage_load failed\n");
        return -1;
    }
    kernel_debug_puts("fs: sys_storage_load returned\n");
    return fs_deserialize_persistent_image(g_fs_persist_buffer,
                                           (uint32_t)sizeof(g_fs_persist_buffer));
}

static int fs_prepare_writeback(void) {
    extern void kernel_debug_puts(const char *);

    if (g_fs_sync_suspended || g_fs_sync_hold_depth != 0u) {
        return 0;
    }
    if (!g_fs_dirty || g_fs_writeback_active) {
        return 0;
    }
    if (fs_serialize_persistent_image(g_fs_persist_buffer,
                                      (uint32_t)sizeof(g_fs_persist_buffer),
                                      &g_fs_writeback_bytes) != 0) {
        kernel_debug_puts("fs: persist image too large\n");
        return -1;
    }
    g_fs_writeback_active = 1;
    g_fs_writeback_generation = g_fs_dirty_generation;
    g_fs_writeback_sector_index = 0u;
    g_fs_writeback_progress_tick = 0xffffffffu;
    kernel_debug_puts("fs: writeback start\n");
    return 0;
}

static int fs_writeback_step(uint32_t sector_budget) {
    uint32_t total_sectors;
    extern void kernel_debug_puts(const char *);

    if (!g_fs_writeback_active) {
        return 0;
    }

    total_sectors = fs_persist_sector_count_for_bytes(g_fs_writeback_bytes);
    if (sector_budget == 0u) {
        sector_budget = total_sectors;
    }

    while (g_fs_writeback_sector_index < total_sectors && sector_budget > 0u) {
        if (sys_storage_write_sectors(KERNEL_PERSIST_START_LBA + g_fs_writeback_sector_index,
                                      g_fs_persist_buffer + (g_fs_writeback_sector_index * FS_SECTOR_SIZE),
                                      1u) != 0) {
            kernel_debug_puts("fs: writeback failed\n");
            g_fs_writeback_active = 0;
            g_fs_writeback_progress_tick = 0xffffffffu;
            return -1;
        }
        g_fs_writeback_sector_index += 1u;
        sector_budget -= 1u;
    }

    if (g_fs_writeback_sector_index < total_sectors) {
        return 0;
    }

    g_fs_writeback_active = 0;
    g_fs_writeback_sector_index = 0u;
    g_fs_writeback_progress_tick = 0xffffffffu;
    if (g_fs_writeback_generation == g_fs_dirty_generation) {
        g_fs_dirty = 0;
        g_fs_last_sync_tick = sys_ticks();
    } else {
        g_fs_last_sync_tick = 0u;
    }
    kernel_debug_puts("fs: writeback done\n");
    return 1;
}

static int fs_sync_now(void) {
    for (;;) {
        int rc;

        if (!g_fs_dirty && !g_fs_writeback_active) {
            return 0;
        }
        if (!g_fs_writeback_active && fs_prepare_writeback() != 0) {
            return -1;
        }
        rc = fs_writeback_step(0u);
        if (rc < 0) {
            return -1;
        }
    }
}

static void fs_mark_dirty(void) {
    g_fs_dirty = 1;
    g_fs_dirty_generation += 1u;
    g_fs_last_sync_tick = 0u;
    if (g_fs_allow_immediate_writeback &&
        !g_fs_sync_suspended &&
        g_fs_sync_hold_depth == 0u &&
        !g_fs_writeback_active) {
        (void)fs_prepare_writeback();
    }
}

void fs_flush(void) {
    if (!g_fs_dirty && !g_fs_writeback_active) {
        return;
    }
    (void)fs_sync_now();
}

void fs_tick(void) {
    uint32_t now;

    if (g_fs_sync_suspended || g_fs_sync_hold_depth != 0u) {
        return;
    }
    now = sys_ticks();
    if (g_fs_writeback_active) {
        if (g_fs_writeback_progress_tick == now) {
            return;
        }
        g_fs_writeback_progress_tick = now;
        (void)fs_writeback_step(FS_WRITEBACK_SECTORS_PER_TICK);
        return;
    }
    if (!g_fs_dirty) {
        return;
    }
    if ((uint32_t)(now - g_fs_last_sync_tick) < FS_SYNC_PERIOD_TICKS) {
        return;
    }
    if (fs_prepare_writeback() != 0) {
        return;
    }
    g_fs_writeback_progress_tick = now;
    (void)fs_writeback_step(FS_WRITEBACK_SECTORS_PER_TICK);
}

static int fs_alloc_node(void) {
    int i;
    for (i = 0; i < FS_MAX_NODES; ++i) {
        if (!g_fs_nodes[i].used) {
            return i;
        }
    }
    return -1;
}

static int fs_find_child(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;
    int guard = 0;
    int ci_match = -1;

    while (child != -1) {
        if (++guard > FS_MAX_NODES) {
            return -1;
        }
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        if (ci_match < 0 &&
            g_fs_nodes[child].used &&
            str_eq_ci(g_fs_nodes[child].name, name)) {
            ci_match = child;
        }
        child = g_fs_nodes[child].next_sibling;
    }

    return ci_match;
}

static void fs_link_child(int parent, int child) {
    g_fs_nodes[child].next_sibling = g_fs_nodes[parent].first_child;
    g_fs_nodes[parent].first_child = child;
    g_fs_nodes[child].parent = parent;
}

static void fs_unlink_child(int parent, int child) {
    int cur = g_fs_nodes[parent].first_child;
    int prev = -1;
    int guard = 0;

    while (cur != -1) {
        if (++guard > FS_MAX_NODES) {
            return;
        }
        if (cur == child) {
            if (prev == -1) {
                g_fs_nodes[parent].first_child = g_fs_nodes[cur].next_sibling;
            } else {
                g_fs_nodes[prev].next_sibling = g_fs_nodes[cur].next_sibling;
            }
            g_fs_nodes[cur].next_sibling = -1;
            return;
        }
        prev = cur;
        cur = g_fs_nodes[cur].next_sibling;
    }
}

static int fs_has_children(int idx) {
    return g_fs_nodes[idx].first_child != -1;
}

static int fs_name_is_valid(const char *name) {
    int len = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (str_eq(name, ".") || str_eq(name, "..")) {
        return 0;
    }

    while (name[len] != '\0') {
        if (name[len] == '/') {
            return 0;
        }
        ++len;
        if (len > FS_NAME_MAX) {
            return 0;
        }
    }
    return 1;
}

static int fs_new_node(const char *name, int is_dir, int parent) {
    int idx = fs_alloc_node();
    if (idx < 0) {
        return -1;
    }

    g_fs_nodes[idx].used = 1;
    g_fs_nodes[idx].is_dir = is_dir;
    g_fs_nodes[idx].parent = parent;
    g_fs_nodes[idx].first_child = -1;
    g_fs_nodes[idx].next_sibling = -1;
    g_fs_nodes[idx].storage_kind = FS_NODE_STORAGE_INLINE;
    g_fs_nodes[idx].size = 0;
    g_fs_nodes[idx].image_lba = 0u;
    g_fs_nodes[idx].image_sector_count = 0u;
    g_fs_nodes[idx].data[0] = '\0';
    str_copy_limited(g_fs_nodes[idx].name, name, FS_NAME_MAX + 1);

    if (parent >= 0) {
        fs_link_child(parent, idx);
    }

    return idx;
}

static int fs_split_path(const char *path,
                         char segments[FS_MAX_SEGMENTS][FS_NAME_MAX + 1],
                         int *is_abs) {
    int count = 0;
    const char *p = path;

    *is_abs = 0;
    if (*p == '/') {
        *is_abs = 1;
        while (*p == '/') {
            ++p;
        }
    }

    while (*p != '\0') {
        int len = 0;

        if (count >= FS_MAX_SEGMENTS) {
            return -1;
        }

        while (*p != '\0' && *p != '/') {
            if (len < FS_NAME_MAX) {
                segments[count][len++] = *p;
            }
            ++p;
        }
        segments[count][len] = '\0';
        ++count;

        while (*p == '/') {
            ++p;
        }
    }

    return count;
}

int fs_resolve(const char *path) {
    char seg[FS_MAX_SEGMENTS][FS_NAME_MAX + 1];
    int is_abs = 0;
    int count;
    int cur;
    int i;

    if (path == 0 || path[0] == '\0') {
        return g_fs_cwd;
    }

    fs_maybe_register_boot_assets_for_path(path);

    count = fs_split_path(path, seg, &is_abs);
    if (count < 0) {
        return -1;
    }

    cur = is_abs ? g_fs_root : g_fs_cwd;
    if (count == 0) {
        return cur;
    }

    for (i = 0; i < count; ++i) {
        if (str_eq(seg[i], ".") || seg[i][0] == '\0') {
            continue;
        }

        if (str_eq(seg[i], "..")) {
            if (cur != g_fs_root) {
                cur = g_fs_nodes[cur].parent;
            }
            continue;
        }

        {
            int child = fs_find_child(cur, seg[i]);
            if (child < 0) {
                return -1;
            }
            if (i < (count - 1) && !g_fs_nodes[child].is_dir) {
                return -1;
            }
            cur = child;
        }
    }

    return cur;
}

int fs_lookup_executable_alias(const char *path, char *name_out, int max_len) {
    const char *basename;

    if (path == 0 || name_out == 0 || max_len <= 0) {
        return -1;
    }

    for (int i = 0; i < (int)G_APP_CATALOG_ALIAS_PATHS_COUNT; ++i) {
        if (!str_eq(path, g_app_catalog_alias_paths[i])) {
            continue;
        }
        basename = fs_path_basename(path);
        if (basename == 0 || basename[0] == '\0') {
            return -1;
        }
        str_copy_limited(name_out, basename, max_len);
        return 0;
    }

    return -1;
}

int fs_read_node_bytes(int node, int offset, void *dst, int size) {
    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir ||
        !dst || size < 0 || offset < 0) {
        return -1;
    }
    if (offset >= g_fs_nodes[node].size) {
        return 0;
    }
    if (offset + size > g_fs_nodes[node].size) {
        size = g_fs_nodes[node].size - offset;
    }
    if (size <= 0) {
        return 0;
    }

    if (g_fs_nodes[node].storage_kind == FS_NODE_STORAGE_INLINE) {
        memcpy(dst, g_fs_nodes[node].data + offset, (size_t)size);
        return size;
    }
    if (g_fs_nodes[node].storage_kind == FS_NODE_STORAGE_IMAGE) {
        if (fs_read_image_bytes(g_fs_nodes[node].image_lba, (uint32_t)offset, dst, (uint32_t)size) != 0) {
            return -1;
        }
        return size;
    }
    return -1;
}

int fs_read_file_bytes(const char *path, int offset, void *dst, int size) {
    int node = fs_resolve(path);
    if (node < 0) {
        return -1;
    }
    return fs_read_node_bytes(node, offset, dst, size);
}

int fs_read_builtin_asset_bytes(const char *path, int offset, void *dst, int size) {
    uint32_t lba = 0u;
    uint32_t total_size = 0u;

    if (path == 0 || dst == 0 || size < 0 || offset < 0) {
        return -1;
    }

    if (str_eq(path, "/assets/vibe_os_boot.wav")) {
        lba = VIBE_BOOT_WAV_IMAGE_LBA;
        total_size = VIBE_BOOT_WAV_IMAGE_BYTES;
    } else if (str_eq(path, "/assets/vibe_os_desktop.wav")) {
        lba = VIBE_DESKTOP_WAV_IMAGE_LBA;
        total_size = VIBE_DESKTOP_WAV_IMAGE_BYTES;
    } else {
        return -1;
    }

    if ((uint32_t)offset >= total_size) {
        return 0;
    }
    if ((uint32_t)offset + (uint32_t)size > total_size) {
        size = (int)(total_size - (uint32_t)offset);
    }
    if (size <= 0) {
        return 0;
    }
    if (fs_read_image_bytes(lba, (uint32_t)offset, dst, (uint32_t)size) != 0) {
        return -1;
    }
    return size;
}

static int fs_resolve_parent(const char *path, int *parent_out, char *name_out) {
    char seg[FS_MAX_SEGMENTS][FS_NAME_MAX + 1];
    int is_abs = 0;
    int count;
    int cur;
    int i;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    count = fs_split_path(path, seg, &is_abs);
    if (count <= 0) {
        return -1;
    }

    cur = is_abs ? g_fs_root : g_fs_cwd;
    for (i = 0; i < (count - 1); ++i) {
        if (str_eq(seg[i], ".") || seg[i][0] == '\0') {
            continue;
        }

        if (str_eq(seg[i], "..")) {
            if (cur != g_fs_root) {
                cur = g_fs_nodes[cur].parent;
            }
            continue;
        }

        {
            int child = fs_find_child(cur, seg[i]);
            if (child < 0 || !g_fs_nodes[child].is_dir) {
                return -1;
            }
            cur = child;
        }
    }

    if (str_eq(seg[count - 1], ".") || str_eq(seg[count - 1], "..") ||
        seg[count - 1][0] == '\0') {
        return -1;
    }

    *parent_out = cur;
    str_copy_limited(name_out, seg[count - 1], FS_NAME_MAX + 1);
    return 0;
}

int fs_create(const char *path, int is_dir) {
    int parent;
    char name[FS_NAME_MAX + 1];
    int created;

    if (fs_resolve_parent(path, &parent, name) != 0) {
        return -1;
    }

    if (!g_fs_nodes[parent].is_dir) {
        return -1;
    }

    if (fs_find_child(parent, name) >= 0) {
        return -2;
    }

    created = fs_new_node(name, is_dir, parent);
    if (created < 0) {
        return -3;
    }
    fs_mark_dirty();
    return 0;
}

int fs_remove(const char *path) {
    int idx = fs_resolve(path);
    int parent;

    if (idx < 0 || idx == g_fs_root) {
        return -1;
    }

    if (g_fs_nodes[idx].is_dir && fs_has_children(idx)) {
        return -2;
    }

    parent = g_fs_nodes[idx].parent;
    fs_unlink_child(parent, idx);
    fs_reset_node(idx);
    fs_mark_dirty();
    return 0;
}

int fs_rename_node(int node, const char *new_name) {
    int parent;
    int existing;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || node == g_fs_root) {
        return -1;
    }
    if (!fs_name_is_valid(new_name)) {
        return -2;
    }

    parent = g_fs_nodes[node].parent;
    existing = fs_find_child(parent, new_name);
    if (existing >= 0 && existing != node) {
        return -3;
    }

    str_copy_limited(g_fs_nodes[node].name, new_name, FS_NAME_MAX + 1);
    fs_mark_dirty();
    return 0;
}

int fs_move_node(int node, int new_parent, const char *new_name) {
    int old_parent;
    int existing;
    int cursor;
    int guard = 0;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || node == g_fs_root) {
        return -1;
    }
    if (new_parent < 0 || new_parent >= FS_MAX_NODES ||
        !g_fs_nodes[new_parent].used || !g_fs_nodes[new_parent].is_dir) {
        return -2;
    }
    if (!fs_name_is_valid(new_name)) {
        return -3;
    }
    if (node == new_parent) {
        return -4;
    }

    if (g_fs_nodes[node].is_dir) {
        cursor = new_parent;
        while (++guard <= FS_MAX_NODES) {
            if (cursor == node) {
                return -5;
            }
            if (cursor == g_fs_root) {
                break;
            }
            cursor = g_fs_nodes[cursor].parent;
            if (cursor < 0 || cursor >= FS_MAX_NODES || !g_fs_nodes[cursor].used) {
                return -5;
            }
        }
        if (guard > FS_MAX_NODES) {
            return -5;
        }
    }

    existing = fs_find_child(new_parent, new_name);
    if (existing >= 0 && existing != node) {
        return -6;
    }

    old_parent = g_fs_nodes[node].parent;
    if (old_parent == new_parent && str_eq(g_fs_nodes[node].name, new_name)) {
        return 0;
    }

    fs_unlink_child(old_parent, node);
    str_copy_limited(g_fs_nodes[node].name, new_name, FS_NAME_MAX + 1);
    fs_link_child(new_parent, node);
    fs_mark_dirty();
    return 0;
}

int fs_write_file(const char *path, const char *text, int append) {
    int idx = fs_resolve(path);
    int i;

    if (idx < 0) {
        if (fs_create(path, 0) != 0) {
            return -1;
        }
        idx = fs_resolve(path);
        if (idx < 0) {
            return -1;
        }
    }

    if (g_fs_nodes[idx].is_dir) {
        return -2;
    }
    if (g_fs_nodes[idx].storage_kind != FS_NODE_STORAGE_INLINE) {
        return -3;
    }

    if (!append) {
        g_fs_nodes[idx].size = 0;
        g_fs_nodes[idx].data[0] = '\0';
    }

    i = g_fs_nodes[idx].size;
    while (*text != '\0' && i < FS_FILE_MAX) {
        g_fs_nodes[idx].data[i++] = *text++;
    }
    g_fs_nodes[idx].data[i] = '\0';
    g_fs_nodes[idx].size = i;
    fs_mark_dirty();
    return 0;
}

int fs_write_bytes(const char *path, const uint8_t *data, int size) {
    int idx = fs_resolve(path);
    uint32_t sector_count;
    uint32_t start_lba;

    if (size < 0 || (size > 0 && !data)) {
        return -3;
    }

    if (idx < 0) {
        if (fs_create(path, 0) != 0) {
            return -1;
        }
        idx = fs_resolve(path);
        if (idx < 0) {
            return -1;
        }
    }

    if (g_fs_nodes[idx].is_dir) {
        return -2;
    }

    if (size <= FS_FILE_MAX) {
        fs_clear_node_storage(idx);
        for (int i = 0; i < size; ++i) {
            g_fs_nodes[idx].data[i] = (char)data[i];
        }
        g_fs_nodes[idx].size = size;
        if (size <= FS_FILE_MAX) {
            g_fs_nodes[idx].data[size] = '\0';
        }
        fs_mark_dirty();
        return 0;
    }

    sector_count = fs_sector_count_for_size(size);
    start_lba = fs_find_free_extent(sector_count, idx);
    if (start_lba == 0u) {
        sys_write_debug("fs: no free extent for large write\n");
        return -4;
    }
    if (fs_write_sector_bytes(start_lba, data, size) != 0) {
        sys_write_debug("fs: large write failed\n");
        return -5;
    }

    g_fs_nodes[idx].storage_kind = FS_NODE_STORAGE_IMAGE;
    g_fs_nodes[idx].size = size;
    g_fs_nodes[idx].image_lba = start_lba;
    g_fs_nodes[idx].image_sector_count = sector_count;
    g_fs_nodes[idx].data[0] = '\0';
    fs_mark_dirty();
    return 0;
}

int fs_register_image_file(const char *path, uint32_t lba, uint32_t sector_count, int size) {
    int idx = fs_resolve(path);

    if (size < 0 || sector_count == 0u) {
        return -3;
    }

    if (idx < 0) {
        if (fs_create(path, 0) != 0) {
            return -1;
        }
        idx = fs_resolve(path);
        if (idx < 0) {
            return -1;
        }
    }

    if (g_fs_nodes[idx].is_dir) {
        return -2;
    }

    if (g_fs_nodes[idx].storage_kind == FS_NODE_STORAGE_IMAGE &&
        g_fs_nodes[idx].size == size &&
        g_fs_nodes[idx].image_lba == lba &&
        g_fs_nodes[idx].image_sector_count == sector_count) {
        return 0;
    }

    g_fs_nodes[idx].storage_kind = FS_NODE_STORAGE_IMAGE;
    g_fs_nodes[idx].size = size;
    g_fs_nodes[idx].image_lba = lba;
    g_fs_nodes[idx].image_sector_count = sector_count;
    g_fs_nodes[idx].data[0] = '\0';
    fs_mark_dirty();
    return 0;
}

int fs_copy_node_to_path(int src_node, const char *dst_path) {
    int dst_node;

    if (src_node < 0 || src_node >= FS_MAX_NODES || !g_fs_nodes[src_node].used ||
        g_fs_nodes[src_node].is_dir || !dst_path) {
        return -1;
    }

    if (g_fs_nodes[src_node].storage_kind == FS_NODE_STORAGE_INLINE) {
        return fs_write_bytes(dst_path, (const uint8_t *)g_fs_nodes[src_node].data, g_fs_nodes[src_node].size);
    }

    if (fs_resolve(dst_path) < 0) {
        if (fs_create(dst_path, 0) != 0) {
            return -1;
        }
    }
    dst_node = fs_resolve(dst_path);
    if (dst_node < 0 || g_fs_nodes[dst_node].is_dir) {
        return -1;
    }

    {
        uint32_t sector_count = g_fs_nodes[src_node].image_sector_count;
        uint32_t start_lba = fs_find_free_extent(sector_count, dst_node);
        uint8_t sector[FS_SECTOR_SIZE];

        if (start_lba == 0u) {
            return -2;
        }
        for (uint32_t i = 0; i < sector_count; ++i) {
            if (sys_storage_read_sectors(g_fs_nodes[src_node].image_lba + i, sector, 1u) != 0 ||
                sys_storage_write_sectors(start_lba + i, sector, 1u) != 0) {
                return -3;
            }
        }

        g_fs_nodes[dst_node].storage_kind = FS_NODE_STORAGE_IMAGE;
        g_fs_nodes[dst_node].size = g_fs_nodes[src_node].size;
        g_fs_nodes[dst_node].image_lba = start_lba;
        g_fs_nodes[dst_node].image_sector_count = sector_count;
        g_fs_nodes[dst_node].data[0] = '\0';
    }

    fs_mark_dirty();
    return 0;
}

static void fs_ensure_doom_wad_registered(void) {
    if (g_fs_root < 0 || g_fs_doom_assets_registered || g_fs_doom_assets_registering) {
        return;
    }
    g_fs_doom_assets_registering = 1;

    if (fs_resolve("/DOOM") < 0) {
        (void)fs_create("/DOOM", 1);
    }

    fs_register_known_asset("/DOOM/DOOM.WAD",
                            DOOM_WAD_IMAGE_LBA,
                            DOOM_WAD_IMAGE_SECTORS,
                            DOOM_WAD_IMAGE_BYTES);
    g_fs_doom_assets_registering = 0;
    g_fs_doom_assets_registered = 1;
}

static void fs_ensure_craft_textures_registered(void) {
    if (g_fs_root < 0 || g_fs_texture_assets_registered || g_fs_texture_assets_registering) {
        return;
    }
    g_fs_texture_assets_registering = 1;

    if (fs_resolve("/textures") < 0) {
        (void)fs_create("/textures", 1);
    }

    fs_register_known_asset("/textures/texture.png",
                            CRAFT_TEXTURE_IMAGE_LBA,
                            CRAFT_TEXTURE_IMAGE_SECTORS,
                            CRAFT_TEXTURE_IMAGE_BYTES);
    fs_register_known_asset("/textures/font.png",
                            CRAFT_FONT_IMAGE_LBA,
                            CRAFT_FONT_IMAGE_SECTORS,
                            CRAFT_FONT_IMAGE_BYTES);
    fs_register_known_asset("/textures/sky.png",
                            CRAFT_SKY_IMAGE_LBA,
                            CRAFT_SKY_IMAGE_SECTORS,
                            CRAFT_SKY_IMAGE_BYTES);
    fs_register_known_asset("/textures/sign.png",
                            CRAFT_SIGN_IMAGE_LBA,
                            CRAFT_SIGN_IMAGE_SECTORS,
                            CRAFT_SIGN_IMAGE_BYTES);
    g_fs_texture_assets_registering = 0;
    g_fs_texture_assets_registered = 1;
}

static void fs_ensure_assets_registered(void) {
    if (g_fs_root < 0 || g_fs_assets_registered || g_fs_assets_registering) {
        return;
    }
    g_fs_assets_registering = 1;

    if (fs_resolve("/assets") < 0) {
        (void)fs_create("/assets", 1);
    }

    fs_register_known_asset("/assets/wallpaper.png",
                            WALLPAPER_IMAGE_LBA,
                            WALLPAPER_IMAGE_SECTORS,
                            WALLPAPER_IMAGE_BYTES);
    fs_register_known_asset("/wallpaper.png",
                            WALLPAPER_IMAGE_LBA,
                            WALLPAPER_IMAGE_SECTORS,
                            WALLPAPER_IMAGE_BYTES);
    fs_register_known_asset("/assets/vibe_os_boot.wav",
                            VIBE_BOOT_WAV_IMAGE_LBA,
                            VIBE_BOOT_WAV_IMAGE_SECTORS,
                            VIBE_BOOT_WAV_IMAGE_BYTES);
    fs_register_known_asset("/assets/vibe_os_desktop.wav",
                            VIBE_DESKTOP_WAV_IMAGE_LBA,
                            VIBE_DESKTOP_WAV_IMAGE_SECTORS,
                            VIBE_DESKTOP_WAV_IMAGE_BYTES);
    fs_register_known_asset("/assets/bootloader_background.png",
                            BOOTLOADER_BG_IMAGE_LBA,
                            BOOTLOADER_BG_IMAGE_SECTORS,
                            BOOTLOADER_BG_IMAGE_BYTES);
    g_fs_assets_registering = 0;
    g_fs_assets_registered = 1;
}

void fs_build_path(int node, char *out, int max_len) {
    int stack[FS_MAX_NODES];
    int top = 0;
    int i;
    int pos = 0;

    if (node == g_fs_root) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }

    while (node != g_fs_root && node >= 0 && top < FS_MAX_NODES) {
        stack[top++] = node;
        node = g_fs_nodes[node].parent;
    }
    if (top >= FS_MAX_NODES) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }

    out[pos++] = '/';
    for (i = top - 1; i >= 0; --i) {
        const char *name = g_fs_nodes[stack[i]].name;
        while (*name != '\0' && pos < (max_len - 1)) {
            out[pos++] = *name++;
        }
        if (i > 0 && pos < (max_len - 1)) {
            out[pos++] = '/';
        }
    }
    out[pos] = '\0';
}

void fs_init(void) {
    int i;
    extern void kernel_debug_puts(const char *);

    g_fs_sync_suspended = 1;
    g_fs_sync_hold_depth = 0u;
    g_fs_dirty = 0;
    g_fs_dirty_generation = 0u;
    g_fs_last_sync_tick = 0u;
    g_fs_writeback_active = 0;
    g_fs_writeback_generation = 0u;
    g_fs_writeback_bytes = 0u;
    g_fs_writeback_sector_index = 0u;
    g_fs_writeback_progress_tick = 0xffffffffu;
    g_fs_allow_immediate_writeback = 0;
    g_fs_doom_assets_registered = 0;
    g_fs_texture_assets_registered = 0;
    g_fs_assets_registered = 0;
    g_fs_doom_assets_registering = 0;
    g_fs_texture_assets_registering = 0;
    g_fs_assets_registering = 0;
    memset(g_fs_persist_buffer, 0, sizeof(g_fs_persist_buffer));
    for (i = 0; i < FS_MAX_NODES; ++i) {
        fs_reset_node(i);
    }

    g_fs_root = -1;
    g_fs_cwd = -1;

    if (fs_load_persistent_image() == 0) {
        fs_ensure_assets_registered();
        kernel_debug_puts("fs: persistent image valid\n");
        kernel_debug_puts("fs: asset scans deferred\n");
        g_fs_sync_suspended = 0;
        g_fs_allow_immediate_writeback = 1;
        return;
    }

    kernel_debug_puts("fs: persistent image invalid, rebuilding\n");

    g_fs_root = fs_new_node("", 1, -1);
    g_fs_nodes[g_fs_root].parent = g_fs_root;
    g_fs_cwd = g_fs_root;

    /* create a minimal UNIX-like hierarchy */
    (void)fs_create("/bin", 1);
    (void)fs_create("/usr", 1);
    (void)fs_create("/usr/bin", 1);
    (void)fs_create("/compat", 1);
    (void)fs_create("/compat/bin", 1);
    (void)fs_create("/home", 1);
    (void)fs_create("/home/user", 1);
    (void)fs_create("/tmp", 1);
    (void)fs_create("/dev", 1);
    (void)fs_create("/DOOM", 1);
    (void)fs_create("/textures", 1);
    (void)fs_create("/assets", 1);
    (void)fs_create("/docs", 1);
    (void)fs_create("/config", 1);
    (void)fs_create("/trash", 1);
    (void)fs_write_file("/README", "SISTEMA DE ARQUIVOS VFS", 0);
    (void)fs_write_file("/teste.lua",
                        "print(\"hello from lua\")\n"
                        "x = 42\n"
                        "print(x)\n",
                        0);
    (void)fs_write_file("/hello.c",
                        "void main() {\n"
                        "  print(\"hello from sectorc\");\n"
                        "}\n",
                        0);
    kernel_debug_puts("fs: base tree created\n");
    kernel_debug_puts("fs: executable aliases are virtual\n");
    fs_ensure_assets_registered();
    kernel_debug_puts("fs: asset scans deferred\n");
    g_fs_sync_suspended = 0;
    fs_mark_dirty();
    g_fs_last_sync_tick = 0u;
    g_fs_allow_immediate_writeback = 1;
    kernel_debug_puts("fs: initial sync deferred\n");
}

int fs_ready(void) {
    return g_fs_root >= 0 &&
           g_fs_root < FS_MAX_NODES &&
           g_fs_nodes[g_fs_root].used != 0 &&
           g_fs_nodes[g_fs_root].is_dir != 0;
}

void fs_suspend_sync(void) {
    if (g_fs_sync_hold_depth != 0xffffffffu) {
        g_fs_sync_hold_depth += 1u;
    }
}

void fs_resume_sync(void) {
    if (g_fs_sync_hold_depth != 0u) {
        g_fs_sync_hold_depth -= 1u;
    }
}
