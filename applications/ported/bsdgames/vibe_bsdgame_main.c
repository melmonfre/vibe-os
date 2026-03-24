#define VIBE_BSDGAME_NO_EXIT_REMAP
#include "vibe_bsdgame_shim.h"

#include <lang/include/vibe_app_runtime.h>

#ifndef VIBE_BSDGAME_FALLBACK_NAME
#define VIBE_BSDGAME_FALLBACK_NAME "bsdgame"
#endif

typedef int vibe_bsdgame_jmp_buf[5];

extern int vibe_bsdgame_main(int argc, char **argv);
void vibe_bsdgame_install_assets(void) __attribute__((weak));

static vibe_bsdgame_jmp_buf g_bsdgame_exit_env;
static int g_bsdgame_exit_active = 0;
static int g_bsdgame_exit_status = 0;

static inline __attribute__((noreturn)) void vibe_bsdgame_longjmp(vibe_bsdgame_jmp_buf env) {
    __builtin_longjmp((int *)env, 1);
}

int vibe_app_main(int argc, char **argv) {
    const char *progname = VIBE_BSDGAME_FALLBACK_NAME;

    if (argc > 0 && argv && argv[0] && argv[0][0] != '\0') {
        progname = argv[0];
    }

    setprogname(progname);
    if (vibe_bsdgame_install_assets) {
        vibe_bsdgame_install_assets();
    }
    g_bsdgame_exit_status = 0;
    g_bsdgame_exit_active = 1;
    if (__builtin_setjmp((int *)g_bsdgame_exit_env) == 0) {
        g_bsdgame_exit_status = vibe_bsdgame_main(argc, argv);
    }
    g_bsdgame_exit_active = 0;
    return g_bsdgame_exit_status;
}

void vibe_bsdgame_exit(int status) {
    g_bsdgame_exit_status = status;
    if (g_bsdgame_exit_active) {
        vibe_bsdgame_longjmp(g_bsdgame_exit_env);
    }
    for (;;) {
        vibe_app_yield();
    }
}
