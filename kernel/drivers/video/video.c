#include <kernel/bootinfo.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/memory/heap.h>

#define GRAPHICS_MIN_WIDTH 640u
#define GRAPHICS_MIN_HEIGHT 480u
#define GRAPHICS_MAX_WIDTH 4096u
#define GRAPHICS_MAX_HEIGHT 2160u
#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u

#define VGA_PEL_MASK 0x3C6u
#define VGA_DAC_WRITE_INDEX 0x3C8u
#define VGA_DAC_DATA 0x3C9u

static struct video_mode g_mode;
static volatile uint8_t *g_fb = 0;
static uint8_t *g_backbuf = 0;
static uint8_t *g_shadow_backbuf = 0;
static size_t g_shadow_backbuf_capacity = 0u;
static uint8_t g_palette[256u * 3u];
static int g_palette_ready = 0;
static int g_graphics_enabled = 0;
static int g_video_initialized = 0;
static int g_backbuf_alloc_failed = 0;

extern int kernel_video_bios_set_mode(uint16_t mode);

static int kernel_video_has_graphics_mode(void) {
    return g_graphics_enabled &&
           g_fb != 0 &&
           g_backbuf != 0 &&
           g_mode.fb_addr >= GRAPHICS_MIN_FB_ADDR &&
           g_mode.width >= GRAPHICS_MIN_WIDTH &&
           g_mode.width <= GRAPHICS_MAX_WIDTH &&
           g_mode.height >= GRAPHICS_MIN_HEIGHT &&
           g_mode.height <= GRAPHICS_MAX_HEIGHT &&
           g_mode.bpp == GRAPHICS_BPP &&
           g_mode.pitch >= g_mode.width;
}

static int kernel_video_mode_usable(const struct video_mode *mode) {
    if (mode == 0) {
        return 0;
    }

    return mode->fb_addr >= GRAPHICS_MIN_FB_ADDR &&
           mode->width >= GRAPHICS_MIN_WIDTH &&
           mode->width <= GRAPHICS_MAX_WIDTH &&
           mode->height >= GRAPHICS_MIN_HEIGHT &&
           mode->height <= GRAPHICS_MAX_HEIGHT &&
           mode->bpp == GRAPHICS_BPP &&
           mode->pitch >= mode->width;
}

static uint32_t kernel_video_mode_bit(uint16_t width, uint16_t height) {
    if (width == 640u && height == 480u) {
        return VIDEO_RES_640X480;
    }
    if (width == 800u && height == 600u) {
        return VIDEO_RES_800X600;
    }
    if (width == 1024u && height == 768u) {
        return VIDEO_RES_1024X768;
    }
    if (width == 1360u && height == 720u) {
        return VIDEO_RES_1360X720;
    }
    if (width == 1920u && height == 1080u) {
        return VIDEO_RES_1920X1080;
    }
    return 0u;
}

static void kernel_video_add_mode_cap(struct video_capabilities *caps,
                                      uint16_t width,
                                      uint16_t height) {
    uint32_t i;

    if (caps == 0 || width == 0u || height == 0u) {
        return;
    }

    caps->supported_modes |= kernel_video_mode_bit(width, height);
    for (i = 0u; i < caps->mode_count; ++i) {
        if (caps->mode_width[i] == width && caps->mode_height[i] == height) {
            return;
        }
    }
    if (caps->mode_count >= VIDEO_MODE_LIST_MAX) {
        return;
    }

    caps->mode_width[caps->mode_count] = width;
    caps->mode_height[caps->mode_count] = height;
    caps->mode_count += 1u;
}

static void kernel_video_load_boot_catalog(struct video_capabilities *caps) {
    const volatile struct bootinfo *bootinfo;
    uint32_t mode_count;

    if (caps == 0) {
        return;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return;
    }

    mode_count = bootinfo->video.mode_count;
    if (mode_count > BOOTINFO_MAX_VESA_MODES) {
        mode_count = BOOTINFO_MAX_VESA_MODES;
    }

    for (uint32_t i = 0u; i < mode_count; ++i) {
        const volatile struct bootinfo_video_mode *mode = &bootinfo->video.modes[i];

        if (mode->width < GRAPHICS_MIN_WIDTH ||
            mode->height < GRAPHICS_MIN_HEIGHT ||
            mode->bpp != GRAPHICS_BPP) {
            continue;
        }
        kernel_video_add_mode_cap(caps, mode->width, mode->height);
    }
}

