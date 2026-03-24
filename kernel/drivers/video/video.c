#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>

#define VIDEO_MODE_VESA_640X480X8 0x0101u
#define GRAPHICS_WIDTH 640u
#define GRAPHICS_HEIGHT 480u
#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u
#define GRAPHICS_BUF_SIZE ((size_t)GRAPHICS_WIDTH * (size_t)GRAPHICS_HEIGHT)

#define VGA_PEL_MASK 0x3C6u
#define VGA_DAC_WRITE_INDEX 0x3C8u
#define VGA_DAC_DATA 0x3C9u

static struct video_mode g_mode;
static volatile uint8_t *g_fb = 0;
static uint8_t *g_backbuf = 0;
static uint32_t g_fb_pitch = 0u;
static uint8_t g_palette[256u * 3u];
static int g_palette_ready = 0;
static int g_graphics_enabled = 0;
static int g_video_initialized = 0;
static uint8_t g_static_backbuf[GRAPHICS_BUF_SIZE];

static int kernel_video_has_fixed_mode(void) {
    return g_graphics_enabled &&
           g_mode.width == GRAPHICS_WIDTH &&
           g_mode.height == GRAPHICS_HEIGHT &&
           g_mode.bpp == GRAPHICS_BPP &&
           g_backbuf != 0;
}

static int kernel_video_mode_usable(const struct video_mode *mode) {
    if (mode == 0) {
        return 0;
    }

    return mode->fb_addr >= GRAPHICS_MIN_FB_ADDR &&
           mode->width == GRAPHICS_WIDTH &&
           mode->height == GRAPHICS_HEIGHT &&
           mode->bpp == GRAPHICS_BPP &&
           mode->pitch >= GRAPHICS_WIDTH;
}

static void kernel_video_program_palette(void) {
    if (!g_palette_ready || !kernel_video_has_fixed_mode()) {
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
    (void)g_graphics_enabled;
}

void kernel_video_init(void) {
    struct video_mode boot_mode;

    if (g_video_initialized) {
        return;
    }

    if (vesa_init(&boot_mode) == 0 && kernel_video_mode_usable(&boot_mode)) {
        g_mode.fb_addr = boot_mode.fb_addr;
        g_mode.width = GRAPHICS_WIDTH;
        g_mode.height = GRAPHICS_HEIGHT;
        g_mode.pitch = GRAPHICS_WIDTH;
        g_mode.bpp = GRAPHICS_BPP;
        g_fb = (volatile uint8_t *)(uintptr_t)boot_mode.fb_addr;
        g_fb_pitch = boot_mode.pitch;
        g_backbuf = g_static_backbuf;
        g_graphics_enabled = 1;
        kernel_video_load_default_palette();
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_debug_printf("video: fixed VESA 640x480x8 fb=%x pitch=%d\n",
                            (unsigned int)g_mode.fb_addr,
                            (int)g_fb_pitch);
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
    g_fb_pitch = 160u;
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
    return g_backbuf;
}

size_t kernel_video_get_pixel_count(void) {
    return kernel_video_has_fixed_mode() ? GRAPHICS_BUF_SIZE : 0u;
}

void kernel_video_clear(uint8_t color) {
    if (!kernel_video_has_fixed_mode()) {
        (void)color;
        return;
    }

    for (size_t i = 0; i < GRAPHICS_BUF_SIZE; ++i) {
        g_backbuf[i] = color;
    }
}

void kernel_video_flip(void) {
    if (!kernel_video_has_fixed_mode() || g_fb == 0) {
        return;
    }

    for (uint32_t y = 0u; y < GRAPHICS_HEIGHT; ++y) {
        uint32_t src_row = y * GRAPHICS_WIDTH;
        uint32_t dst_row = y * g_fb_pitch;

        for (uint32_t x = 0u; x < GRAPHICS_WIDTH; ++x) {
            g_fb[dst_row + x] = g_backbuf[src_row + x];
        }
    }
}

void kernel_video_leave_graphics(void) {
    if (kernel_video_has_fixed_mode()) {
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_text_init();
    }
}

int kernel_video_set_mode(uint32_t width, uint32_t height) {
    if (width != GRAPHICS_WIDTH || height != GRAPHICS_HEIGHT || !kernel_video_has_fixed_mode()) {
        return -1;
    }
    kernel_video_load_default_palette();
    return 0;
}

void kernel_video_get_capabilities(struct video_capabilities *caps) {
    if (caps == 0) {
        return;
    }

    caps->flags = 0u;
    caps->supported_modes = VIDEO_RES_640X480;
    caps->active_width = g_mode.width;
    caps->active_height = g_mode.height;
    caps->active_bpp = g_mode.bpp;
    caps->mode_count = 0u;
    for (uint32_t i = 0u; i < VIDEO_MODE_LIST_MAX; ++i) {
        caps->mode_width[i] = 0u;
        caps->mode_height[i] = 0u;
    }

    if (kernel_video_has_fixed_mode()) {
        caps->flags |= VIDEO_CAPS_BOOT_LFB;
        caps->mode_count = 1u;
        caps->mode_width[0] = GRAPHICS_WIDTH;
        caps->mode_height[0] = GRAPHICS_HEIGHT;
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
    if (rgb_triplets == NULL || !kernel_video_has_fixed_mode()) {
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
    if (rgb_triplets == NULL || !kernel_video_has_fixed_mode()) {
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
