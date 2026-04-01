#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

static void ftp_usage(void) {
    fprintf(stderr, "usage: ftp host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;

    if (argc != 2) {
        ftp_usage();
        return 1;
    }

    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "ftp: network status unavailable\n");
        return 1;
    }

    fprintf(stderr,
            "ftp: transport unsupported target=%s active=%s dns=%s\n",
            argv[1],
            status.active_if[0] != '\0' ? status.active_if : "-",
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
