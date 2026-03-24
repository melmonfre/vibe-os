#ifndef KERNEL_BOOTINFO_H
#define KERNEL_BOOTINFO_H

#include <stdint.h>

#define BOOTINFO_ADDR 0x8000u
#define BOOTINFO_MAGIC 0x544F4256u
#define BOOTINFO_VERSION 1u

#define BOOTINFO_FLAG_VESA_VALID 0x00000001u
#define BOOTINFO_FLAG_MEMINFO_VALID 0x00000002u
#define BOOTINFO_FLAG_PARTITIONS_VALID 0x00000004u
#define BOOTINFO_FLAG_BOOT_TO_DESKTOP 0x00010000u
#define BOOTINFO_FLAG_BOOT_SAFE_MODE 0x00020000u
#define BOOTINFO_FLAG_BOOT_RESCUE_SHELL 0x00040000u

struct bootinfo_vesa {
    uint16_t mode;
    uint16_t reserved0;
    uint32_t fb_addr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t reserved1[3];
};

struct bootinfo_meminfo {
    uint32_t largest_base;
    uint32_t largest_size;
    uint32_t largest_end;
    uint32_t reserved;
};

struct bootinfo_disk {
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
};

struct bootinfo {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
    struct bootinfo_vesa vesa;
    struct bootinfo_meminfo meminfo;
    struct bootinfo_disk disk;
};

#endif
