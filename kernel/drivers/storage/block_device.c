#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/bootinfo.h>
#include <kernel/kernel_string.h>

#define BLOCK_DEVICE_MBR_PARTITION_OFFSET 446u
#define BLOCK_DEVICE_MBR_SIGNATURE_OFFSET 510u
#define BLOCK_DEVICE_STORAGE_PARTITION_INDEX 1u
#define BLOCK_DEVICE_IO_RETRIES 6u

struct kernel_block_device {
    const char *name;
    void *context;
    uint32_t sector_count;
    uint32_t partition_start_lba;
    kernel_block_device_read_fn read_sector;
    kernel_block_device_write_fn write_sector;
};

static struct kernel_block_device g_primary_block_device;

static uint32_t block_device_read_u32_le(const uint8_t *src) {
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

void kernel_block_device_reset(void) {
    memset(&g_primary_block_device, 0, sizeof(g_primary_block_device));
}

int kernel_block_device_register_primary(const char *name,
                                         void *context,
                                         uint32_t sector_count,
                                         uint32_t partition_start_lba,
                                         kernel_block_device_read_fn read_sector,
                                         kernel_block_device_write_fn write_sector) {
    if (name == 0 || read_sector == 0 || write_sector == 0 || sector_count == 0u) {
        return -1;
    }

    g_primary_block_device.name = name;
    g_primary_block_device.context = context;
    g_primary_block_device.sector_count = sector_count;
    g_primary_block_device.partition_start_lba = partition_start_lba;
    g_primary_block_device.read_sector = read_sector;
    g_primary_block_device.write_sector = write_sector;
    return 0;
}

int kernel_block_device_detect_mbr_partition(void *context,
                                             uint32_t total_sectors,
                                             kernel_block_device_read_fn read_sector,
                                             uint32_t *partition_start_lba,
                                             uint32_t *partition_sector_count) {
    uint8_t mbr[KERNEL_PERSIST_SECTOR_SIZE];
    const uint8_t *entry;
    uint32_t start_lba;
    uint32_t sector_count;

    if (partition_start_lba == 0 || partition_sector_count == 0 || read_sector == 0 ||
        total_sectors == 0u) {
        return -1;
    }

    *partition_start_lba = 0u;
    *partition_sector_count = total_sectors;

    if (read_sector(context, 0u, mbr) != 0) {
        return 0;
    }
    if (mbr[BLOCK_DEVICE_MBR_SIGNATURE_OFFSET] != 0x55u ||
        mbr[BLOCK_DEVICE_MBR_SIGNATURE_OFFSET + 1] != 0xAAu) {
        return 0;
    }

    entry = &mbr[BLOCK_DEVICE_MBR_PARTITION_OFFSET + (BLOCK_DEVICE_STORAGE_PARTITION_INDEX * 16u)];
    start_lba = block_device_read_u32_le(entry + 8);
    sector_count = block_device_read_u32_le(entry + 12);
    if (start_lba != 0u && sector_count != 0u && start_lba < total_sectors) {
        if (sector_count > (total_sectors - start_lba)) {
            sector_count = total_sectors - start_lba;
        }
        *partition_start_lba = start_lba;
        *partition_sector_count = sector_count;
        return 0;
    }

    /* Fallback: if the canonical raw-data partition entry is invalid, scan all
     * partition slots for a compatible non-empty partition. This improves
     * support for disks with unusual partition layouts while preserving the
     * documented entry-1 convention when it is valid. */
    for (uint32_t index = 0u; index < 4u; ++index) {
        const uint8_t *probe_entry = &mbr[BLOCK_DEVICE_MBR_PARTITION_OFFSET + (index * 16u)];
        uint32_t probe_start = block_device_read_u32_le(probe_entry + 8);
        uint32_t probe_count = block_device_read_u32_le(probe_entry + 12);

        if (probe_start == 0u || probe_count == 0u || probe_start >= total_sectors) {
            continue;
        }
        if (probe_count > (total_sectors - probe_start)) {
            probe_count = total_sectors - probe_start;
        }
        *partition_start_lba = probe_start;
        *partition_sector_count = probe_count;
        return 0;
    }

    return 0;
}

static int kernel_block_device_detect_bootinfo_partition(uint32_t total_sectors,
                                                         uint32_t *partition_start_lba,
                                                         uint32_t *partition_sector_count) {
    const volatile struct bootinfo *bootinfo;
    uint32_t start_lba;
    uint32_t sector_count;

    if (partition_start_lba == 0 || partition_sector_count == 0 || total_sectors == 0u) {
        return -1;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_PARTITIONS_VALID) == 0u) {
        return -1;
    }

    start_lba = bootinfo->disk.data_partition_lba;
    sector_count = bootinfo->disk.data_partition_sectors;
    if (start_lba == 0u || sector_count == 0u || start_lba >= total_sectors) {
        return -1;
    }
    if (sector_count > (total_sectors - start_lba)) {
        sector_count = total_sectors - start_lba;
    }

    *partition_start_lba = start_lba;
    *partition_sector_count = sector_count;
    return 0;
}

