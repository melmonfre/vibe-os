#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <userland/applications/games/craft/upstream/deps/lodepng/lodepng.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/icon_theme.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

#define ICON_THEME_CACHE_ENTRIES 48
#define ICON_THEME_MAX_DIM 64
#define ICON_THEME_MAX_THEME_NAME 32
#define ICON_THEME_MAX_SIZES_PER_CONTEXT 16

struct icon_theme_cache_entry {
    uint8_t used;
    uint8_t width;
    uint8_t height;
    uint16_t node;
    uint32_t stamp;
    uint8_t pixels[ICON_THEME_MAX_DIM * ICON_THEME_MAX_DIM];
    uint8_t opaque[ICON_THEME_MAX_DIM * ICON_THEME_MAX_DIM];
};

static struct icon_theme_cache_entry g_icon_theme_cache[ICON_THEME_CACHE_ENTRIES];
static uint32_t g_icon_theme_cache_stamp = 1u;
static char g_icon_theme_current[ICON_THEME_MAX_THEME_NAME];
static uint8_t g_icon_theme_sizes[ICON_THEME_CONTEXT_PANEL + 1][ICON_THEME_MAX_SIZES_PER_CONTEXT];
static uint8_t g_icon_theme_size_count[ICON_THEME_CONTEXT_PANEL + 1];

static void icon_theme_reset_directory_index(void);
static void icon_theme_load_directory_index(void);

static int icon_theme_starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) {
        return 0;
    }
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static const char *icon_theme_context_dir(enum icon_theme_context context) {
    switch (context) {
    case ICON_THEME_CONTEXT_APPS:
        return "apps";
    case ICON_THEME_CONTEXT_PLACES:
        return "places";
    case ICON_THEME_CONTEXT_ACTIONS:
        return "actions";
    case ICON_THEME_CONTEXT_STATUS:
        return "status";
    case ICON_THEME_CONTEXT_NOTIFICATIONS:
        return "notifications";
    case ICON_THEME_CONTEXT_PANEL:
        return "panel";
    default:
        return "apps";
    }
}

static int icon_theme_parse_uint_token(const char *text, int *value_out) {
    int value = 0;

    if (text == 0 || *text == '\0' || value_out == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        value = (value * 10) + (int)(*text - '0');
        ++text;
    }
    *value_out = value;
    return 1;
}

static int icon_theme_context_from_dir_name(const char *dir_name, enum icon_theme_context *context_out) {
    if (dir_name == 0 || context_out == 0) {
        return 0;
    }
    if (str_eq(dir_name, "apps")) {
        *context_out = ICON_THEME_CONTEXT_APPS;
        return 1;
    }
    if (str_eq(dir_name, "places")) {
        *context_out = ICON_THEME_CONTEXT_PLACES;
        return 1;
    }
    if (str_eq(dir_name, "actions")) {
        *context_out = ICON_THEME_CONTEXT_ACTIONS;
        return 1;
    }
    if (str_eq(dir_name, "status")) {
        *context_out = ICON_THEME_CONTEXT_STATUS;
        return 1;
    }
    if (str_eq(dir_name, "notifications")) {
        *context_out = ICON_THEME_CONTEXT_NOTIFICATIONS;
        return 1;
    }
    if (str_eq(dir_name, "panel")) {
        *context_out = ICON_THEME_CONTEXT_PANEL;
        return 1;
    }
    return 0;
}

static void icon_theme_add_directory_size(enum icon_theme_context context, int size) {
    uint8_t *count = &g_icon_theme_size_count[(int)context];
    uint8_t *sizes = g_icon_theme_sizes[(int)context];

    if (size <= 0 || size > 255) {
        return;
    }
    for (int i = 0; i < *count; ++i) {
        if (sizes[i] == (uint8_t)size) {
            return;
        }
    }
    if (*count >= ICON_THEME_MAX_SIZES_PER_CONTEXT) {
        return;
    }
    sizes[*count] = (uint8_t)size;
    *count += 1u;
}

