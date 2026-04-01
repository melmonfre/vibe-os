#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

static int host_is_loopback_target(const char *target) {
    if (target == 0) {
        return 0;
    }
    return strcmp(target, "localhost") == 0 || strcmp(target, "127.0.0.1") == 0;
}

static void host_usage(void) {
    fprintf(stderr, "usage: host name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;
    const char *target;

    if (argc != 2) {
        host_usage();
        return 1;
    }

    target = argv[1];
    if (host_is_loopback_target(target)) {
        printf("localhost has address 127.0.0.1\n");
        printf("host: loopback-ok query=%s\n", target);
        return 0;
    }

    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "host: network status unavailable\n");
        return 1;
    }

    fprintf(stderr,
            "host: query=%s server=%s status=unsupported\n",
            target,
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
