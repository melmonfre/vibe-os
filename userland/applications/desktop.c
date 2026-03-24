#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/ui_cursor.h>
#include <userland/applications/include/clock.h>
#include <userland/applications/include/filemanager.h>
#include <userland/applications/include/editor.h>
#include <userland/applications/include/taskmgr.h>
#include <userland/applications/include/calculator.h>
#include <userland/applications/include/imageviewer.h>
#include <userland/applications/include/sketchpad.h>
#include <userland/applications/include/games/snake.h>
#include <userland/applications/include/games/tetris.h>
#include <userland/applications/include/games/pacman.h>
#include <userland/applications/include/games/space_invaders.h>
#include <userland/applications/include/games/pong.h>
#include <userland/applications/include/games/donkey_kong.h>
#include <userland/applications/include/games/brick_race.h>
#include <userland/applications/include/games/flap_birb.h>
#include <userland/applications/include/games/doom.h>
#include <userland/applications/include/games/craft.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

static struct window g_windows[MAX_WINDOWS];
static struct terminal_state g_terms[MAX_TERMINALS];
static int g_term_used[MAX_TERMINALS];
static struct clock_state g_clocks[MAX_CLOCKS];
static int g_clock_used[MAX_CLOCKS];
static struct filemanager_state g_fms[MAX_FILEMANAGERS];
static int g_fm_used[MAX_FILEMANAGERS];
static struct editor_state g_editors[MAX_EDITORS];
static int g_editor_used[MAX_EDITORS];
static struct taskmgr_state g_tms[MAX_TASKMGRS];
static int g_tm_used[MAX_TASKMGRS];
static struct calculator_state g_calcs[MAX_CALCULATORS];
static int g_calc_used[MAX_CALCULATORS];
static struct imageviewer_state g_imageviewers[MAX_IMAGEVIEWERS];
static int g_imageviewer_used[MAX_IMAGEVIEWERS];
static struct sketchpad_state g_sketches[MAX_SKETCHPADS];
static int g_sketch_used[MAX_SKETCHPADS];
static struct snake_state g_snakes[MAX_SNAKES];
static int g_snake_used[MAX_SNAKES];
static struct tetris_state g_tetris[MAX_TETRIS];
static int g_tetris_used[MAX_TETRIS];
static struct pacman_state g_pacman[MAX_PACMAN];
static int g_pacman_used[MAX_PACMAN];
static struct space_invaders_state g_space_invaders[MAX_SPACE_INVADERS];
static int g_space_invaders_used[MAX_SPACE_INVADERS];
static struct pong_state g_pong[MAX_PONG];
static int g_pong_used[MAX_PONG];
static struct donkey_kong_state g_donkey_kong[MAX_DONKEY_KONG];
static int g_donkey_kong_used[MAX_DONKEY_KONG];
static struct brick_race_state g_brick_race[MAX_BRICK_RACE];
static int g_brick_race_used[MAX_BRICK_RACE];
static struct flap_birb_state g_flap_birb[MAX_FLAP_BIRB];
static int g_flap_birb_used[MAX_FLAP_BIRB];
static struct doom_state g_doom[MAX_DOOM];
static int g_doom_used[MAX_DOOM];
static struct craft_state g_craft[MAX_CRAFT];
static int g_craft_used[MAX_CRAFT];
struct personalize_state {
    struct rect window;
    enum theme_slot selected_slot;
    int color_picker_open;
    int color_picker_start_x;
    int color_picker_start_y;
    char resolution_status[48];
};
struct trash_state {
    struct rect window;
    int selected_entry;
    char status[64];
};
static struct personalize_state g_pers;
static int g_pers_used = 0;
static struct trash_state g_trash;
static int g_trash_used = 0;

static const uint8_t g_color_palette_256[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};

#define TASKBAR_HEIGHT 22
#define WINDOW_MIN_W 400
#define WINDOW_MIN_H 300
#define TRASH_ROOT_PATH "/trash"
#define TRASH_ENTRY_BASE "trash"
#define TRASH_META_NAME "__origin"
#define TRASH_ITEM_NAME "__item"

enum {
    FMENU_OPEN = 0,
    FMENU_COPY,
    FMENU_PASTE,
    FMENU_NEW_DIR,
    FMENU_NEW_FILE,
    FMENU_RENAME,
    FMENU_MOVE_TO_TRASH,
    FMENU_SET_WALLPAPER,
    FMENU_COUNT
};
enum {
    START_MENU_ENTRY_COUNT = 60,
    START_MENU_SEARCH_MAX = 24,
    START_MENU_SCROLLBAR_W = 10
};
enum {
    APPCTX_PRIMARY = 0,
    APPCTX_SAVE_AS,
    APPCTX_COUNT
};
enum file_dialog_mode {
    FILE_DIALOG_NONE = 0,
    FILE_DIALOG_EDITOR_SAVE,
    FILE_DIALOG_SKETCH_EXPORT,
    FILE_DIALOG_WALLPAPER_PATH,
    FILE_DIALOG_FILE_RENAME
};
struct app_context_state {
    int open;
    int window;
    enum app_type type;
    struct rect menu;
};
struct file_dialog_state {
    int active;
    enum file_dialog_mode mode;
    int owner_window;
    int target_node;
    int active_field;
    struct rect window;
    char title[28];
    char confirm[16];
    char name[FS_NAME_MAX + 1];
    char ext[FS_NAME_MAX + 1];
    char path[80];
    char status[40];
};
static int g_clipboard_node = -1;
static enum app_type g_launch_app_type = APP_NONE;
static int g_launch_editor_pending = 0;
static int g_launch_editor_nano = 0;
static char g_launch_editor_path[80];
static int g_launch_terminal_pending = 0;
static char g_launch_terminal_command[INPUT_MAX + 1];
static int g_fm_context_has_wallpaper_action = 0;
struct resolution_option {
    uint16_t width;
    uint16_t height;
};

static const struct resolution_option g_resolution_fallbacks[] = {
    {640u, 480u},
    {800u, 600u},
    {1024u, 768u},
    {1360u, 720u},
    {1920u, 1080u}
};
static struct resolution_option g_resolution_options[VIDEO_MODE_LIST_MAX];
static int g_resolution_option_count = 0;
static int g_resolution_can_set = 0;

static void set_personalize_resolution_status(const char *msg) {
    str_copy_limited(g_pers.resolution_status, msg, (int)sizeof(g_pers.resolution_status));
}

static void set_trash_status(const char *msg) {
    str_copy_limited(g_trash.status, msg, (int)sizeof(g_trash.status));
}

static void refresh_resolution_options(void) {
    struct video_capabilities caps;
    int count = 0;

    g_resolution_can_set = 0;
    if (sys_gfx_caps(&caps) == 0) {
        g_resolution_can_set = (caps.flags & VIDEO_CAPS_CAN_SET_MODE) != 0u;
        for (uint32_t i = 0; i < caps.mode_count && count < (int)VIDEO_MODE_LIST_MAX; ++i) {
            uint16_t width = caps.mode_width[i];
            uint16_t height = caps.mode_height[i];
            int duplicate = 0;

            if (width == 0u || height == 0u) {
                continue;
            }
            for (int j = 0; j < count; ++j) {
                if (g_resolution_options[j].width == width &&
                    g_resolution_options[j].height == height) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            g_resolution_options[count].width = width;
            g_resolution_options[count].height = height;
            ++count;
        }
    }

    if (count == 0) {
        count = (int)(sizeof(g_resolution_fallbacks) / sizeof(g_resolution_fallbacks[0]));
        for (int i = 0; i < count; ++i) {
            g_resolution_options[i] = g_resolution_fallbacks[i];
        }
    }

    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            uint32_t area_i = (uint32_t)g_resolution_options[i].width * (uint32_t)g_resolution_options[i].height;
            uint32_t area_j = (uint32_t)g_resolution_options[j].width * (uint32_t)g_resolution_options[j].height;

            if (area_j < area_i ||
                (area_j == area_i && g_resolution_options[j].width < g_resolution_options[i].width)) {
                struct resolution_option temp = g_resolution_options[i];
                g_resolution_options[i] = g_resolution_options[j];
                g_resolution_options[j] = temp;
            }
        }
    }

    g_resolution_option_count = count;
}

struct start_menu_entry {
    const char *label;
    const char *meta;
    enum app_type type;
    enum start_menu_tab tab;
    const char *command;
};

static const struct start_menu_entry g_start_menu_entries[START_MENU_ENTRY_COUNT] = {
    {"Terminal", "Sistema", APP_TERMINAL, START_MENU_TAB_APPS, 0},
    {"Relogio", "Acessorios", APP_CLOCK, START_MENU_TAB_APPS, 0},
    {"Arquivos", "Sistema", APP_FILEMANAGER, START_MENU_TAB_APPS, 0},
    {"Editor", "Produtividade", APP_EDITOR, START_MENU_TAB_APPS, 0},
    {"Tasks", "Sistema", APP_TASKMANAGER, START_MENU_TAB_APPS, 0},
    {"Calculadora", "Acessorios", APP_CALCULATOR, START_MENU_TAB_APPS, 0},
    {"Imagens", "Midia", APP_IMAGEVIEWER, START_MENU_TAB_APPS, 0},
    {"Sketchpad", "Criacao", APP_SKETCHPAD, START_MENU_TAB_APPS, 0},
    {"Personalizar", "Desktop", APP_PERSONALIZE, START_MENU_TAB_APPS, 0},
    {"Snake", "Classicos", APP_SNAKE, START_MENU_TAB_GAMES, 0},
    {"Tetris", "Classicos", APP_TETRIS, START_MENU_TAB_GAMES, 0},
    {"Adventure", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "adventure"},
    {"Arithmetic", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "arithmetic"},
    {"ATC", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "atc"},
    {"Backgammon", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "backgammon"},
    {"Banner", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "banner"},
    {"BCD", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "bcd"},
    {"Battlestar", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "battlestar"},
    {"Boggle", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "boggle"},
    {"Battleship", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "bs"},
    {"Caesar", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "caesar"},
    {"Canfield", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "canfield"},
    {"Cribbage", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "cribbage"},
    {"Factor", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "factor"},
    {"Fish", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "fish"},
    {"Fortune", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "fortune"},
    {"Gomoku", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "gomoku"},
    {"GRDC", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "grdc"},
    {"Hack", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "hack"},
    {"Hangman", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "hangman"},
    {"Mille", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "mille"},
    {"Monop", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "monop"},
    {"Morse", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "morse"},
    {"Number", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "number"},
    {"Phantasia", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "phantasia"},
    {"Pig", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "pig"},
    {"Pom", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "pom"},
    {"PPT", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "ppt"},
    {"Primes", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "primes"},
    {"Quiz", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "quiz"},
    {"Rain", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "rain"},
    {"Random", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "random"},
    {"Robots", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "robots"},
    {"Sail", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "sail"},
    {"Snake BSD", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "snake-bsd"},
    {"Teachgammon", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "teachgammon"},
    {"Tetris BSD", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "tetris-bsd"},
    {"Trek", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "trek"},
    {"Wargames", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "wargames"},
    {"Worm", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "worm"},
    {"Worms", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "worms"},
    {"Wump", "BSD Port", APP_TERMINAL, START_MENU_TAB_GAMES, "wump"},
    {"Pacman", "Arcade", APP_PACMAN, START_MENU_TAB_GAMES, 0},
    {"Invaders", "Arcade", APP_SPACE_INVADERS, START_MENU_TAB_GAMES, 0},
    {"Pong", "Arcade", APP_PONG, START_MENU_TAB_GAMES, 0},
    {"Donkey Kong", "Arcade", APP_DONKEY_KONG, START_MENU_TAB_GAMES, 0},
    {"Brick Race", "Arcade", APP_BRICK_RACE, START_MENU_TAB_GAMES, 0},
    {"Flap Birb", "Arcade", APP_FLAP_BIRB, START_MENU_TAB_GAMES, 0},
    {"DOOM", "Port", APP_DOOM, START_MENU_TAB_GAMES, 0},
    {"Craft", "Port", APP_CRAFT, START_MENU_TAB_GAMES, 0}
};

static void sync_window_instance_rect(int widx);
static int alloc_window(enum app_type type);
static int find_window_by_type(enum app_type type);
static int raise_window_to_front(int widx, int *focused);
static int open_imageviewer_window_for_node(int node, int *focused);
static int open_window_or_focus_existing(enum app_type type, int *focused);
static int launch_start_menu_entry(const struct start_menu_entry *entry, int *focused);
static void append_uint_limited(char *buf, unsigned value, int max_len);
static void debug_window_event(const char *tag, int widx, enum app_type type, int instance);
static void clamp_mouse_state(struct mouse_state *mouse);

static int app_type_valid(enum app_type type) {
    return type > APP_NONE && type <= APP_TRASH;
}

static int window_instance_valid(enum app_type type, int instance) {
    switch (type) {
    case APP_TERMINAL: return instance >= 0 && instance < MAX_TERMINALS;
    case APP_CLOCK: return instance >= 0 && instance < MAX_CLOCKS;
    case APP_FILEMANAGER: return instance >= 0 && instance < MAX_FILEMANAGERS;
    case APP_EDITOR: return instance >= 0 && instance < MAX_EDITORS;
    case APP_TASKMANAGER: return instance >= 0 && instance < MAX_TASKMGRS;
    case APP_CALCULATOR: return instance >= 0 && instance < MAX_CALCULATORS;
    case APP_IMAGEVIEWER: return instance >= 0 && instance < MAX_IMAGEVIEWERS;
    case APP_SKETCHPAD: return instance >= 0 && instance < MAX_SKETCHPADS;
    case APP_SNAKE: return instance >= 0 && instance < MAX_SNAKES;
    case APP_TETRIS: return instance >= 0 && instance < MAX_TETRIS;
    case APP_PACMAN: return instance >= 0 && instance < MAX_PACMAN;
    case APP_SPACE_INVADERS: return instance >= 0 && instance < MAX_SPACE_INVADERS;
    case APP_PONG: return instance >= 0 && instance < MAX_PONG;
    case APP_DONKEY_KONG: return instance >= 0 && instance < MAX_DONKEY_KONG;
    case APP_BRICK_RACE: return instance >= 0 && instance < MAX_BRICK_RACE;
    case APP_FLAP_BIRB: return instance >= 0 && instance < MAX_FLAP_BIRB;
    case APP_DOOM: return instance >= 0 && instance < MAX_DOOM;
    case APP_CRAFT: return instance >= 0 && instance < MAX_CRAFT;
    case APP_PERSONALIZE: return instance == 0;
    case APP_TRASH: return instance == 0;
    default: return 0;
    }
}

static int sanitize_windows(int *focused) {
    int term_used[MAX_TERMINALS] = {0};
    int clock_used[MAX_CLOCKS] = {0};
    int fm_used[MAX_FILEMANAGERS] = {0};
    int editor_used[MAX_EDITORS] = {0};
    int tm_used[MAX_TASKMGRS] = {0};
    int calc_used[MAX_CALCULATORS] = {0};
    int imageviewer_used[MAX_IMAGEVIEWERS] = {0};
    int sketch_used[MAX_SKETCHPADS] = {0};
    int snake_used[MAX_SNAKES] = {0};
    int tetris_used[MAX_TETRIS] = {0};
    int pacman_used[MAX_PACMAN] = {0};
    int space_invaders_used[MAX_SPACE_INVADERS] = {0};
    int pong_used[MAX_PONG] = {0};
    int donkey_kong_used[MAX_DONKEY_KONG] = {0};
    int brick_race_used[MAX_BRICK_RACE] = {0};
    int flap_birb_used[MAX_FLAP_BIRB] = {0};
    int doom_used[MAX_DOOM] = {0};
    int craft_used[MAX_CRAFT] = {0};
    int pers_used = 0;
    int trash_used = 0;
    int changed = 0;

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        int duplicate = 0;

        if (!g_windows[i].active) {
            continue;
        }
        if (!app_type_valid(g_windows[i].type) ||
            !window_instance_valid(g_windows[i].type, g_windows[i].instance)) {
            debug_window_event(" sanitize-invalid", i, g_windows[i].type, g_windows[i].instance);
            g_windows[i].active = 0;
            if (*focused == i) {
                *focused = -1;
            }
            changed = 1;
            continue;
        }

        switch (g_windows[i].type) {
        case APP_TERMINAL:
            duplicate = term_used[g_windows[i].instance];
            term_used[g_windows[i].instance] = 1;
            break;
        case APP_CLOCK:
            duplicate = clock_used[g_windows[i].instance];
            clock_used[g_windows[i].instance] = 1;
            break;
        case APP_FILEMANAGER:
            duplicate = fm_used[g_windows[i].instance];
            fm_used[g_windows[i].instance] = 1;
            break;
        case APP_EDITOR:
            duplicate = editor_used[g_windows[i].instance];
            editor_used[g_windows[i].instance] = 1;
            break;
        case APP_TASKMANAGER:
            duplicate = tm_used[g_windows[i].instance];
            tm_used[g_windows[i].instance] = 1;
            break;
        case APP_CALCULATOR:
            duplicate = calc_used[g_windows[i].instance];
            calc_used[g_windows[i].instance] = 1;
            break;
        case APP_IMAGEVIEWER:
            duplicate = imageviewer_used[g_windows[i].instance];
            imageviewer_used[g_windows[i].instance] = 1;
            break;
        case APP_SKETCHPAD:
            duplicate = sketch_used[g_windows[i].instance];
            sketch_used[g_windows[i].instance] = 1;
            break;
        case APP_SNAKE:
            duplicate = snake_used[g_windows[i].instance];
            snake_used[g_windows[i].instance] = 1;
            break;
        case APP_TETRIS:
            duplicate = tetris_used[g_windows[i].instance];
            tetris_used[g_windows[i].instance] = 1;
            break;
        case APP_PACMAN:
            duplicate = pacman_used[g_windows[i].instance];
            pacman_used[g_windows[i].instance] = 1;
            break;
        case APP_SPACE_INVADERS:
            duplicate = space_invaders_used[g_windows[i].instance];
            space_invaders_used[g_windows[i].instance] = 1;
            break;
        case APP_PONG:
            duplicate = pong_used[g_windows[i].instance];
            pong_used[g_windows[i].instance] = 1;
            break;
        case APP_DONKEY_KONG:
            duplicate = donkey_kong_used[g_windows[i].instance];
            donkey_kong_used[g_windows[i].instance] = 1;
            break;
        case APP_BRICK_RACE:
            duplicate = brick_race_used[g_windows[i].instance];
            brick_race_used[g_windows[i].instance] = 1;
            break;
        case APP_FLAP_BIRB:
            duplicate = flap_birb_used[g_windows[i].instance];
            flap_birb_used[g_windows[i].instance] = 1;
            break;
        case APP_DOOM:
            duplicate = doom_used[g_windows[i].instance];
            doom_used[g_windows[i].instance] = 1;
            break;
        case APP_CRAFT:
            duplicate = craft_used[g_windows[i].instance];
            craft_used[g_windows[i].instance] = 1;
            break;
        case APP_PERSONALIZE:
            duplicate = pers_used;
            pers_used = 1;
            break;
        case APP_TRASH:
            duplicate = trash_used;
            trash_used = 1;
            break;
        default:
            duplicate = 1;
            break;
        }

        if (duplicate) {
            debug_window_event(" sanitize-dup", i, g_windows[i].type, g_windows[i].instance);
            g_windows[i].active = 0;
            if (*focused == i) {
                *focused = -1;
            }
            changed = 1;
        }
    }

    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = term_used[i];
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = clock_used[i];
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = fm_used[i];
    for (int i = 0; i < MAX_EDITORS; ++i) g_editor_used[i] = editor_used[i];
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = tm_used[i];
    for (int i = 0; i < MAX_CALCULATORS; ++i) g_calc_used[i] = calc_used[i];
    for (int i = 0; i < MAX_IMAGEVIEWERS; ++i) g_imageviewer_used[i] = imageviewer_used[i];
    for (int i = 0; i < MAX_SKETCHPADS; ++i) g_sketch_used[i] = sketch_used[i];
    for (int i = 0; i < MAX_SNAKES; ++i) g_snake_used[i] = snake_used[i];
    for (int i = 0; i < MAX_TETRIS; ++i) g_tetris_used[i] = tetris_used[i];
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = pacman_used[i];
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = space_invaders_used[i];
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = pong_used[i];
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = donkey_kong_used[i];
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = brick_race_used[i];
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = flap_birb_used[i];
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = doom_used[i];
    for (int i = 0; i < MAX_CRAFT; ++i) g_craft_used[i] = craft_used[i];
    g_pers_used = pers_used;
    g_trash_used = trash_used;

    return changed;
}

