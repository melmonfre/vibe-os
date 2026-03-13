#ifndef KERNEL_DRIVERS_STORAGE_ATA_H
#define KERNEL_DRIVERS_STORAGE_ATA_H

#include <stdint.h>

#define KERNEL_PERSIST_START_LBA 512u
#define KERNEL_PERSIST_SECTOR_SIZE 512u
#define KERNEL_PERSIST_SECTOR_COUNT 640u
#define KERNEL_PERSIST_MAX_BYTES (KERNEL_PERSIST_SECTOR_SIZE * KERNEL_PERSIST_SECTOR_COUNT)

void kernel_storage_init(void);
int kernel_storage_ready(void);
int kernel_storage_load(void *dst, uint32_t size);
int kernel_storage_save(const void *src, uint32_t size);

#endif
