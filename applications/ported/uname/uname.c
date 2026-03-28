/*
 * uname - VibeOS port based on compat/usr.bin/uname/uname.c
 */

#include "compat/include/compat.h"

#define PRINT_SYSNAME      0x01
#define PRINT_NODENAME     0x02
#define PRINT_RELEASE      0x04
#define PRINT_VERSION      0x08
#define PRINT_MACHINE      0x10
#define PRINT_ALL          0x1f
#define PRINT_MACHINE_ARCH 0x20

struct vibe_utsname {
    char sysname[64];
    char nodename[64];
    char release[64];
    char version[64];
    char machine[64];
    char machine_arch[64];
};

static void uname_fill(struct vibe_utsname *u) {
    const char *hostname;
    const char *release;
    const char *version;

    if (u == 0) {
        return;
    }

    memset(u, 0, sizeof(*u));
    snprintf(u->sysname, sizeof(u->sysname), "VibeOS");
    hostname = getenv("HOSTNAME");
    if (hostname == 0 || hostname[0] == '\0') {
        hostname = "vibe-machine";
    }
    snprintf(u->nodename, sizeof(u->nodename), "%s", hostname);
    release = getenv("VIBE_RELEASE");
    if (release == 0 || release[0] == '\0') {
        release = "compat";
    }
    snprintf(u->release, sizeof(u->release), "%s", release);
    version = getenv("VIBE_VERSION");
    if (version == 0 || version[0] == '\0') {
        version = "userspace-appfs";
    }
    snprintf(u->version, sizeof(u->version), "%s", version);
    snprintf(u->machine, sizeof(u->machine), "i686");
    snprintf(u->machine_arch, sizeof(u->machine_arch), "i386");
}

static void usage(void) {
    fprintf(stderr, "usage: uname [-amnprsv]\n");
}

int vibe_app_main(int argc, char **argv) {
    struct vibe_utsname u;
    int print_mask = 0;
    int argi;
    int space = 0;

    for (argi = 1; argi < argc; ++argi) {
        const char *arg = argv[argi];
        int ch_index;

        if (arg == 0 || arg[0] != '-' || arg[1] == '\0') {
            usage();
            return 1;
        }
        for (ch_index = 1; arg[ch_index] != '\0'; ++ch_index) {
            switch (arg[ch_index]) {
            case 'a':
                print_mask |= PRINT_ALL;
                break;
            case 'm':
                print_mask |= PRINT_MACHINE;
                break;
            case 'n':
                print_mask |= PRINT_NODENAME;
                break;
            case 'p':
                print_mask |= PRINT_MACHINE_ARCH;
                break;
            case 'r':
                print_mask |= PRINT_RELEASE;
                break;
            case 's':
                print_mask |= PRINT_SYSNAME;
                break;
            case 'v':
                print_mask |= PRINT_VERSION;
                break;
            default:
                usage();
                return 1;
            }
        }
    }

    if (print_mask == 0) {
        print_mask = PRINT_SYSNAME;
    }

    uname_fill(&u);

    if ((print_mask & PRINT_SYSNAME) != 0) {
        fputs(u.sysname, stdout);
        space = 1;
    }
    if ((print_mask & PRINT_NODENAME) != 0) {
        if (space != 0) {
            putchar(' ');
        }
        fputs(u.nodename, stdout);
        space = 1;
    }
    if ((print_mask & PRINT_RELEASE) != 0) {
        if (space != 0) {
            putchar(' ');
        }
        fputs(u.release, stdout);
        space = 1;
    }
    if ((print_mask & PRINT_VERSION) != 0) {
        if (space != 0) {
            putchar(' ');
        }
        fputs(u.version, stdout);
        space = 1;
    }
    if ((print_mask & PRINT_MACHINE) != 0) {
        if (space != 0) {
            putchar(' ');
        }
        fputs(u.machine, stdout);
        space = 1;
    }
    if ((print_mask & PRINT_MACHINE_ARCH) != 0) {
        if (space != 0) {
            putchar(' ');
        }
        fputs(u.machine_arch, stdout);
    }
    putchar('\n');
    return 0;
}