static int has_active_window_instance(enum app_type type, int instance) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (g_windows[i].type == type && g_windows[i].instance == instance) {
            return 1;
        }
    }
    return 0;
}

static void debug_append_int(char *buf, int value, int max_len) {
    if (value < 0) {
        str_append(buf, "-", max_len);
        value = -value;
    }
    append_uint_limited(buf, (unsigned)value, max_len);
}

static void debug_window_event(const char *tag, int widx, enum app_type type, int instance) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, "desktop:", (int)sizeof(msg));
    str_append(msg, tag, (int)sizeof(msg));
    str_append(msg, " w=", (int)sizeof(msg));
    debug_append_int(msg, widx, (int)sizeof(msg));
    str_append(msg, " t=", (int)sizeof(msg));
    debug_append_int(msg, (int)type, (int)sizeof(msg));
    str_append(msg, " i=", (int)sizeof(msg));
    debug_append_int(msg, instance, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void clamp_mouse_state(struct mouse_state *mouse) {
    if (mouse->x < 0) mouse->x = 0;
    if (mouse->y < 0) mouse->y = 0;
    if (mouse->x >= (int)SCREEN_WIDTH) mouse->x = (int)SCREEN_WIDTH - 1;
    if (mouse->y >= (int)SCREEN_HEIGHT) mouse->y = (int)SCREEN_HEIGHT - 1;
}

static int fs_child_by_name(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;

    while (child != -1) {
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }
    return -1;
}

static void append_uint_limited(char *buf, unsigned value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

static void make_unique_child_name(int parent, const char *base, char *out, int max_len) {
    unsigned suffix = 0u;

    for (;;) {
        int copy_len = max_len - 1;
        int base_len = str_len(base);
        int limit = base_len;

        if (suffix > 0u) {
            char digits[12] = "";
            int digit_len;

            append_uint_limited(digits, suffix, sizeof(digits));
            digit_len = str_len(digits);
            limit = copy_len - digit_len;
            if (limit < 1) {
                limit = 1;
            }
        }

        if (limit > base_len) {
            limit = base_len;
        }

        for (int i = 0; i < limit; ++i) {
            out[i] = base[i];
        }
        out[limit] = '\0';

        if (suffix > 0u) {
            append_uint_limited(out, suffix, max_len);
        }

        if (fs_child_by_name(parent, out) < 0) {
            return;
        }
        ++suffix;
    }
}

static void build_child_path(int parent, const char *name, char *out, int max_len) {
    fs_build_path(parent, out, max_len);
    if (!str_eq(out, "/")) {
        str_append(out, "/", max_len);
    }
    str_append(out, name, max_len);
}

static int create_node_in_directory(int parent, int is_dir, const char *base_name) {
    char name[FS_NAME_MAX + 1];
    char path[80];

    make_unique_child_name(parent, base_name, name, sizeof(name));
    build_child_path(parent, name, path, sizeof(path));
    if (fs_create(path, is_dir) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int clone_node_to_directory(int src_node, int dst_parent) {
    char name[FS_NAME_MAX + 1];
    char path[80];
    int created;

    if (src_node < 0 || !g_fs_nodes[src_node].used || dst_parent < 0 || !g_fs_nodes[dst_parent].is_dir) {
        return -1;
    }

    make_unique_child_name(dst_parent, g_fs_nodes[src_node].name, name, sizeof(name));
    build_child_path(dst_parent, name, path, sizeof(path));

    if (g_fs_nodes[src_node].is_dir) {
        int child;

        if (fs_create(path, 1) != 0) {
            return -1;
        }
        created = fs_resolve(path);
        if (created < 0) {
            return -1;
        }

        child = g_fs_nodes[src_node].first_child;
        while (child != -1) {
            if (clone_node_to_directory(child, created) != 0) {
                return -1;
            }
            child = g_fs_nodes[child].next_sibling;
        }
        return created;
    }

    if (fs_copy_node_to_path(src_node, path) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int node_is_descendant_of(int node, int ancestor) {
    int guard = 0;

    while (node >= 0 && ++guard <= FS_MAX_NODES) {
        if (node == ancestor) {
            return 1;
        }
        if (node == g_fs_root) {
            break;
        }
        node = g_fs_nodes[node].parent;
    }
    return 0;
}

static int trash_root_node(void) {
    int node = fs_resolve(TRASH_ROOT_PATH);

    if (node < 0) {
        if (fs_create(TRASH_ROOT_PATH, 1) != 0) {
            return -1;
        }
        node = fs_resolve(TRASH_ROOT_PATH);
    }

    if (node < 0 || !g_fs_nodes[node].used || !g_fs_nodes[node].is_dir) {
        return -1;
    }
    return node;
}

static int trash_entry_meta_node(int entry) {
    if (entry < 0 || !g_fs_nodes[entry].used || !g_fs_nodes[entry].is_dir) {
        return -1;
    }
    return fs_child_by_name(entry, TRASH_META_NAME);
}

static int trash_entry_item_node(int entry) {
    if (entry < 0 || !g_fs_nodes[entry].used || !g_fs_nodes[entry].is_dir) {
        return -1;
    }
    return fs_child_by_name(entry, TRASH_ITEM_NAME);
}

static int trash_read_entry_origin(int entry, char *out, int max_len) {
    int meta = trash_entry_meta_node(entry);
    int bytes_read;

    if (!out || max_len < 2 || meta < 0 || g_fs_nodes[meta].is_dir) {
        return -1;
    }

    bytes_read = fs_read_node_bytes(meta, 0, out, max_len - 1);
    if (bytes_read < 0) {
        return -1;
    }
    out[bytes_read] = '\0';
    return 0;
}

static void trash_split_path(const char *path,
                             char *parent_out,
                             int parent_len,
                             char *name_out,
                             int name_len) {
    int last_slash = -1;
    int i = 0;

    if (parent_len > 0) {
        parent_out[0] = '\0';
    }
    if (name_len > 0) {
        name_out[0] = '\0';
    }
    if (!path || path[0] == '\0') {
        str_copy_limited(parent_out, "/", parent_len);
        return;
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            last_slash = i;
        }
        ++i;
    }

    if (last_slash < 0) {
        str_copy_limited(parent_out, "/", parent_len);
        str_copy_limited(name_out, path, name_len);
        return;
    }
    if (last_slash == 0) {
        str_copy_limited(parent_out, "/", parent_len);
    } else {
        int copy_len = last_slash;

        if (copy_len > parent_len - 1) {
            copy_len = parent_len - 1;
        }
        for (int j = 0; j < copy_len; ++j) {
            parent_out[j] = path[j];
        }
        parent_out[copy_len] = '\0';
    }

    str_copy_limited(name_out, path + last_slash + 1, name_len);
}

static int trash_collect_entries(int *out_entries, int max_entries) {
    int root = trash_root_node();
    int child;
    int count = 0;

    if (root < 0 || !out_entries || max_entries <= 0) {
        return 0;
    }

    child = g_fs_nodes[root].first_child;
    while (child != -1 && count < max_entries) {
        if (g_fs_nodes[child].used && g_fs_nodes[child].is_dir) {
            out_entries[count++] = child;
        }
        child = g_fs_nodes[child].next_sibling;
    }

    return count;
}

static void trash_entry_display_name(int entry, char *out, int max_len) {
    char origin[160];
    char parent_path[160];
    char base_name[FS_NAME_MAX + 1];
    int item = trash_entry_item_node(entry);

    out[0] = '\0';
    if (trash_read_entry_origin(entry, origin, (int)sizeof(origin)) == 0) {
        trash_split_path(origin,
                         parent_path,
                         (int)sizeof(parent_path),
                         base_name,
                         (int)sizeof(base_name));
        if (base_name[0] != '\0') {
            str_copy_limited(out, base_name, max_len);
        }
    }

    if (out[0] == '\0' && item >= 0 && g_fs_nodes[item].used) {
        str_copy_limited(out, g_fs_nodes[item].name, max_len);
    }
    if (out[0] == '\0') {
        str_copy_limited(out, "(entrada)", max_len);
    }

    if (item >= 0 && g_fs_nodes[item].used && g_fs_nodes[item].is_dir) {
        str_append(out, "/", max_len);
    }
}

static int trash_delete_node_recursive(int node) {
    int child;
    char path[160];

    if (node < 0 || !g_fs_nodes[node].used) {
        return -1;
    }

    child = g_fs_nodes[node].first_child;
    while (child != -1) {
        int next = g_fs_nodes[child].next_sibling;

        if (trash_delete_node_recursive(child) != 0) {
            return -1;
        }
        child = next;
    }

    fs_build_path(node, path, (int)sizeof(path));
    return fs_remove(path);
}

static int trash_move_node_to_bin(int node) {
    int root = trash_root_node();
    char origin[160];
    char entry_name[FS_NAME_MAX + 1];
    char entry_path[80];
    char meta_path[96];
    int entry;

    if (node < 0 || !g_fs_nodes[node].used || node == g_fs_root || root < 0) {
        return -1;
    }
    if (node == root || node_is_descendant_of(node, root)) {
        return -1;
    }

    fs_build_path(node, origin, (int)sizeof(origin));
    make_unique_child_name(root, TRASH_ENTRY_BASE, entry_name, (int)sizeof(entry_name));
    build_child_path(root, entry_name, entry_path, (int)sizeof(entry_path));
    if (fs_create(entry_path, 1) != 0) {
        return -1;
    }

    entry = fs_resolve(entry_path);
    if (entry < 0) {
        return -1;
    }

    build_child_path(entry, TRASH_META_NAME, meta_path, (int)sizeof(meta_path));
    if (fs_write_file(meta_path, origin, 0) != 0) {
        (void)fs_remove(entry_path);
        return -1;
    }

    if (fs_move_node(node, entry, TRASH_ITEM_NAME) != 0) {
        (void)fs_remove(meta_path);
        (void)fs_remove(entry_path);
        return -1;
    }

    return 0;
}

static int trash_restore_entry(int entry, char *status, int status_len) {
    int item = trash_entry_item_node(entry);
    char origin[160];
    char parent_path[160];
    char base_name[FS_NAME_MAX + 1];
    char final_name[FS_NAME_MAX + 1];
    char restored_path[160];
    int parent;

    if (item < 0 || !g_fs_nodes[item].used) {
        str_copy_limited(status, "Entrada invalida", status_len);
        return -1;
    }
    if (trash_read_entry_origin(entry, origin, (int)sizeof(origin)) != 0) {
        str_copy_limited(status, "Origem da lixeira ausente", status_len);
        return -1;
    }

    trash_split_path(origin,
                     parent_path,
                     (int)sizeof(parent_path),
                     base_name,
                     (int)sizeof(base_name));
    if (base_name[0] == '\0') {
        str_copy_limited(base_name, "item", (int)sizeof(base_name));
    }

    parent = fs_resolve(parent_path);
    if (parent < 0 || !g_fs_nodes[parent].used || !g_fs_nodes[parent].is_dir) {
        parent = g_fs_root;
    }

    make_unique_child_name(parent, base_name, final_name, (int)sizeof(final_name));
    if (fs_move_node(item, parent, final_name) != 0) {
        str_copy_limited(status, "Falha ao restaurar", status_len);
        return -1;
    }
    if (trash_delete_node_recursive(entry) != 0) {
        str_copy_limited(status, "Restaurado, mas sobrou metadata", status_len);
        return 0;
    }

    fs_build_path(item, restored_path, (int)sizeof(restored_path));
    str_copy_limited(status, restored_path, status_len);
    return 0;
}

static int trash_delete_entry(int entry, char *status, int status_len) {
    if (entry < 0 || !g_fs_nodes[entry].used) {
        str_copy_limited(status, "Entrada invalida", status_len);
        return -1;
    }
    if (trash_delete_node_recursive(entry) != 0) {
        str_copy_limited(status, "Falha ao excluir", status_len);
        return -1;
    }
    str_copy_limited(status, "Arquivo removido", status_len);
    return 0;
}

static int trash_empty_all(char *status, int status_len) {
    int entries[FS_MAX_NODES];
    int count = trash_collect_entries(entries, FS_MAX_NODES);

    if (count == 0) {
        str_copy_limited(status, "Lixeira vazia", status_len);
        return 0;
    }

    for (int i = 0; i < count; ++i) {
        if (trash_delete_node_recursive(entries[i]) != 0) {
            str_copy_limited(status, "Falha ao esvaziar", status_len);
            return -1;
        }
    }

    str_copy_limited(status, "Lixeira esvaziada", status_len);
    return 0;
}

static int node_is_wallpaper_candidate(int node) {
    return image_node_is_supported(node);
}

static int find_wallpaper_nodes(int *out_nodes, int max_nodes) {
    int count = 0;

    (void)fs_resolve("/wallpaper.png");
    for (int i = 0; i < FS_MAX_NODES && count < max_nodes; ++i) {
        if (node_is_wallpaper_candidate(i)) {
            out_nodes[count++] = i;
        }
    }
    return count;
}

static const char *filemanager_menu_label(int action) {
    switch (action) {
    case FMENU_OPEN: return "Abrir";
    case FMENU_COPY: return "Copiar";
    case FMENU_PASTE: return "Colar";
    case FMENU_NEW_DIR: return "Novo diretorio";
    case FMENU_NEW_FILE: return "Novo arquivo";
    case FMENU_RENAME: return "Renomear";
    case FMENU_MOVE_TO_TRASH: return "Mover p/ lixeira";
    case FMENU_SET_WALLPAPER: return "Definir plano";
    default: return "";
    }
}

static const char *app_context_menu_label(enum app_type type, int action) {
    if (type == APP_EDITOR) {
        return action == APPCTX_PRIMARY ? "Salvar" : "Salvar como...";
    }
    if (type == APP_SKETCHPAD) {
        return action == APPCTX_PRIMARY ? "Exportar" : "Exportar como...";
    }
    return "";
}

static void set_dialog_status(struct file_dialog_state *dialog, const char *msg) {
    str_copy_limited(dialog->status, msg, (int)sizeof(dialog->status));
}

static int find_last_char(const char *text, char ch) {
    int last = -1;

    for (int i = 0; text[i] != '\0'; ++i) {
        if (text[i] == ch) {
            last = i;
        }
    }
    return last;
}

static void split_filename_parts(const char *filename,
                                 char *name,
                                 int name_len,
                                 char *ext,
                                 int ext_len) {
    int dot = -1;
    int len = str_len(filename);

    name[0] = '\0';
    ext[0] = '\0';
    if (filename == 0 || filename[0] == '\0') {
        return;
    }

    dot = find_last_char(filename, '.');
    if (dot > 0 && dot < len - 1) {
        for (int i = 0; i < dot && i < name_len - 1; ++i) {
            name[i] = filename[i];
            name[i + 1] = '\0';
        }
        str_copy_limited(ext, filename + dot + 1, ext_len);
        return;
    }

    str_copy_limited(name, filename, name_len);
}

static void extract_basename_parts(const char *path,
                                   char *name,
                                   int name_len,
                                   char *ext,
                                   int ext_len) {
    const char *base = path;

    if (path == 0) {
        name[0] = '\0';
        ext[0] = '\0';
        return;
    }
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    split_filename_parts(base, name, name_len, ext, ext_len);
}

static int build_filename_from_dialog(const struct file_dialog_state *dialog,
                                      char *out,
                                      int max_len) {
    int total;

    if (dialog->name[0] == '\0') {
        return 0;
    }
    total = str_len(dialog->name);
    if (dialog->ext[0] != '\0') {
        total += 1 + str_len(dialog->ext);
    }
    if (total > FS_NAME_MAX) {
        return 0;
    }

    str_copy_limited(out, dialog->name, max_len);
    if (dialog->ext[0] != '\0') {
        str_append(out, ".", max_len);
        str_append(out, dialog->ext, max_len);
    }
    return 1;
}

static struct rect app_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 116, 32};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect app_context_item_rect(const struct rect *menu, int action) {
    struct rect r = {menu->x + 2, menu->y + 2 + (action * 14), menu->w - 4, 12};
    return r;
}

static struct rect file_dialog_window_rect(void) {
    struct rect r = {(int)SCREEN_WIDTH / 2 - 140, (int)SCREEN_HEIGHT / 2 - 60, 280, 128};
    if (r.x < 8) r.x = 8;
    if (r.y < 8) r.y = 8;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 8) {
        r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h - 8;
    }
    return r;
}

static struct rect file_dialog_close_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 14, dialog->window.y + 2, 10, 10};
    return r;
}

static struct rect file_dialog_name_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 16, dialog->window.y + 34, 146, 16};
    return r;
}

