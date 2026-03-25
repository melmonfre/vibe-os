/*
 * Port adapted from OpenBSD compat/bin/mkdir/mkdir.c for VibeOS.
 *
 * Supported flags:
 *   -p       create parent directories as needed
 *   -m MODE  accepted for compatibility, currently ignored by VibeOS
 */

#include "compat/include/compat.h"

static void mkdir_usage(void) {
    fprintf(stderr, "usage: mkdir [-p] [-m mode] directory ...\n");
}

static void trim_trailing_slashes(char *path) {
    size_t len;

    if (path == 0 || path[0] == '\0') {
        return;
    }

    len = strlen(path);
    while (len > 1u && path[len - 1u] == '/') {
        path[len - 1u] = '\0';
        --len;
    }
}

static int ensure_existing_dir(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 0 : -1;
}

static int mkdir_parents(char *path) {
    char *cursor;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    trim_trailing_slashes(path);
    cursor = path;
    if (*cursor == '/') {
        ++cursor;
    }

    for (;;) {
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }

        if (*cursor == '/') {
            *cursor = '\0';
            if (path[0] != '\0' && mkdir(path, 0777) != 0 && ensure_existing_dir(path) != 0) {
                *cursor = '/';
                return -1;
            }
            *cursor = '/';
            while (*cursor == '/') {
                ++cursor;
            }
            continue;
        }

        if (mkdir(path, 0777) != 0 && ensure_existing_dir(path) != 0) {
            return -1;
        }
        return 0;
    }
}

int vibe_app_main(int argc, char **argv) {
    int pflag = 0;
    int errors = 0;
    int i = 1;

    while (i < argc && argv[i] != 0 && argv[i][0] == '-') {
        if (strcmp(argv[i], "--") == 0) {
            ++i;
            break;
        }
        if (strcmp(argv[i], "-p") == 0) {
            pflag = 1;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                mkdir_usage();
                return 1;
            }
            i += 2;
            continue;
        }

        mkdir_usage();
        return 1;
    }

    if (i >= argc) {
        mkdir_usage();
        return 1;
    }

    for (; i < argc; ++i) {
        char path[256];
        size_t len = strlen(argv[i]);

        if (len >= sizeof(path)) {
            fprintf(stderr, "mkdir: %s: path too long\n", argv[i]);
            errors = 1;
            continue;
        }

        memcpy(path, argv[i], len + 1u);
        trim_trailing_slashes(path);

        if (path[0] == '\0') {
            fprintf(stderr, "mkdir: %s: invalid path\n", argv[i]);
            errors = 1;
            continue;
        }

        if (pflag) {
            if (mkdir_parents(path) != 0) {
                fprintf(stderr, "mkdir: %s: failed\n", argv[i]);
                errors = 1;
            }
            continue;
        }

        if (mkdir(path, 0777) != 0) {
            fprintf(stderr, "mkdir: %s: failed\n", argv[i]);
            errors = 1;
        }
    }

    return errors;
}
