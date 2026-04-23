#include <stdlib.h>

#include <userland/applications/include/imageviewer.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_IMAGEVIEWER_WINDOW = {52, 34, 520, 360};

enum imageviewer_button_id {
    IMAGEVIEWER_BUTTON_PREV = 0,
    IMAGEVIEWER_BUTTON_NEXT,
    IMAGEVIEWER_BUTTON_WALLPAPER
};

static void append_uint(char *buf, unsigned value, int max_len) {
    char tmp[12];
    int pos = 0;

    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        for (int i = 0; i < pos / 2; ++i) {
            char swap = tmp[i];
            tmp[i] = tmp[pos - 1 - i];
            tmp[pos - 1 - i] = swap;
        }
    }

    tmp[pos] = '\0';
    str_append(buf, tmp, max_len);
}

static struct rect imageviewer_body_rect(const struct imageviewer_state *viewer) {
    struct rect r = {viewer->window.x + 4, viewer->window.y + 18, viewer->window.w - 8, viewer->window.h - 22};
    return r;
}

static struct rect imageviewer_toolbar_rect(const struct imageviewer_state *viewer) {
    struct rect r = {viewer->window.x + 10, viewer->window.y + 24, viewer->window.w - 20, 28};
    return r;
}

static struct rect imageviewer_status_rect(const struct imageviewer_state *viewer) {
    struct rect r = {viewer->window.x + 12, viewer->window.y + viewer->window.h - 28, viewer->window.w - 24, 14};
    return r;
}

static struct rect imageviewer_canvas_rect(const struct imageviewer_state *viewer) {
    struct rect r = {viewer->window.x + 12, viewer->window.y + 58, viewer->window.w - 24, viewer->window.h - 92};
    return r;
}

static struct rect imageviewer_content_rect(const struct imageviewer_state *viewer) {
    struct rect canvas = imageviewer_canvas_rect(viewer);
    struct rect r = {canvas.x + 6, canvas.y + 6, canvas.w - 12, canvas.h - 12};

    if (r.w < 1) {
        r.w = 1;
    }
    if (r.h < 1) {
        r.h = 1;
    }
    return r;
}

static struct rect imageviewer_button_rect(const struct imageviewer_state *viewer, int button) {
    struct rect toolbar = imageviewer_toolbar_rect(viewer);
    struct rect r = {toolbar.x, toolbar.y + 6, 44, 16};

    if (button == IMAGEVIEWER_BUTTON_PREV) {
        r.x = toolbar.x + toolbar.w - 154;
        r.w = 42;
    } else if (button == IMAGEVIEWER_BUTTON_NEXT) {
        r.x = toolbar.x + toolbar.w - 108;
        r.w = 42;
    } else {
        r.x = toolbar.x + toolbar.w - 62;
        r.w = 54;
    }

    return r;
}

static void imageviewer_release_pixels(struct imageviewer_state *viewer) {
    if (viewer->pixels) {
        free(viewer->pixels);
        viewer->pixels = 0;
    }
    viewer->image_w = 0;
    viewer->image_h = 0;
    viewer->render_limit_w = 0;
    viewer->render_limit_h = 0;
    viewer->rendered_node = -1;
}

static void imageviewer_set_message(struct imageviewer_state *viewer, const char *message) {
    str_copy_limited(viewer->message, message, (int)sizeof(viewer->message));
}

static int imageviewer_find_first_supported_node(void) {
    for (int i = 0; i < FS_MAX_NODES; ++i) {
        if (image_node_is_supported(i)) {
            return i;
        }
    }
    return -1;
}

static int imageviewer_wallpaper_node(void) {
    int node = ui_wallpaper_source_node();

    if (image_node_is_supported(node)) {
        return node;
    }

    node = fs_resolve("/assets/wallpaper.png");
    if (!image_node_is_supported(node)) {
        node = fs_resolve("/wallpaper.png");
    }
    if (image_node_is_supported(node)) {
        return node;
    }

    return -1;
}

static int imageviewer_find_adjacent_node(int current, int direction) {
    int node;

    if (direction == 0) {
        return current;
    }
    if (!image_node_is_supported(current)) {
        return imageviewer_find_first_supported_node();
    }

    node = current;
    for (int attempts = 0; attempts < FS_MAX_NODES; ++attempts) {
        node += direction;
        if (node >= FS_MAX_NODES) {
            node = 0;
        } else if (node < 0) {
            node = FS_MAX_NODES - 1;
        }

        if (image_node_is_supported(node)) {
            return node;
        }
    }

    return current;
}

