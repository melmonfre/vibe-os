/*
 * sync - VibeOS port based on compat/bin/sync/sync.c
 */

#include "compat/include/compat.h"

int vibe_app_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    sync();
    return 0;
}
