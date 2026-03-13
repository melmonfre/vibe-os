#include <userland/modules/include/ui.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/dirty_rects.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/ui_cursor.h>

/* Global screen resolution vars - initialized at startup */
uint32_t SCREEN_WIDTH = 320;
uint32_t SCREEN_HEIGHT = 200;
uint32_t SCREEN_PITCH = 320;
struct video_mode g_screen_mode = {0};
static struct desktop_theme g_theme = {3, 7, 8, 7, 9, 0};
static int g_ui_loading_settings = 0;
static struct {
    int active;
    int source_node;
    int width;
    int height;
    uint8_t pixels[BMP_MAX_TARGET_H][BMP_MAX_TARGET_W];
} g_wallpaper = {0, -1, 0, 0, {{0}}};

#define TASKBAR_HEIGHT 22
#define START_MENU_WIDTH 158
#define START_MENU_HEIGHT 176
#define START_MENU_ITEM_X 28
#define START_MENU_ITEM_Y 8
#define START_MENU_ITEM_W 126
#define START_MENU_ITEM_H 14
#define START_MENU_ITEM_STEP 16
#define UI_SETTINGS_PATH "/config/ui.cfg"

static int ui_starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static int ui_parse_uint(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;

    if (text == 0 || *text == '\0') {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        parsed = (parsed * 10u) + (uint32_t)(*text - '0');
        ++text;
    }
    *value = parsed;
    return 1;
}

static void ui_append_uint(char *buf, uint32_t value, int max_len) {
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

static void ui_append_kv_u32(char *buf, const char *key, uint32_t value, int max_len) {
    str_append(buf, key, max_len);
    ui_append_uint(buf, value, max_len);
    str_append(buf, "\n", max_len);
}

static void ui_save_settings(void) {
    char text[256];
    char wallpaper[80];

    if (fs_resolve("/config") < 0) {
        (void)fs_create("/config", 1);
    }

    text[0] = '\0';
    wallpaper[0] = '\0';
    if (g_wallpaper.source_node >= 0) {
        fs_build_path(g_wallpaper.source_node, wallpaper, (int)sizeof(wallpaper));
    }

    ui_append_kv_u32(text, "background=", g_theme.background, (int)sizeof(text));
    ui_append_kv_u32(text, "menu=", g_theme.menu, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button=", g_theme.menu_button, (int)sizeof(text));
    ui_append_kv_u32(text, "taskbar=", g_theme.taskbar, (int)sizeof(text));
    ui_append_kv_u32(text, "window=", g_theme.window, (int)sizeof(text));
    ui_append_kv_u32(text, "text=", g_theme.text, (int)sizeof(text));
    ui_append_kv_u32(text, "width=", SCREEN_WIDTH, (int)sizeof(text));
    ui_append_kv_u32(text, "height=", SCREEN_HEIGHT, (int)sizeof(text));
    str_append(text, "wallpaper=", (int)sizeof(text));
    str_append(text, wallpaper, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));

    (void)fs_write_file(UI_SETTINGS_PATH, text, 0);
}

static void ui_load_settings(void) {
    int idx = fs_resolve(UI_SETTINGS_PATH);
    char text[256];
    char wallpaper[80];
    struct desktop_theme loaded = g_theme;
    uint32_t width = SCREEN_WIDTH;
    uint32_t height = SCREEN_HEIGHT;
    int clear_wallpaper = 1;

    if (idx < 0 || g_fs_nodes[idx].is_dir || g_fs_nodes[idx].size <= 0) {
        return;
    }

    str_copy_limited(text, g_fs_nodes[idx].data, (int)sizeof(text));
    wallpaper[0] = '\0';

    for (char *line = text; *line != '\0'; ) {
        char *next = line;
        uint32_t value = 0u;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (ui_starts_with(line, "background=") && ui_parse_uint(line + 11, &value)) {
            loaded.background = (uint8_t)value;
        } else if (ui_starts_with(line, "menu=") && ui_parse_uint(line + 5, &value)) {
            loaded.menu = (uint8_t)value;
        } else if (ui_starts_with(line, "menu_button=") && ui_parse_uint(line + 12, &value)) {
            loaded.menu_button = (uint8_t)value;
        } else if (ui_starts_with(line, "taskbar=") && ui_parse_uint(line + 8, &value)) {
            loaded.taskbar = (uint8_t)value;
        } else if (ui_starts_with(line, "window=") && ui_parse_uint(line + 7, &value)) {
            loaded.window = (uint8_t)value;
        } else if (ui_starts_with(line, "text=") && ui_parse_uint(line + 5, &value)) {
            loaded.text = (uint8_t)value;
        } else if (ui_starts_with(line, "width=") && ui_parse_uint(line + 6, &value)) {
            width = value;
        } else if (ui_starts_with(line, "height=") && ui_parse_uint(line + 7, &value)) {
            height = value;
        } else if (ui_starts_with(line, "wallpaper=")) {
            str_copy_limited(wallpaper, line + 10, (int)sizeof(wallpaper));
            clear_wallpaper = wallpaper[0] == '\0';
        }

        line = next;
    }

    if (width != SCREEN_WIDTH || height != SCREEN_HEIGHT) {
        (void)ui_set_resolution(width, height);
    }

    g_theme = loaded;
    if (clear_wallpaper) {
        ui_wallpaper_clear();
    } else {
        int node = fs_resolve(wallpaper);
        if (node >= 0) {
            (void)ui_wallpaper_set_from_node(node);
        } else {
            ui_wallpaper_clear();
        }
    }
}

void ui_refresh_metrics(void) {
    if (sys_gfx_info(&g_screen_mode) == 0) {
        SCREEN_WIDTH = g_screen_mode.width;
        SCREEN_HEIGHT = g_screen_mode.height;
        SCREEN_PITCH = g_screen_mode.pitch;
    }
}

void ui_init(void) {
    ui_refresh_metrics();
    g_ui_loading_settings = 1;
    ui_load_settings();
    g_ui_loading_settings = 0;

    dirty_init();
    clip_init();
    cursor_init();
}

int ui_set_resolution(uint32_t width, uint32_t height) {
    if (sys_gfx_set_mode(width, height) != 0) {
        return -1;
    }
    ui_refresh_metrics();
    dirty_init();
    clip_init();
    cursor_init();
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
}

const struct desktop_theme *ui_theme_get(void) {
    return &g_theme;
}

void ui_wallpaper_clear(void) {
    g_wallpaper.active = 0;
    g_wallpaper.source_node = -1;
    g_wallpaper.width = 0;
    g_wallpaper.height = 0;
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
}

int ui_wallpaper_set_from_node(int node) {
    int width = 0;
    int height = 0;

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (bmp_decode_to_palette((const uint8_t *)g_fs_nodes[node].data,
                              g_fs_nodes[node].size,
                              &g_wallpaper.pixels[0][0],
                              BMP_MAX_TARGET_W,
                              BMP_MAX_TARGET_W,
                              BMP_MAX_TARGET_H,
                              &width,
                              &height) != 0) {
        return -1;
    }

    g_wallpaper.active = 1;
    g_wallpaper.source_node = node;
    g_wallpaper.width = width;
    g_wallpaper.height = height;
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
}

int ui_wallpaper_source_node(void) {
    return g_wallpaper.source_node;
}

void ui_theme_set_slot(enum theme_slot slot, uint8_t color) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND:
        g_theme.background = color;
        break;
    case THEME_SLOT_MENU:
        g_theme.menu = color;
        break;
    case THEME_SLOT_MENU_BUTTON:
        g_theme.menu_button = color;
        break;
    case THEME_SLOT_TASKBAR:
        g_theme.taskbar = color;
        break;
    case THEME_SLOT_WINDOW:
        g_theme.window = color;
        break;
    case THEME_SLOT_TEXT:
        g_theme.text = color;
        break;
    default:
        break;
    }
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
}