static void imageviewer_load_default(struct imageviewer_state *viewer) {
    int node = imageviewer_wallpaper_node();

    if (node < 0) {
        node = imageviewer_find_first_supported_node();
    }

    if (node >= 0) {
        (void)imageviewer_open_node(viewer, node);
        return;
    }

    viewer->image_node = -1;
    imageviewer_release_pixels(viewer);
    imageviewer_set_message(viewer, "Nenhuma imagem .png/.bmp encontrada");
}

static int imageviewer_refresh_scaled(struct imageviewer_state *viewer) {
    struct rect content;
    size_t pixel_count;
    uint8_t *pixels;
    int width = 0;
    int height = 0;

    if (!image_node_is_supported(viewer->image_node)) {
        imageviewer_release_pixels(viewer);
        imageviewer_set_message(viewer, "Arquivo nao e uma imagem suportada");
        return -1;
    }

    content = imageviewer_content_rect(viewer);
    if (viewer->pixels &&
        viewer->rendered_node == viewer->image_node &&
        viewer->render_limit_w == content.w &&
        viewer->render_limit_h == content.h) {
        return 0;
    }

    pixel_count = (size_t)content.w * (size_t)content.h;
    pixels = (uint8_t *)malloc(pixel_count);
    if (!pixels) {
        imageviewer_release_pixels(viewer);
        imageviewer_set_message(viewer, "Sem memoria para abrir a imagem");
        return -1;
    }

    if (image_decode_node_to_palette(viewer->image_node,
                                     pixels,
                                     content.w,
                                     content.w,
                                     content.h,
                                     &width,
                                     &height) != 0) {
        free(pixels);
        imageviewer_release_pixels(viewer);
        imageviewer_set_message(viewer, "Falha ao decodificar a imagem");
        return -1;
    }

    imageviewer_release_pixels(viewer);
    viewer->pixels = pixels;
    viewer->image_w = width;
    viewer->image_h = height;
    viewer->render_limit_w = content.w;
    viewer->render_limit_h = content.h;
    viewer->rendered_node = viewer->image_node;
    viewer->message[0] = '\0';
    return 0;
}

static void imageviewer_build_dims(const struct imageviewer_state *viewer, char *out, int max_len) {
    out[0] = '\0';

    if (viewer->image_w <= 0 || viewer->image_h <= 0) {
        return;
    }

    append_uint(out, (unsigned)viewer->image_w, max_len);
    str_append(out, "x", max_len);
    append_uint(out, (unsigned)viewer->image_h, max_len);
}

static void imageviewer_build_status(const struct imageviewer_state *viewer, char *out, int max_len) {
    if (viewer->message[0] != '\0') {
        str_copy_limited(out, viewer->message, max_len);
        return;
    }

    if (viewer->image_node >= 0 && g_fs_nodes[viewer->image_node].used) {
        fs_build_path(viewer->image_node, out, max_len);
        return;
    }

    str_copy_limited(out, "Use ANT/PROX para navegar pelas imagens", max_len);
}

void imageviewer_init_state(struct imageviewer_state *viewer) {
    viewer->window = DEFAULT_IMAGEVIEWER_WINDOW;
    viewer->image_node = -1;
    viewer->rendered_node = -1;
    viewer->image_w = 0;
    viewer->image_h = 0;
    viewer->render_limit_w = 0;
    viewer->render_limit_h = 0;
    viewer->pixels = 0;
    viewer->message[0] = '\0';
    imageviewer_load_default(viewer);
}

void imageviewer_shutdown_state(struct imageviewer_state *viewer) {
    if (!viewer) {
        return;
    }
    imageviewer_release_pixels(viewer);
}

int imageviewer_open_node(struct imageviewer_state *viewer, int node) {
    if (!viewer || !image_node_is_supported(node)) {
        if (viewer) {
            imageviewer_set_message(viewer, "Arquivo nao e uma imagem suportada");
        }
        return -1;
    }

    viewer->image_node = node;
    viewer->message[0] = '\0';
    imageviewer_release_pixels(viewer);
    return imageviewer_refresh_scaled(viewer);
}

