#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

static int dig_is_loopback_target(const char *target) {
    if (target == 0) {
        return 0;
    }
    return strcmp(target, "localhost") == 0 || strcmp(target, "127.0.0.1") == 0;
}

static void dig_usage(void) {
    fprintf(stderr, "usage: dig name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;
    const char *target;
    const char *server;

    if (argc != 2) {
        dig_usage();
        return 1;
    }

    target = argv[1];
    server = "127.0.0.1";
    if (dig_is_loopback_target(target)) {
        printf("; <<>> Vibe dig <<>> %s\n", target);
        printf(";; SERVER: %s#53\n", server);
        printf("\n;; ANSWER SECTION:\n");
        printf("localhost.\t0\tIN\tA\t127.0.0.1\n");
        printf("\n;; status: loopback\n");
        printf("dig: loopback-ok query=%s\n", target);
        return 0;
    }

    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "dig: network status unavailable\n");
        return 1;
    }

    server = status.dns_server[0] != '\0' ? status.dns_server : "-";
    printf("; <<>> Vibe dig <<>> %s\n", target);
    printf(";; SERVER: %s#53\n", server);
    printf(";; status: unsupported\n");
    fprintf(stderr, "dig: query=%s server=%s status=unsupported\n", target, server);
    return 1;
}
