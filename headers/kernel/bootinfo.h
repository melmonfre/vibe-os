#ifndef KERNEL_BOOTINFO_H
#define KERNEL_BOOTINFO_H

#include <stddef.h>
#include <stdint.h>

#define BOOTINFO_ADDR 0x8D00u
#define BOOTINFO_MAGIC 0x544F4256u
#define BOOTINFO_VERSION 2u
#define BOOTINFO_MAX_VESA_MODES 16u
#define BOOTINFO_VIDEO_INDEX_NONE 0xFFu

#define BOOTINFO_FLAG_VESA_VALID 0x00000001u
#define BOOTINFO_FLAG_MEMINFO_VALID 0x00000002u
#define BOOTINFO_FLAG_PARTITIONS_VALID 0x00000004u
#define BOOTINFO_FLAG_BOOT_TO_DESKTOP 0x00010000u
#define BOOTINFO_FLAG_BOOT_SAFE_MODE 0x00020000u
#define BOOTINFO_FLAG_BOOT_RESCUE_SHELL 0x00040000u
#define BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT 0x00100000u
#define BOOTINFO_FLAG_FORCE_LEGACY_VIDEO 0x00200000u
#define BOOTINFO_FLAG_EXPERIMENTAL_SMP 0x00400000u

struct bootinfo_vesa {
    uint16_t mode;
    uint16_t reserved0;
    uint32_t fb_addr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t reserved1;
} __attribute__((packed));

struct bootinfo_meminfo {
    uint32_t largest_base;
    uint32_t largest_size;
    uint32_t largest_end;
    uint32_t reserved;
} __attribute__((packed));

struct bootinfo_disk {
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
} __attribute__((packed));

struct bootinfo_video_mode {
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t reserved0;
} __attribute__((packed));

struct bootinfo_video_catalog {
    uint8_t mode_count;
    uint8_t active_index;
    uint8_t fallback_index;
    uint8_t selected_index;
    struct bootinfo_video_mode modes[BOOTINFO_MAX_VESA_MODES];
} __attribute__((packed));

struct bootinfo {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
    struct bootinfo_vesa vesa;
    struct bootinfo_meminfo meminfo;
    struct bootinfo_disk disk;
    struct bootinfo_video_catalog video;
} __attribute__((packed));

_Static_assert(offsetof(struct bootinfo, vesa) == 16u, "bootinfo vesa offset mismatch");
_Static_assert(offsetof(struct bootinfo, meminfo) == 32u, "bootinfo meminfo offset mismatch");
_Static_assert(offsetof(struct bootinfo, disk) == 48u, "bootinfo disk offset mismatch");
_Static_assert(offsetof(struct bootinfo, video) == 64u, "bootinfo video offset mismatch");
_Static_assert(sizeof(struct bootinfo_vesa) == 16u, "bootinfo_vesa size mismatch");
_Static_assert(sizeof(struct bootinfo_video_mode) == 8u, "bootinfo_video_mode size mismatch");
_Static_assert(sizeof(struct bootinfo_video_catalog) == (4u + (BOOTINFO_MAX_VESA_MODES * sizeof(struct bootinfo_video_mode))),
               "bootinfo_video_catalog size mismatch");
_Static_assert(sizeof(struct bootinfo) == (64u + sizeof(struct bootinfo_video_catalog)),
               "bootinfo size mismatch");

#endif