const char *ui_theme_slot_name(enum theme_slot slot) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND: return "Plano";
    case THEME_SLOT_MENU: return "Menu";
    case THEME_SLOT_MENU_BUTTON: return "Botao";
    case THEME_SLOT_TASKBAR: return "Barra";
    case THEME_SLOT_WINDOW: return "Janela";
    case THEME_SLOT_TEXT: return "Texto";
    default: return "Tema";
    }
}

struct rect ui_taskbar_start_button_rect(void) {
    struct rect r = {4, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3, 56, 16};
    return r;
}

struct rect ui_start_menu_rect(void) {
    struct rect r = {2, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - START_MENU_HEIGHT,
                     START_MENU_WIDTH, START_MENU_HEIGHT};
    return r;
}

struct rect ui_start_menu_item_rect(int index) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {
        menu.x + START_MENU_ITEM_X,
        menu.y + START_MENU_ITEM_Y + (index * START_MENU_ITEM_STEP),
        START_MENU_ITEM_W,
        START_MENU_ITEM_H
    };
    return r;
}

static void draw_panel(const struct rect *r, uint8_t face, uint8_t light, uint8_t dark) {
    sys_rect(r->x, r->y, r->w, r->h, face);
    sys_rect(r->x, r->y, r->w, 1, light);
    sys_rect(r->x, r->y, 1, r->h, light);
    sys_rect(r->x, r->y + r->h - 1, r->w, 1, dark);
    sys_rect(r->x + r->w - 1, r->y, 1, r->h, dark);
}

static void draw_wallpaper(int desktop_h) {
    if (!g_wallpaper.active || g_wallpaper.width <= 0 || g_wallpaper.height <= 0) {
        sys_rect(0, 0, (int)SCREEN_WIDTH, desktop_h, g_theme.background);
        return;
    }

    for (int y = 0; y < g_wallpaper.height; ++y) {
        int y0 = (y * desktop_h) / g_wallpaper.height;
        int y1 = ((y + 1) * desktop_h) / g_wallpaper.height;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }

        for (int x = 0; x < g_wallpaper.width; ++x) {
            int x0 = (x * (int)SCREEN_WIDTH) / g_wallpaper.width;
            int x1 = ((x + 1) * (int)SCREEN_WIDTH) / g_wallpaper.width;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            sys_rect(x0, y0, x1 - x0, y1 - y0, g_wallpaper.pixels[y][x]);
        }
    }
}

