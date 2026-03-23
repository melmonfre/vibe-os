#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>

#define ATA_PRIMARY_IO 0x1F0u
#define ATA_PRIMARY_CTRL 0x3F6u

#define ATA_REG_DATA (ATA_PRIMARY_IO + 0u)
#define ATA_REG_FEATURES (ATA_PRIMARY_IO + 1u)
#define ATA_REG_SECCOUNT0 (ATA_PRIMARY_IO + 2u)
#define ATA_REG_LBA0 (ATA_PRIMARY_IO + 3u)
#define ATA_REG_LBA1 (ATA_PRIMARY_IO + 4u)
#define ATA_REG_LBA2 (ATA_PRIMARY_IO + 5u)
#define ATA_REG_HDDEVSEL (ATA_PRIMARY_IO + 6u)
#define ATA_REG_COMMAND (ATA_PRIMARY_IO + 7u)
#define ATA_REG_STATUS (ATA_PRIMARY_IO + 7u)
#define ATA_REG_ALTSTATUS ATA_PRIMARY_CTRL

#define ATA_CMD_READ_PIO 0x20u
#define ATA_CMD_WRITE_PIO 0x30u
#define ATA_CMD_CACHE_FLUSH 0xE7u
#define ATA_CMD_IDENTIFY 0xECu

#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_DF 0x20u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_BSY 0x80u

#define ATA_TIMEOUT 100000u
#define ATA_WRITE_VERIFY_RETRIES 3u
static int g_ata_ready = 0;
static uint32_t g_ata_total_sectors = 0u;
static uint32_t g_storage_partition_start_lba = 0u;
static uint32_t g_storage_partition_sector_count = 0u;
static spinlock_t g_ata_lock;

static int ata_read_sector_locked(uint32_t lba, uint8_t *buf);
static int ata_write_sector_locked(uint32_t lba, const uint8_t *buf);
static int ata_read_sector(uint32_t lba, uint8_t *buf);
static int ata_write_sector(uint32_t lba, const uint8_t *buf);
static int ata_block_read(void *context, uint32_t lba, uint8_t *buf);
static int ata_block_write(void *context, uint32_t lba, const uint8_t *buf);
static int ata_disk_read(void *context, uint32_t lba, uint8_t *buf);

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u) {
            return 0;
        }
    }
    return -1;
}

static void ata_delay_400ns(void) {
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
}

static void ata_settle_bus(void) {
    for (uint32_t i = 0; i < 2048u; ++i) {
        (void)inb(ATA_REG_ALTSTATUS);
    }
}

static int ata_wait_data_ready(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_ERR) != 0u || (status & ATA_SR_DF) != 0u) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u &&
            (status & ATA_SR_DRQ) != 0u &&
            (status & ATA_SR_DRDY) != 0u) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_command_done(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_ERR) != 0u || (status & ATA_SR_DF) != 0u) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u &&
            (status & ATA_SR_DRQ) == 0u) {
            return 0;
        }
    }
    return -1;
}

static void ata_select_lba(uint32_t lba) {
    outb(ATA_REG_HDDEVSEL, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    ata_delay_400ns();
}

static int ata_identify(void) {
    uint16_t identify_data[256];

    ata_select_lba(0u);
    outb(ATA_PRIMARY_CTRL, 0u);
    outb(ATA_REG_SECCOUNT0, 0u);
    outb(ATA_REG_LBA0, 0u);
    outb(ATA_REG_LBA1, 0u);
    outb(ATA_REG_LBA2, 0u);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ATA_REG_STATUS) == 0u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }
    if (inb(ATA_REG_LBA1) != 0u || inb(ATA_REG_LBA2) != 0u) {
        return -1;
    }
    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        identify_data[i] = inw(ATA_REG_DATA);
    }
    g_ata_total_sectors = ((uint32_t)identify_data[61] << 16) | (uint32_t)identify_data[60];
    if (g_ata_total_sectors == 0u) {
        return -1;
    }
    return 0;
}

static int ata_read_logical_sector(uint32_t lba, uint8_t *buf) {
    if (lba >= g_storage_partition_sector_count) {
        return -1;
    }
    return ata_read_sector(g_storage_partition_start_lba + lba, buf);
}

static int ata_write_logical_sector(uint32_t lba, const uint8_t *buf) {
    if (lba >= g_storage_partition_sector_count) {
        return -1;
    }
    return ata_write_sector(g_storage_partition_start_lba + lba, buf);
}

