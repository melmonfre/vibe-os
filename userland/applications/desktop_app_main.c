#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

static enum app_type desktop_app_type(const char *app_name) {
    if (str_eq(app_name, "vi") || str_eq(app_name, "vim")) {
        app_name = "edit";
    } else if (str_eq(app_name, "mg")) {
        app_name = "nano";
    }

    if (str_eq(app_name, "terminal")) {
        return APP_TERMINAL;
    }
    if (str_eq(app_name, "clock")) {
        return APP_CLOCK;
    }
    if (str_eq(app_name, "filemanager")) {
        return APP_FILEMANAGER;
    }
    if (str_eq(app_name, "editor")) {
        return APP_EDITOR;
    }
    if (str_eq(app_name, "taskmgr")) {
        return APP_TASKMANAGER;
    }
    if (str_eq(app_name, "calculator")) {
        return APP_CALCULATOR;
    }
    if (str_eq(app_name, "sketchpad")) {
        return APP_SKETCHPAD;
    }
    if (str_eq(app_name, "snake")) {
        return APP_SNAKE;
    }
    if (str_eq(app_name, "tetris")) {
        return APP_TETRIS;
    }
    if (str_eq(app_name, "2048")) {
        return APP_2048;
    }
    if (str_eq(app_name, "minesweeper")) {
        return APP_MINESWEEPER;
    }
    if (str_eq(app_name, "pacman")) {
        return APP_PACMAN;
    }
    if (str_eq(app_name, "space_invaders")) {
        return APP_SPACE_INVADERS;
    }
    if (str_eq(app_name, "pong")) {
        return APP_PONG;
    }
    if (str_eq(app_name, "donkey_kong")) {
        return APP_DONKEY_KONG;
    }
    if (str_eq(app_name, "brick_race")) {
        return APP_BRICK_RACE;
    }
    if (str_eq(app_name, "flap_birb")) {
        return APP_FLAP_BIRB;
    }
    if (str_eq(app_name, "doom")) {
        return APP_DOOM;
    }
    if (str_eq(app_name, "craft")) {
        return APP_CRAFT;
    }
    if (str_eq(app_name, "imageviewer")) {
        return APP_IMAGEVIEWER;
    }
    if (str_eq(app_name, "audioplayer")) {
        return APP_AUDIO_PLAYER;
    }
    if (str_eq(app_name, "personalize")) {
        return APP_PERSONALIZE;
    }
    return APP_NONE;
}

static const char *desktop_app_name(int argc, char **argv) {
    if (argc > 0 && argv && argv[0] && argv[0][0] != '\0') {
        return argv[0];
    }
    return "startx";
}

static void desktop_app_debug_launch(const char *app_name) {
    char msg[64];

    msg[0] = '\0';
    str_append(msg, "desktop.app: launch ", (int)sizeof(msg));
    str_append(msg, app_name ? app_name : "startx", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void desktop_prepare_launch(int argc, char **argv) {
    const char *app_name = desktop_app_name(argc, argv);
    const char *path = "";
    enum app_type type = desktop_app_type(app_name);

    if (argc > 1 && argv && argv[1]) {
        path = argv[1];
    }

    if (str_eq(app_name, "edit") || str_eq(app_name, "vi") || str_eq(app_name, "vim")) {
        desktop_request_open_editor(path);
    } else if (str_eq(app_name, "nano") || str_eq(app_name, "mg")) {
        desktop_request_open_nano(path);
    } else if (type != APP_NONE) {
        desktop_request_open_app(type);
    }
}

int vibe_app_main(int argc, char **argv) {
    const char *app_name = desktop_app_name(argc, argv);

    desktop_app_debug_launch(app_name);
    if (!fs_ready()) {
        sys_write_debug("desktop.app: fs init\n");
        fs_init();
    } else {
        sys_write_debug("desktop.app: fs ready\n");
    }
    sys_write_debug("desktop.app: prepare launch\n");
    desktop_prepare_launch(argc, argv);
    sys_write_debug("desktop.app: enter desktop_main\n");
    desktop_main();
    sys_write_debug("desktop.app: desktop_main returned\n");
    return 0;
}