static void icon_theme_add_directory_token(const char *token) {
    char dir_name[24];
    char size_text[12];
    int slash = -1;
    int token_len;
    int size = 0;
    enum icon_theme_context context;

    if (token == 0 || *token == '\0') {
        return;
    }

    token_len = str_len(token);
    for (int i = 0; i < token_len; ++i) {
        if (token[i] == '/') {
            slash = i;
            break;
        }
    }
    if (slash <= 0 || slash >= token_len - 1) {
        return;
    }

    memset(dir_name, 0, sizeof(dir_name));
    memset(size_text, 0, sizeof(size_text));
    memcpy(dir_name, token, (size_t)slash < sizeof(dir_name) - 1 ? (size_t)slash : sizeof(dir_name) - 1);
    memcpy(size_text,
           token + slash + 1,
           (size_t)(token_len - slash - 1) < sizeof(size_text) - 1 ?
               (size_t)(token_len - slash - 1) :
               sizeof(size_text) - 1);

    if (!icon_theme_context_from_dir_name(dir_name, &context) ||
        !icon_theme_parse_uint_token(size_text, &size)) {
        return;
    }

    icon_theme_add_directory_size(context, size);
}

static void icon_theme_parse_directories_line(const char *text) {
    char token[40];
    int token_len = 0;

    if (text == 0 || *text == '\0') {
        return;
    }

    while (*text != '\0') {
        if (*text == ',') {
            token[token_len] = '\0';
            icon_theme_add_directory_token(token);
            token_len = 0;
        } else if (token_len < (int)sizeof(token) - 1) {
            token[token_len++] = *text;
        }
        ++text;
    }
    token[token_len] = '\0';
    icon_theme_add_directory_token(token);
}

static void icon_theme_reset_directory_index(void) {
    memset(g_icon_theme_sizes, 0, sizeof(g_icon_theme_sizes));
    memset(g_icon_theme_size_count, 0, sizeof(g_icon_theme_size_count));
}

static void icon_theme_load_directory_index(void) {
    static const int fallback_sizes[] = {16, 22, 24, 32, 48, 64, 128, 256};
    int node = fs_resolve("/assets/icons/index.theme");

    icon_theme_reset_directory_index();

    if (node >= 0 && g_fs_nodes[node].used && !g_fs_nodes[node].is_dir && g_fs_nodes[node].size > 0) {
        const char *cursor = g_fs_nodes[node].data;
        char line[512];

        while (*cursor != '\0') {
            int len = 0;

            while (*cursor == '\r' || *cursor == '\n') {
                ++cursor;
            }
            while (cursor[len] != '\0' &&
                   cursor[len] != '\r' &&
                   cursor[len] != '\n' &&
                   len < (int)sizeof(line) - 1) {
                line[len] = cursor[len];
                ++len;
            }
            line[len] = '\0';
            cursor += len;

            if (line[0] == '\0') {
                continue;
            }
            if (icon_theme_starts_with(line, "Directories=")) {
                icon_theme_parse_directories_line(line + 12);
                break;
            }
        }
    }

    for (int context = 0; context <= ICON_THEME_CONTEXT_PANEL; ++context) {
        if (g_icon_theme_size_count[context] != 0u) {
            continue;
        }
        for (int i = 0; i < (int)(sizeof(fallback_sizes) / sizeof(fallback_sizes[0])); ++i) {
            icon_theme_add_directory_size((enum icon_theme_context)context, fallback_sizes[i]);
        }
    }
}

static void icon_theme_append_uint(char *buf, uint32_t value, int max_len) {
    char digits[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        digits[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(digits)) {
            digits[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = digits[--pos];
    }
    buf[len] = '\0';
}

static int icon_theme_try_path(enum icon_theme_context context,
                               int size,
                               const char *name,
                               char *path,
                               int path_len) {
    if (name == 0 || *name == '\0' || path == 0 || path_len <= 0) {
        return -1;
    }

    path[0] = '\0';
    str_append(path, "/assets/icons/", path_len);
    str_append(path, icon_theme_context_dir(context), path_len);
    str_append(path, "/", path_len);
    icon_theme_append_uint(path, (uint32_t)size, path_len);
    str_append(path, "/", path_len);
    str_append(path, name, path_len);
    str_append(path, ".png", path_len);
    return fs_resolve(path);
}

static int icon_theme_resolve_node(enum icon_theme_context context,
                                   int preferred_size,
                                   const char *name) {
    int best_node = -1;
    int best_distance = 0x7fffffff;
    int count = (int)g_icon_theme_size_count[(int)context];

    for (int i = 0; i < count; ++i) {
        char candidate_path[96];
        int node;
        int distance;
        int candidate_size = (int)g_icon_theme_sizes[(int)context][i];

        node = icon_theme_try_path(context,
                                   candidate_size,
                                   name,
                                   candidate_path,
                                   (int)sizeof(candidate_path));
        if (node < 0) {
            continue;
        }
        distance = candidate_size - preferred_size;
        if (distance < 0) {
            distance = -distance;
        }
        if (best_node < 0 ||
            distance < best_distance ||
            (distance == best_distance && candidate_size >= preferred_size)) {
            best_node = node;
            best_distance = distance;
            if (distance == 0) {
                break;
            }
        }
    }

    return best_node;
}

static uint8_t icon_theme_nearest_palette(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_dist = 0xffffffffu;
    uint8_t best_index = 0u;

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
            best_index = (uint8_t)i;
        }
    }

    return best_index;
}

