#ifndef VIBE_PORTED_NETWORK_DIAG_COMMON_H
#define VIBE_PORTED_NETWORK_DIAG_COMMON_H

#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

static const char *netdiag_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netdiag_if_kind_name(uint32_t kind) {
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

static const char *netdiag_transport_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST) != 0u) {
        return "service-host";
    }
    return "legacy-local";
}

static const char *netdiag_datapath_executor_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "kernel";
    }
    return "userland";
}

static const char *netdiag_packet_path_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_REAL_PACKET_DATAPATH) != 0u) {
        return "real";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "telemetry-only";
    }
    return "none";
}

static const char *netdiag_socket_scope_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) != 0u) {
        return "local-only";
    }
    return "network";
}

static int netdiag_load_snapshot(const char *tool,
                                 struct mk_network_info *info,
                                 struct mk_network_status *status) {
    if (info == 0 || status == 0) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    memset(status, 0, sizeof(*status));
    if (vibe_app_network_get_info(info) != 0 ||
        vibe_app_network_get_status(status) != 0) {
        fprintf(stderr, "%s: network service unavailable\n", tool);
        return -1;
    }
    return 0;
}

static __attribute__((unused)) void netdiag_print_status_summary(
    const struct mk_network_info *info,
    const struct mk_network_status *status) {
    printf("state: %s\n", netdiag_link_state_name(status->link_state));
    printf("active: %s (%s)\n",
           status->active_if[0] != '\0' ? status->active_if : "-",
           netdiag_if_kind_name(status->active_kind));
    printf("ip: %s\n", status->ip_address[0] != '\0' ? status->ip_address : "-");
    printf("gateway: %s\n", status->gateway[0] != '\0' ? status->gateway : "-");
    printf("dns: %s\n", status->dns_server[0] != '\0' ? status->dns_server : "-");
    printf("transport: %s\n", netdiag_transport_name(info));
    printf("datapath executor: %s\n", netdiag_datapath_executor_name(info));
    printf("packet path: %s\n", netdiag_packet_path_name(info));
    printf("socket scope: %s\n", netdiag_socket_scope_name(info));
}

static int netdiag_require_real_packet_path(const char *tool,
                                            const char *subject,
                                            const struct mk_network_info *info,
                                            const struct mk_network_status *status) {
    if (info != 0 &&
        (info->flags & MK_NETWORK_CAPS_REAL_PACKET_DATAPATH) != 0u &&
        (info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) == 0u) {
        return 0;
    }

    fprintf(stderr,
            "%s: %s unavailable: current stack reports packet_path=%s socket_scope=%s\n",
            tool,
            subject,
            netdiag_packet_path_name(info),
            netdiag_socket_scope_name(info));
    if (status != 0) {
        fprintf(stderr,
                "%s: link=%s active=%s ip=%s dns=%s\n",
                tool,
                netdiag_link_state_name(status->link_state),
                status->active_if[0] != '\0' ? status->active_if : "-",
                status->ip_address[0] != '\0' ? status->ip_address : "-",
                status->dns_server[0] != '\0' ? status->dns_server : "-");
    }
    return -1;
}

#endif
