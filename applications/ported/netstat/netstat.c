#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>
#include "applications/ported/include/network_diag_common.h"

static const char *netstat_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netstat_if_kind_name(uint32_t kind) {
    switch (kind) {
    case MK_NETWORK_IF_WIFI:
        return "wifi";
    case MK_NETWORK_IF_ETHERNET:
        return "ethernet";
    case MK_NETWORK_IF_LOOPBACK:
        return "loopback";
    default:
        return "unknown";
    }
}

static const char *netstat_lease_state_name(const struct mk_network_status *status) {
    if (status == 0) {
        return "unavailable";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTING) {
        return "acquiring";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTED) {
        return status->ip_address[0] != '\0' ? "bound" : "link-only";
    }
    return "none";
}

static void netstat_usage(void) {
    fprintf(stderr, "usage: netstat\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *active_if;
    const char *dns;
    const char *gateway;

    (void)argv;
    if (argc != 1) {
        netstat_usage();
        return 1;
    }

    memset(&info, 0, sizeof(info));
    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_info(&info) != 0 ||
        vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "netstat: network status unavailable\n");
        return 1;
    }

    active_if = status.active_if[0] != '\0' ? status.active_if : "lo0";
    gateway = status.gateway[0] != '\0' ? status.gateway : "-";
    dns = status.dns_server[0] != '\0' ? status.dns_server : "-";

    printf("netstat: state=%s active=%s lease=%s\n",
           netstat_link_state_name(status.link_state),
           active_if,
           netstat_lease_state_name(&status));
    printf("Active Internet connections\n");
    printf("Proto  Recv-Q Send-Q Local Address      Foreign Address    State\n");
    printf("link   0      0      %-18s %-18s %s\n",
           active_if,
           gateway,
           netstat_link_state_name(status.link_state));
    printf("dns    0      0      %-18s %-18s %s\n",
           dns,
           "-",
           status.dns_server[0] != '\0' ? "configured" : "pending");
    printf("info   0      0      %-18s %-18s %s\n",
           status.ip_address[0] != '\0' ? status.ip_address : "-",
           netstat_if_kind_name(status.active_kind),
           netstat_lease_state_name(&status));
    printf("\n");
    printf("caps: families=0x%x sockets=0x%x flags=0x%x\n",
           (unsigned)info.supported_families,
           (unsigned)info.supported_socket_types,
           (unsigned)info.flags);
    printf("telemetry: max=%u rx-cap=%u eventq=%u backlog=%u\n",
           (unsigned)info.max_sockets,
           (unsigned)info.socket_rx_capacity,
           (unsigned)info.event_queue_depth,
           (unsigned)info.listen_backlog_max);
    printf("sockets: open=%u listening=%u connected=%u recv-ready=%u accept-ready=%u pending-rx=%u\n",
           (unsigned)status.open_socket_count,
           (unsigned)status.listening_socket_count,
           (unsigned)status.connected_socket_count,
           (unsigned)status.recv_ready_count,
           (unsigned)status.accept_ready_count,
           (unsigned)status.pending_rx_bytes);
    printf("backend: rx-frames=%u tx-frames=%u\n",
           (unsigned)status.backend_rx_frames,
           (unsigned)status.backend_tx_frames);
    printf("note: live socket table export is not implemented; readiness/backlog telemetry reflects the current local/socket MVP\n");
    netdiag_debugf("netstat: telemetry ok active=%s lease=%s\n",
                   active_if,
                   netstat_lease_state_name(&status));
    return 0;
}
