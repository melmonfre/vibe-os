#ifndef KERNEL_DRIVERS_STORAGE_BLOCK_DEVICE_H
#define KERNEL_DRIVERS_STORAGE_BLOCK_DEVICE_H

#include <stdint.h>

typedef int (*kernel_block_device_read_fn)(void *context, uint32_t lba, uint8_t *buf);
typedef int (*kernel_block_device_write_fn)(void *context, uint32_t lba, const uint8_t *buf);

void kernel_block_device_reset(void);
int kernel_block_device_register_primary(const char *name,
                                         void *context,
                                         uint32_t sector_count,
                                         uint32_t partition_start_lba,
                                         kernel_block_device_read_fn read_sector,
                                         kernel_block_device_write_fn write_sector);
int kernel_block_device_detect_mbr_partition(void *context,
                                             uint32_t total_sectors,
                                             kernel_block_device_read_fn read_sector,
                                             uint32_t *partition_start_lba,
                                             uint32_t *partition_sector_count);

const char *kernel_block_device_name(void);
int kernel_storage_ready(void);
int kernel_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int kernel_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
uint32_t kernel_storage_total_sectors(void);
uint32_t kernel_storage_partition_start_lba(void);
int kernel_storage_load(void *dst, uint32_t size);
int kernel_storage_save(const void *src, uint32_t size);

#endif
