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
    const char *resolved_source = 0;
    char canonical[64];
    char resolved_addr[16];
    uint8_t ipv4[4];

    if (argc != 2) {
        dig_usage();
        return 1;
    }

    target = argv[1];
    server = "127.0.0.1";

    if (netdiag_load_snapshot("dig", &info, &status) != 0) {
        return 1;
    }
    memset(canonical, 0, sizeof(canonical));
    if (netdiag_resolve_name_local(target,
                                   &status,
                                   ipv4,
                                   canonical,
                                   sizeof(canonical),
                                   &resolved_source) == 0) {
        netdiag_format_ipv4(resolved_addr, sizeof(resolved_addr), ipv4);
        server = strcmp(resolved_source != 0 ? resolved_source : "", "loopback") == 0 ? "127.0.0.1" : "runtime";
        printf("; <<>> Vibe dig <<>> %s\n", target);
        printf(";; SERVER: %s#53\n", server);
        printf("\n;; ANSWER SECTION:\n");
        printf("%s.\t0\tIN\tA\t%s\n",
               canonical[0] != '\0' ? canonical : target,
               resolved_addr);
        printf("\n;; status: local-%s\n", resolved_source != 0 ? resolved_source : "resolved");
        netdiag_debugf("dig: local-ok query=%s canonical=%s addr=%s source=%s\n",
                       target,
                       canonical[0] != '\0' ? canonical : target,
                       resolved_addr,
                       resolved_source != 0 ? resolved_source : "-");
        return 0;
    }
    if (netdiag_require_real_packet_path("dig", "dns query", &info, &status) != 0) {
        return 1;
    }

    server = status.dns_server[0] != '\0' ? status.dns_server : "-";
    printf("; <<>> Vibe dig <<>> %s\n", target);
    printf(";; SERVER: %s#53\n", server);
    printf(";; status: not-implemented\n");
    fprintf(stderr, "dig: dns query not implemented yet query=%s server=%s\n", target, server);
    netdiag_debugf("dig: dns query pending query=%s server=%s\n", target, server);
    return 1;
}