static int kernel_video_find_catalog_mode(uint32_t width,
                                          uint32_t height,
                                          uint8_t *index_out,
                                          uint16_t *mode_out) {
    const volatile struct bootinfo *bootinfo;
    uint32_t mode_count;

    if (index_out != 0) {
        *index_out = BOOTINFO_VIDEO_INDEX_NONE;
    }
    if (mode_out != 0) {
        *mode_out = 0u;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    mode_count = bootinfo->video.mode_count;
    if (mode_count > BOOTINFO_MAX_VESA_MODES) {
        mode_count = BOOTINFO_MAX_VESA_MODES;
    }

    for (uint32_t i = 0u; i < mode_count; ++i) {
        const volatile struct bootinfo_video_mode *mode = &bootinfo->video.modes[i];

        if ((uint32_t)mode->width != width ||
            (uint32_t)mode->height != height ||
            mode->bpp != GRAPHICS_BPP ||
            mode->mode == 0u) {
            continue;
        }

        if (index_out != 0) {
            *index_out = (uint8_t)i;
        }
        if (mode_out != 0) {
            *mode_out = mode->mode;
        }
        return 0;
    }

    return -1;
}

static void kernel_video_mark_catalog_active(uint8_t index) {
    volatile struct bootinfo *bootinfo;

    if (index == BOOTINFO_VIDEO_INDEX_NONE) {
        return;
    }

    bootinfo = (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return;
    }
    if ((uint32_t)index >= bootinfo->video.mode_count) {
        return;
    }

    bootinfo->video.active_index = index;
    bootinfo->video.selected_index = index;
}

static int kernel_video_backbuffer_is_lfb(void) {
    return g_backbuf != 0 && g_fb != 0 &&
           (uintptr_t)g_backbuf == (uintptr_t)g_fb;
}

static int kernel_video_heap_ready(void) {
    return kernel_heap_end() > kernel_heap_start();
}

static size_t kernel_video_frame_bytes(void) {
    if (!kernel_video_has_graphics_mode()) {
        return 0u;
    }
    return (size_t)g_mode.pitch * (size_t)g_mode.height;
}

static void kernel_video_copy_lfb_to_buffer(uint8_t *dst) {
    if (dst == 0 || g_fb == 0) {
        return;
    }

    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        size_t row_off = (size_t)y * g_mode.pitch;

        for (uint32_t x = 0u; x < g_mode.pitch; ++x) {
            dst[row_off + x] = g_fb[row_off + x];
        }
    }
}

static void kernel_video_bind_graphics_mode(const struct video_mode *mode) {
    size_t frame_bytes;

    if (!kernel_video_mode_usable(mode)) {
        return;
    }

    g_mode = *mode;
    g_fb = (volatile uint8_t *)(uintptr_t)mode->fb_addr;
    frame_bytes = (size_t)mode->pitch * (size_t)mode->height;
    if (g_shadow_backbuf != 0 && g_shadow_backbuf_capacity >= frame_bytes) {
        g_backbuf = g_shadow_backbuf;
    } else {
        g_backbuf = (uint8_t *)(uintptr_t)mode->fb_addr;
    }
    g_graphics_enabled = 1;
    g_backbuf_alloc_failed = 0;
}

static void kernel_video_try_alloc_backbuffer(void) {
    uint8_t *shadow;
    size_t frame_bytes;

    if (!kernel_video_has_graphics_mode() ||
        !kernel_video_backbuffer_is_lfb() ||
        g_backbuf_alloc_failed ||
        !kernel_video_heap_ready()) {
        return;
    }

    frame_bytes = kernel_video_frame_bytes();
    if (frame_bytes == 0u) {
        return;
    }

    if (g_shadow_backbuf != 0 && g_shadow_backbuf_capacity >= frame_bytes) {
        kernel_video_copy_lfb_to_buffer(g_shadow_backbuf);
        g_backbuf = g_shadow_backbuf;
        return;
    }

    shadow = (uint8_t *)kernel_malloc(frame_bytes);
    if (shadow == 0) {
        g_backbuf_alloc_failed = 1;
        kernel_debug_puts("video: shadow backbuffer alloc failed, staying direct-lfb\n");
        return;
    }

    kernel_video_copy_lfb_to_buffer(shadow);
    g_shadow_backbuf = shadow;
    g_shadow_backbuf_capacity = frame_bytes;
    g_backbuf = shadow;
    kernel_debug_printf("video: shadow backbuffer enabled bytes=%u\n",
                        (unsigned int)frame_bytes);
}

