#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/ui.h>

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

static const char *desktop_app_name(int argc, char **argv) {
    if (argc > 0 && argv && argv[0] && argv[0][0] != '\0') {
        return argv[0];
    }
    return "startx";
}

static void desktop_prepare_launch(int argc, char **argv) {
    const char *app_name = desktop_app_name(argc, argv);
    const char *path = "";

    if (argc > 1 && argv && argv[1]) {
        path = argv[1];
    }

    if (str_eq(app_name, "edit")) {
        desktop_request_open_editor(path);
    } else if (str_eq(app_name, "nano")) {
        desktop_request_open_nano(path);
    }
}

int vibe_app_main(int argc, char **argv) {
    console_init();
    fs_init();
    desktop_prepare_launch(argc, argv);
    desktop_main();
    return 0;
}
