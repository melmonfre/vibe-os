#include <kernel/drivers/video/video.h>

/* read information placed by the bootloader */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static volatile uint8_t *vesa_bytes = (volatile uint8_t *)0x500;
#pragma GCC diagnostic pop

int vesa_init(struct video_mode *mode) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
    uint16_t vesa_mode = (((uint16_t)vesa_bytes[1]) << 8) | vesa_bytes[0];
    if (vesa_mode == 0u) {
        return -1;
    }

    // PhysicalBasePointer is a 32-bit value stored at offset 2
    mode->fb_addr = (((uint32_t)vesa_bytes[5]) << 24) |
                    (((uint32_t)vesa_bytes[4]) << 16) |
                    (((uint32_t)vesa_bytes[3]) << 8)  |
                    vesa_bytes[2];
    mode->pitch   = (((uint16_t)vesa_bytes[7]) << 8) | vesa_bytes[6];
    mode->width   = (((uint16_t)vesa_bytes[9]) << 8) | vesa_bytes[8];
    mode->height  = (((uint16_t)vesa_bytes[11]) << 8) | vesa_bytes[10];
    mode->bpp     = vesa_bytes[12];
    #pragma GCC diagnostic pop
    if (mode->fb_addr == 0u || mode->width == 0u || mode->height == 0u || mode->pitch < mode->width) {
        return -1;
    }
    return 0;
}