static void kernel_video_program_palette(void) {
    if (!g_palette_ready || !kernel_video_has_graphics_mode()) {
        return;
    }

    outb(VGA_PEL_MASK, 0xFFu);
    outb(VGA_DAC_WRITE_INDEX, 0u);
    for (int i = 0; i < 256 * 3; ++i) {
        outb(VGA_DAC_DATA, (uint8_t)(g_palette[i] >> 2));
    }
}

static void kernel_video_load_default_palette(void) {
    static const uint8_t ega16[16][3] = {
        {0x00u, 0x00u, 0x00u},
        {0x00u, 0x00u, 0xAAu},
        {0x00u, 0xAAu, 0x00u},
        {0x00u, 0xAAu, 0xAAu},
        {0xAAu, 0x00u, 0x00u},
        {0xAAu, 0x00u, 0xAAu},
        {0xAAu, 0x55u, 0x00u},
        {0xAAu, 0xAAu, 0xAAu},
        {0x55u, 0x55u, 0x55u},
        {0x55u, 0x55u, 0xFFu},
        {0x55u, 0xFFu, 0x55u},
        {0x55u, 0xFFu, 0xFFu},
        {0xFFu, 0x55u, 0x55u},
        {0xFFu, 0x55u, 0xFFu},
        {0xFFu, 0xFFu, 0x55u},
        {0xFFu, 0xFFu, 0xFFu}
    };

    for (int i = 0; i < 16; ++i) {
        g_palette[i * 3 + 0] = ega16[i][0];
        g_palette[i * 3 + 1] = ega16[i][1];
        g_palette[i * 3 + 2] = ega16[i][2];
    }
    for (int i = 16; i < 256; ++i) {
        g_palette[i * 3 + 0] = (uint8_t)((((unsigned)i >> 5) & 0x07u) * 255u / 7u);
        g_palette[i * 3 + 1] = (uint8_t)((((unsigned)i >> 2) & 0x07u) * 255u / 7u);
        g_palette[i * 3 + 2] = (uint8_t)(((unsigned)i & 0x03u) * 255u / 3u);
    }
    g_palette_ready = 1;
    kernel_video_program_palette();
}

static void kernel_video_enter_graphics(void) {
    kernel_video_try_alloc_backbuffer();
}

void kernel_video_init(void) {
    struct video_mode boot_mode;

    if (g_video_initialized) {
        return;
    }

    if (vesa_init(&boot_mode) == 0 && kernel_video_mode_usable(&boot_mode)) {
        kernel_video_bind_graphics_mode(&boot_mode);
        kernel_video_load_default_palette();
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_debug_printf("video: VESA %dx%dx%d fb=%x pitch=%d direct-lfb\n",
                            (int)boot_mode.width,
                            (int)boot_mode.height,
                            (int)boot_mode.bpp,
                            (unsigned int)g_mode.fb_addr,
                            (int)g_mode.pitch);
        g_video_initialized = 1;
        return;
    }

    /* stay in BIOS text mode until a graphical syscall is used */
    g_mode.fb_addr = 0xB8000;
    g_mode.width = 80;
    g_mode.height = 25;
    g_mode.pitch = 160; /* 80 cols * 2 bytes */
    g_mode.bpp = 16;
    g_fb = (volatile uint8_t *)g_mode.fb_addr;
    g_backbuf = 0;
    g_video_initialized = 1;
    kernel_debug_puts("video: VESA 640x480x8 unavailable, staying in text mode\n");
}

struct video_mode *kernel_video_get_mode(void) {
    return &g_mode;
}

volatile uint8_t *kernel_video_get_fb(void) {
    return g_fb;
}

uint8_t *kernel_video_get_backbuffer(void) {
    kernel_video_try_alloc_backbuffer();
    return g_backbuf;
}

size_t kernel_video_get_pixel_count(void) {
    if (!kernel_video_has_graphics_mode()) {
        return 0u;
    }
    return (size_t)g_mode.width * (size_t)g_mode.height;
}

void kernel_video_clear(uint8_t color) {
    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode()) {
        (void)color;
        return;
    }

    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        uint8_t *row = g_backbuf + ((size_t)y * g_mode.pitch);

        for (uint32_t x = 0u; x < g_mode.width; ++x) {
            row[x] = color;
        }
    }
}

