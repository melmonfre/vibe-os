#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/sectorc/include/sectorc.h>

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

int vibe_app_main(int argc, char **argv) {
    console_init();
    fs_init();
    return sectorc_main(argc, argv);
}
