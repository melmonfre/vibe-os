#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"
#include "applications/ported/include/http_client.h"

/* Item P3-91: wget over the stack's own TCP/HTTP client (the same
 * http_client.h that curl uses — every failure mode is named, nothing
 * fakes a success). Difference from curl: the body is written to a LOCAL
 * FILE (vibe_app_write_file) instead of stdout, like real wget -O. */

#define WGET_BODY_MAX VIBE_HTTP_BODY_MAX
#define WGET_DEFAULT_OUT "/runtime/wget.out"

static const char *wget_default_out_name(const char *url) {
    static char name[40];
    const char *slash;
    const char *q;
    size_t len = 0u;

    name[0] = '\0';
    if (url == 0) {
        return name;
    }
    slash = url;
    for (const char *p = url; *p != '\0'; ++p) {
        if (*p == '/') {
            slash = p + 1;
        }
    }
    q = slash;
    while (*q != '\0' && *q != '?' && *q != '#') {
        ++q;
    }
    len = (size_t)(q - slash);
    if (len == 0u || len >= sizeof(name)) {
        return name;
    }
    for (size_t i = 0u; i < len; ++i) {
        name[i] = slash[i];
    }
    name[len] = '\0';
    return name;
}

static void wget_usage(void) {
    fprintf(stderr, "usage: wget <http://url> [output-path]\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    static uint8_t body[WGET_BODY_MAX];
    char server[40];
    char line0[64];
    char out_path[96];
    const char *out = 0;
    const char *srv = 0;
    uint32_t body_len = 0u;
    uint32_t status_code = 0u;
    int hrc;
    int wrc;

    if (argc < 2 || argc > 3) {
        wget_usage();
        return 1;
    }
    if (argv[1] == 0 || strncmp(argv[1], "http://", 7) != 0) {
        fprintf(stderr, "wget: only http:// urls are supported (own TCP stack)\n");
        netdiag_debugf("wget: unsupported scheme arg=%s\n",
                       argv[1] != 0 ? argv[1] : "-");
        return 1;
    }

    out = argv[2];
    if (out == 0 || *out == '\0') {
        out = wget_default_out_name(argv[1]);
        if (*out == '\0') {
            out = WGET_DEFAULT_OUT;
        }
    }
    if (out[0] != '/') {
        fprintf(stderr, "wget: output path must be absolute: %s\n", out);
        return 1;
    }

    memset(out_path, 0, sizeof(out_path));
    for (int i = 0; out[i] != '\0' && i < (int)sizeof(out_path) - 1; ++i) {
        out_path[i] = out[i];
    }

    if (netdiag_load_snapshot("wget", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("wget", "remote transfer", &info, &status) != 0) {
        return 1;
    }

    server[0] = '\0';
    if (vibe_http_pick_server(&status, server, (int)sizeof(server)) == 0) {
        srv = server;
    }

    hrc = vibe_http_get(argv[1], srv, body, (uint32_t)sizeof(body),
                        &body_len, &status_code, line0, (int)sizeof(line0));

    if (hrc == -5) {
        /* body larger than our fixed buffer: save the truncated part but
         * FAIL honestly — a partial download is not a download */
        wrc = vibe_app_write_file(out_path, body, (int)body_len);
        netdiag_debugf("wget: truncated url=%s rc=-5 bytes=%u out=%s write=%d\n",
                       argv[1], (unsigned)body_len, out_path, wrc);
        fprintf(stderr, "wget: response larger than %u bytes; saved partial "
                        "%u bytes (truncated) to %s\n",
                WGET_BODY_MAX, (unsigned)body_len, out_path);
        return 1;
    }
    if (hrc != 0) {
        fprintf(stderr, "wget: http failed url=%s rc=%d dns=%s\n",
                argv[1], hrc, srv != 0 ? srv : "-");
        netdiag_debugf("wget: http-fail url=%s rc=%d dns=%s\n",
                       argv[1], hrc, srv != 0 ? srv : "-");
        return 1;
    }

    wrc = vibe_app_write_file(out_path, body, (int)body_len);
    if (wrc != 0) {
        fprintf(stderr, "wget: failed to write %s rc=%d\n", out_path, wrc);
        netdiag_debugf("wget: write-fail out=%s rc=%d bytes=%u\n",
                       out_path, wrc, (unsigned)body_len);
        return 1;
    }

    {
        /* Read the file back through the FS API and compare against the
         * bytes we just handed to write_file.  The buffer comes from the
         * fs node, not from our own array, so this proves the bytes
         * actually landed in the filesystem.  A shell `cat` CANNOT be the
         * proof: the interactive shell and the app host are separate
         * binaries, each with its own fs pool (KNOWN_BUGS #28). */
        const char *rb = 0;
        int rb_size = 0;
        int rrc = vibe_app_read_file(out_path, &rb, &rb_size);
        int match = (rrc == 0 && rb != 0 && rb_size == (int)body_len);
        for (int i = 0; match && i < (int)body_len; ++i) {
            if (rb[i] != body[i]) {
                match = 0;
            }
        }
        netdiag_debugf("wget: readback out=%s rc=%d size=%d match=%d\n",
                       out_path, rrc, rb_size, match);
        if (!match) {
            fprintf(stderr, "wget: readback-fail out=%s rc=%d size=%d\n",
                    out_path, rrc, rb_size);
            return 1;
        }
    }

    /* Witness first: console echo is per-char IPC and slow under TCG. */
    netdiag_debugf("wget: saved url=%s out=%s bytes=%u status=%u line0=%s\n",
                   argv[1], out_path, (unsigned)body_len,
                   (unsigned)status_code, line0);
    printf("wget: saved url=%s out=%s bytes=%u status=%u\n",
           argv[1], out_path, (unsigned)body_len, (unsigned)status_code);
    return 0;
}