void kernel_video_flip(void) {
    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode() || g_fb == 0 || kernel_video_backbuffer_is_lfb()) {
        return;
    }

    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        uint8_t *src_row = g_backbuf + ((size_t)y * g_mode.pitch);
        volatile uint8_t *dst_row = g_fb + ((size_t)y * g_mode.pitch);

        for (uint32_t x = 0u; x < g_mode.width; ++x) {
            dst_row[x] = src_row[x];
        }
    }
}

void kernel_video_leave_graphics(void) {
    if (kernel_video_has_graphics_mode()) {
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_text_init();
    }
}

int kernel_video_set_mode(uint32_t width, uint32_t height) {
    struct video_mode new_mode;
    uint8_t mode_index;
    uint16_t bios_mode;
    uint32_t irq_flags;
    int switch_rc;

    if (!kernel_video_has_graphics_mode()) {
        return -1;
    }

    if (width == g_mode.width && height == g_mode.height) {
        kernel_video_load_default_palette();
        return 0;
    }

    if (kernel_video_find_catalog_mode(width, height, &mode_index, &bios_mode) != 0) {
        return -1;
    }

    irq_flags = kernel_irq_save();
    kernel_keyboard_prepare_for_graphics();
    kernel_mouse_prepare_for_graphics();
    switch_rc = kernel_video_bios_set_mode(bios_mode);
    if (switch_rc == 0 &&
        vesa_init(&new_mode) == 0 &&
        kernel_video_mode_usable(&new_mode)) {
        kernel_video_mark_catalog_active(mode_index);
        kernel_video_bind_graphics_mode(&new_mode);
        kernel_video_load_default_palette();
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_mouse_sync_to_video();
        kernel_irq_restore(irq_flags);
        kernel_debug_printf("video: runtime mode %dx%dx%d fb=%x pitch=%d\n",
                            (int)new_mode.width,
                            (int)new_mode.height,
                            (int)new_mode.bpp,
                            (unsigned int)new_mode.fb_addr,
                            (int)new_mode.pitch);
        return 0;
    }
    kernel_irq_restore(irq_flags);
    kernel_debug_printf("video: runtime mode switch failed for %ux%u\n",
                        (unsigned int)width,
                        (unsigned int)height);
    return -1;
}

void kernel_video_get_capabilities(struct video_capabilities *caps) {
    if (caps == 0) {
        return;
    }

    caps->flags = 0u;
    caps->supported_modes = 0u;
    caps->active_width = g_mode.width;
    caps->active_height = g_mode.height;
    caps->active_bpp = g_mode.bpp;
    caps->mode_count = 0u;
    for (uint32_t i = 0u; i < VIDEO_MODE_LIST_MAX; ++i) {
        caps->mode_width[i] = 0u;
        caps->mode_height[i] = 0u;
    }

    if (kernel_video_has_graphics_mode()) {
        caps->flags |= VIDEO_CAPS_BOOT_LFB;
        kernel_video_load_boot_catalog(caps);
        if (caps->mode_count != 0u) {
            caps->flags |= VIDEO_CAPS_CAN_SET_MODE;
        }
        kernel_video_add_mode_cap(caps, (uint16_t)g_mode.width, (uint16_t)g_mode.height);
    } else {
        caps->flags |= VIDEO_CAPS_TEXT_ONLY;
        caps->supported_modes = 0u;
    }
}

/* graphics helper internal font & routines copied from stage2 */