static const char *app_caption(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Tasks";
    case APP_CALCULATOR: return "Calc";
    case APP_SKETCHPAD: return "Sketch";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_PERSONALIZE: return "Cores";
    default: return "App";
    }
}

static void draw_task_button(const struct rect *r, const char *label, int active) {
    uint8_t face = active ? g_theme.window : g_theme.menu_button;
    draw_panel(r, face, 15, 0);
    sys_text(r->x + 5, r->y + 4, g_theme.text, label);
}

void draw_window_frame(const struct rect *w, const char *title,
                       int active,
                       int min_hover,
                       int max_hover,
                       int close_hover) {
    const struct rect min = window_min_button(w);
    const struct rect max = window_max_button(w);
    const struct rect close = window_close_button(w);
    const struct rect outer = {w->x, w->y, w->w, w->h};
    const struct rect title_bar = {w->x + 2, w->y + 2, w->w - 4, 12};

    draw_panel(&outer, 7, 15, 0);
    sys_rect(title_bar.x, title_bar.y, title_bar.w, title_bar.h, active ? g_theme.window : 8);
    sys_text(w->x + 6, w->y + 4, g_theme.text, title);

    draw_panel(&min, min_hover ? 15 : g_theme.menu_button, 15, 0);
    draw_panel(&max, max_hover ? 15 : g_theme.menu_button, 15, 0);
    draw_panel(&close, close_hover ? 12 : g_theme.menu_button, 15, 0);
    sys_text(min.x + 3, min.y + 2, g_theme.text, "-");
    sys_text(max.x + 3, max.y + 2, g_theme.text, "0");
    sys_text(close.x + 3, close.y + 2, g_theme.text, "X");
}

static void draw_start_menu(int taskbar_y,
                            const int *menu_item_hover) {
    static const char *labels[START_MENU_ITEM_COUNT] = {
        "Terminal",
        "Relogio",
        "Arquivos",
        "Editor",
        "Tasks",
        "Calculadora",
        "Sketchpad",
        "Snake",
        "Tetris",
        "Encerrar sessao"
    };
    const struct rect menu_rect = {2, taskbar_y - START_MENU_HEIGHT, START_MENU_WIDTH, START_MENU_HEIGHT};
    const struct rect blue_strip = {menu_rect.x + 2, menu_rect.y + 2, 22, menu_rect.h - 4};

    draw_panel(&menu_rect, g_theme.menu, 15, 0);
    sys_rect(blue_strip.x, blue_strip.y, blue_strip.w, blue_strip.h, 9);
    sys_text(blue_strip.x + 4, blue_strip.y + 6, 15, "V");
    sys_text(blue_strip.x + 4, blue_strip.y + 18, 15, "I");
    sys_text(blue_strip.x + 4, blue_strip.y + 30, 15,  "B");
    sys_text(blue_strip.x + 4, blue_strip.y + 42, 15, "E");

    for (int i = 0; i < START_MENU_ITEM_COUNT; ++i) {
        struct rect item = ui_start_menu_item_rect(i);
        if (i == START_MENU_LOGOUT) {
            sys_rect(item.x, item.y - 6, item.w, 1, 8);
        }
        draw_task_button(&item, labels[i], menu_item_hover[i]);
    }
}

static void draw_taskbar(const struct window *wins, int win_count, int focused, int start_hover) {
    const int taskbar_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;
    struct rect start_button = ui_taskbar_start_button_rect();
    int x = 66;

    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 22, g_theme.taskbar);
    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 1, 15);
    sys_rect(0, taskbar_y + 21, (int)SCREEN_WIDTH, 1, 0);
    draw_task_button(&start_button, "Iniciar", start_hover);

    for (int i = 0; i < win_count; ++i) {
        struct rect button;

        if (!wins[i].active) {
            continue;
        }

        button.x = x;
        button.y = taskbar_y + 3;
        button.w = 68;
        button.h = 16;
        if (button.x + button.w > (int)SCREEN_WIDTH - 4) {
            break;
        }
        draw_task_button(&button, app_caption(wins[i].type), i == focused && !wins[i].minimized);
        x += button.w + 4;
    }
}

void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  const int *menu_item_hover,
                  const struct window *wins,
                  int win_count,
                  int focused) {
    const int desktop_h = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;

    sys_clear(g_theme.background);
    draw_wallpaper(desktop_h);
    sys_rect(20, 18, 140, 18, g_theme.window);
    sys_text(28, 24, g_theme.text, "VIBE DESKTOP");
    sys_rect((int)SCREEN_WIDTH - 108, 24, 72, 72, 11);
    sys_text((int)SCREEN_WIDTH - 94, 52, g_theme.text, "Meu PC");

    draw_taskbar(wins, win_count, focused, start_hover);

    if (menu_open) {
        draw_start_menu((int)SCREEN_HEIGHT - TASKBAR_HEIGHT, menu_item_hover);
    }
    (void)mouse;
}
