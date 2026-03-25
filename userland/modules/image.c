#include <stdlib.h>
#include <string.h>

#include <userland/applications/games/craft/upstream/deps/lodepng/lodepng.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/utils.h>

enum image_scale_mode {
    IMAGE_SCALE_FIT = 0,
    IMAGE_SCALE_COVER = 1,
    IMAGE_SCALE_STRETCH = 2
};

static int image_name_has_extension(const char *name, const char *ext) {
    int name_len;
    int ext_len;

    if (!name || !ext) {
        return 0;
    }

    name_len = str_len(name);
    ext_len = str_len(ext);
    if (ext_len <= 0 || name_len <= ext_len) {
        return 0;
    }
    return str_eq_ci(name + name_len - ext_len, ext);
}

static uint8_t image_nearest_palette(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_dist = 0xFFFFFFFFu;
    uint8_t best = 0u;

    for (int i = 0; i < 256; ++i) {
        uint8_t pr;
        uint8_t pg;
        uint8_t pb;
        int dr;
        int dg;
        int db;
        uint32_t dist;

        bmp_palette_color((uint8_t)i, &pr, &pg, &pb);
        dr = (int)r - (int)pr;
        dg = (int)g - (int)pg;
        db = (int)b - (int)pb;
        dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best = (uint8_t)i;
        }
    }

    return best;
}

static int image_decode_png_to_palette_internal(const uint8_t *data, int size,
                                                uint8_t *out_pixels, int out_stride,
                                                int target_w_limit, int target_h_limit,
                                                int *out_w, int *out_h,
                                                enum image_scale_mode mode) {
    unsigned char *rgba = 0;
    unsigned width = 0;
    unsigned height = 0;
    int target_w;
    int target_h;
    unsigned sample_w;
    unsigned sample_h;
    unsigned sample_x0;
    unsigned sample_y0;

    if (lodepng_decode32(&rgba, &width, &height, data, (size_t)size) != 0 || !rgba) {
        return -1;
    }
    if (width == 0u || height == 0u) {
        free(rgba);
        return -1;
    }

    target_w = (int)width;
    target_h = (int)height;
    if (mode != IMAGE_SCALE_FIT) {
        if (target_w_limit <= 0 || target_h_limit <= 0) {
            free(rgba);
            return -1;
        }
        target_w = target_w_limit;
        target_h = target_h_limit;
    } else {
        if (target_w > target_w_limit) {
            target_h = (target_h * target_w_limit) / target_w;
            target_w = target_w_limit;
        }
        if (target_h > target_h_limit) {
            target_w = (target_w * target_h_limit) / target_h;
            target_h = target_h_limit;
        }
    }
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;

    if (mode == IMAGE_SCALE_COVER) {
        if ((uint64_t)width * (uint64_t)target_h > (uint64_t)height * (uint64_t)target_w) {
            sample_w = (unsigned)(((uint64_t)height * (uint64_t)target_w) / (uint64_t)target_h);
            sample_h = height;
            sample_x0 = (width - sample_w) / 2u;
            sample_y0 = 0u;
        } else {
            sample_w = width;
            sample_h = (unsigned)(((uint64_t)width * (uint64_t)target_h) / (uint64_t)target_w);
            sample_x0 = 0u;
            sample_y0 = (height - sample_h) / 2u;
        }
        if (sample_w == 0u) sample_w = 1u;
        if (sample_h == 0u) sample_h = 1u;
    } else {
        sample_w = width;
        sample_h = height;
        sample_x0 = 0u;
        sample_y0 = 0u;
    }

    for (int y = 0; y < target_h; ++y) {
        unsigned src_y = sample_y0 + (((unsigned)y * sample_h) / (unsigned)target_h);

        for (int x = 0; x < target_w; ++x) {
            unsigned src_x = sample_x0 + (((unsigned)x * sample_w) / (unsigned)target_w);
            const unsigned char *px = rgba + ((((size_t)src_y * width) + src_x) * 4u);
            uint8_t a = px[3];
            uint8_t r = (uint8_t)(((unsigned)px[0] * a) / 255u);
            uint8_t g = (uint8_t)(((unsigned)px[1] * a) / 255u);
            uint8_t b = (uint8_t)(((unsigned)px[2] * a) / 255u);

            out_pixels[y * out_stride + x] = image_nearest_palette(r, g, b);
        }
    }

    free(rgba);
    *out_w = target_w;
    *out_h = target_h;
    return 0;
}

static int image_decode_png_to_palette(const uint8_t *data, int size,
                                       uint8_t *out_pixels, int out_stride,
                                       int max_w, int max_h,
                                       int *out_w, int *out_h) {
    return image_decode_png_to_palette_internal(data,
                                                size,
                                                out_pixels,
                                                out_stride,
                                                max_w,
                                                max_h,
                                                out_w,
                                                out_h,
                                                IMAGE_SCALE_FIT);
}

static int image_decode_png_to_palette_cover(const uint8_t *data, int size,
                                             uint8_t *out_pixels, int out_stride,
                                             int target_w, int target_h,
                                             int *out_w, int *out_h) {
    return image_decode_png_to_palette_internal(data,
                                                size,
                                                out_pixels,
                                                out_stride,
                                                target_w,
                                                target_h,
                                                out_w,
                                                out_h,
                                                IMAGE_SCALE_COVER);
}

