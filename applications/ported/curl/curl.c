#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

static int curl_starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) {
        return 0;
    }
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static const char *curl_resolve_file_target(const char *arg) {
    if (arg == 0) {
        return 0;
    }
    if (curl_starts_with(arg, "file://")) {
        return arg + 7;
    }
    if (arg[0] == '/') {
        return arg;
    }
    return 0;
}

static void curl_usage(void) {
    fprintf(stderr, "usage: curl <url|file>\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *data = 0;
    int size = 0;

    if (argc != 2) {
        curl_usage();
        return 1;
    }

    target = curl_resolve_file_target(argv[1]);
    if (target != 0) {
        if (vibe_app_read_file(target, &data, &size) != 0 || data == 0 || size < 0) {
            fprintf(stderr, "curl: failed to read %s\n", target);
            return 1;
        }
        if (size > 0) {
            (void)fwrite(data, 1u, (size_t)size, stdout);
            if (data[size - 1] != '\n') {
                putchar('\n');
            }
        }
        printf("curl: file-ok target=%s bytes=%d\n", target, size);
        return 0;
    }

    if (netdiag_load_snapshot("curl", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("curl", "remote transfer", &info, &status) != 0) {
        return 1;
    }

    fprintf(stderr,
            "curl: remote transfer not implemented yet url=%s active=%s dns=%s\n",
            argv[1],
            status.active_if[0] != '\0' ? status.active_if : "-",
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