int kernel_block_device_resolve_partition(void *context,
                                          uint32_t total_sectors,
                                          kernel_block_device_read_fn read_sector,
                                          uint32_t *partition_start_lba,
                                          uint32_t *partition_sector_count) {
    if (partition_start_lba == 0 || partition_sector_count == 0 || total_sectors == 0u) {
        return -1;
    }

    *partition_start_lba = 0u;
    *partition_sector_count = total_sectors;
    if (kernel_block_device_detect_bootinfo_partition(total_sectors,
                                                      partition_start_lba,
                                                      partition_sector_count) == 0) {
        return 0;
    }

    return kernel_block_device_detect_mbr_partition(context,
                                                    total_sectors,
                                                    read_sector,
                                                    partition_start_lba,
                                                    partition_sector_count);
}

const char *kernel_block_device_name(void) {
    return g_primary_block_device.name;
}

int kernel_storage_ready(void) {
    return g_primary_block_device.read_sector != 0 && g_primary_block_device.write_sector != 0;
}

int kernel_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    uint8_t *out = (uint8_t *)dst;

    if (!kernel_storage_ready() || dst == 0 || sector_count == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < sector_count; ++i) {
        int ok = 0;
        for (uint32_t attempt = 0; attempt < BLOCK_DEVICE_IO_RETRIES; ++attempt) {
            if (g_primary_block_device.read_sector(g_primary_block_device.context,
                                                   lba + i,
                                                   out + (i * KERNEL_PERSIST_SECTOR_SIZE)) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return -1;
        }
    }

    return 0;
}

int kernel_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    const uint8_t *in = (const uint8_t *)src;

    if (!kernel_storage_ready() || src == 0 || sector_count == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < sector_count; ++i) {
        int ok = 0;
        for (uint32_t attempt = 0; attempt < BLOCK_DEVICE_IO_RETRIES; ++attempt) {
            if (g_primary_block_device.write_sector(g_primary_block_device.context,
                                                    lba + i,
                                                    in + (i * KERNEL_PERSIST_SECTOR_SIZE)) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return -1;
        }
    }

    return 0;
}

uint32_t kernel_storage_total_sectors(void) {
    return g_primary_block_device.sector_count;
}

uint32_t kernel_storage_partition_start_lba(void) {
    return g_primary_block_device.partition_start_lba;
}

int kernel_storage_load(void *dst, uint32_t size) {
    uint8_t sector[KERNEL_PERSIST_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)dst;
    uint32_t sectors;
    uint32_t remaining;

    if (!kernel_storage_ready() || dst == 0 || size > KERNEL_PERSIST_MAX_BYTES) {
        return -1;
    }

    sectors = (size + (KERNEL_PERSIST_SECTOR_SIZE - 1u)) / KERNEL_PERSIST_SECTOR_SIZE;
    remaining = size;
    for (uint32_t i = 0; i < sectors; ++i) {
        uint32_t chunk = remaining > KERNEL_PERSIST_SECTOR_SIZE ? KERNEL_PERSIST_SECTOR_SIZE : remaining;
        if (g_primary_block_device.read_sector(g_primary_block_device.context,
                                               KERNEL_PERSIST_START_LBA + i,
                                               sector) != 0) {
            return -1;
        }
        memcpy(out + (i * KERNEL_PERSIST_SECTOR_SIZE), sector, chunk);
        remaining -= chunk;
    }

    return 0;
}

int kernel_storage_save(const void *src, uint32_t size) {
    uint8_t sector[KERNEL_PERSIST_SECTOR_SIZE];
    const uint8_t *in = (const uint8_t *)src;
    uint32_t sectors;
    uint32_t remaining;

    if (!kernel_storage_ready() || src == 0 || size > KERNEL_PERSIST_MAX_BYTES) {
        return -1;
    }

    sectors = (size + (KERNEL_PERSIST_SECTOR_SIZE - 1u)) / KERNEL_PERSIST_SECTOR_SIZE;
    remaining = size;
    for (uint32_t i = 0; i < sectors; ++i) {
        uint32_t chunk = remaining > KERNEL_PERSIST_SECTOR_SIZE ? KERNEL_PERSIST_SECTOR_SIZE : remaining;
        memset(sector, 0, sizeof(sector));
        memcpy(sector, in + (i * KERNEL_PERSIST_SECTOR_SIZE), chunk);
        if (g_primary_block_device.write_sector(g_primary_block_device.context,
                                                KERNEL_PERSIST_START_LBA + i,
                                                sector) != 0) {
            return -1;
        }
        remaining -= chunk;
    }

    return 0;
}
