#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static void host_usage(void) {
    fprintf(stderr, "usage: host name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *resolved_source = 0;
    char canonical[64];
    char resolved_addr[16];
    uint8_t ipv4[4];

    if (argc != 2) {
        host_usage();
        return 1;
    }

    target = argv[1];
    if (netdiag_load_snapshot("host", &info, &status) != 0) {
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
        printf("%s has address %s\n",
               canonical[0] != '\0' ? canonical : target,
               resolved_addr);
        netdiag_debugf("host: local-ok query=%s canonical=%s addr=%s source=%s\n",
                       target,
                       canonical[0] != '\0' ? canonical : target,
                       resolved_addr,
                       resolved_source != 0 ? resolved_source : "-");
        return 0;
    }
    if (netdiag_require_real_packet_path("host", "dns lookup", &info, &status) != 0) {
        return 1;
    }
    fprintf(stderr,
            "host: dns lookup not implemented yet query=%s server=%s\n",
            target,
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    netdiag_debugf("host: dns lookup pending query=%s server=%s\n",
                   target,
                   status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