static int image_decode_png_to_palette_stretch(const uint8_t *data, int size,
                                               uint8_t *out_pixels, int out_stride,
                                               int target_w, int target_h,
                                               int *out_w, int *out_h) {
    return image_decode_png_to_palette_internal(data,
                                                size,
                                                out_pixels,
                                                out_stride,
                                                target_w,
                                                target_h,
                                                out_w,
                                                out_h,
                                                IMAGE_SCALE_STRETCH);
}

int image_decode_to_palette(const uint8_t *data, int size,
                            uint8_t *out_pixels, int out_stride,
                            int max_w, int max_h,
                            int *out_w, int *out_h) {
    static const uint8_t png_signature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};

    if (!data || size <= 0 || !out_pixels || !out_w || !out_h) {
        return -1;
    }

    if (bmp_decode_to_palette(data, size, out_pixels, out_stride, max_w, max_h, out_w, out_h) == 0) {
        return 0;
    }
    if (size >= (int)sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0 &&
        image_decode_png_to_palette(data, size, out_pixels, out_stride, max_w, max_h, out_w, out_h) == 0) {
        return 0;
    }
    return -1;
}

int image_decode_to_palette_cover(const uint8_t *data, int size,
                                  uint8_t *out_pixels, int out_stride,
                                  int target_w, int target_h,
                                  int *out_w, int *out_h) {
    static const uint8_t png_signature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};

    if (!data || size <= 0 || !out_pixels || !out_w || !out_h) {
        return -1;
    }

    if (bmp_decode_to_palette_cover(data, size, out_pixels, out_stride, target_w, target_h, out_w, out_h) == 0) {
        return 0;
    }
    if (size >= (int)sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0 &&
        image_decode_png_to_palette_cover(data, size, out_pixels, out_stride, target_w, target_h, out_w, out_h) == 0) {
        return 0;
    }
    return -1;
}

int image_decode_to_palette_stretch(const uint8_t *data, int size,
                                    uint8_t *out_pixels, int out_stride,
                                    int target_w, int target_h,
                                    int *out_w, int *out_h) {
    static const uint8_t png_signature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};

    if (!data || size <= 0 || !out_pixels || !out_w || !out_h) {
        return -1;
    }

    if (bmp_decode_to_palette_stretch(data, size, out_pixels, out_stride, target_w, target_h, out_w, out_h) == 0) {
        return 0;
    }
    if (size >= (int)sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0 &&
        image_decode_png_to_palette_stretch(data, size, out_pixels, out_stride, target_w, target_h, out_w, out_h) == 0) {
        return 0;
    }
    return -1;
}

int image_decode_node_to_palette(int node,
                                 uint8_t *out_pixels,
                                 int out_stride,
                                 int max_w,
                                 int max_h,
                                 int *out_w,
                                 int *out_h) {
    uint8_t *buffer;
    int bytes_read;
    int rc;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (g_fs_nodes[node].size <= 0) {
        return -1;
    }

    buffer = (uint8_t *)malloc((size_t)g_fs_nodes[node].size);
    if (!buffer) {
        return -1;
    }

    bytes_read = fs_read_node_bytes(node, 0, buffer, g_fs_nodes[node].size);
    if (bytes_read != g_fs_nodes[node].size) {
        free(buffer);
        return -1;
    }

    rc = image_decode_to_palette(buffer, bytes_read, out_pixels, out_stride, max_w, max_h, out_w, out_h);
    free(buffer);
    return rc;
}

int image_decode_node_to_palette_cover(int node,
                                       uint8_t *out_pixels,
                                       int out_stride,
                                       int target_w,
                                       int target_h,
                                       int *out_w,
                                       int *out_h) {
    uint8_t *buffer;
    int bytes_read;
    int rc;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (g_fs_nodes[node].size <= 0) {
        return -1;
    }

    buffer = (uint8_t *)malloc((size_t)g_fs_nodes[node].size);
    if (!buffer) {
        return -1;
    }

    bytes_read = fs_read_node_bytes(node, 0, buffer, g_fs_nodes[node].size);
    if (bytes_read != g_fs_nodes[node].size) {
        free(buffer);
        return -1;
    }

    rc = image_decode_to_palette_cover(buffer, bytes_read, out_pixels, out_stride, target_w, target_h, out_w, out_h);
    free(buffer);
    return rc;
}

int image_decode_node_to_palette_stretch(int node,
                                         uint8_t *out_pixels,
                                         int out_stride,
                                         int target_w,
                                         int target_h,
                                         int *out_w,
                                         int *out_h) {
    uint8_t *buffer;
    int bytes_read;
    int rc;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (g_fs_nodes[node].size <= 0) {
        return -1;
    }

    buffer = (uint8_t *)malloc((size_t)g_fs_nodes[node].size);
    if (!buffer) {
        return -1;
    }

    bytes_read = fs_read_node_bytes(node, 0, buffer, g_fs_nodes[node].size);
    if (bytes_read != g_fs_nodes[node].size) {
        free(buffer);
        return -1;
    }

    rc = image_decode_to_palette_stretch(buffer, bytes_read, out_pixels, out_stride, target_w, target_h, out_w, out_h);
    free(buffer);
    return rc;
}

int image_node_is_supported(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return 0;
    }

    return image_name_has_extension(g_fs_nodes[node].name, ".bmp") ||
           image_name_has_extension(g_fs_nodes[node].name, ".png");
}