static int ata_read_sector_locked(uint32_t lba, uint8_t *buf) {
    if (buf == 0 || lba >= 0x10000000u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }

    ata_select_lba(lba);
    outb(ATA_REG_FEATURES, 0u);
    outb(ATA_REG_SECCOUNT0, 1u);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    ata_delay_400ns();

    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t value = inw(ATA_REG_DATA);
        buf[(i * 2) + 0] = (uint8_t)(value & 0xFFu);
        buf[(i * 2) + 1] = (uint8_t)((value >> 8) & 0xFFu);
    }
    return 0;
}

static int ata_write_sector_locked(uint32_t lba, const uint8_t *buf) {
    uint8_t verify[512];

    if (buf == 0 || lba >= 0x10000000u) {
        return -1;
    }

    for (uint32_t attempt = 0; attempt < ATA_WRITE_VERIFY_RETRIES; ++attempt) {
        if (ata_wait_not_busy() != 0) {
            return -1;
        }

        ata_select_lba(lba);
        outb(ATA_REG_FEATURES, 0u);
        outb(ATA_REG_SECCOUNT0, 1u);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
        outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        ata_delay_400ns();

        if (ata_wait_data_ready() != 0) {
            return -1;
        }

        for (int i = 0; i < 256; ++i) {
            uint16_t value = (uint16_t)buf[(i * 2) + 0] |
                             ((uint16_t)buf[(i * 2) + 1] << 8);
            outw(ATA_REG_DATA, value);
        }
        ata_delay_400ns();
        if (ata_wait_command_done() != 0) {
            continue;
        }

        outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_delay_400ns();
        if (ata_wait_command_done() != 0) {
            continue;
        }
        ata_settle_bus();
        if (ata_read_sector_locked(lba, verify) != 0) {
            ata_settle_bus();
            continue;
        }

        for (int i = 0; i < 512; ++i) {
            if (verify[i] != buf[i]) {
                ata_settle_bus();
                goto retry;
            }
        }
        return 0;

retry:
        ;
    }

    return -1;
}

static int ata_read_sector(uint32_t lba, uint8_t *buf) {
    uint32_t flags = spinlock_lock_irqsave(&g_ata_lock);
    int rc = ata_read_sector_locked(lba, buf);
    spinlock_unlock_irqrestore(&g_ata_lock, flags);
    return rc;
}

static int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    uint32_t flags = spinlock_lock_irqsave(&g_ata_lock);
    int rc = ata_write_sector_locked(lba, buf);
    spinlock_unlock_irqrestore(&g_ata_lock, flags);
    return rc;
}

int kernel_ata_init(void) {
    spinlock_init(&g_ata_lock);
    g_ata_ready = ata_identify() == 0;
    if (!g_ata_ready) {
        return -1;
    }
    g_storage_partition_start_lba = 0u;
    g_storage_partition_sector_count = g_ata_total_sectors;
    if (kernel_block_device_detect_mbr_partition(0,
                                                 g_ata_total_sectors,
                                                 ata_disk_read,
                                                 &g_storage_partition_start_lba,
                                                 &g_storage_partition_sector_count) != 0) {
        g_ata_ready = 0;
        return -1;
    }
    if (kernel_block_device_register_primary("ata",
                                             0,
                                             g_storage_partition_sector_count,
                                             g_storage_partition_start_lba,
                                             ata_block_read,
                                             ata_block_write) != 0) {
        g_ata_ready = 0;
        return -1;
    }
    kernel_debug_printf("ata: start=%u sectors=%u total=%u\n",
                        g_storage_partition_start_lba,
                        g_storage_partition_sector_count,
                        g_ata_total_sectors);
    return 0;
}

static int ata_block_read(void *context, uint32_t lba, uint8_t *buf) {
    (void)context;
    if (!g_ata_ready) {
        return -1;
    }
    return ata_read_logical_sector(lba, buf);
}

static int ata_block_write(void *context, uint32_t lba, const uint8_t *buf) {
    (void)context;
    if (!g_ata_ready) {
        return -1;
    }
    return ata_write_logical_sector(lba, buf);
}

static int ata_disk_read(void *context, uint32_t lba, uint8_t *buf) {
    (void)context;
    if (!g_ata_ready) {
        return -1;
    }
    return ata_read_sector(lba, buf);
}
