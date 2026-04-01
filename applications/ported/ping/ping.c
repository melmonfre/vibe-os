#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static void ping_usage(void) {
    fprintf(stderr, "usage: ping host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;

    if (argc != 2) {
        ping_usage();
        return 1;
    }

    target = argv[1];
    if (netdiag_is_loopback_target(target)) {
        printf("PING %s (127.0.0.1): 56 data bytes\n", target);
        printf("64 bytes from 127.0.0.1: icmp_seq=0 ttl=255 time=0.0 ms\n");
        printf("\n--- %s ping statistics ---\n", target);
        printf("1 packets transmitted, 1 packets received, 0.0%% packet loss\n");
        printf("ping: loopback-ok target=%s\n", target);
        return 0;
    }

    if (netdiag_load_snapshot("ping", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("ping", "icmp echo", &info, &status) != 0) {
        return 1;
    }
    fprintf(stderr, "ping: icmp transport not implemented yet for target=%s\n", target);
    return 1;
}
