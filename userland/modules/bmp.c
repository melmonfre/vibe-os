#include <userland/modules/include/bmp.h>

static uint16_t bmp_read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t bmp_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t bmp_read_s32(const uint8_t *p) {
    return (int32_t)bmp_read_u32(p);
}

void bmp_palette_color(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b) {
    static const uint8_t palette[20][3] = {
        {0, 0, 0},
        {0, 0, 170},
        {0, 170, 0},
        {0, 170, 170},
        {170, 0, 0},
        {170, 0, 170},
        {170, 85, 0},
        {170, 170, 170},
        {85, 85, 85},
        {85, 85, 255},
        {85, 255, 85},
        {85, 255, 255},
        {255, 85, 85},
        {255, 85, 255},
        {255, 255, 85},
        {255, 255, 255},
        {255, 170, 170},
        {0, 136, 136},
        {255, 170, 85},
        {136, 0, 136}
    };

    if (index < (uint8_t)(sizeof(palette) / sizeof(palette[0]))) {
        *r = palette[index][0];
        *g = palette[index][1];
        *b = palette[index][2];
        return;
    }

    *r = index;
    *g = index;
    *b = index;
}

static uint8_t bmp_nearest_palette(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_dist = 0xFFFFFFFFu;
    uint8_t best = 0;

    for (uint8_t i = 0; i < 20u; ++i) {
        uint8_t pr;
        uint8_t pg;
        uint8_t pb;
        int dr;
        int dg;
        int db;
        uint32_t dist;

        bmp_palette_color(i, &pr, &pg, &pb);
        dr = (int)r - (int)pr;
        dg = (int)g - (int)pg;
        db = (int)b - (int)pb;
        dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    return best;
}

int bmp_decode_to_palette(const uint8_t *data, int size,
                          uint8_t *out_pixels, int out_stride,
                          int max_w, int max_h,
                          int *out_w, int *out_h) {
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t src_w;
    int32_t src_h_raw;
    int src_h;
    int top_down;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t colors_used;
    int target_w;
    int target_h;
    int row_size;

    if (data == 0 || out_pixels == 0 || out_w == 0 || out_h == 0 || size < 54) {
        return -1;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        return -1;
    }

    pixel_offset = bmp_read_u32(data + 10);
    dib_size = bmp_read_u32(data + 14);
    if (dib_size < 40u || size < (14 + (int)dib_size)) {
        return -1;
    }

    src_w = bmp_read_s32(data + 18);
    src_h_raw = bmp_read_s32(data + 22);
    planes = bmp_read_u16(data + 26);
    bpp = bmp_read_u16(data + 28);
    compression = bmp_read_u32(data + 30);
    colors_used = bmp_read_u32(data + 46);

    if (src_w <= 0 || src_h_raw == 0 || planes != 1u || compression != 0u) {
        return -1;
    }
    if (bpp != 8u && bpp != 24u) {
        return -1;
    }

    top_down = src_h_raw < 0;
    src_h = src_h_raw < 0 ? -src_h_raw : src_h_raw;
    target_w = src_w;
    target_h = src_h;

    if (target_w > max_w) {
        target_h = (target_h * max_w) / target_w;
        target_w = max_w;
    }
    if (target_h > max_h) {
        target_w = (target_w * max_h) / target_h;
        target_h = max_h;
    }
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;

    row_size = (int)((((uint32_t)src_w * (uint32_t)bpp) + 31u) / 32u) * 4;
    if ((int)pixel_offset + (row_size * src_h) > size) {
        return -1;
    }
    if (bpp == 8u) {
        uint32_t palette_entries = colors_used == 0u ? 256u : colors_used;
        if (14 + (int)dib_size + (int)(palette_entries * 4u) > size) {
            return -1;
        }
    }

    for (int y = 0; y < target_h; ++y) {
        int src_y = (y * src_h) / target_h;
        int file_y = top_down ? src_y : (src_h - 1 - src_y);
        const uint8_t *row = data + pixel_offset + (file_y * row_size);

        for (int x = 0; x < target_w; ++x) {
            int src_x = (x * src_w) / target_w;
            uint8_t r;
            uint8_t g;
            uint8_t b;

            if (bpp == 8u) {
                uint8_t pal_index = row[src_x];
                const uint8_t *entry = data + 14 + dib_size + ((uint32_t)pal_index * 4u);
                b = entry[0];
                g = entry[1];
                r = entry[2];
            } else {
                const uint8_t *px = row + (src_x * 3);
                b = px[0];
                g = px[1];
                r = px[2];
            }

            out_pixels[y * out_stride + x] = bmp_nearest_palette(r, g, b);
        }
    }

    *out_w = target_w;
    *out_h = target_h;
    return 0;
}

int bmp_encode_8bit(const uint8_t *pixels, int width, int height, int stride,
                    uint8_t *out, int out_cap) {
    int row_size;
    int pixel_bytes;
    int total_size;

    if (pixels == 0 || out == 0 || width <= 0 || height <= 0 || stride < width) {
        return -1;
    }

    row_size = (width + 3) & ~3;
    pixel_bytes = row_size * height;
    total_size = 14 + 40 + (256 * 4) + pixel_bytes;
    if (total_size > out_cap) {
        return -1;
    }

    for (int i = 0; i < total_size; ++i) {
        out[i] = 0u;
    }

    out[0] = 'B';
    out[1] = 'M';
    out[2] = (uint8_t)(total_size & 0xFF);
    out[3] = (uint8_t)((total_size >> 8) & 0xFF);
    out[4] = (uint8_t)((total_size >> 16) & 0xFF);
    out[5] = (uint8_t)((total_size >> 24) & 0xFF);
    out[10] = 14 + 40;
    out[11] = (uint8_t)(((14 + 40) >> 8) & 0xFF);
    out[14] = 40;
    out[18] = (uint8_t)(width & 0xFF);
    out[19] = (uint8_t)((width >> 8) & 0xFF);
    out[20] = (uint8_t)((width >> 16) & 0xFF);
    out[21] = (uint8_t)((width >> 24) & 0xFF);
    out[22] = (uint8_t)(height & 0xFF);
    out[23] = (uint8_t)((height >> 8) & 0xFF);
    out[24] = (uint8_t)((height >> 16) & 0xFF);
    out[25] = (uint8_t)((height >> 24) & 0xFF);
    out[26] = 1;
    out[28] = 8;
    out[34] = (uint8_t)(pixel_bytes & 0xFF);
    out[35] = (uint8_t)((pixel_bytes >> 8) & 0xFF);
    out[36] = (uint8_t)((pixel_bytes >> 16) & 0xFF);
    out[37] = (uint8_t)((pixel_bytes >> 24) & 0xFF);
    out[46] = 0;
    out[47] = 1;
    out[50] = 0;
    out[51] = 1;

    for (int i = 0; i < 256; ++i) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        int pal = 14 + 40 + (i * 4);

        bmp_palette_color((uint8_t)i, &r, &g, &b);
        out[pal + 0] = b;
        out[pal + 1] = g;
        out[pal + 2] = r;
        out[pal + 3] = 0u;
    }

    out[10] = (uint8_t)((14 + 40 + (256 * 4)) & 0xFF);
    out[11] = (uint8_t)(((14 + 40 + (256 * 4)) >> 8) & 0xFF);

    for (int y = 0; y < height; ++y) {
        int dst_y = height - 1 - y;
        uint8_t *row = out + 14 + 40 + (256 * 4) + (dst_y * row_size);

        for (int x = 0; x < width; ++x) {
            row[x] = pixels[y * stride + x];
        }
    }

    return total_size;
}
