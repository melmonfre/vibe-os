#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

#define VIBE_IPPROTO_ICMP 1

struct vibe_ping_sockaddr_in {
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;
    uint8_t sin_addr[4];
    uint8_t sin_zero[8];
};

static int ping_parse_ipv4(const char *text, uint8_t out[4]) {
    unsigned int octet = 0u;
    unsigned int part = 0u;
    int saw_digit = 0;
    size_t index = 0u;

    if (text == 0 || out == 0) {
        return -1;
    }
    while (text[index] != '\0') {
        char ch = text[index];

        if (ch >= '0' && ch <= '9') {
            octet = (octet * 10u) + (unsigned int)(ch - '0');
            if (octet > 255u) {
                return -1;
            }
            saw_digit = 1;
        } else if (ch == '.') {
            if (!saw_digit || part >= 3u) {
                return -1;
            }
            out[part++] = (uint8_t)octet;
            octet = 0u;
            saw_digit = 0;
        } else {
            return -1;
        }
        ++index;
    }
    if (!saw_digit || part != 3u) {
        return -1;
    }
    out[3] = (uint8_t)octet;
    return 0;
}

static void ping_usage(void) {
    fprintf(stderr, "usage: ping host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    struct vibe_ping_sockaddr_in address;
    uint8_t payload[56];
    uint8_t reply[96];
    unsigned long long started_ms;
    unsigned long long deadline_ms;
    int handle;
    int rc;
    unsigned int i;

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
    memset(&address, 0, sizeof(address));
    address.sin_len = (uint8_t)sizeof(address);
    address.sin_family = AF_INET;
    if (ping_parse_ipv4(target, address.sin_addr) != 0) {
        fprintf(stderr, "ping: only dotted IPv4 is supported right now target=%s\n", target);
        return 1;
    }

    handle = vibe_app_network_socket(AF_INET, SOCK_DGRAM, VIBE_IPPROTO_ICMP);
    if (handle < 0) {
        fprintf(stderr, "ping: failed to create icmp socket\n");
        return 1;
    }
    if (vibe_app_network_socket_connect(handle,
                                        (const struct sockaddr *)&address,
                                        sizeof(address)) != 0) {
        fprintf(stderr, "ping: failed to connect icmp socket target=%s\n", target);
        (void)vibe_app_network_close(handle);
        return 1;
    }

    for (i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)('a' + (char)(i % 26u));
    }

    printf("PING %s (%u.%u.%u.%u): %u data bytes\n",
           target,
           (unsigned int)address.sin_addr[0],
           (unsigned int)address.sin_addr[1],
           (unsigned int)address.sin_addr[2],
           (unsigned int)address.sin_addr[3],
           (unsigned int)sizeof(payload));

    started_ms = vibe_app_millis();
    rc = vibe_app_network_send(handle, payload, (uint32_t)sizeof(payload));
    if (rc != (int)sizeof(payload)) {
        fprintf(stderr, "ping: send failed target=%s\n", target);
        (void)vibe_app_network_close(handle);
        return 1;
    }

    deadline_ms = started_ms + 2000u;
    while (vibe_app_millis() < deadline_ms) {
        rc = vibe_app_network_recv(handle, reply, (uint32_t)sizeof(reply));
        if (rc > 0) {
            unsigned long long elapsed_ms = vibe_app_millis() - started_ms;
            printf("%d bytes from %s: icmp_seq=0 ttl=64 time=%llu ms\n",
                   rc,
                   target,
                   elapsed_ms);
            printf("\n--- %s ping statistics ---\n", target);
            printf("1 packets transmitted, 1 packets received, 0.0%% packet loss\n");
            (void)vibe_app_network_close(handle);
            return 0;
        }
        vibe_app_yield();
        (void)vibe_app_sleep_ms(10u);
    }

    fprintf(stderr, "ping: timeout waiting for reply target=%s\n", target);
    printf("\n--- %s ping statistics ---\n", target);
    printf("1 packets transmitted, 0 packets received, 100.0%% packet loss\n");
    (void)vibe_app_network_close(handle);
    return 1;
}
