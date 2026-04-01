#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static void host_usage(void) {
    fprintf(stderr, "usage: host name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;

    if (argc != 2) {
        host_usage();
        return 1;
    }

    target = argv[1];
    if (netdiag_is_loopback_target(target)) {
        printf("localhost has address 127.0.0.1\n");
        printf("host: loopback-ok query=%s\n", target);
        return 0;
    }

    if (netdiag_load_snapshot("host", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("host", "dns lookup", &info, &status) != 0) {
        return 1;
    }
    fprintf(stderr,
            "host: dns lookup not implemented yet query=%s server=%s\n",
            target,
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