static struct rect file_dialog_ext_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 178, dialog->window.y + 34, 86, 16};
    return r;
}

static struct rect file_dialog_path_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 16, dialog->window.y + 34, dialog->window.w - 32, 16};
    return r;
}

static struct rect file_dialog_ok_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 118, dialog->window.y + dialog->window.h - 24, 50, 14};
    return r;
}

static struct rect file_dialog_cancel_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 62, dialog->window.y + dialog->window.h - 24, 50, 14};
    return r;
}

static void file_dialog_reset(struct file_dialog_state *dialog) {
    dialog->active = 0;
    dialog->mode = FILE_DIALOG_NONE;
    dialog->owner_window = -1;
    dialog->target_node = -1;
    dialog->active_field = 0;
    dialog->title[0] = '\0';
    dialog->confirm[0] = '\0';
    dialog->name[0] = '\0';
    dialog->ext[0] = '\0';
    dialog->path[0] = '\0';
    dialog->status[0] = '\0';
}

static void file_dialog_open_editor(struct file_dialog_state *dialog, int window_index) {
    struct editor_state *ed = &g_editors[g_windows[window_index].instance];

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_EDITOR_SAVE;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Salvar documento", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Salvar", (int)sizeof(dialog->confirm));
    if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
        split_filename_parts(g_fs_nodes[ed->file_node].name,
                             dialog->name, (int)sizeof(dialog->name),
                             dialog->ext, (int)sizeof(dialog->ext));
    } else {
        str_copy_limited(dialog->name, "nota", (int)sizeof(dialog->name));
        str_copy_limited(dialog->ext, "txt", (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "Defina nome e extensao");
}

static void file_dialog_open_sketch(struct file_dialog_state *dialog, int window_index) {
    struct sketchpad_state *sketch = &g_sketches[g_windows[window_index].instance];

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_SKETCH_EXPORT;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Exportar bitmap", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Exportar", (int)sizeof(dialog->confirm));
    if (sketch->last_export_path[0] != '\0') {
        extract_basename_parts(sketch->last_export_path,
                               dialog->name, (int)sizeof(dialog->name),
                               dialog->ext, (int)sizeof(dialog->ext));
    } else {
        str_copy_limited(dialog->name, "sketch", (int)sizeof(dialog->name));
        str_copy_limited(dialog->ext, "bmp", (int)sizeof(dialog->ext));
    }
    if (dialog->ext[0] == '\0') {
        str_copy_limited(dialog->ext, "bmp", (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "Arquivo exportado em /docs");
}

static void file_dialog_open_rename(struct file_dialog_state *dialog, int window_index, int node) {
    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_FILE_RENAME;
    dialog->owner_window = window_index;
    dialog->target_node = node;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Renomear item", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Aplicar", (int)sizeof(dialog->confirm));
    if (node >= 0 && g_fs_nodes[node].used) {
        split_filename_parts(g_fs_nodes[node].name,
                             dialog->name, (int)sizeof(dialog->name),
                             dialog->ext, (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "O nome final deve caber em 15 chars");
}

static void file_dialog_open_wallpaper(struct file_dialog_state *dialog, int window_index) {
    int node = ui_wallpaper_source_node();

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_WALLPAPER_PATH;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Escolher wallpaper", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Aplicar", (int)sizeof(dialog->confirm));
    if (node >= 0 && g_fs_nodes[node].used) {
        fs_build_path(node, dialog->path, (int)sizeof(dialog->path));
    } else {
        str_copy_limited(dialog->path, "/wallpaper.png", (int)sizeof(dialog->path));
    }
    set_dialog_status(dialog, "Digite um caminho .bmp ou .png");
}

static int file_dialog_apply(struct file_dialog_state *dialog) {
    char filename[FS_NAME_MAX + 1];

    if (dialog->mode != FILE_DIALOG_WALLPAPER_PATH) {
        if (!build_filename_from_dialog(dialog, filename, (int)sizeof(filename))) {
            set_dialog_status(dialog, "Nome ou extensao invalido");
            return 0;
        }
    }

    if (dialog->mode == FILE_DIALOG_EDITOR_SAVE) {
        if (!editor_save_named(&g_editors[g_windows[dialog->owner_window].instance], filename)) {
            set_dialog_status(dialog, "Falha ao salvar");
            return 0;
        }
        return 1;
    }
    if (dialog->mode == FILE_DIALOG_SKETCH_EXPORT) {
        if (!sketchpad_export_bitmap_named(&g_sketches[g_windows[dialog->owner_window].instance], filename)) {
            set_dialog_status(dialog, "Falha ao exportar");
            return 0;
        }
        return 1;
    }
    if (dialog->mode == FILE_DIALOG_FILE_RENAME) {
        if (fs_rename_node(dialog->target_node, filename) != 0) {
            set_dialog_status(dialog, "Falha ao renomear");
            return 0;
        }
        return 1;
    }
    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        int node = fs_resolve(dialog->path);

        if (node < 0) {
            set_dialog_status(dialog, "Arquivo nao encontrado");
            return 0;
        }
        if (!node_is_wallpaper_candidate(node)) {
            set_dialog_status(dialog, "Use arquivo .bmp ou .png");
            return 0;
        }
        if (ui_wallpaper_set_from_node(node) != 0) {
            set_dialog_status(dialog, "Falha ao aplicar imagem");
            return 0;
        }
        return 1;
    }
    return 0;
}

static int file_dialog_accepts_char(const struct file_dialog_state *dialog, int key) {
    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        return key >= 32 && key <= 126;
    }
    return key >= 32 && key <= 126 && key != '/' && key != '.';
}

static void file_dialog_insert_char(struct file_dialog_state *dialog, int key) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int max_len = dialog->active_field == 0 ? (int)sizeof(dialog->name) : (int)sizeof(dialog->ext);
    int len = str_len(dst);

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        dst = dialog->path;
        max_len = (int)sizeof(dialog->path);
        len = str_len(dst);
    }

    if (!file_dialog_accepts_char(dialog, key) || len >= max_len - 1) {
        return;
    }
    dst[len] = (char)key;
    dst[len + 1] = '\0';
}

static void file_dialog_backspace(struct file_dialog_state *dialog) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int len = str_len(dst);

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        dst = dialog->path;
        len = str_len(dst);
    }

    if (len > 0) {
        dst[len - 1] = '\0';
    }
}

static void draw_file_dialog(const struct file_dialog_state *dialog, const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {dialog->window.x + 4, dialog->window.y + 18, dialog->window.w - 8, dialog->window.h - 22};
    struct rect title = {dialog->window.x + 2, dialog->window.y + 2, dialog->window.w - 4, 12};
    struct rect close = file_dialog_close_rect(dialog);
    struct rect name_field = file_dialog_name_rect(dialog);
    struct rect ext_field = file_dialog_ext_rect(dialog);
    struct rect path_field = file_dialog_path_rect(dialog);
    struct rect ok = file_dialog_ok_rect(dialog);
    struct rect cancel = file_dialog_cancel_rect(dialog);
    int close_hover = point_in_rect(&close, mouse->x, mouse->y);
    int ok_hover = point_in_rect(&ok, mouse->x, mouse->y);
    int cancel_hover = point_in_rect(&cancel, mouse->x, mouse->y);
    int name_active = dialog->active_field == 0;
    int ext_active = dialog->active_field == 1;
    int name_len = str_len(dialog->name);
    int ext_len = str_len(dialog->ext);

    ui_draw_surface(&dialog->window, ui_color_panel());
    sys_rect(title.x, title.y, title.w, title.h, theme->window);
    sys_rect(title.x, title.y + title.h - 1, title.w, 1, 0);
    sys_text(dialog->window.x + 8, dialog->window.y + 4, theme->text, dialog->title);
    ui_draw_button(&close, "X", UI_BUTTON_DANGER, close_hover);
    ui_draw_surface(&body, ui_color_window_bg());

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        int path_len = str_len(dialog->path);

        sys_text(body.x + 8, body.y + 10, theme->text, "Arquivo");
        if (name_active) {
            sys_rect(path_field.x - 1, path_field.y - 1, path_field.w + 2, path_field.h + 2, theme->window);
        }
        ui_draw_inset(&path_field, ui_color_window_bg());
        sys_text(path_field.x + 4, path_field.y + 4, theme->text, dialog->path);
        if (name_active && path_len < (int)sizeof(dialog->path) - 1) {
            sys_rect(path_field.x + 4 + (path_len * 6), path_field.y + 12, 6, 1, theme->text);
        }
        sys_text(body.x + 8, body.y + 58, ui_color_muted(), "Preview");
        ui_draw_inset(&(struct rect){body.x + 8, body.y + 68, body.w - 16, 14}, ui_color_window_bg());
        sys_text(body.x + 12, body.y + 72, theme->text, dialog->path[0] != '\0' ? dialog->path : "(vazio)");
    } else {
        sys_text(body.x + 8, body.y + 10, theme->text, "Nome");
        sys_text(body.x + 170, body.y + 10, theme->text, "Ext");
        if (name_active) {
            sys_rect(name_field.x - 1, name_field.y - 1, name_field.w + 2, name_field.h + 2, theme->window);
        }
        if (ext_active) {
            sys_rect(ext_field.x - 1, ext_field.y - 1, ext_field.w + 2, ext_field.h + 2, theme->window);
        }
        ui_draw_inset(&name_field, ui_color_window_bg());
        ui_draw_inset(&ext_field, ui_color_window_bg());
        sys_text(name_field.x + 4, name_field.y + 4, theme->text, dialog->name);
        sys_text(ext_field.x + 4, ext_field.y + 4, theme->text, dialog->ext);
        if (name_active && name_len < FS_NAME_MAX) {
            sys_rect(name_field.x + 4 + (name_len * 6), name_field.y + 12, 6, 1, theme->text);
        }
        if (ext_active && ext_len < FS_NAME_MAX) {
            sys_rect(ext_field.x + 4 + (ext_len * 6), ext_field.y + 12, 6, 1, theme->text);
        }

        sys_text(body.x + 8, body.y + 58, ui_color_muted(), "Preview");
        {
            char preview[FS_NAME_MAX + 2] = "";
            build_filename_from_dialog(dialog, preview, (int)sizeof(preview));
            ui_draw_inset(&(struct rect){body.x + 8, body.y + 68, body.w - 16, 14}, ui_color_window_bg());
            sys_text(body.x + 12, body.y + 72, theme->text, preview[0] != '\0' ? preview : "(vazio)");
        }
    }
    ui_draw_status(&(struct rect){body.x + 8, body.y + 86, body.w - 16, 12}, dialog->status);
    ui_draw_button(&ok, dialog->confirm, UI_BUTTON_PRIMARY, ok_hover);
    ui_draw_button(&cancel, "Cancelar", UI_BUTTON_NORMAL, cancel_hover);
}