int imageviewer_handle_click(struct imageviewer_state *viewer, int x, int y) {
    struct rect prev;
    struct rect next;
    struct rect wallpaper;
    int node = -1;

    if (!viewer) {
        return 0;
    }

    prev = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_PREV);
    next = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_NEXT);
    wallpaper = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_WALLPAPER);

    if (point_in_rect(&prev, x, y)) {
        node = imageviewer_find_adjacent_node(viewer->image_node, -1);
    } else if (point_in_rect(&next, x, y)) {
        node = imageviewer_find_adjacent_node(viewer->image_node, 1);
    } else if (point_in_rect(&wallpaper, x, y)) {
        if (!image_node_is_supported(viewer->image_node)) {
            imageviewer_set_message(viewer, "Abra uma imagem para definir o plano");
            return 1;
        }
        if (ui_wallpaper_set_from_node(viewer->image_node) != 0) {
            imageviewer_set_message(viewer, "Falha ao definir wallpaper");
            return 1;
        }
        imageviewer_set_message(viewer, "Wallpaper aplicado");
        return 1;
    } else {
        return 0;
    }

    if (node < 0) {
        imageviewer_set_message(viewer, "Nenhuma imagem disponivel");
        imageviewer_release_pixels(viewer);
        viewer->image_node = -1;
        return 1;
    }

    if (node == viewer->image_node &&
        viewer->pixels &&
        viewer->rendered_node == viewer->image_node) {
        return 0;
    }

    (void)imageviewer_open_node(viewer, node);
    return 1;
}

void imageviewer_draw_window(struct imageviewer_state *viewer, int active,
                             int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = imageviewer_body_rect(viewer);
    struct rect toolbar = imageviewer_toolbar_rect(viewer);
    struct rect canvas = imageviewer_canvas_rect(viewer);
    struct rect content = imageviewer_content_rect(viewer);
    struct rect status = imageviewer_status_rect(viewer);
    struct rect prev = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_PREV);
    struct rect next = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_NEXT);
    struct rect wallpaper = imageviewer_button_rect(viewer, IMAGEVIEWER_BUTTON_WALLPAPER);
    struct rect title_bounds = {toolbar.x + 30, toolbar.y, prev.x - (toolbar.x + 30) - 8, toolbar.h};
    struct rect dims_bounds = {toolbar.x + toolbar.w - 214, toolbar.y, 54, toolbar.h};
    char status_text[80];
    char dims[24];
    char title_fit[48];
    char dims_fit[24];

    draw_window_frame(&viewer->window, "IMAGEM", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&toolbar, ui_color_panel());
    ui_draw_surface(&canvas, ui_color_panel());
    ui_draw_inset(&canvas, ui_color_window_bg());
    (void)icon_theme_draw("camera-photo",
                          ICON_THEME_CONTEXT_APPS,
                          16,
                          toolbar.x + 8,
                          toolbar.y + 6,
                          16,
                          16);
    ui_draw_button_with_icon(&prev,
                             "ANT",
                             UI_BUTTON_NORMAL,
                             0,
                             "go-up",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
    ui_draw_button_with_icon(&next,
                             "PROX",
                             UI_BUTTON_PRIMARY,
                             0,
                             "go-down",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
    ui_draw_button_with_icon(&wallpaper,
                             "PLANO",
                             UI_BUTTON_ACTIVE,
                             0,
                             "preferences-desktop-wallpaper",
                             ICON_THEME_CONTEXT_APPS,
                             16,
                             10,
                             10);

    if (viewer->image_node >= 0 && g_fs_nodes[viewer->image_node].used) {
        ui_text_copy_fit(title_fit,
                         (int)sizeof(title_fit),
                         g_fs_nodes[viewer->image_node].name,
                         title_bounds.w);
    } else {
        ui_text_copy_fit(title_fit, (int)sizeof(title_fit), "Nenhuma imagem", title_bounds.w);
    }
    ui_draw_text_clipped(&title_bounds, title_bounds.x, toolbar.y + 10, theme->text, title_fit);

    if (viewer->image_node >= 0) {
        (void)imageviewer_refresh_scaled(viewer);
    }

    imageviewer_build_dims(viewer, dims, (int)sizeof(dims));
    if (dims[0] != '\0') {
        ui_text_copy_fit(dims_fit, (int)sizeof(dims_fit), dims, dims_bounds.w);
        ui_draw_text_clipped(&dims_bounds, dims_bounds.x, toolbar.y + 10, ui_color_muted(), dims_fit);
    }

    if (viewer->pixels && viewer->image_w > 0 && viewer->image_h > 0) {
        int dst_x = content.x + ((content.w - viewer->image_w) / 2);
        int dst_y = content.y + ((content.h - viewer->image_h) / 2);

        sys_gfx_blit8(viewer->pixels, viewer->image_w, viewer->image_h, dst_x, dst_y, 1);
    } else {
        ui_draw_text_clipped(&canvas, canvas.x + 12, canvas.y + 14, ui_color_muted(), "Abra uma imagem .png ou .bmp");
        ui_draw_text_clipped(&canvas, canvas.x + 12, canvas.y + 28, ui_color_muted(), "O wallpaper atual aparece por padrao");
    }

    imageviewer_build_status(viewer, status_text, (int)sizeof(status_text));
    ui_draw_status(&status, status_text);
}