static uint16_t icon_theme_png_read_sample(const unsigned char *row, unsigned x, unsigned bitdepth) {
    if (bitdepth == 16u) {
        unsigned offset = x * 2u;
        return (uint16_t)(((uint16_t)row[offset] << 8) | (uint16_t)row[offset + 1u]);
    }
    if (bitdepth == 8u) {
        return row[x];
    }

    {
        unsigned mask = (1u << bitdepth) - 1u;
        unsigned bit_index = x * bitdepth;
        unsigned byte_index = bit_index >> 3;
        unsigned shift = 8u - bitdepth - (bit_index & 7u);
        return (uint16_t)((row[byte_index] >> shift) & mask);
    }
}

static uint8_t icon_theme_png_expand_sample(uint16_t sample, unsigned bitdepth) {
    if (bitdepth == 16u) {
        return (uint8_t)(sample >> 8);
    }
    if (bitdepth == 8u) {
        return (uint8_t)sample;
    }
    return (uint8_t)((sample * 255u) / ((1u << bitdepth) - 1u));
}

static int icon_theme_decode_png(const uint8_t *png_data,
                                 int png_size,
                                 int target_w,
                                 int target_h,
                                 struct icon_theme_cache_entry *entry) {
    LodePNGState state;
    unsigned char *raw = 0;
    unsigned width = 0u;
    unsigned height = 0u;
    unsigned error;
    unsigned bitdepth;
    LodePNGColorType colortype;

    lodepng_state_init(&state);
    state.decoder.color_convert = 0u;
    error = lodepng_decode(&raw, &width, &height, &state, png_data, (size_t)png_size);
    if (error != 0u || raw == 0 || width == 0u || height == 0u) {
        free(raw);
        lodepng_state_cleanup(&state);
        return -1;
    }
    bitdepth = state.info_png.color.bitdepth;
    colortype = state.info_png.color.colortype;

    if (target_w < 1) {
        target_w = 1;
    }
    if (target_h < 1) {
        target_h = 1;
    }
    if (target_w > ICON_THEME_MAX_DIM || target_h > ICON_THEME_MAX_DIM) {
        free(raw);
        lodepng_state_cleanup(&state);
        return -1;
    }

    entry->width = (uint8_t)target_w;
    entry->height = (uint8_t)target_h;

    for (int y = 0; y < target_h; ++y) {
        unsigned src_y = ((unsigned)y * height) / (unsigned)target_h;

        for (int x = 0; x < target_w; ++x) {
            unsigned src_x = ((unsigned)x * width) / (unsigned)target_w;
            uint8_t r = 0u;
            uint8_t g = 0u;
            uint8_t b = 0u;
            uint8_t a = 255u;
            int index = (y * target_w) + x;

            switch (colortype) {
            case LCT_PALETTE: {
                uint16_t palette_index = icon_theme_png_read_sample(raw + (src_y * ((width * bitdepth + 7u) / 8u)),
                                                                    src_x,
                                                                    bitdepth);
                const unsigned char *palette = state.info_png.color.palette;

                if (palette_index >= state.info_png.color.palettesize || palette == 0) {
                    free(raw);
                    lodepng_state_cleanup(&state);
                    return -1;
                }
                r = palette[palette_index * 4u + 0u];
                g = palette[palette_index * 4u + 1u];
                b = palette[palette_index * 4u + 2u];
                a = palette[palette_index * 4u + 3u];
                break;
            }
            case LCT_GREY: {
                const unsigned char *row = raw + (src_y * ((width * bitdepth + 7u) / 8u));
                uint16_t sample = icon_theme_png_read_sample(row, src_x, bitdepth);
                r = g = b = icon_theme_png_expand_sample(sample, bitdepth);
                if (state.info_png.color.key_defined && sample == state.info_png.color.key_r) {
                    a = 0u;
                }
                break;
            }
            case LCT_RGB: {
                const unsigned char *px = raw + ((src_y * width + src_x) * (bitdepth == 16u ? 6u : 3u));
                uint16_t rs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[0] << 8) | (uint16_t)px[1]) : px[0];
                uint16_t gs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[2] << 8) | (uint16_t)px[3]) : px[1];
                uint16_t bs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[4] << 8) | (uint16_t)px[5]) : px[2];
                r = icon_theme_png_expand_sample(rs, bitdepth);
                g = icon_theme_png_expand_sample(gs, bitdepth);
                b = icon_theme_png_expand_sample(bs, bitdepth);
                if (state.info_png.color.key_defined &&
                    rs == state.info_png.color.key_r &&
                    gs == state.info_png.color.key_g &&
                    bs == state.info_png.color.key_b) {
                    a = 0u;
                }
                break;
            }
            case LCT_GREY_ALPHA: {
                const unsigned char *px = raw + ((src_y * width + src_x) * (bitdepth == 16u ? 4u : 2u));
                uint16_t gs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[0] << 8) | px[1]) : px[0];
                uint16_t as = bitdepth == 16u ? (uint16_t)(((uint16_t)px[2] << 8) | px[3]) : px[1];
                r = g = b = icon_theme_png_expand_sample(gs, bitdepth);
                a = icon_theme_png_expand_sample(as, bitdepth);
                break;
            }
            case LCT_RGBA: {
                const unsigned char *px = raw + ((src_y * width + src_x) * (bitdepth == 16u ? 8u : 4u));
                uint16_t rs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[0] << 8) | px[1]) : px[0];
                uint16_t gs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[2] << 8) | px[3]) : px[1];
                uint16_t bs = bitdepth == 16u ? (uint16_t)(((uint16_t)px[4] << 8) | px[5]) : px[2];
                uint16_t as = bitdepth == 16u ? (uint16_t)(((uint16_t)px[6] << 8) | px[7]) : px[3];
                r = icon_theme_png_expand_sample(rs, bitdepth);
                g = icon_theme_png_expand_sample(gs, bitdepth);
                b = icon_theme_png_expand_sample(bs, bitdepth);
                a = icon_theme_png_expand_sample(as, bitdepth);
                break;
            }
            default:
                free(raw);
                lodepng_state_cleanup(&state);
                return -1;
            }

            if (a < 24u) {
                entry->opaque[index] = 0u;
                entry->pixels[index] = 0u;
                continue;
            }
            if (a < 255u) {
                r = (uint8_t)(((unsigned)r * a) / 255u);
                g = (uint8_t)(((unsigned)g * a) / 255u);
                b = (uint8_t)(((unsigned)b * a) / 255u);
            }
            entry->opaque[index] = 1u;
            entry->pixels[index] = icon_theme_nearest_palette(r, g, b);
        }
    }

    free(raw);
    lodepng_state_cleanup(&state);
    return 0;
}

