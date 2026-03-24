#include <kernel/bootinfo.h>
#include <kernel/drivers/video/video.h>

int vesa_init(struct video_mode *mode) {
    const volatile struct bootinfo *bootinfo;

    if (mode == 0) {
        return -1;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        bootinfo->vesa.mode != 0x0101u) {
        return -1;
    }

    mode->fb_addr = bootinfo->vesa.fb_addr;
    mode->pitch = bootinfo->vesa.pitch;
    mode->width = bootinfo->vesa.width;
    mode->height = bootinfo->vesa.height;
    mode->bpp = bootinfo->vesa.bpp;
    return 0;
}