static int open_editor_window_for_node(int node, int *focused) {
    int idx = alloc_window(APP_EDITOR);

    if (idx < 0) {
        return -1;
    }
    if (node >= 0) {
        (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
    }
    sync_window_instance_rect(idx);
    *focused = idx;
    debug_window_event(" open-editor", idx, g_windows[idx].type, g_windows[idx].instance);
    return idx;
}

static int alloc_term(void) {
    for (int i = 0; i < MAX_TERMINALS; ++i) {
        if (!g_term_used[i]) {
            g_term_used[i] = 1;
            terminal_init_state(&g_terms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_clock(void) {
    for (int i = 0; i < MAX_CLOCKS; ++i) {
        if (!g_clock_used[i]) {
            g_clock_used[i] = 1;
            clock_init_state(&g_clocks[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_fm(void) {
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) {
        if (!g_fm_used[i]) {
            g_fm_used[i] = 1;
            filemanager_init_state(&g_fms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_editor(void) {
    for (int i = 0; i < MAX_EDITORS; ++i) {
        if (!g_editor_used[i]) {
            g_editor_used[i] = 1;
            editor_init_state(&g_editors[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tm(void) {
    for (int i = 0; i < MAX_TASKMGRS; ++i) {
        if (!g_tm_used[i]) {
            g_tm_used[i] = 1;
            taskmgr_init_state(&g_tms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_calc(void) {
    for (int i = 0; i < MAX_CALCULATORS; ++i) {
        if (!g_calc_used[i]) {
            g_calc_used[i] = 1;
            calculator_init_state(&g_calcs[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_imageviewer(void) {
    for (int i = 0; i < MAX_IMAGEVIEWERS; ++i) {
        if (!g_imageviewer_used[i]) {
            g_imageviewer_used[i] = 1;
            imageviewer_init_state(&g_imageviewers[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_sketch(void) {
    for (int i = 0; i < MAX_SKETCHPADS; ++i) {
        if (!g_sketch_used[i]) {
            g_sketch_used[i] = 1;
            sketchpad_init_state(&g_sketches[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_snake(void) {
    for (int i = 0; i < MAX_SNAKES; ++i) {
        if (!g_snake_used[i]) {
            g_snake_used[i] = 1;
            snake_init_state(&g_snakes[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tetris(void) {
    for (int i = 0; i < MAX_TETRIS; ++i) {
        if (!g_tetris_used[i]) {
            g_tetris_used[i] = 1;
            tetris_init_state(&g_tetris[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_pacman(void) {
    for (int i = 0; i < MAX_PACMAN; ++i) {
        if (!g_pacman_used[i]) {
            g_pacman_used[i] = 1;
            pacman_init_state(&g_pacman[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_space_invaders(void) {
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) {
        if (!g_space_invaders_used[i]) {
            g_space_invaders_used[i] = 1;
            space_invaders_init_state(&g_space_invaders[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_pong(void) {
    for (int i = 0; i < MAX_PONG; ++i) {
        if (!g_pong_used[i]) {
            g_pong_used[i] = 1;
            pong_init_state(&g_pong[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_donkey_kong(void) {
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) {
        if (!g_donkey_kong_used[i]) {
            g_donkey_kong_used[i] = 1;
            donkey_kong_init_state(&g_donkey_kong[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_brick_race(void) {
    for (int i = 0; i < MAX_BRICK_RACE; ++i) {
        if (!g_brick_race_used[i]) {
            g_brick_race_used[i] = 1;
            brick_race_init_state(&g_brick_race[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_flap_birb(void) {
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) {
        if (!g_flap_birb_used[i]) {
            g_flap_birb_used[i] = 1;
            flap_birb_init_state(&g_flap_birb[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_doom(void) {
    for (int i = 0; i < MAX_DOOM; ++i) {
        if (!g_doom_used[i]) {
            g_doom_used[i] = 1;
            doom_init_state(&g_doom[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_craft(void) {
    for (int i = 0; i < MAX_CRAFT; ++i) {
        if (!g_craft_used[i]) {
            g_craft_used[i] = 1;
            craft_init_state(&g_craft[i]);
            return i;
        }
    }
    return -1;
}

void desktop_request_open_editor(const char *path) {
    if (path == 0) {
        g_launch_editor_path[0] = '\0';
    } else {
        str_copy_limited(g_launch_editor_path, path, (int)sizeof(g_launch_editor_path));
    }
    g_launch_editor_nano = 0;
    g_launch_editor_pending = 1;
}

void desktop_request_open_nano(const char *path) {
    if (path == 0) {
        g_launch_editor_path[0] = '\0';
    } else {
        str_copy_limited(g_launch_editor_path, path, (int)sizeof(g_launch_editor_path));
    }
    g_launch_editor_nano = 1;
    g_launch_editor_pending = 1;
}

void desktop_request_open_app(enum app_type type) {
    if (!app_type_valid(type)) {
        return;
    }
    g_launch_app_type = type;
}

static void desktop_request_open_terminal_command(const char *command) {
    if (!command) {
        return;
    }
    str_copy_limited(g_launch_terminal_command, command, (int)sizeof(g_launch_terminal_command));
    g_launch_terminal_pending = g_launch_terminal_command[0] != '\0';
}

static int desktop_process_pending_launches(int *focused) {
    int dirty = 0;

    if (!focused) {
        return 0;
    }

    if (g_launch_app_type != APP_NONE) {
        if (open_window_or_focus_existing(g_launch_app_type, focused) >= 0) {
            dirty = 1;
        }
        g_launch_app_type = APP_NONE;
    }

    if (g_launch_editor_pending) {
        int idx = open_editor_window_for_node(-1, focused);

        if (idx >= 0) {
            editor_set_nano_mode(&g_editors[g_windows[idx].instance], g_launch_editor_nano);
        }
        if (idx >= 0 && g_launch_editor_path[0] != '\0') {
            int node = fs_resolve(g_launch_editor_path);

            if (node >= 0 && !g_fs_nodes[node].is_dir) {
                (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
            }
        }
        g_launch_editor_pending = 0;
        g_launch_editor_nano = 0;
        g_launch_editor_path[0] = '\0';
        dirty = 1;
    }

    if (g_launch_terminal_pending) {
        int idx = open_window_or_focus_existing(APP_TERMINAL, focused);

        if (idx >= 0) {
            terminal_run_command(&g_terms[g_windows[idx].instance], g_launch_terminal_command, 1);
            dirty = 1;
        }
        g_launch_terminal_pending = 0;
        g_launch_terminal_command[0] = '\0';
    }

    return dirty;
}

static void sync_window_instance_rect(int widx) {
    switch (g_windows[widx].type) {
    case APP_TERMINAL:
        g_terms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CLOCK:
        g_clocks[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_FILEMANAGER:
        g_fms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_EDITOR:
        g_editors[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_TASKMANAGER:
        g_tms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CALCULATOR:
        g_calcs[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_IMAGEVIEWER:
        g_imageviewers[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SKETCHPAD:
        g_sketches[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SNAKE:
        g_snakes[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_TETRIS:
        g_tetris[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PACMAN:
        g_pacman[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SPACE_INVADERS:
        g_space_invaders[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PONG:
        g_pong[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_DONKEY_KONG:
        g_donkey_kong[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_BRICK_RACE:
        g_brick_race[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_FLAP_BIRB:
        g_flap_birb[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_DOOM:
        g_doom[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CRAFT:
        g_craft[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PERSONALIZE:
        g_pers.window = g_windows[widx].rect;
        break;
    case APP_TRASH:
        g_trash.window = g_windows[widx].rect;
        break;
    default:
        break;
    }
}

static void clamp_window_rect(struct rect *r) {
    int max_w = (int)SCREEN_WIDTH;
    int max_h = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;
    int max_x;
    int max_y;

    if (r->w < WINDOW_MIN_W) r->w = WINDOW_MIN_W;
    if (r->h < WINDOW_MIN_H) r->h = WINDOW_MIN_H;
    if (r->w > max_w) r->w = max_w;
    if (r->h > max_h) r->h = max_h;
    max_x = (int)SCREEN_WIDTH - r->w;
    max_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r->h;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (r->x < 0) r->x = 0;
    if (r->y < 0) r->y = 0;
    if (r->x > max_x) r->x = max_x;
    if (r->y > max_y) r->y = max_y;
}

static struct rect maximized_rect(void) {
    struct rect r = {6, 6, (int)SCREEN_WIDTH - 12, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 12};
    if (r.w < WINDOW_MIN_W) r.w = WINDOW_MIN_W;
    if (r.h < WINDOW_MIN_H) r.h = WINDOW_MIN_H;
    return r;
}

static int alloc_window(enum app_type type) {
    if (!app_type_valid(type)) {
        debug_window_event(" alloc-bad-type", -1, type, -1);
        return -1;
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            int dx = 20 * i;
            int dy = 12 * i;
            int instance = 0;
            struct rect rect = {0, 0, 0, 0};

            switch (type) {
            case APP_TERMINAL: {
                int idx = alloc_term();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_terms[idx].window;
            } break;
            case APP_CLOCK: {
                int idx = alloc_clock();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_clocks[idx].window;
            } break;
            case APP_FILEMANAGER: {
                int idx = alloc_fm();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_fms[idx].window;
            } break;
            case APP_EDITOR: {
                int idx = alloc_editor();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_editors[idx].window;
            } break;
            case APP_TASKMANAGER: {
                int idx = alloc_tm();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_tms[idx].window;
            } break;
            case APP_CALCULATOR: {
                int idx = alloc_calc();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_calcs[idx].window;
            } break;
            case APP_IMAGEVIEWER: {
                int idx = alloc_imageviewer();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_imageviewers[idx].window;
            } break;
            case APP_SKETCHPAD: {
                int idx = alloc_sketch();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_sketches[idx].window;
            } break;
            case APP_SNAKE: {
                int idx = alloc_snake();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_snakes[idx].window;
            } break;
            case APP_TETRIS: {
                int idx = alloc_tetris();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_tetris[idx].window;
            } break;
            case APP_PACMAN: {
                int idx = alloc_pacman();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_pacman[idx].window;
            } break;
            case APP_SPACE_INVADERS: {
                int idx = alloc_space_invaders();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_space_invaders[idx].window;
            } break;
            case APP_PONG: {
                int idx = alloc_pong();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_pong[idx].window;
            } break;
            case APP_DONKEY_KONG: {
                int idx = alloc_donkey_kong();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_donkey_kong[idx].window;
            } break;
            case APP_BRICK_RACE: {
                int idx = alloc_brick_race();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_brick_race[idx].window;
            } break;
            case APP_FLAP_BIRB: {
                int idx = alloc_flap_birb();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_flap_birb[idx].window;
            } break;
            case APP_DOOM: {
                int idx = alloc_doom();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_doom[idx].window;
            } break;
            case APP_CRAFT: {
                int idx = alloc_craft();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_craft[idx].window;
            } break;
            case APP_PERSONALIZE:
                if (g_pers_used) return -1;
                g_pers_used = 1;
                g_pers.window = (struct rect){16, 16, 424, 372};
                g_pers.selected_slot = THEME_SLOT_BACKGROUND;
                g_pers.color_picker_open = 0;
                g_pers.color_picker_start_x = 0;
                g_pers.color_picker_start_y = 0;
                refresh_resolution_options();
                set_personalize_resolution_status(g_resolution_can_set ?
                                                  "Aplicacao imediata" :
                                                  "driver atual: resolucao so no boot");
                instance = 0;
                rect = g_pers.window;
                break;
            case APP_TRASH:
                if (g_trash_used) return -1;
                g_trash_used = 1;
                g_trash.window = (struct rect){28, 28, 456, 324};
                g_trash.selected_entry = -1;
                g_trash.status[0] = '\0';
                instance = 0;
                rect = g_trash.window;
                break;
            default:
                return -1;
            }

            g_windows[i].active = 1;
            g_windows[i].type = type;
            g_windows[i].instance = instance;
            g_windows[i].start_ticks = sys_ticks();
            g_windows[i].minimized = 0;
            g_windows[i].maximized = 0;
            g_windows[i].rect = rect;
            g_windows[i].rect.x += dx;
            g_windows[i].rect.y += dy;
            clamp_window_rect(&g_windows[i].rect);
            g_windows[i].restore_rect = g_windows[i].rect;
            sync_window_instance_rect(i);
            debug_window_event(" alloc-ok", i, type, instance);
            return i;
        }
    }
    debug_window_event(" alloc-no-slot", -1, type, -1);
    return -1;
}

static int open_window_or_focus_existing(enum app_type type, int *focused) {
    int idx = alloc_window(type);

    if (idx >= 0) {
        *focused = idx;
        debug_window_event(" open-new", idx, g_windows[idx].type, g_windows[idx].instance);
        return idx;
    }

    idx = find_window_by_type(type);
    if (idx >= 0) {
        if (g_windows[idx].minimized) {
            g_windows[idx].minimized = 0;
        }
        *focused = raise_window_to_front(idx, focused);
        debug_window_event(" open-focus", *focused, g_windows[*focused].type, g_windows[*focused].instance);
        return *focused;
    }

    debug_window_event(" open-fail", -1, type, -1);
    return -1;
}

static int open_imageviewer_window_for_node(int node, int *focused) {
    int idx = find_window_by_type(APP_IMAGEVIEWER);

    if (idx < 0) {
        idx = alloc_window(APP_IMAGEVIEWER);
        if (idx < 0) {
            return -1;
        }
    }

    if (node >= 0) {
        (void)imageviewer_open_node(&g_imageviewers[g_windows[idx].instance], node);
    }

    if (g_windows[idx].minimized) {
        g_windows[idx].minimized = 0;
    }

    *focused = raise_window_to_front(idx, focused);
    debug_window_event(" open-image", *focused, g_windows[*focused].type, g_windows[*focused].instance);
    return *focused;
}

static void free_window(int widx) {
    struct window *w = &g_windows[widx];

    if (!w->active) return;
    debug_window_event(" free", widx, w->type, w->instance);
    switch (w->type) {
    case APP_TERMINAL: g_term_used[w->instance] = 0; break;
    case APP_CLOCK: g_clock_used[w->instance] = 0; break;
    case APP_FILEMANAGER: g_fm_used[w->instance] = 0; break;
    case APP_EDITOR: g_editor_used[w->instance] = 0; break;
    case APP_TASKMANAGER: g_tm_used[w->instance] = 0; break;
    case APP_CALCULATOR: g_calc_used[w->instance] = 0; break;
    case APP_IMAGEVIEWER:
        imageviewer_shutdown_state(&g_imageviewers[w->instance]);
        g_imageviewer_used[w->instance] = 0;
        break;
    case APP_SKETCHPAD: g_sketch_used[w->instance] = 0; break;
    case APP_SNAKE: g_snake_used[w->instance] = 0; break;
    case APP_TETRIS: g_tetris_used[w->instance] = 0; break;
    case APP_PACMAN: g_pacman_used[w->instance] = 0; break;
    case APP_SPACE_INVADERS: g_space_invaders_used[w->instance] = 0; break;
    case APP_PONG: g_pong_used[w->instance] = 0; break;
    case APP_DONKEY_KONG: g_donkey_kong_used[w->instance] = 0; break;
    case APP_BRICK_RACE: g_brick_race_used[w->instance] = 0; break;
    case APP_FLAP_BIRB: g_flap_birb_used[w->instance] = 0; break;
    case APP_DOOM: g_doom_used[w->instance] = 0; break;
    case APP_CRAFT:
        craft_shutdown_state(&g_craft[w->instance]);
        g_craft_used[w->instance] = 0;
        break;
    case APP_PERSONALIZE: g_pers_used = 0; break;
    case APP_TRASH: g_trash_used = 0; break;
    default: break;
    }
    w->active = 0;
}

static int find_window_by_type(enum app_type type) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (g_windows[i].active && g_windows[i].type == type) {
            return i;
        }
    }
    return -1;
}

static int launch_start_menu_entry(const struct start_menu_entry *entry, int *focused) {
    if (!entry) {
        return -1;
    }
    if (entry->command && entry->command[0] != '\0') {
        desktop_request_open_terminal_command(entry->command);
        return 0;
    }
    return open_window_or_focus_existing(entry->type, focused);
}

static int topmost_window_at(int x, int y) {
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        if (g_windows[i].active &&
            !g_windows[i].minimized &&
            point_in_rect(&g_windows[i].rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static int raise_window_to_front(int widx, int *focused) {
    int i = widx;

    while (i + 1 < MAX_WINDOWS && g_windows[i + 1].active) {
        struct window tmp = g_windows[i];
        g_windows[i] = g_windows[i + 1];
        g_windows[i + 1] = tmp;
        if (*focused == i) {
            *focused = i + 1;
        } else if (*focused == i + 1) {
            *focused = i;
        }
        ++i;
    }
    return i;
}

static struct rect window_title_bar(const struct rect *w) {
    struct rect bar = {w->x, w->y, w->w, 14};
    return bar;
}

static struct rect taskbar_button_rect_for_window(int win_index) {
    struct rect r = {0, 0, 0, 0};
    int x = 66;

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (i == win_index) {
            r.x = x;
            r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3;
            r.w = 68;
            r.h = 16;
            return r;
        }
        x += 72;
    }
    return r;
}

static struct rect desktop_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 116, 20};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect personalize_window_slot_rect(const struct rect *w, int slot) {
    int col = slot % 2;
    int row = slot / 2;
    struct rect r = {w->x + 26 + (col * 100), w->y + 58 + (row * 38), 88, 32};
    return r;
}

static struct rect personalize_window_wallpaper_button_rect(const struct rect *w, int index) {
    struct rect r = {w->x + 242, w->y + 132 + (index * 14), 160, 12};
    return r;
}

static struct rect personalize_window_wallpaper_choose_rect(const struct rect *w) {
    return personalize_window_wallpaper_button_rect(w, 4);
}

static struct rect personalize_window_resolution_button_rect(const struct rect *w, int index) {
    struct rect r = {w->x + 242, w->y + 216 + (index * 16), 160, 12};
    return r;
}

static struct rect personalize_color_picker_rect(void) {
    struct rect r = {(int)SCREEN_WIDTH / 2 - 98, (int)SCREEN_HEIGHT / 2 - 100, 200, 220};
    return r;
}

static struct rect personalize_color_swatch_rect(const struct rect *picker, int idx) {
    int col = idx % 16;
    int row = idx / 16;
    struct rect r = {picker->x + 4 + (col * 12), picker->y + 24 + (row * 12), 10, 10};
    return r;
}

static struct rect trash_window_list_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + 12, state->window.y + 64, state->window.w - 116, state->window.h - 98};
    return r;
}

static struct rect trash_window_row_rect(const struct trash_state *state, int row) {
    struct rect list = trash_window_list_rect(state);
    struct rect r = {list.x, list.y + (row * 16), list.w, 14};
    return r;
}

static struct rect trash_window_restore_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 68, 78, 14};
    return r;
}

static struct rect trash_window_delete_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 88, 78, 14};
    return r;
}

static struct rect trash_window_empty_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 108, 78, 14};
    return r;
}

static struct rect trash_window_status_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + 12, state->window.y + state->window.h - 28, state->window.w - 24, 14};
    return r;
}

static struct rect filemanager_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 112, g_fm_context_has_wallpaper_action ? 116 : 102};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect filemanager_context_item_rect(const struct rect *menu, int action) {
    struct rect r = {menu->x + 2, menu->y + 2 + (action * 14), menu->w - 4, 12};
    return r;
}

static int start_menu_contains_ci(const char *text, const char *needle) {
    int needle_len = str_len(needle);

    if (needle_len <= 0) {
        return 1;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
        int match = 1;
        for (int j = 0; j < needle_len; ++j) {
            if (text[i + j] == '\0' || to_upper(text[i + j]) != to_upper(needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            return 1;
        }
    }
    return 0;
}

static int start_menu_search_active(const char *query) {
    return query != 0 && query[0] != '\0';
}

static struct rect start_menu_header_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 10, menu.y + 10, menu.w - 20, 48};
    return r;
}

static struct rect start_menu_user_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + 16, 194, 34};
    return r;
}

static struct rect start_menu_tab_rect(int tab) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12 + (tab * 88), menu.y + 64, 82, 18};
    return r;
}

static struct rect start_menu_list_panel_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + 88, 202, menu.h - 146};
    return r;
}

static struct rect start_menu_list_view_rect(void) {
    struct rect panel = start_menu_list_panel_rect();
    struct rect r = {panel.x + 4, panel.y + 4, panel.w - 18, panel.h - 8};
    return r;
}

static struct rect start_menu_search_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + menu.h - 50, 202, 28};
    return r;
}

static struct rect start_menu_search_clear_rect(void) {
    struct rect box = start_menu_search_rect();
    struct rect r = {box.x + box.w - 20, box.y + 6, 12, 12};
    return r;
}

static struct rect start_menu_sidebar_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + menu.w - 108, menu.y + 16, 88, menu.h - 32};
    return r;
}

static struct rect start_menu_sidebar_button_rect(int index) {
    struct rect side = start_menu_sidebar_rect();
    struct rect r = {side.x + 8, side.y + 94 + (index * 28), side.w - 16, 22};
    return r;
}

static struct rect start_menu_logout_rect(void) {
    struct rect side = start_menu_sidebar_rect();
    struct rect r = {side.x + 8, side.y + side.h - 30, side.w - 16, 20};
    return r;
}

static struct rect start_menu_scroll_track_rect(void) {
    struct rect panel = start_menu_list_panel_rect();
    struct rect r = {panel.x + panel.w - 12, panel.y + 4, START_MENU_SCROLLBAR_W, panel.h - 8};
    return r;
}

static int start_menu_visible_count(void) {
    struct rect view = start_menu_list_view_rect();
    int visible = view.h / 36;

    if (visible < 1) {
        visible = 1;
    }
    return visible;
}

static int start_menu_scroll_limit_for_count(int count) {
    int limit = count - start_menu_visible_count();

    if (limit < 0) {
        return 0;
    }
    return limit;
}

static void start_menu_clamp_scroll(int *scroll_offset, int count) {
    int limit = start_menu_scroll_limit_for_count(count);

    if (*scroll_offset < 0) {
        *scroll_offset = 0;
    }
    if (*scroll_offset > limit) {
        *scroll_offset = limit;
    }
}

static struct rect start_menu_item_rect_visible(int visible_slot) {
    struct rect view = start_menu_list_view_rect();
    struct rect r = {view.x, view.y + (visible_slot * 36), view.w, 32};
    return r;
}

static int start_menu_entry_matches(const struct start_menu_entry *entry,
                                    enum start_menu_tab active_tab,
                                    const char *query) {
    if (!start_menu_search_active(query) && entry->tab != active_tab) {
        return 0;
    }
    if (!start_menu_search_active(query)) {
        return 1;
    }
    return start_menu_contains_ci(entry->label, query) || start_menu_contains_ci(entry->meta, query);
}

static int start_menu_build_filtered_indices(enum start_menu_tab active_tab,
                                             const char *query,
                                             int *indices,
                                             int max_indices) {
    int count = 0;

    for (int i = 0; i < START_MENU_ENTRY_COUNT && count < max_indices; ++i) {
        if (!start_menu_entry_matches(&g_start_menu_entries[i], active_tab, query)) {
            continue;
        }
        indices[count++] = i;
    }
    return count;
}

static struct rect start_menu_scroll_thumb_rect(int result_count, int scroll_offset) {
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = track;
    int visible = start_menu_visible_count();
    int limit = start_menu_scroll_limit_for_count(result_count);
    int thumb_h;
    int travel;

    if (result_count <= visible) {
        return thumb;
    }

    thumb_h = (track.h * visible) / result_count;
    if (thumb_h < 18) {
        thumb_h = 18;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }
    travel = track.h - thumb_h;
    thumb.h = thumb_h;
    thumb.y = track.y + ((travel * scroll_offset) / (limit > 0 ? limit : 1));
    return thumb;
}

static int start_menu_result_at_point(int filtered_count,
                                      int scroll_offset,
                                      int x,
                                      int y) {
    int visible = start_menu_visible_count();

    for (int slot = 0; slot < visible; ++slot) {
        int result_index = scroll_offset + slot;
        struct rect item;

        if (result_index >= filtered_count) {
            break;
        }
        item = start_menu_item_rect_visible(slot);
        if (point_in_rect(&item, x, y)) {
            return result_index;
        }
    }
    return -1;
}

static int start_menu_scroll_from_thumb_y(int result_count, int thumb_y) {
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = start_menu_scroll_thumb_rect(result_count, 0);
    int limit = start_menu_scroll_limit_for_count(result_count);
    int travel = track.h - thumb.h;
    int relative = thumb_y - track.y;

    if (travel <= 0 || limit <= 0) {
        return 0;
    }
    if (relative < 0) {
        relative = 0;
    }
    if (relative > travel) {
        relative = travel;
    }
    return (relative * limit) / travel;
}

static void start_menu_append_search_char(char *query, int *len, int key) {
    if (*len >= START_MENU_SEARCH_MAX || key < 32 || key > 126) {
        return;
    }
    query[*len] = (char)key;
    *len += 1;
    query[*len] = '\0';
}

static void start_menu_backspace(char *query, int *len) {
    if (*len <= 0) {
        return;
    }
    *len -= 1;
    query[*len] = '\0';
}

static void draw_start_menu_with_tab(enum start_menu_tab active_tab,
                                     const int *filtered_indices,
                                     int filtered_count,
                                     int hovered_result,
                                     int apps_tab_hover,
                                     int games_tab_hover,
                                     int scroll_offset,
                                     const char *query,
                                     int search_hover,
                                     int clear_hover,
                                     int sidebar_hover,
                                     int sidebar_files_hover,
                                     int sidebar_terminal_hover,
                                     int sidebar_personalize_hover,
                                     int logout_hover,
                                     int scroll_thumb_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect menu_rect = ui_start_menu_rect();
    struct rect header = start_menu_header_rect();
    struct rect user = start_menu_user_rect();
    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
    struct rect list_panel = start_menu_list_panel_rect();
    struct rect search = start_menu_search_rect();
    struct rect search_clear = start_menu_search_clear_rect();
    struct rect sidebar = start_menu_sidebar_rect();
    struct rect logout = start_menu_logout_rect();
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = start_menu_scroll_thumb_rect(filtered_count, scroll_offset);
    struct rect sidebar_files = start_menu_sidebar_button_rect(0);
    struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
    struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
    int visible = start_menu_visible_count();

    ui_draw_surface(&menu_rect, ui_color_panel());
    ui_draw_surface(&header, theme->menu);
    ui_draw_surface(&user, theme->window);
    sys_text(user.x + 10, user.y + 8, theme->text, "VibeOS");
    sys_text(user.x + 10, user.y + 20, theme->text, "Tudo pronto para abrir apps");

    ui_draw_button(&apps_tab,
                   "Programas",
                   active_tab == START_MENU_TAB_APPS ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   apps_tab_hover);
    ui_draw_button(&games_tab,
                   "Jogos",
                   active_tab == START_MENU_TAB_GAMES ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   games_tab_hover);

    ui_draw_inset(&list_panel, ui_color_window_bg());
    ui_draw_inset(&search, ui_color_window_bg());
    ui_draw_surface(&sidebar, ui_color_window_bg());
    sys_text(sidebar.x + 10, sidebar.y + 10, theme->text, "Painel");
    sys_text(sidebar.x + 10, sidebar.y + 24, theme->text,
             start_menu_search_active(query) ? "Busca global" :
             (active_tab == START_MENU_TAB_APPS ? "Apps do sistema" : "Biblioteca de jogos"));
    sys_rect(sidebar.x + 8, sidebar.y + 44, sidebar.w - 16, 1, ui_color_muted());
    ui_draw_button(&sidebar_files, "Arquivos", UI_BUTTON_NORMAL, sidebar_files_hover);
    ui_draw_button(&sidebar_terminal, "Terminal", UI_BUTTON_NORMAL, sidebar_terminal_hover);
    ui_draw_button(&sidebar_personalize, "Personalizar", UI_BUTTON_NORMAL, sidebar_personalize_hover);
    ui_draw_button(&logout, "Encerrar", UI_BUTTON_DANGER, logout_hover);

    sys_text(search.x + 8, search.y + 6, theme->text, "Pesquisar");
    sys_text(search.x + 8, search.y + 15, theme->text, query[0] != '\0' ? query : "digite para filtrar apps e jogos");
    if (query[0] != '\0') {
        ui_draw_button(&search_clear, "X", UI_BUTTON_NORMAL, clear_hover);
    } else if (search_hover) {
        sys_rect(search.x + 6, search.y + search.h - 5, search.w - 12, 1, ui_color_muted());
    }

    if (filtered_count <= 0) {
        sys_text(list_panel.x + 18, list_panel.y + 18, theme->text, "Nenhum resultado encontrado");
    } else {
        for (int slot = 0; slot < visible; ++slot) {
            int result_index = scroll_offset + slot;
            int entry_index;
            struct rect item;

            if (result_index >= filtered_count) {
                break;
            }
            entry_index = filtered_indices[result_index];
            item = start_menu_item_rect_visible(slot);
            ui_draw_button(&item,
                           g_start_menu_entries[entry_index].label,
                           hovered_result == result_index ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                           hovered_result == result_index);
            sys_text(item.x + 8, item.y + 20, theme->text, g_start_menu_entries[entry_index].meta);
        }
    }

    if (filtered_count > visible) {
        sys_rect(track.x, track.y, track.w, track.h, ui_color_muted());
        sys_rect(thumb.x, thumb.y, thumb.w, thumb.h, scroll_thumb_hover ? theme->window : theme->menu_button);
    }

    (void)sidebar_hover;
}

static void draw_personalize_window(struct personalize_state *state,
                                    int active,
                                    int min_hover,
                                    int max_hover,
                                    int close_hover,
                                    const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    int wallpaper_nodes[3];
    int wallpaper_count = find_wallpaper_nodes(wallpaper_nodes, 3);
    int current_wallpaper = ui_wallpaper_source_node();
    int current_in_quick_list = current_wallpaper < 0;
    uint8_t selected_color = theme->background;
    struct rect body = {state->window.x + 6, state->window.y + 20, state->window.w - 12, state->window.h - 26};
    struct rect theme_panel = {body.x + 8, body.y + 8, 216, 190};
    struct rect preview_panel = {body.x + 232, body.y + 8, body.w - 240, 62};
    struct rect wallpaper_panel = {body.x + 232, body.y + 78, body.w - 240, 74};
    struct rect resolution_panel = {body.x + 232, body.y + 160, body.w - 240, 132};
    struct rect palette_panel = {body.x + 8, body.y + body.h - 98, 216, 88};
    struct rect preview = {preview_panel.x + 12, preview_panel.y + 20, preview_panel.w - 24, 34};
    struct rect preview_chip = {preview.x + 8, preview.y + 8, 48, 16};
    struct rect preview_strip = {preview.x + 8, preview.y + 26, preview.w - 16, 3};

    refresh_resolution_options();

    draw_window_frame(&state->window, "Personalizar", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_panel());
    ui_draw_surface(&theme_panel, ui_color_window_bg());
    ui_draw_surface(&preview_panel, ui_color_window_bg());
    ui_draw_surface(&wallpaper_panel, ui_color_window_bg());
    ui_draw_surface(&palette_panel, ui_color_window_bg());
    ui_draw_surface(&resolution_panel, ui_color_window_bg());

    sys_text(theme_panel.x + 8, theme_panel.y + 8, theme->text, "Area do tema");
    sys_text(preview_panel.x + 8, preview_panel.y + 8, theme->text, "Preview");
    sys_text(wallpaper_panel.x + 8, wallpaper_panel.y + 8, theme->text, "Wallpaper");
    sys_text(palette_panel.x + 8, palette_panel.y + 8, theme->text, "Paleta");
    sys_text(resolution_panel.x + 8, resolution_panel.y + 8, theme->text, "Resolucao");

    if (state->selected_slot == THEME_SLOT_MENU) selected_color = theme->menu;
    else if (state->selected_slot == THEME_SLOT_MENU_BUTTON) selected_color = theme->menu_button;
    else if (state->selected_slot == THEME_SLOT_MENU_BUTTON_INACTIVE) selected_color = theme->menu_button_inactive;
    else if (state->selected_slot == THEME_SLOT_TASKBAR) selected_color = theme->taskbar;
    else if (state->selected_slot == THEME_SLOT_WINDOW) selected_color = theme->window;
    else if (state->selected_slot == THEME_SLOT_WINDOW_BG) selected_color = theme->window_bg;
    else if (state->selected_slot == THEME_SLOT_TEXT) selected_color = theme->text;

    for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
        struct rect tile = personalize_window_slot_rect(&state->window, slot);
        uint8_t color = theme->background;

        if (slot == THEME_SLOT_MENU) color = theme->menu;
        else if (slot == THEME_SLOT_MENU_BUTTON) color = theme->menu_button;
        else if (slot == THEME_SLOT_MENU_BUTTON_INACTIVE) color = theme->menu_button_inactive;
        else if (slot == THEME_SLOT_TASKBAR) color = theme->taskbar;
        else if (slot == THEME_SLOT_WINDOW) color = theme->window;
        else if (slot == THEME_SLOT_WINDOW_BG) color = theme->window_bg;
        else if (slot == THEME_SLOT_TEXT) color = theme->text;

        ui_draw_surface(&tile, slot == (int)state->selected_slot ? theme->window : ui_color_panel());
        sys_rect(tile.x + 5, tile.y + 5, tile.w - 10, 10, slot == (int)THEME_SLOT_TEXT ? theme->window : color);
        if (slot == (int)THEME_SLOT_TEXT) {
            sys_text(tile.x + 29, tile.y + 6, color, "A");
        } else {
            sys_rect(tile.x + 8, tile.y + 17, tile.w - 16, 4,
                     slot == (int)state->selected_slot ? ui_color_window_bg() : ui_color_muted());
        }
        sys_text(tile.x + 3, tile.y + 20, theme->text, ui_theme_slot_name((enum theme_slot)slot));
    }

    ui_draw_inset(&preview, ui_color_window_bg());
    sys_rect(preview_chip.x, preview_chip.y, preview_chip.w, preview_chip.h,
             state->selected_slot == THEME_SLOT_TEXT ? theme->window : selected_color);
    sys_rect(preview_strip.x, preview_strip.y, preview_strip.w, preview_strip.h, theme->window);
    sys_text(preview.x + 78, preview.y + 12,
             state->selected_slot == THEME_SLOT_TEXT ? selected_color : theme->text,
             "Aa");
    sys_text(preview.x + 78, preview.y + 28, theme->text, ui_theme_slot_name(state->selected_slot));
    sys_text(preview_panel.x + 16, preview_panel.y + 68, theme->text, "Ajuste rapido do desktop");

    {
        struct rect mais_cores_btn = {palette_panel.x + 8, palette_panel.y + 30, palette_panel.w - 16, 14};
        int mais_cores_hover = point_in_rect(&mais_cores_btn, mouse->x, mouse->y);

        ui_draw_button(&mais_cores_btn, "Seletor de cores",
                       state->color_picker_open ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       mais_cores_hover);
    }

    for (int i = -1; i < wallpaper_count; ++i) {
        struct rect button = personalize_window_wallpaper_button_rect(&state->window, i + 1);
        int node = i < 0 ? -1 : wallpaper_nodes[i];
        int hover = point_in_rect(&button, mouse->x, mouse->y);
        int selected = current_wallpaper == node;
        const char *label = i < 0 ? "Somente cor" : g_fs_nodes[node].name;

        if (selected) {
            current_in_quick_list = 1;
        }
        ui_draw_button(&button,
                       label,
                       selected ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       hover);
    }
    {
        struct rect choose_button = personalize_window_wallpaper_choose_rect(&state->window);
        int choose_hover = point_in_rect(&choose_button, mouse->x, mouse->y);

        ui_draw_button(&choose_button,
                       "Escolher arquivo...",
                       (!current_in_quick_list && current_wallpaper >= 0) ? UI_BUTTON_ACTIVE : UI_BUTTON_PRIMARY,
                       choose_hover);
    }
    if (wallpaper_count == 0) {
        sys_text(wallpaper_panel.x + 8, wallpaper_panel.y + wallpaper_panel.h - 16, theme->text, "sem atalhos .bmp/.png");
    }

    for (int i = 0; i < g_resolution_option_count; ++i) {
        struct rect button = personalize_window_resolution_button_rect(&state->window, i);
        char label[16] = "";
        int hover = point_in_rect(&button, mouse->x, mouse->y);
        int selected = (SCREEN_WIDTH == g_resolution_options[i].width &&
                        SCREEN_HEIGHT == g_resolution_options[i].height);

        append_uint_limited(label, g_resolution_options[i].width, (int)sizeof(label));
        str_append(label, "x", (int)sizeof(label));
        append_uint_limited(label, g_resolution_options[i].height, (int)sizeof(label));
        ui_draw_button(&button,
                       label,
                       selected ? UI_BUTTON_ACTIVE :
                       (g_resolution_can_set ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL),
                       hover);
    }
    {
        const char *res_text = state->resolution_status[0] != '\0'
                                   ? state->resolution_status
                                   : (g_resolution_can_set
                                          ? "Aplicacao imediata"
                                          : "driver atual: resolucao so no boot");
        int res_text_w = str_len(res_text) * 6;

        sys_text(resolution_panel.x + (resolution_panel.w - res_text_w) / 2,
                 resolution_panel.y + resolution_panel.h - 14, theme->text,
                 res_text);
    }

    if (state->color_picker_open) {
        struct rect picker = personalize_color_picker_rect();

        ui_draw_surface(&picker, ui_color_muted());
        sys_text(picker.x + 8, picker.y + 6, theme->text, "Selecione cor (0-255)");

        for (int i = 0; i < 256; ++i) {
            struct rect swatch = personalize_color_swatch_rect(&picker, i);
            int hover = point_in_rect(&swatch, mouse->x, mouse->y);

            if (hover) {
                sys_rect(swatch.x - 1, swatch.y - 1, swatch.w + 2, swatch.h + 2, theme->text);
            }
            sys_rect(swatch.x, swatch.y, swatch.w, swatch.h, g_color_palette_256[i]);
        }
    }
}

static void draw_trash_window(struct trash_state *state,
                              int active,
                              int min_hover,
                              int max_hover,
                              int close_hover,
                              const struct mouse_state *mouse) {
    int entries[FS_MAX_NODES];
    int count = trash_collect_entries(entries, FS_MAX_NODES);
    int selected_visible = 0;
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {state->window.x + 4, state->window.y + 18, state->window.w - 8, state->window.h - 22};
    struct rect hero = {state->window.x + 10, state->window.y + 24, state->window.w - 20, 32};
    struct rect list = trash_window_list_rect(state);
    struct rect restore_button = trash_window_restore_button_rect(state);
    struct rect delete_button = trash_window_delete_button_rect(state);
    struct rect empty_button = trash_window_empty_button_rect(state);
    struct rect status = trash_window_status_rect(state);
    char status_text[64];

    for (int i = 0; i < count; ++i) {
        if (entries[i] == state->selected_entry) {
            selected_visible = 1;
            break;
        }
    }
    if (!selected_visible) {
        state->selected_entry = -1;
    }

    draw_window_frame(&state->window, "Lixeira", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&hero, ui_color_panel());
    ui_draw_inset(&list, ui_color_window_bg());
    sys_text(hero.x + 6, hero.y + 6, theme->text, "Arquivos enviados para a lixeira");

    for (int i = 0; i < count; ++i) {
        struct rect row = trash_window_row_rect(state, i);
        char label[24];
        int hover;

        if (row.y + row.h > list.y + list.h) {
            break;
        }

        trash_entry_display_name(entries[i], label, (int)sizeof(label));
        hover = point_in_rect(&row, mouse->x, mouse->y);
        if (entries[i] == state->selected_entry) {
            ui_draw_surface(&row, theme->window);
        } else {
            ui_draw_inset(&row, ui_color_window_bg());
        }
        if (hover && entries[i] != state->selected_entry) {
            sys_rect(row.x, row.y, row.w, row.h, theme->window);
        }
        sys_text(row.x + 4, row.y + 4, theme->text, label);
    }

    if (count == 0) {
        sys_text(list.x + 4, list.y + 4, theme->text, "(vazia)");
    }

    ui_draw_button(&restore_button, "Restaurar", UI_BUTTON_PRIMARY,
                   point_in_rect(&restore_button, mouse->x, mouse->y));
    ui_draw_button(&delete_button, "Excluir", UI_BUTTON_DANGER,
                   point_in_rect(&delete_button, mouse->x, mouse->y));
    ui_draw_button(&empty_button, "Esvaziar", UI_BUTTON_NORMAL,
                   point_in_rect(&empty_button, mouse->x, mouse->y));

    status_text[0] = '\0';
    if (state->status[0] != '\0') {
        str_copy_limited(status_text, state->status, (int)sizeof(status_text));
    } else if (state->selected_entry >= 0 &&
               trash_read_entry_origin(state->selected_entry, status_text, (int)sizeof(status_text)) == 0) {
        /* show original path for the currently-selected item */
    } else {
        str_copy_limited(status_text, "Selecione um item", (int)sizeof(status_text));
    }
    ui_draw_status(&status, status_text);
}

static void restore_or_toggle_window(int widx, int *focused) {
    if (g_windows[widx].minimized) {
        g_windows[widx].minimized = 0;
        *focused = raise_window_to_front(widx, focused);
        return;
    }

    if (*focused == widx) {
        g_windows[widx].minimized = 1;
        *focused = -1;
        return;
    }

    *focused = raise_window_to_front(widx, focused);
}

static void maximize_window(int widx) {
    if (!g_windows[widx].maximized) {
        g_windows[widx].restore_rect = g_windows[widx].rect;
        g_windows[widx].rect = maximized_rect();
        g_windows[widx].maximized = 1;
    } else {
        g_windows[widx].rect = g_windows[widx].restore_rect;
        g_windows[widx].maximized = 0;
    }
    clamp_window_rect(&g_windows[widx].rect);
    sync_window_instance_rect(widx);
}

void desktop_main(void) {
    struct rect start_button;
    struct rect menu_rect;
    struct rect context_menu;
    struct rect fm_context_menu;
    struct mouse_state mouse;
    struct app_context_state app_context = {0, -1, APP_NONE, {0, 0, 0, 0}};
    struct file_dialog_state file_dialog;
    int menu_hover[START_MENU_ENTRY_COUNT];
    int filtered_indices[START_MENU_ENTRY_COUNT];
    int filtered_count = 0;
    int hovered_result = -1;
    int apps_tab_hover = 0;
    int games_tab_hover = 0;
    int menu_search_hover = 0;
    int menu_search_clear_hover = 0;
    int menu_sidebar_hover = 0;
    int menu_sidebar_files_hover = 0;
    int menu_sidebar_terminal_hover = 0;
    int menu_sidebar_personalize_hover = 0;
    int menu_logout_hover = 0;
    int menu_scroll_thumb_hover = 0;
    int menu_scroll_dragging = 0;
    int menu_scroll_drag_offset_y = 0;
    enum start_menu_tab start_menu_tab = START_MENU_TAB_APPS;
    int start_menu_scroll[2] = {0, 0};
    int start_menu_search_scroll = 0;
    char start_menu_search[START_MENU_SEARCH_MAX + 1] = "";
    int start_menu_search_len = 0;
    int menu_open = 0;
    int context_open = 0;
    int fm_context_open = 0;
    int fm_context_window = -1;
    int fm_context_target = FILEMANAGER_HIT_NONE;
    int dirty = 1;
    int focused = -1;
    int dragging = -1;
    int resizing = -1;
    int drag_offset_x = 0;
    int drag_offset_y = 0;
    struct rect resize_origin = {0, 0, 0, 0};
    int resize_anchor_x = 0;
    int resize_anchor_y = 0;
    int running = 1;

    ui_init();
    sys_write_debug("desktop: session start\n");
    start_button = ui_taskbar_start_button_rect();
    menu_rect = ui_start_menu_rect();
    context_menu = desktop_context_menu_rect(0, 0);
    fm_context_menu = filemanager_context_menu_rect(0, 0);
    mouse.x = (int)SCREEN_WIDTH / 2;
    mouse.y = (int)SCREEN_HEIGHT / 2;
    mouse.buttons = 0;
    file_dialog_reset(&file_dialog);

    for (int i = 0; i < MAX_WINDOWS; ++i) g_windows[i].active = 0;
    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = 0;
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = 0;
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = 0;
    for (int i = 0; i < MAX_EDITORS; ++i) g_editor_used[i] = 0;
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = 0;
    for (int i = 0; i < MAX_CALCULATORS; ++i) g_calc_used[i] = 0;
    for (int i = 0; i < MAX_SKETCHPADS; ++i) g_sketch_used[i] = 0;
    for (int i = 0; i < MAX_SNAKES; ++i) g_snake_used[i] = 0;
    for (int i = 0; i < MAX_TETRIS; ++i) g_tetris_used[i] = 0;
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = 0;
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = 0;
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = 0;
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = 0;
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = 0;
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = 0;
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = 0;
    for (int i = 0; i < MAX_CRAFT; ++i) g_craft_used[i] = 0;
    g_clipboard_node = -1;

    dirty |= desktop_process_pending_launches(&focused);

    while (running) {
        int key;
        fs_tick();
        dirty |= desktop_process_pending_launches(&focused);
        uint32_t ticks = sys_ticks();
        int mouse_event = 0;
        int left_pressed = (mouse.buttons & 0x01u) != 0;
        int right_pressed = (mouse.buttons & 0x02u) != 0;
        int left_just_pressed = 0;
        int right_just_pressed = 0;
        int start_hover;
        int left_press_x = mouse.x;
        int left_press_y = mouse.y;
        int right_press_x = mouse.x;
        int right_press_y = mouse.y;
        struct mouse_state polled_mouse;

        start_button = ui_taskbar_start_button_rect();
        menu_rect = ui_start_menu_rect();
        apps_tab_hover = 0;
        games_tab_hover = 0;
        if (sanitize_windows(&focused)) {
            dirty = 1;
        }

        filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                           start_menu_search,
                                                           filtered_indices,
                                                           START_MENU_ENTRY_COUNT);
        start_menu_clamp_scroll(start_menu_search_active(start_menu_search) ?
                                &start_menu_search_scroll :
                                &start_menu_scroll[(int)start_menu_tab],
                                filtered_count);

        for (int i = 0; i < START_MENU_ENTRY_COUNT; ++i) {
            menu_hover[i] = 0;
        }

        while (sys_poll_mouse(&polled_mouse)) {
            int new_left;
            int new_right;

            mouse = polled_mouse;
            clamp_mouse_state(&mouse);
            mouse_event = 1;
            new_left = (mouse.buttons & 0x01u) != 0;
            new_right = (mouse.buttons & 0x02u) != 0;
            if (new_left && !left_pressed) {
                left_just_pressed = 1;
                left_press_x = mouse.x;
                left_press_y = mouse.y;
            }
            if (new_right && !right_pressed) {
                right_just_pressed = 1;
                right_press_x = mouse.x;
                right_press_y = mouse.y;
            }
            left_pressed = new_left;
            right_pressed = new_right;
        }

        start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        if (menu_open) {
            struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
            struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
            struct rect search_box = start_menu_search_rect();
            struct rect search_clear = start_menu_search_clear_rect();
            struct rect sidebar = start_menu_sidebar_rect();
            struct rect sidebar_files = start_menu_sidebar_button_rect(0);
            struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
            struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
            struct rect logout = start_menu_logout_rect();
            struct rect thumb = start_menu_scroll_thumb_rect(filtered_count,
                                                            start_menu_search_active(start_menu_search) ?
                                                            start_menu_search_scroll :
                                                            start_menu_scroll[(int)start_menu_tab]);
            apps_tab_hover = point_in_rect(&apps_tab, mouse.x, mouse.y);
            games_tab_hover = point_in_rect(&games_tab, mouse.x, mouse.y);
            menu_search_hover = point_in_rect(&search_box, mouse.x, mouse.y);
            menu_search_clear_hover = start_menu_search[0] != '\0' &&
                                      point_in_rect(&search_clear, mouse.x, mouse.y);
            menu_sidebar_hover = point_in_rect(&sidebar, mouse.x, mouse.y);
            menu_sidebar_files_hover = point_in_rect(&sidebar_files, mouse.x, mouse.y);
            menu_sidebar_terminal_hover = point_in_rect(&sidebar_terminal, mouse.x, mouse.y);
            menu_sidebar_personalize_hover = point_in_rect(&sidebar_personalize, mouse.x, mouse.y);
            menu_logout_hover = point_in_rect(&logout, mouse.x, mouse.y);
            menu_scroll_thumb_hover = filtered_count > start_menu_visible_count() &&
                                      point_in_rect(&thumb, mouse.x, mouse.y);
        }
        hovered_result = menu_open ? start_menu_result_at_point(filtered_count,
                                                                start_menu_search_active(start_menu_search) ?
                                                                start_menu_search_scroll :
                                                                start_menu_scroll[(int)start_menu_tab],
                                                                mouse.x,
                                                                mouse.y) : -1;
        if (menu_open && hovered_result >= 0 && hovered_result < filtered_count) {
            menu_hover[filtered_indices[hovered_result]] = 1;
        }
        int context_personalize_hover = context_open && point_in_rect(&context_menu, mouse.x, mouse.y);
        struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
        struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
        struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
        struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
        struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
        struct rect fm_rename_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_RENAME);
        struct rect fm_trash_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_MOVE_TO_TRASH);
        struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_SET_WALLPAPER);
        struct rect app_primary_rect = app_context_item_rect(&app_context.menu, APPCTX_PRIMARY);
        struct rect app_save_as_rect = app_context_item_rect(&app_context.menu, APPCTX_SAVE_AS);
        int fm_open_hover = fm_context_open && point_in_rect(&fm_open_rect, mouse.x, mouse.y);
        int fm_copy_hover = fm_context_open && point_in_rect(&fm_copy_rect, mouse.x, mouse.y);
        int fm_paste_hover = fm_context_open && point_in_rect(&fm_paste_rect, mouse.x, mouse.y);
        int fm_new_dir_hover = fm_context_open && point_in_rect(&fm_new_dir_rect, mouse.x, mouse.y);
        int fm_new_file_hover = fm_context_open && point_in_rect(&fm_new_file_rect, mouse.x, mouse.y);
        int fm_rename_hover = fm_context_open && point_in_rect(&fm_rename_rect, mouse.x, mouse.y);
        int fm_trash_hover = fm_context_open && point_in_rect(&fm_trash_rect, mouse.x, mouse.y);
        int fm_set_wallpaper_hover = fm_context_open &&
                                     g_fm_context_has_wallpaper_action &&
                                     point_in_rect(&fm_set_wallpaper_rect, mouse.x, mouse.y);
        int app_primary_hover = app_context.open && point_in_rect(&app_primary_rect, mouse.x, mouse.y);
        int app_save_as_hover = app_context.open && point_in_rect(&app_save_as_rect, mouse.x, mouse.y);

        if (fm_context_open) {
            if (fm_context_window < 0 ||
                !g_windows[fm_context_window].active ||
                g_windows[fm_context_window].type != APP_FILEMANAGER) {
                fm_context_open = 0;
                fm_context_window = -1;
                g_fm_context_has_wallpaper_action = 0;
                dirty = 1;
            }
        }
        if (app_context.open) {
            if (app_context.window < 0 ||
                !g_windows[app_context.window].active ||
                g_windows[app_context.window].type != app_context.type) {
                app_context.open = 0;
                app_context.window = -1;
                app_context.type = APP_NONE;
                dirty = 1;
            }
        }

        if (mouse_event) {
            dirty = 1;
        }

        for (int i = 0; i < MAX_CLOCKS; ++i) {
            if (g_clock_used[i] && !has_active_window_instance(APP_CLOCK, i)) {
                g_clock_used[i] = 0;
            } else if (g_clock_used[i] && clock_step(&g_clocks[i])) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_SNAKES; ++i) {
            if (g_snake_used[i] && !has_active_window_instance(APP_SNAKE, i)) {
                g_snake_used[i] = 0;
            } else if (g_snake_used[i] && snake_step(&g_snakes[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_TETRIS; ++i) {
            if (g_tetris_used[i] && !has_active_window_instance(APP_TETRIS, i)) {
                g_tetris_used[i] = 0;
            } else if (g_tetris_used[i] && tetris_step(&g_tetris[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_PACMAN; ++i) {
            if (g_pacman_used[i] && !has_active_window_instance(APP_PACMAN, i)) {
                g_pacman_used[i] = 0;
            } else if (g_pacman_used[i] && pacman_step(&g_pacman[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_SPACE_INVADERS; ++i) {
            if (g_space_invaders_used[i] && !has_active_window_instance(APP_SPACE_INVADERS, i)) {
                g_space_invaders_used[i] = 0;
            } else if (g_space_invaders_used[i] && space_invaders_step(&g_space_invaders[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_PONG; ++i) {
            if (g_pong_used[i] && !has_active_window_instance(APP_PONG, i)) {
                g_pong_used[i] = 0;
            } else if (g_pong_used[i] && pong_step(&g_pong[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_DONKEY_KONG; ++i) {
            if (g_donkey_kong_used[i] && !has_active_window_instance(APP_DONKEY_KONG, i)) {
                g_donkey_kong_used[i] = 0;
            } else if (g_donkey_kong_used[i] && donkey_kong_step(&g_donkey_kong[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_BRICK_RACE; ++i) {
            if (g_brick_race_used[i] && !has_active_window_instance(APP_BRICK_RACE, i)) {
                g_brick_race_used[i] = 0;
            } else if (g_brick_race_used[i] && brick_race_step(&g_brick_race[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_FLAP_BIRB; ++i) {
            if (g_flap_birb_used[i] && !has_active_window_instance(APP_FLAP_BIRB, i)) {
                g_flap_birb_used[i] = 0;
            } else if (g_flap_birb_used[i] && flap_birb_step(&g_flap_birb[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_DOOM; ++i) {
            if (g_doom_used[i] && !has_active_window_instance(APP_DOOM, i)) {
                g_doom_used[i] = 0;
            } else if (g_doom_used[i] && doom_step(&g_doom[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_CRAFT; ++i) {
            if (g_craft_used[i] && !has_active_window_instance(APP_CRAFT, i)) {
                craft_shutdown_state(&g_craft[i]);
                g_craft_used[i] = 0;
            } else if (g_craft_used[i]) {
                craft_update_input(&g_craft[i],
                                   focused >= 0 &&
                                   g_windows[focused].type == APP_CRAFT &&
                                   g_windows[focused].instance == i,
                                   mouse.x, mouse.y, mouse.dx, mouse.dy,
                                   mouse.buttons);
                if (craft_step(&g_craft[i], ticks)) {
                    dirty = 1;
                }
            }
        }

        if (dragging >= 0 && left_pressed && mouse_event && !g_windows[dragging].maximized) {
            g_windows[dragging].rect.x = mouse.x - drag_offset_x;
            g_windows[dragging].rect.y = mouse.y - drag_offset_y;
            clamp_window_rect(&g_windows[dragging].rect);
            sync_window_instance_rect(dragging);
            dirty = 1;
        }

        if (resizing >= 0 && left_pressed && mouse_event && !g_windows[resizing].maximized) {
            g_windows[resizing].rect = resize_origin;
            g_windows[resizing].rect.w += mouse.x - resize_anchor_x;
            g_windows[resizing].rect.h += mouse.y - resize_anchor_y;
            clamp_window_rect(&g_windows[resizing].rect);
            sync_window_instance_rect(resizing);
            dirty = 1;
        }

        if (menu_open && menu_scroll_dragging && left_pressed && mouse_event) {
            int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                              &start_menu_search_scroll :
                              &start_menu_scroll[(int)start_menu_tab];

            *scroll_ptr = start_menu_scroll_from_thumb_y(filtered_count,
                                                         mouse.y - menu_scroll_drag_offset_y);
            start_menu_clamp_scroll(scroll_ptr, filtered_count);
            dirty = 1;
        }

        if (!left_pressed) {
            dragging = -1;
            resizing = -1;
            menu_scroll_dragging = 0;
        }

        if (focused >= 0 &&
            g_windows[focused].active &&
            !g_windows[focused].minimized &&
            g_windows[focused].type == APP_SKETCHPAD &&
            left_pressed &&
            mouse_event &&
            dragging < 0 &&
            resizing < 0) {
            if (sketchpad_paint_at(&g_sketches[g_windows[focused].instance], mouse.x, mouse.y)) {
                dirty = 1;
            }
        }

        if (right_just_pressed) {
            int click_x = right_press_x;
            int click_y = right_press_y;
            int hit_window = topmost_window_at(click_x, click_y);

            if (file_dialog.active) {
                app_context.open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (hit_window >= 0 && g_windows[hit_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                struct rect list = filemanager_list_rect(fm);

                if (point_in_rect(&list, click_x, click_y)) {
                    int new_index = raise_window_to_front(hit_window, &focused);
                    int target;

                    focused = new_index;
                    hit_window = new_index;
                    fm = &g_fms[g_windows[hit_window].instance];
                    target = filemanager_hit_test_entry(fm, click_x, click_y);
                    g_fm_context_has_wallpaper_action = (target >= 0) && node_is_wallpaper_candidate(target);
                    fm_context_menu = filemanager_context_menu_rect(click_x, click_y);
                    fm_context_open = 1;
                    fm_context_window = hit_window;
                    fm_context_target = target;
                    if (target >= 0) {
                        fm->selected_node = target;
                    } else if (target == FILEMANAGER_HIT_NONE) {
                        fm->selected_node = -1;
                    }
                    context_open = 0;
                    menu_open = 0;
                    app_context.open = 0;
                    dirty = 1;
                } else if (fm_context_open || context_open) {
                    fm_context_open = 0;
                    g_fm_context_has_wallpaper_action = 0;
                    context_open = 0;
                    app_context.open = 0;
                    dirty = 1;
                }
            } else if (hit_window >= 0 &&
                       (g_windows[hit_window].type == APP_EDITOR || g_windows[hit_window].type == APP_SKETCHPAD)) {
                int new_index = raise_window_to_front(hit_window, &focused);

                focused = new_index;
                app_context.open = 1;
                app_context.window = new_index;
                app_context.type = g_windows[new_index].type;
                app_context.menu = app_context_menu_rect(click_x, click_y);
                context_open = 0;
                fm_context_open = 0;
                menu_open = 0;
                dirty = 1;
            } else if (hit_window < 0 &&
                       click_y < (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) {
                context_menu = desktop_context_menu_rect(click_x, click_y);
                context_open = 1;
                fm_context_open = 0;
                app_context.open = 0;
                menu_open = 0;
                dirty = 1;
            } else if (context_open || fm_context_open || app_context.open) {
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                g_fm_context_has_wallpaper_action = 0;
                dirty = 1;
            }
        }

        if (left_just_pressed) {
            int click_x = left_press_x;
            int click_y = left_press_y;
            int start_click_hover = point_in_rect(&start_button, click_x, click_y);
            int hit_window = -1;
            int handled = 0;

            if (file_dialog.active) {
                struct rect close = file_dialog_close_rect(&file_dialog);
                struct rect name_field = file_dialog_name_rect(&file_dialog);
                struct rect ext_field = file_dialog_ext_rect(&file_dialog);
                struct rect path_field = file_dialog_path_rect(&file_dialog);
                struct rect ok = file_dialog_ok_rect(&file_dialog);
                struct rect cancel = file_dialog_cancel_rect(&file_dialog);

                handled = 1;
                if (point_in_rect(&close, click_x, click_y) ||
                    point_in_rect(&cancel, click_x, click_y)) {
                    file_dialog_reset(&file_dialog);
                    dirty = 1;
                } else if (point_in_rect(&ok, click_x, click_y)) {
                    if (file_dialog_apply(&file_dialog)) {
                        file_dialog_reset(&file_dialog);
                    }
                    dirty = 1;
                } else if (file_dialog.mode == FILE_DIALOG_WALLPAPER_PATH &&
                           point_in_rect(&path_field, click_x, click_y)) {
                    file_dialog.active_field = 0;
                    dirty = 1;
                } else if (point_in_rect(&name_field, click_x, click_y)) {
                    file_dialog.active_field = 0;
                    dirty = 1;
                } else if (file_dialog.mode != FILE_DIALOG_WALLPAPER_PATH &&
                           point_in_rect(&ext_field, click_x, click_y)) {
                    file_dialog.active_field = 1;
                    dirty = 1;
                }
            }

            if (!handled && app_context.open && point_in_rect(&app_context.menu, click_x, click_y)) {
                handled = 1;
                if (point_in_rect(&app_primary_rect, click_x, click_y)) {
                    if (app_context.type == APP_EDITOR) {
                        struct editor_state *ed = &g_editors[g_windows[app_context.window].instance];
                        if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
                            if (editor_save(ed)) {
                                dirty = 1;
                            }
                        } else {
                            file_dialog_open_editor(&file_dialog, app_context.window);
                            dirty = 1;
                        }
                    } else if (app_context.type == APP_SKETCHPAD) {
                        struct sketchpad_state *sketch = &g_sketches[g_windows[app_context.window].instance];
                        if (sketch->last_export_path[0] != '\0') {
                            char name[FS_NAME_MAX + 1];
                            char ext[FS_NAME_MAX + 1];
                            char filename[FS_NAME_MAX + 1];

                            extract_basename_parts(sketch->last_export_path,
                                                   name, (int)sizeof(name),
                                                   ext, (int)sizeof(ext));
                            filename[0] = '\0';
                            str_copy_limited(filename, name, (int)sizeof(filename));
                            if (ext[0] != '\0') {
                                str_append(filename, ".", (int)sizeof(filename));
                                str_append(filename, ext, (int)sizeof(filename));
                            }
                            if (sketchpad_export_bitmap_named(sketch, filename)) {
                                dirty = 1;
                            }
                        } else {
                            file_dialog_open_sketch(&file_dialog, app_context.window);
                            dirty = 1;
                        }
                    }
                    app_context.open = 0;
                } else if (point_in_rect(&app_save_as_rect, click_x, click_y)) {
                    if (app_context.type == APP_EDITOR) {
                        file_dialog_open_editor(&file_dialog, app_context.window);
                    } else if (app_context.type == APP_SKETCHPAD) {
                        file_dialog_open_sketch(&file_dialog, app_context.window);
                    }
                    app_context.open = 0;
                    dirty = 1;
                }
            } else if (!handled && app_context.open && !point_in_rect(&app_context.menu, click_x, click_y)) {
                app_context.open = 0;
                dirty = 1;
            }

            if (!handled && fm_context_open && fm_context_window >= 0 &&
                g_windows[fm_context_window].active &&
                g_windows[fm_context_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[fm_context_window].instance];
                struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
                struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
                struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
                struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
                struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
                struct rect fm_rename_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_RENAME);
                struct rect fm_trash_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_MOVE_TO_TRASH);
                struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_SET_WALLPAPER);
                int fm_open_hover = point_in_rect(&fm_open_rect, click_x, click_y);
                int fm_copy_hover = point_in_rect(&fm_copy_rect, click_x, click_y);
                int fm_paste_hover = point_in_rect(&fm_paste_rect, click_x, click_y);
                int fm_new_dir_hover = point_in_rect(&fm_new_dir_rect, click_x, click_y);
                int fm_new_file_hover = point_in_rect(&fm_new_file_rect, click_x, click_y);
                int fm_rename_hover = point_in_rect(&fm_rename_rect, click_x, click_y);
                int fm_trash_hover = point_in_rect(&fm_trash_rect, click_x, click_y);
                int fm_set_wallpaper_hover = g_fm_context_has_wallpaper_action &&
                                             point_in_rect(&fm_set_wallpaper_rect, click_x, click_y);
                int target = fm_context_target;

                if (fm_open_hover || fm_copy_hover || fm_paste_hover || fm_new_dir_hover || fm_new_file_hover ||
                    fm_rename_hover || fm_trash_hover ||
                    fm_set_wallpaper_hover) {
                    if (target == FILEMANAGER_HIT_NONE) {
                        target = fm->selected_node;
                    }

                    if (fm_open_hover && target != FILEMANAGER_HIT_NONE) {
                        if (target >= 0 && !g_fs_nodes[target].is_dir) {
                            if (image_node_is_supported(target)) {
                                if (open_imageviewer_window_for_node(target, &focused) >= 0) {
                                    dirty = 1;
                                }
                            } else if (open_editor_window_for_node(target, &focused) >= 0) {
                                dirty = 1;
                            }
                        } else if (filemanager_open_node(fm, target)) {
                            dirty = 1;
                        } else if (target >= 0) {
                            fm->selected_node = target;
                            dirty = 1;
                        }
                    } else if (fm_copy_hover && target >= 0) {
                        g_clipboard_node = target;
                        dirty = 1;
                    } else if (fm_paste_hover && g_clipboard_node >= 0) {
                        if (clone_node_to_directory(g_clipboard_node, fm->cwd) >= 0) {
                            dirty = 1;
                        }
                    } else if (fm_new_dir_hover) {
                        int created = create_node_in_directory(fm->cwd, 1, "pasta");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    } else if (fm_new_file_hover) {
                        int created = create_node_in_directory(fm->cwd, 0, "arquivo");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    } else if (fm_rename_hover && target >= 0) {
                        file_dialog_open_rename(&file_dialog, fm_context_window, target);
                        dirty = 1;
                    } else if (fm_trash_hover && target >= 0) {
                        if (trash_move_node_to_bin(target) == 0) {
                            fm->selected_node = -1;
                            dirty = 1;
                        }
                    } else if (fm_set_wallpaper_hover && target >= 0) {
                        if (ui_wallpaper_set_from_node(target) == 0) {
                            dirty = 1;
                        }
                    }

                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                    handled = 1;
                } else if (!point_in_rect(&fm_context_menu, click_x, click_y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                }
            }

            if (handled) {
            } else if (context_open && point_in_rect(&context_menu, click_x, click_y)) {
                if (open_window_or_focus_existing(APP_PERSONALIZE, &focused) >= 0) {
                    dirty = 1;
                }
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                handled = 1;
            } else if (start_click_hover) {
                menu_open = !menu_open;
                if (menu_open) {
                    start_menu_search[0] = '\0';
                    start_menu_search_len = 0;
                    start_menu_search_scroll = 0;
                } else {
                    menu_scroll_dragging = 0;
                }
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                dirty = 1;
            } else {
                struct rect files_icon = ui_desktop_files_icon_rect();
                struct rect image_icon = ui_desktop_image_icon_rect();
                struct rect craft_icon = ui_desktop_craft_icon_rect();
                struct rect trash_icon = ui_desktop_trash_icon_rect();

                if (point_in_rect(&files_icon, click_x, click_y)) {
                    if (open_window_or_focus_existing(APP_FILEMANAGER, &focused) >= 0) {
                        dirty = 1;
                    }
                    menu_open = 0;
                    context_open = 0;
                    fm_context_open = 0;
                    app_context.open = 0;
                    handled = 1;
                } else if (point_in_rect(&image_icon, click_x, click_y)) {
                    if (open_window_or_focus_existing(APP_IMAGEVIEWER, &focused) >= 0) {
                        dirty = 1;
                    }
                    menu_open = 0;
                    context_open = 0;
                    fm_context_open = 0;
                    app_context.open = 0;
                    handled = 1;
                } else if (point_in_rect(&craft_icon, click_x, click_y)) {
                    if (open_window_or_focus_existing(APP_CRAFT, &focused) >= 0) {
                        dirty = 1;
                    }
                    menu_open = 0;
                    context_open = 0;
                    fm_context_open = 0;
                    app_context.open = 0;
                    handled = 1;
                } else if (point_in_rect(&trash_icon, click_x, click_y)) {
                    if (open_window_or_focus_existing(APP_TRASH, &focused) >= 0) {
                        dirty = 1;
                    }
                    menu_open = 0;
                    context_open = 0;
                    fm_context_open = 0;
                    app_context.open = 0;
                    handled = 1;
                }

                if (!handled && menu_open) {
                    enum app_type launch_type = APP_NONE;
                    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
                    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
                    struct rect search_box = start_menu_search_rect();
                    struct rect search_clear = start_menu_search_clear_rect();
                    struct rect sidebar_files = start_menu_sidebar_button_rect(0);
                    struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
                    struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
                    struct rect logout = start_menu_logout_rect();
                    struct rect track = start_menu_scroll_track_rect();
                    struct rect thumb = start_menu_scroll_thumb_rect(filtered_count,
                                                                    start_menu_search_active(start_menu_search) ?
                                                                    start_menu_search_scroll :
                                                                    start_menu_scroll[(int)start_menu_tab]);
                    int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                                      &start_menu_search_scroll :
                                      &start_menu_scroll[(int)start_menu_tab];
                    int menu_contains_click = point_in_rect(&menu_rect, click_x, click_y);
                    const struct start_menu_entry *launch_entry = 0;

                    if (menu_contains_click) {
                        if (point_in_rect(&apps_tab, click_x, click_y)) {
                            start_menu_tab = START_MENU_TAB_APPS;
                            start_menu_clamp_scroll(&start_menu_scroll[(int)start_menu_tab], filtered_count);
                            dirty = 1;
                        } else if (point_in_rect(&games_tab, click_x, click_y)) {
                            start_menu_tab = START_MENU_TAB_GAMES;
                            start_menu_clamp_scroll(&start_menu_scroll[(int)start_menu_tab], filtered_count);
                            dirty = 1;
                        } else if (point_in_rect(&search_clear, click_x, click_y) && start_menu_search[0] != '\0') {
                            start_menu_search[0] = '\0';
                            start_menu_search_len = 0;
                            start_menu_search_scroll = 0;
                            dirty = 1;
                        } else if (point_in_rect(&search_box, click_x, click_y)) {
                            dirty = 1;
                        } else if (point_in_rect(&sidebar_files, click_x, click_y)) {
                            launch_type = APP_FILEMANAGER;
                        } else if (point_in_rect(&sidebar_terminal, click_x, click_y)) {
                            launch_type = APP_TERMINAL;
                        } else if (point_in_rect(&sidebar_personalize, click_x, click_y)) {
                            launch_type = APP_PERSONALIZE;
                        } else if (point_in_rect(&logout, click_x, click_y)) {
                            running = 0;
                        } else if (filtered_count > start_menu_visible_count() &&
                                   point_in_rect(&thumb, click_x, click_y)) {
                            menu_scroll_dragging = 1;
                            menu_scroll_drag_offset_y = click_y - thumb.y;
                            dirty = 1;
                        } else if (filtered_count > start_menu_visible_count() &&
                                   point_in_rect(&track, click_x, click_y)) {
                            int thumb_target_y = click_y - (thumb.h / 2);
                            *scroll_ptr = start_menu_scroll_from_thumb_y(filtered_count, thumb_target_y);
                            start_menu_clamp_scroll(scroll_ptr, filtered_count);
                            dirty = 1;
                        } else {
                            int clicked_result = start_menu_result_at_point(filtered_count,
                                                                            *scroll_ptr,
                                                                            click_x,
                                                                            click_y);

                            if (clicked_result >= 0 && clicked_result < filtered_count) {
                                launch_entry = &g_start_menu_entries[filtered_indices[clicked_result]];
                            }
                        }
                        if (launch_entry != 0) {
                            if (launch_start_menu_entry(launch_entry, &focused) >= 0) {
                                dirty = 1;
                            }
                            menu_open = 0;
                            menu_scroll_dragging = 0;
                            context_open = 0;
                            fm_context_open = 0;
                            app_context.open = 0;
                        } else if (launch_type != APP_NONE) {
                            if (open_window_or_focus_existing(launch_type, &focused) >= 0) {
                                dirty = 1;
                            }
                            menu_open = 0;
                            menu_scroll_dragging = 0;
                            context_open = 0;
                            fm_context_open = 0;
                            app_context.open = 0;
                        }
                        handled = 1;
                    } else {
                        menu_open = 0;
                        menu_scroll_dragging = 0;
                        dirty = 1;
                    }

                }

                if (!handled) {
                if (context_open && !point_in_rect(&context_menu, click_x, click_y)) {
                    context_open = 0;
                    dirty = 1;
                }
                if (fm_context_open && !point_in_rect(&fm_context_menu, click_x, click_y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                }
                if (app_context.open && !point_in_rect(&app_context.menu, click_x, click_y)) {
                    app_context.open = 0;
                    dirty = 1;
                }
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    struct rect task_button;
                    if (!g_windows[i].active) {
                        continue;
                    }
                    task_button = taskbar_button_rect_for_window(i);
                    if (point_in_rect(&task_button, click_x, click_y)) {
                        restore_or_toggle_window(i, &focused);
                        menu_open = 0;
                        context_open = 0;
                        fm_context_open = 0;
                        g_fm_context_has_wallpaper_action = 0;
                        dirty = 1;
                        hit_window = -2;
                        break;
                    }
                }

                if (hit_window != -2) {
                    hit_window = topmost_window_at(click_x, click_y);
                    if (menu_open && !point_in_rect(&menu_rect, click_x, click_y)) {
                        menu_open = 0;
                        dirty = 1;
                    }

                    if (hit_window >= 0) {
                        struct rect close;
                        struct rect min;
                        struct rect max;
                        struct rect title;
                        struct rect grip;
                        int type;

                        hit_window = raise_window_to_front(hit_window, &focused);
                        focused = hit_window;
                        type = g_windows[hit_window].type;
                        close = window_close_button(&g_windows[hit_window].rect);
                        min = window_min_button(&g_windows[hit_window].rect);
                        max = window_max_button(&g_windows[hit_window].rect);
                        title = window_title_bar(&g_windows[hit_window].rect);
                        grip = window_resize_grip(&g_windows[hit_window].rect);

                        if (point_in_rect(&close, click_x, click_y)) {
                            free_window(hit_window);
                            focused = -1;
                        } else if (point_in_rect(&min, click_x, click_y)) {
                            g_windows[hit_window].minimized = 1;
                            focused = -1;
                        } else if (point_in_rect(&max, click_x, click_y)) {
                            maximize_window(hit_window);
                        } else if (point_in_rect(&grip, click_x, click_y)) {
                            resizing = hit_window;
                            resize_origin = g_windows[hit_window].rect;
                            resize_anchor_x = click_x;
                            resize_anchor_y = click_y;
                        } else if (point_in_rect(&title, click_x, click_y)) {
                            dragging = hit_window;
                            drag_offset_x = click_x - g_windows[hit_window].rect.x;
                            drag_offset_y = click_y - g_windows[hit_window].rect.y;
                        } else if (type == APP_EDITOR) {
                            struct editor_state *ed = &g_editors[g_windows[hit_window].instance];
                            struct rect save = editor_save_button_rect(ed);

                            if (point_in_rect(&save, click_x, click_y)) {
                                if ((ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) ? editor_save(ed) : (file_dialog_open_editor(&file_dialog, hit_window), 0)) {
                                    dirty = 1;
                                } else {
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_FILEMANAGER) {
                            struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                            struct rect up = filemanager_up_button_rect(fm);
                            struct rect list = filemanager_list_rect(fm);
                            int target = FILEMANAGER_HIT_NONE;

                            if (point_in_rect(&up, click_x, click_y)) {
                                if (filemanager_open_node(fm, FILEMANAGER_HIT_PARENT)) {
                                    dirty = 1;
                                }
                            } else if (point_in_rect(&list, click_x, click_y)) {
                                target = filemanager_hit_test_entry(fm, click_x, click_y);
                                if (target == FILEMANAGER_HIT_PARENT) {
                                    if (filemanager_open_node(fm, target)) {
                                        dirty = 1;
                                    }
                                } else if (target >= 0) {
                                    if (g_fs_nodes[target].is_dir) {
                                        if (filemanager_open_node(fm, target)) {
                                            dirty = 1;
                                        }
                                    } else {
                                        fm->selected_node = target;
                                        dirty = 1;
                                    }
                                } else {
                                    fm->selected_node = -1;
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_TRASH) {
                            struct rect list = trash_window_list_rect(&g_trash);
                            struct rect restore_button = trash_window_restore_button_rect(&g_trash);
                            struct rect delete_button = trash_window_delete_button_rect(&g_trash);
                            struct rect empty_button = trash_window_empty_button_rect(&g_trash);

                            if (point_in_rect(&restore_button, click_x, click_y)) {
                                if (g_trash.selected_entry >= 0) {
                                    if (trash_restore_entry(g_trash.selected_entry,
                                                            g_trash.status,
                                                            (int)sizeof(g_trash.status)) == 0) {
                                        g_trash.selected_entry = -1;
                                    }
                                } else {
                                    set_trash_status("Selecione um item");
                                }
                                dirty = 1;
                            } else if (point_in_rect(&delete_button, click_x, click_y)) {
                                if (g_trash.selected_entry >= 0) {
                                    if (trash_delete_entry(g_trash.selected_entry,
                                                           g_trash.status,
                                                           (int)sizeof(g_trash.status)) == 0) {
                                        g_trash.selected_entry = -1;
                                    }
                                } else {
                                    set_trash_status("Selecione um item");
                                }
                                dirty = 1;
                            } else if (point_in_rect(&empty_button, click_x, click_y)) {
                                (void)trash_empty_all(g_trash.status, (int)sizeof(g_trash.status));
                                g_trash.selected_entry = -1;
                                dirty = 1;
                            } else if (point_in_rect(&list, click_x, click_y)) {
                                int entries[FS_MAX_NODES];
                                int count = trash_collect_entries(entries, FS_MAX_NODES);

                                g_trash.selected_entry = -1;
                                g_trash.status[0] = '\0';
                                for (int row = 0; row < count; ++row) {
                                    struct rect row_rect = trash_window_row_rect(&g_trash, row);

                                    if (row_rect.y + row_rect.h > list.y + list.h) {
                                        break;
                                    }
                                    if (point_in_rect(&row_rect, click_x, click_y)) {
                                        g_trash.selected_entry = entries[row];
                                        break;
                                    }
                                }
                                dirty = 1;
                            }
                        } else if (type == APP_IMAGEVIEWER) {
                            if (imageviewer_handle_click(&g_imageviewers[g_windows[hit_window].instance],
                                                         click_x, click_y)) {
                                dirty = 1;
                            }
                        } else if (type == APP_TASKMANAGER) {
                            int close_target = taskmgr_hit_test_close(&g_tms[g_windows[hit_window].instance],
                                                                      g_windows,
                                                                      MAX_WINDOWS,
                                                                      click_x,
                                                                      click_y);
                            if (close_target >= 0) {
                                free_window(close_target);
                                if (close_target == hit_window) {
                                    focused = -1;
                                }
                                dirty = 1;
                            }
                        } else if (type == APP_CALCULATOR) {
                            int button = calculator_hit_test(&g_calcs[g_windows[hit_window].instance],
                                                             click_x,
                                                             click_y);
                            if (button >= 0) {
                                calculator_press_key(&g_calcs[g_windows[hit_window].instance],
                                                     calculator_button_key(button));
                                dirty = 1;
                            }
                        } else if (type == APP_SKETCHPAD) {
                            struct sketchpad_state *sketch = &g_sketches[g_windows[hit_window].instance];
                            struct rect clear_button = sketchpad_clear_button_rect(sketch);
                            struct rect export_button = sketchpad_export_button_rect(sketch);
                            int color_index = sketchpad_hit_color(sketch, click_x, click_y);

                            if (point_in_rect(&clear_button, click_x, click_y)) {
                                sketchpad_clear(sketch);
                                dirty = 1;
                            } else if (point_in_rect(&export_button, click_x, click_y)) {
                                if (sketch->last_export_path[0] != '\0') {
                                    char name[FS_NAME_MAX + 1];
                                    char ext[FS_NAME_MAX + 1];
                                    char filename[FS_NAME_MAX + 1];

                                    extract_basename_parts(sketch->last_export_path,
                                                           name, (int)sizeof(name),
                                                           ext, (int)sizeof(ext));
                                    filename[0] = '\0';
                                    str_copy_limited(filename, name, (int)sizeof(filename));
                                    if (ext[0] != '\0') {
                                        str_append(filename, ".", (int)sizeof(filename));
                                        str_append(filename, ext, (int)sizeof(filename));
                                    }
                                    if (sketchpad_export_bitmap_named(sketch, filename)) {
                                        dirty = 1;
                                    }
                                } else {
                                    file_dialog_open_sketch(&file_dialog, hit_window);
                                    dirty = 1;
                                }
                            } else if (color_index >= 0) {
                                sketch->current_color = (uint8_t)color_index;
                                dirty = 1;
                            } else if (sketchpad_paint_at(sketch, click_x, click_y)) {
                                dirty = 1;
                            }
                        } else if (type == APP_FLAP_BIRB) {
                            if (flap_birb_handle_click(&g_flap_birb[g_windows[hit_window].instance])) {
                                dirty = 1;
                            }
                        } else if (type == APP_DOOM) {
                            if (doom_handle_click(&g_doom[g_windows[hit_window].instance])) {
                                dirty = 1;
                            }
                        } else if (type == APP_CRAFT) {
                            if (craft_handle_click(&g_craft[g_windows[hit_window].instance])) {
                                dirty = 1;
                            }
                        } else if (type == APP_PERSONALIZE) {
                            int wallpaper_nodes[3];
                            int wallpaper_count = find_wallpaper_nodes(wallpaper_nodes, 3);

                            if (g_pers.color_picker_open) {
                                struct rect picker = personalize_color_picker_rect();

                                for (int i = 0; i < 256; ++i) {
                                    struct rect swatch = personalize_color_swatch_rect(&picker, i);
                                    if (point_in_rect(&swatch, click_x, click_y)) {
                                        ui_theme_set_slot(g_pers.selected_slot, g_color_palette_256[i]);
                                        g_pers.color_picker_open = 0;
                                        dirty = 1;
                                        break;
                                    }
                                }
                                if (!point_in_rect(&picker, click_x, click_y)) {
                                    g_pers.color_picker_open = 0;
                                }
                            } else {
                                struct rect body = {g_windows[hit_window].rect.x + 6,
                                                   g_windows[hit_window].rect.y + 20,
                                                   g_windows[hit_window].rect.w - 12,
                                                   g_windows[hit_window].rect.h - 26};
                                struct rect palette_panel = {body.x + 8, body.y + body.h - 98, 216, 88};
                                struct rect mais_cores_btn = {palette_panel.x + 8, palette_panel.y + 30, palette_panel.w - 16, 14};

                                if (point_in_rect(&mais_cores_btn, click_x, click_y)) {
                                    g_pers.color_picker_open = 1;
                                    dirty = 1;
                                }

                                for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
                                    struct rect tile = personalize_window_slot_rect(&g_windows[hit_window].rect, slot);
                                    if (point_in_rect(&tile, click_x, click_y)) {
                                        g_pers.selected_slot = (enum theme_slot)slot;
                                        dirty = 1;
                                    }
                                }
                            }
                            for (int i = -1; i < wallpaper_count; ++i) {
                                struct rect button = personalize_window_wallpaper_button_rect(&g_windows[hit_window].rect, i + 1);
                                if (point_in_rect(&button, click_x, click_y)) {
                                    if (i < 0) {
                                        ui_wallpaper_clear();
                                    } else {
                                        (void)ui_wallpaper_set_from_node(wallpaper_nodes[i]);
                                    }
                                    dirty = 1;
                                }
                            }
                            {
                                struct rect choose_button = personalize_window_wallpaper_choose_rect(&g_windows[hit_window].rect);
                                if (point_in_rect(&choose_button, click_x, click_y)) {
                                    file_dialog_open_wallpaper(&file_dialog, hit_window);
                                    dirty = 1;
                                }
                            }
                            refresh_resolution_options();
                            for (int i = 0; i < g_resolution_option_count; ++i) {
                                struct rect button = personalize_window_resolution_button_rect(&g_windows[hit_window].rect, i);
                                if (point_in_rect(&button, click_x, click_y)) {
                                    if (SCREEN_WIDTH == g_resolution_options[i].width &&
                                        SCREEN_HEIGHT == g_resolution_options[i].height) {
                                        set_personalize_resolution_status("Resolucao ja esta ativa");
                                        dirty = 1;
                                        break;
                                    }
                                    if (!g_resolution_can_set) {
                                        set_personalize_resolution_status("driver atual: resolucao so no boot");
                                        dirty = 1;
                                        break;
                                    }
                                    if (ui_set_resolution(g_resolution_options[i].width,
                                                          g_resolution_options[i].height) == 0) {
                                        set_personalize_resolution_status("Resolucao aplicada agora");
                                        for (int w = 0; w < MAX_WINDOWS; ++w) {
                                            if (g_windows[w].active) {
                                                clamp_window_rect(&g_windows[w].rect);
                                                sync_window_instance_rect(w);
                                            }
                                        }
                                        start_button = ui_taskbar_start_button_rect();
                                        menu_rect = ui_start_menu_rect();
                                        context_menu = desktop_context_menu_rect(context_menu.x, context_menu.y);
                                        fm_context_menu = filemanager_context_menu_rect(fm_context_menu.x, fm_context_menu.y);
                                        mouse.x = (int)SCREEN_WIDTH / 2;
                                        mouse.y = (int)SCREEN_HEIGHT / 2;
                                        mouse.buttons = 0;
                                        dragging = -1;
                                        resizing = -1;
                                        menu_open = 0;
                                        context_open = 0;
                                        fm_context_open = 0;
                                        g_fm_context_has_wallpaper_action = 0;
                                        dirty = 1;
                                    } else {
                                        set_personalize_resolution_status("Falha ao trocar resolucao");
                                        dirty = 1;
                                    }
                                }
                            }
                        }
                        dirty = 1;
                    }
                }
                }
            }
        }

        while ((key = sys_poll_key()) != 0) {
            if (file_dialog.active) {
                if (key == '\b' || key == 127) {
                    file_dialog_backspace(&file_dialog);
                    dirty = 1;
                } else if (key == '\t') {
                    if (file_dialog.mode != FILE_DIALOG_WALLPAPER_PATH) {
                        file_dialog.active_field = 1 - file_dialog.active_field;
                    }
                    dirty = 1;
                } else if (key == '\n') {
                    if (file_dialog_apply(&file_dialog)) {
                        file_dialog_reset(&file_dialog);
                    }
                    dirty = 1;
                } else if (key >= 32 && key <= 126) {
                    file_dialog_insert_char(&file_dialog, key);
                    dirty = 1;
                }
                continue;
            }

            if (key == 6) {
                if (open_window_or_focus_existing(APP_FILEMANAGER, &focused) >= 0) {
                    dirty = 1;
                }
                continue;
            }
            if (menu_open) {
                int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                                  &start_menu_search_scroll :
                                  &start_menu_scroll[(int)start_menu_tab];

                if (key == 27) {
                    menu_open = 0;
                    menu_scroll_dragging = 0;
                    dirty = 1;
                    continue;
                }
                if (key == '\b' || key == 127) {
                    start_menu_backspace(start_menu_search, &start_menu_search_len);
                    filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                                       start_menu_search,
                                                                       filtered_indices,
                                                                       START_MENU_ENTRY_COUNT);
                    start_menu_clamp_scroll(&start_menu_search_scroll, filtered_count);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    if (filtered_count > 0) {
                        const struct start_menu_entry *launch_entry =
                            &g_start_menu_entries[filtered_indices[*scroll_ptr]];

                        if (launch_start_menu_entry(launch_entry, &focused) >= 0) {
                            dirty = 1;
                        }
                        menu_open = 0;
                    }
                    continue;
                }
                if (key == '\t') {
                    start_menu_tab = start_menu_tab == START_MENU_TAB_APPS ?
                                     START_MENU_TAB_GAMES : START_MENU_TAB_APPS;
                    dirty = 1;
                    continue;
                }
                if (key == 'w' || key == 'W' || key == KEY_ARROW_UP) {
                    if (*scroll_ptr > 0) {
                        *scroll_ptr -= 1;
                        dirty = 1;
                    }
                    continue;
                }
                if (key == 's' || key == 'S' || key == KEY_ARROW_DOWN) {
                    if (*scroll_ptr < start_menu_scroll_limit_for_count(filtered_count)) {
                        *scroll_ptr += 1;
                        dirty = 1;
                    }
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    start_menu_append_search_char(start_menu_search, &start_menu_search_len, key);
                    filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                                       start_menu_search,
                                                                       filtered_indices,
                                                                       START_MENU_ENTRY_COUNT);
                    start_menu_clamp_scroll(&start_menu_search_scroll, filtered_count);
                    dirty = 1;
                    continue;
                }
                continue;
            }
            if (key == 20) {
                desktop_request_open_terminal_command("vibefetch");
                dirty = 1;
                continue;
            }

            if (focused < 0 ||
                !g_windows[focused].active ||
                g_windows[focused].minimized) {
                continue;
            }

            if (g_windows[focused].type == APP_TERMINAL) {
                struct terminal_state *term = &g_terms[g_windows[focused].instance];
                if (key == '\b') {
                    terminal_backspace(term);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    if (terminal_execute_command(term)) {
                        free_window(focused);
                        focused = -1;
                    }
                    dirty = 1;
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    terminal_add_input_char(term, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_EDITOR) {
                struct editor_state *ed = &g_editors[g_windows[focused].instance];

                if (key == '\b') {
                    editor_backspace(ed);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    editor_newline(ed);
                    dirty = 1;
                    continue;
                }
                if (key == 19) {
                    if (editor_save(ed)) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    editor_insert_char(ed, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_CALCULATOR) {
                struct calculator_state *calc = &g_calcs[g_windows[focused].instance];

                if (key == '\b' || key == 127) {
                    calculator_backspace(calc);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    calculator_press_key(calc, '=');
                    dirty = 1;
                    continue;
                }
                if ((key >= '0' && key <= '9') ||
                    key == '+' || key == '-' || key == '*' || key == '/' ||
                    key == '=' || key == 'c' || key == 'C') {
                    calculator_press_key(calc, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SKETCHPAD) {
                if (key == 'c' || key == 'C') {
                    sketchpad_clear(&g_sketches[g_windows[focused].instance]);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SNAKE) {
                if (snake_handle_key(&g_snakes[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_TETRIS) {
                if (tetris_handle_key(&g_tetris[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_PACMAN) {
                if (pacman_handle_key(&g_pacman[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SPACE_INVADERS) {
                if (space_invaders_handle_key(&g_space_invaders[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_PONG) {
                if (pong_handle_key(&g_pong[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_DONKEY_KONG) {
                if (donkey_kong_handle_key(&g_donkey_kong[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_BRICK_RACE) {
                if (brick_race_handle_key(&g_brick_race[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_FLAP_BIRB) {
                if (flap_birb_handle_key(&g_flap_birb[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_DOOM) {
                if (doom_handle_key(&g_doom[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_CRAFT) {
                if (key == 'q' || key == 'Q') {
                    free_window(focused);
                    focused = -1;
                    dirty = 1;
                    continue;
                }
                if (craft_handle_key(&g_craft[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            draw_desktop(&mouse, menu_open, start_hover,
                         menu_hover, g_windows, MAX_WINDOWS, focused);

            for (int i = 0; i < MAX_WINDOWS; ++i) {
                int close_hover;
                int min_hover;
                int max_hover;
                int active;
                struct rect close;
                struct rect min;
                struct rect max;

                if (!g_windows[i].active || g_windows[i].minimized) {
                    continue;
                }

                close = window_close_button(&g_windows[i].rect);
                min = window_min_button(&g_windows[i].rect);
                max = window_max_button(&g_windows[i].rect);
                close_hover = point_in_rect(&close, mouse.x, mouse.y);
                min_hover = point_in_rect(&min, mouse.x, mouse.y);
                max_hover = point_in_rect(&max, mouse.x, mouse.y);
                active = (i == focused);

                switch (g_windows[i].type) {
                case APP_TERMINAL:
                    terminal_draw_window(&g_terms[g_windows[i].instance], active,
                                         min_hover, max_hover, close_hover);
                    break;
                case APP_CLOCK:
                    clock_draw_window(&g_clocks[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_FILEMANAGER:
                    filemanager_draw_window(&g_fms[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_EDITOR:
                    editor_draw_window(&g_editors[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_TASKMANAGER:
                    taskmgr_draw_window(&g_tms[g_windows[i].instance], g_windows, MAX_WINDOWS, ticks,
                                        active, min_hover, max_hover, close_hover);
                    break;
                case APP_CALCULATOR:
                    calculator_draw_window(&g_calcs[g_windows[i].instance], active,
                                           min_hover, max_hover, close_hover);
                    break;
                case APP_IMAGEVIEWER:
                    imageviewer_draw_window(&g_imageviewers[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_SKETCHPAD:
                    sketchpad_draw_window(&g_sketches[g_windows[i].instance], active,
                                          min_hover, max_hover, close_hover);
                    break;
                case APP_SNAKE:
                    snake_draw_window(&g_snakes[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_TETRIS:
                    tetris_draw_window(&g_tetris[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_PACMAN:
                    pacman_draw_window(&g_pacman[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_SPACE_INVADERS:
                    space_invaders_draw_window(&g_space_invaders[g_windows[i].instance], active,
                                               min_hover, max_hover, close_hover);
                    break;
                case APP_PONG:
                    pong_draw_window(&g_pong[g_windows[i].instance], active,
                                     min_hover, max_hover, close_hover);
                    break;
                case APP_DONKEY_KONG:
                    donkey_kong_draw_window(&g_donkey_kong[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_BRICK_RACE:
                    brick_race_draw_window(&g_brick_race[g_windows[i].instance], active,
                                           min_hover, max_hover, close_hover);
                    break;
                case APP_FLAP_BIRB:
                    flap_birb_draw_window(&g_flap_birb[g_windows[i].instance], active,
                                          min_hover, max_hover, close_hover);
                    break;
                case APP_DOOM:
                    doom_draw_window(&g_doom[g_windows[i].instance], active,
                                     min_hover, max_hover, close_hover);
                    break;
                case APP_CRAFT:
                    craft_draw_window(&g_craft[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_PERSONALIZE:
                    draw_personalize_window(&g_pers, active, min_hover, max_hover, close_hover, &mouse);
                    break;
                case APP_TRASH:
                    draw_trash_window(&g_trash, active, min_hover, max_hover, close_hover, &mouse);
                    break;
                default:
                    break;
                }
            }

            if (menu_open) {
                draw_start_menu_with_tab(start_menu_tab,
                                         filtered_indices,
                                         filtered_count,
                                         hovered_result,
                                         apps_tab_hover,
                                         games_tab_hover,
                                         start_menu_search_active(start_menu_search) ?
                                         start_menu_search_scroll :
                                         start_menu_scroll[(int)start_menu_tab],
                                         start_menu_search,
                                         menu_search_hover,
                                         menu_search_clear_hover,
                                         menu_sidebar_hover,
                                         menu_sidebar_files_hover,
                                         menu_sidebar_terminal_hover,
                                         menu_sidebar_personalize_hover,
                                         menu_logout_hover,
                                         menu_scroll_thumb_hover);
            }

            if (context_open) {
                ui_draw_surface(&context_menu, ui_color_panel());
                ui_draw_button(&(struct rect){context_menu.x + 2, context_menu.y + 2, context_menu.w - 4, context_menu.h - 4},
                               "Personalizar...",
                               UI_BUTTON_NORMAL,
                               context_personalize_hover);
            }

            if (fm_context_open) {
                ui_draw_surface(&fm_context_menu, ui_color_panel());
                for (int action = 0; action < FMENU_COUNT; ++action) {
                    struct rect item = filemanager_context_item_rect(&fm_context_menu, action);
                    int hover = 0;

                    if (action == FMENU_SET_WALLPAPER && !g_fm_context_has_wallpaper_action) {
                        continue;
                    }

                    if (action == FMENU_OPEN) hover = fm_open_hover;
                    else if (action == FMENU_COPY) hover = fm_copy_hover;
                    else if (action == FMENU_PASTE) hover = fm_paste_hover;
                    else if (action == FMENU_NEW_DIR) hover = fm_new_dir_hover;
                    else if (action == FMENU_NEW_FILE) hover = fm_new_file_hover;
                    else if (action == FMENU_RENAME) hover = fm_rename_hover;
                    else if (action == FMENU_MOVE_TO_TRASH) hover = fm_trash_hover;
                    else if (action == FMENU_SET_WALLPAPER) hover = fm_set_wallpaper_hover;

                    ui_draw_button(&item, filemanager_menu_label(action), UI_BUTTON_NORMAL, hover);
                }
            }

            if (app_context.open) {
                ui_draw_surface(&app_context.menu, ui_color_panel());
                for (int action = 0; action < APPCTX_COUNT; ++action) {
                    struct rect item = app_context_item_rect(&app_context.menu, action);
                    int hover = action == APPCTX_PRIMARY ? app_primary_hover : app_save_as_hover;

                    ui_draw_button(&item,
                                   app_context_menu_label(app_context.type, action),
                                   action == APPCTX_PRIMARY ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL,
                                   hover);
                }
            }

            if (file_dialog.active) {
                draw_file_dialog(&file_dialog, &mouse);
            }

            if (!(focused >= 0 &&
                  g_windows[focused].active &&
                  !g_windows[focused].minimized &&
                  g_windows[focused].type == APP_CRAFT &&
                  g_craft[g_windows[focused].instance].started)) {
                cursor_draw(mouse.x, mouse.y);
            }
            sys_present();
            dirty = 0;
        }

        sys_sleep();
    }

    sys_leave_graphics();
    sys_write_debug("desktop: session stop\n");
}