static struct icon_theme_cache_entry *icon_theme_find_cache(int node,
                                                            int width,
                                                            int height) {
    for (int i = 0; i < ICON_THEME_CACHE_ENTRIES; ++i) {
        struct icon_theme_cache_entry *entry = &g_icon_theme_cache[i];

        if (!entry->used) {
            continue;
        }
        if (entry->node != (uint16_t)node ||
            entry->width != (uint8_t)width ||
            entry->height != (uint8_t)height) {
            continue;
        }
        entry->stamp = g_icon_theme_cache_stamp++;
        return entry;
    }
    return 0;
}

static struct icon_theme_cache_entry *icon_theme_alloc_cache(void) {
    struct icon_theme_cache_entry *oldest = &g_icon_theme_cache[0];

    for (int i = 0; i < ICON_THEME_CACHE_ENTRIES; ++i) {
        struct icon_theme_cache_entry *entry = &g_icon_theme_cache[i];

        if (!entry->used) {
            return entry;
        }
        if (entry->stamp < oldest->stamp) {
            oldest = entry;
        }
    }
    return oldest;
}

static struct icon_theme_cache_entry *icon_theme_load_cache(const char *name,
                                                            enum icon_theme_context context,
                                                            int preferred_size,
                                                            int width,
                                                            int height) {
    struct icon_theme_cache_entry *entry;
    uint8_t *png_data;
    int node;
    int bytes_read;

    node = icon_theme_resolve_node(context, preferred_size, name);
    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir || g_fs_nodes[node].size <= 0) {
        return 0;
    }
    entry = icon_theme_find_cache(node, width, height);
    if (entry != 0) {
        return entry;
    }

    png_data = (uint8_t *)malloc((size_t)g_fs_nodes[node].size);
    if (png_data == 0) {
        return 0;
    }

    bytes_read = fs_read_node_bytes(node, 0, png_data, g_fs_nodes[node].size);
    if (bytes_read != g_fs_nodes[node].size) {
        free(png_data);
        return 0;
    }

    entry = icon_theme_alloc_cache();
    memset(entry, 0, sizeof(*entry));
    if (icon_theme_decode_png(png_data, g_fs_nodes[node].size, width, height, entry) != 0) {
        free(png_data);
        memset(entry, 0, sizeof(*entry));
        return 0;
    }
    free(png_data);

    entry->used = 1u;
    entry->node = (uint16_t)node;
    entry->stamp = g_icon_theme_cache_stamp++;
    return entry;
}

