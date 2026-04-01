#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static void dig_usage(void) {
    fprintf(stderr, "usage: dig name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *server;

    if (argc != 2) {
        dig_usage();
        return 1;
    }

    target = argv[1];
    server = "127.0.0.1";
    if (netdiag_is_loopback_target(target)) {
        printf("; <<>> Vibe dig <<>> %s\n", target);
        printf(";; SERVER: %s#53\n", server);
        printf("\n;; ANSWER SECTION:\n");
        printf("localhost.\t0\tIN\tA\t127.0.0.1\n");
        printf("\n;; status: loopback\n");
        printf("dig: loopback-ok query=%s\n", target);
        return 0;
    }

    if (netdiag_load_snapshot("dig", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("dig", "dns query", &info, &status) != 0) {
        return 1;
    }

    server = status.dns_server[0] != '\0' ? status.dns_server : "-";
    printf("; <<>> Vibe dig <<>> %s\n", target);
    printf(";; SERVER: %s#53\n", server);
    printf(";; status: not-implemented\n");
    fprintf(stderr, "dig: dns query not implemented yet query=%s server=%s\n", target, server);
    return 1;
}
