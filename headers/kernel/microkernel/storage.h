#ifndef KERNEL_MICROKERNEL_STORAGE_H
#define KERNEL_MICROKERNEL_STORAGE_H

#include <stdint.h>

struct mk_message;

struct mk_storage_sectors_request {
    uint32_t lba;
    uint32_t sector_count;
    uint32_t transfer_id;
};

struct mk_storage_persist_request {
    uint32_t size;
    uint32_t transfer_id;
};

struct mk_storage_info {
    uint32_t total_sectors;
    uint32_t partition_start_lba;
};

void mk_storage_service_init(void);
int mk_storage_service_ready(void);
int mk_storage_service_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int mk_storage_service_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
int mk_storage_service_load(void *dst, uint32_t size);
int mk_storage_service_save(const void *src, uint32_t size);
uint32_t mk_storage_service_total_sectors(void);
uint32_t mk_storage_service_partition_start_lba(void);
int mk_storage_service_last_request(struct mk_message *message);

#endif
