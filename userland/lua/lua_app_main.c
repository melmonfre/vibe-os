#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/lua/include/lua_main.h>

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

int vibe_app_main(int argc, char **argv) {
    console_init();
    fs_init();
    return vibe_lua_main(argc, argv);
}
