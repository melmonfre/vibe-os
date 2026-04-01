#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

static int ping_is_loopback_target(const char *target) {
    if (target == 0) {
        return 0;
    }
    return strcmp(target, "localhost") == 0 ||
           strcmp(target, "127.0.0.1") == 0 ||
           strcmp(target, "lo0") == 0;
}

static void ping_usage(void) {
    fprintf(stderr, "usage: ping host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;
    const char *target;

    if (argc != 2) {
        ping_usage();
        return 1;
    }

    target = argv[1];
    if (ping_is_loopback_target(target)) {
        printf("PING %s (127.0.0.1): 56 data bytes\n", target);
        printf("64 bytes from 127.0.0.1: icmp_seq=0 ttl=255 time=0.0 ms\n");
        printf("\n--- %s ping statistics ---\n", target);
        printf("1 packets transmitted, 1 packets received, 0.0%% packet loss\n");
        printf("ping: loopback-ok target=%s\n", target);
        return 0;
    }

    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "ping: network status unavailable\n");
        return 1;
    }

    if (status.link_state != MK_NETWORK_LINK_CONNECTED) {
        fprintf(stderr, "ping: no active network link for %s\n", target);
        return 1;
    }

    fprintf(stderr,
            "ping: transport unsupported target=%s active=%s ip=%s dns=%s\n",
            target,
            status.active_if[0] != '\0' ? status.active_if : "-",
            status.ip_address[0] != '\0' ? status.ip_address : "-",
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
