#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static void ftp_usage(void) {
    fprintf(stderr, "usage: ftp host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;

    if (argc != 2) {
        ftp_usage();
        return 1;
    }

    if (netdiag_load_snapshot("ftp", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("ftp", "ftp transport", &info, &status) != 0) {
        return 1;
    }
    fprintf(stderr,
            "ftp: ftp transport not implemented yet target=%s active=%s dns=%s\n",
            argv[1],
            status.active_if[0] != '\0' ? status.active_if : "-",
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    netdiag_debugf("ftp: ftp transport pending target=%s active=%s dns=%s\n",
                   argv[1],
                   status.active_if[0] != '\0' ? status.active_if : "-",
                   status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