void icon_theme_init(void) {
    str_copy_limited(g_icon_theme_current, "BeOS-r5-Icons", (int)sizeof(g_icon_theme_current));
    icon_theme_reset_cache();
    icon_theme_load_directory_index();
}

void icon_theme_reset_cache(void) {
    memset(g_icon_theme_cache, 0, sizeof(g_icon_theme_cache));
    g_icon_theme_cache_stamp = 1u;
}

void icon_theme_set_current(const char *theme_name) {
    if (theme_name == 0 || *theme_name == '\0') {
        str_copy_limited(g_icon_theme_current, "BeOS-r5-Icons", (int)sizeof(g_icon_theme_current));
    } else {
        str_copy_limited(g_icon_theme_current, theme_name, (int)sizeof(g_icon_theme_current));
    }
    icon_theme_reset_cache();
    icon_theme_load_directory_index();
}

const char *icon_theme_current(void) {
    return g_icon_theme_current;
}

int icon_theme_draw(const char *name,
                    enum icon_theme_context context,
                    int preferred_size,
                    int dst_x,
                    int dst_y,
                    int dst_w,
                    int dst_h) {
    struct icon_theme_cache_entry *entry;

    if (name == 0 || *name == '\0' || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }

    entry = icon_theme_load_cache(name, context, preferred_size, dst_w, dst_h);
    if (entry == 0) {
        return -1;
    }

    for (int y = 0; y < entry->height; ++y) {
        int x = 0;

        while (x < entry->width) {
            int row_index = y * entry->width;
            int run_start;
            uint8_t color;

            while (x < entry->width && entry->opaque[row_index + x] == 0u) {
                x += 1;
            }
            if (x >= entry->width) {
                break;
            }

            run_start = x;
            color = entry->pixels[row_index + x];
            while (x < entry->width &&
                   entry->opaque[row_index + x] != 0u &&
                   entry->pixels[row_index + x] == color) {
                x += 1;
            }
            sys_rect(dst_x + run_start, dst_y + y, x - run_start, 1, color);
        }
    }

    return 0;
}

int icon_theme_draw_inset(const char *name,
                          enum icon_theme_context context,
                          int preferred_size,
                          const struct rect *outer,
                          int padding_x,
                          int padding_y,
                          int width,
                          int height) {
    int x;
    int y;

    if (outer == 0) {
        return -1;
    }

    x = outer->x + padding_x;
    y = outer->y + padding_y;
    if (width <= 0) {
        width = outer->w - (padding_x * 2);
    }
    if (height <= 0) {
        height = outer->h - (padding_y * 2);
    }

    return icon_theme_draw(name, context, preferred_size, x, y, width, height);
}