static char uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static uint8_t font_row_bits(char c, int row) {
    c = uppercase_char(c);
    /* the big switch from stage2/graphics.c */
    switch (c) {
        case 'A': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'B': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
        case 'C': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
        case 'D': { static const uint8_t g[7] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; return g[row]; }
        case 'E': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
        case 'F': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'G': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g[row]; }
        case 'H': { static const uint8_t g[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'I': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; return g[row]; }
        case 'J': { static const uint8_t g[7] = {0x1F,0x02,0x02,0x02,0x12,0x12,0x0C}; return g[row]; }
        case 'K': { static const uint8_t g[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
        case 'L': { static const uint8_t g[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
        case 'M': { static const uint8_t g[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
        case 'N': { static const uint8_t g[7] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11}; return g[row]; }
        case 'O': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'P': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'Q': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g[row]; }
        case 'R': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
        case 'S': { static const uint8_t g[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
        case 'T': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
        case 'U': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'V': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
        case 'W': { static const uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
        case 'X': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
        case 'Y': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
        case 'Z': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
        case '0': { static const uint8_t g[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g[row]; }
        case '1': { static const uint8_t g[7] = {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F}; return g[row]; }
        case '2': { static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g[row]; }
        case '3': { static const uint8_t g[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return g[row]; }
        case '4': { static const uint8_t g[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
        case '5': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return g[row]; }
        case '6': { static const uint8_t g[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
        case '7': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
        case '8': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
        case '9': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return g[row]; }
        case '>': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x04,0x08,0x10}; return g[row]; }
        case '<': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01}; return g[row]; }
        case ':': { static const uint8_t g[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return g[row]; }
        case '-': { static const uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
        case '_': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}; return g[row]; }
        case '.': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g[row]; }
        case '/': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0x00,0x00}; return g[row]; }
        case '\\': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0x00,0x00}; return g[row]; }
        case '[': { static const uint8_t g[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return g[row]; }
        case ']': { static const uint8_t g[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return g[row]; }
        case '=': { static const uint8_t g[7] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}; return g[row]; }
        case '+': { static const uint8_t g[7] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; return g[row]; }
        case '(':{ static const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
        case ')':{ static const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
        case '?':{ static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; return g[row]; }
        case '!':{ static const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g[row]; }
        case ' ': return 0x00;
        default: { static const uint8_t g[7] = {0x1F,0x01,0x05,0x09,0x11,0x00,0x11}; return g[row]; }
    }
}

void kernel_gfx_putpixel(int x, int y, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();

    if (!bb) return;
    if (x < 0 || y < 0 || x >= (int)mode->width || y >= (int)mode->height) return;
    bb[(y * mode->pitch) + x] = color;
}

void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();
    if (!bb || w <= 0 || h <= 0) return;
    int x0 = x<0?0:x;
    int y0 = y<0?0:y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)mode->width) x1 = (int)mode->width;
    if (y1 > (int)mode->height) y1 = (int)mode->height;
    for (int py=y0; py<y1; ++py)
        for (int px=x0; px<x1; ++px)
            bb[(py * mode->pitch) + px] = color;
}

void kernel_gfx_clear(uint8_t color) {
    kernel_video_enter_graphics();
    kernel_video_clear(color);
}

void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color) {
    kernel_video_enter_graphics();
    int cx = x, cy = y;
    while (*text) {
        char c = *text++;
        if (c=='\n') { cx = x; cy += 8; continue; }
        /* draw char */
        for (int row=0; row<7; ++row) {
            uint8_t bits = font_row_bits(c,row);
            for (int col=0; col<5; ++col) {
                if (!(bits & (1u << (4-col)))) continue;
                int px = cx + col;
                int py = cy + row;
                if (px<0||py<0||px>= (int)g_mode.width||py>=(int)g_mode.height) continue;
                g_backbuf[(py * g_mode.pitch) + px] = color;
            }
        }
        cx += 6;
    }
}

void kernel_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct video_mode *mode;
    uint8_t *bb;

    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }

    mode = kernel_video_get_mode();
    bb = kernel_video_get_backbuffer();
    if (mode == NULL || bb == NULL) {
        return;
    }

    for (int sy = 0; sy < src_h; ++sy) {
        int py0 = dst_y + (sy * scale);
        for (int sx = 0; sx < src_w; ++sx) {
            int px0 = dst_x + (sx * scale);
            uint8_t color = src[(sy * src_w) + sx];

            for (int oy = 0; oy < scale; ++oy) {
                int py = py0 + oy;
                if (py < 0 || py >= (int)mode->height) {
                    continue;
                }
                for (int ox = 0; ox < scale; ++ox) {
                    int px = px0 + ox;
                    if (px < 0 || px >= (int)mode->width) {
                        continue;
                    }
                    bb[(py * mode->pitch) + px] = color;
                }
            }
        }
    }
}

int kernel_video_set_palette(const uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL || !kernel_video_has_graphics_mode()) {
        return -1;
    }

    for (int i = 0; i < 256 * 3; ++i) {
        g_palette[i] = rgb_triplets[i];
    }
    g_palette_ready = 1;
    kernel_video_program_palette();
    return 0;
}

int kernel_video_get_palette(uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL || !kernel_video_has_graphics_mode()) {
        return -1;
    }

    if (!g_palette_ready) {
        kernel_video_load_default_palette();
    }
    for (int i = 0; i < 256 * 3; ++i) {
        rgb_triplets[i] = g_palette[i];
    }
    return 0;
}
