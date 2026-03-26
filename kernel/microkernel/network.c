#include <kernel/kernel_string.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>
#include <string.h>

struct mk_network_scan_entry_state {
    char ssid[MK_NETWORK_SSID_MAX + 1];
    uint32_t signal_strength;
    uint32_t security;
};

#define MK_NETWORK_SOCKET_MAX 8
#define MK_NETWORK_SOCKET_RX_CAPACITY 512
#define MK_NETWORK_SOCKET_ADDRESS_MAX ((uint32_t)sizeof(struct sockaddr_storage))

struct mk_network_socket_state {
    int used;
    uint32_t owner_pid;
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    int bound;
    int listening;
    int connected;
    int peer_handle;
    int pending_accept_handle;
    uint32_t address_length;
    struct sockaddr_storage address;
    uint32_t rx_size;
    uint8_t rx_buffer[MK_NETWORK_SOCKET_RX_CAPACITY];
};

struct mk_network_service_state {
    struct mk_network_info info;
    struct mk_network_status status;
    struct mk_network_scan_entry_state scans[4];
    uint32_t scan_count;
    uint32_t ethernet_transition_polls;
    int ethernet_pending;
    struct mk_network_socket_state sockets[MK_NETWORK_SOCKET_MAX];
};

static struct mk_message g_last_network_request;
static struct mk_message g_last_network_reply;
static struct mk_network_service_state g_network_state;

static uint32_t mk_network_current_pid(void) {
    process_t *current = scheduler_current();

    return current != 0 ? (uint32_t)current->pid : 0u;
}

static int mk_network_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_NETWORK);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_network_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_network_reply_result(struct mk_message *reply, int value) {
    struct mk_network_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_network_reply_info(struct mk_message *reply) {
    return mk_message_set_payload(reply, &g_network_state.info, sizeof(g_network_state.info));
}

static int mk_network_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == 0 || dst_size == 0u) {
        return -1;
    }

    if (src == 0) {
        dst[0] = '\0';
        return 0;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
    return 0;
}

static int mk_network_string_eq(const char *a, const char *b) {
    size_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static int mk_network_socket_valid_domain(uint32_t domain) {
    return domain == AF_UNIX || domain == AF_LOCAL || domain == AF_INET;
}

static int mk_network_socket_valid_type(uint32_t type) {
    return type == SOCK_STREAM || type == SOCK_DGRAM;
}

static struct mk_network_socket_state *mk_network_socket_by_handle(int handle) {
    if (handle <= 0 || handle > MK_NETWORK_SOCKET_MAX) {
        return 0;
    }
    if (!g_network_state.sockets[handle - 1].used) {
        return 0;
    }
    return &g_network_state.sockets[handle - 1];
}

static struct mk_network_socket_state *mk_network_socket_by_handle_for_current_pid(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle(handle);

    if (socket_state == 0 || socket_state->owner_pid != mk_network_current_pid()) {
        return 0;
    }
    return socket_state;
}

static int mk_network_socket_address_match(const struct mk_network_socket_state *socket_state,
                                           const struct sockaddr *address,
                                           uint32_t address_length) {
    if (socket_state == 0 || address == 0) {
        return 0;
    }
    if (!socket_state->bound || socket_state->address_length != address_length) {
        return 0;
    }
    return memcmp(&socket_state->address, address, address_length) == 0;
}

static void mk_network_socket_disconnect_peer(struct mk_network_socket_state *socket_state) {
    struct mk_network_socket_state *peer;

    if (socket_state == 0 || !socket_state->connected || socket_state->peer_handle <= 0) {
        return;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer != 0 && peer->peer_handle == (int)(socket_state - g_network_state.sockets) + 1) {
        peer->connected = 0;
        peer->peer_handle = -1;
    }
    socket_state->connected = 0;
    socket_state->peer_handle = -1;
}

static struct mk_network_socket_state *mk_network_socket_allocate(uint32_t owner_pid,
                                                                  uint32_t domain,
                                                                  uint32_t type,
                                                                  uint32_t protocol,
                                                                  int *handle_out) {
    uint32_t index;

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *socket_state = &g_network_state.sockets[index];

        if (socket_state->used) {
            continue;
        }
        memset(socket_state, 0, sizeof(*socket_state));
        socket_state->used = 1;
        socket_state->owner_pid = owner_pid;
        socket_state->domain = domain;
        socket_state->type = type;
        socket_state->protocol = protocol;
        socket_state->peer_handle = -1;
        socket_state->pending_accept_handle = -1;
        if (handle_out != 0) {
            *handle_out = (int)index + 1;
        }
        return socket_state;
    }

    return 0;
}

static void mk_network_apply_ethernet_profile(void) {
    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 "em0");
    g_network_state.status.current_ssid[0] = '\0';
    (void)mk_network_copy_string(g_network_state.status.ip_address,
                                 sizeof(g_network_state.status.ip_address),
                                 "10.0.2.15");
    (void)mk_network_copy_string(g_network_state.status.gateway,
                                 sizeof(g_network_state.status.gateway),
                                 "10.0.2.2");
    (void)mk_network_copy_string(g_network_state.status.dns_server,
                                 sizeof(g_network_state.status.dns_server),
                                 "10.0.2.3");
}

static int mk_network_apply_ethernet_config(const struct mk_network_ethernet_config *config) {
    if (config == 0 || !mk_network_string_eq(config->if_name, "em0")) {
        return -1;
    }

    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 config->if_name);
    g_network_state.status.current_ssid[0] = '\0';
    (void)mk_network_copy_string(g_network_state.status.ip_address,
                                 sizeof(g_network_state.status.ip_address),
                                 config->ip_address);
    (void)mk_network_copy_string(g_network_state.status.gateway,
                                 sizeof(g_network_state.status.gateway),
                                 config->gateway);
    (void)mk_network_copy_string(g_network_state.status.dns_server,
                                 sizeof(g_network_state.status.dns_server),
                                 config->dns_server);
    return 0;
}

static void mk_network_begin_ethernet_connect(void) {
    g_network_state.ethernet_pending = 1;
    g_network_state.ethernet_transition_polls = 2u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTING;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 "em0");
    g_network_state.status.current_ssid[0] = '\0';
    g_network_state.status.ip_address[0] = '\0';
    g_network_state.status.gateway[0] = '\0';
    g_network_state.status.dns_server[0] = '\0';
}

static void mk_network_progress_state(void) {
    if (!g_network_state.ethernet_pending) {
        return;
    }
    if (g_network_state.ethernet_transition_polls > 0u) {
        g_network_state.ethernet_transition_polls -= 1u;
        if (g_network_state.ethernet_transition_polls > 0u) {
            return;
        }
    }
    mk_network_apply_ethernet_profile();
}

static int mk_network_fill_scan_info(uint32_t index, struct mk_network_scan_info *info) {
    if (info == 0 || index >= g_network_state.scan_count) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->index = index;
    info->signal_strength = g_network_state.scans[index].signal_strength;
    info->security = g_network_state.scans[index].security;
    info->connected = g_network_state.status.active_kind == MK_NETWORK_IF_WIFI &&
                      mk_network_string_eq(g_network_state.status.current_ssid,
                                           g_network_state.scans[index].ssid);
    (void)mk_network_copy_string(info->if_name, sizeof(info->if_name), "wlan0");
    (void)mk_network_copy_string(info->ssid, sizeof(info->ssid), g_network_state.scans[index].ssid);
    return 0;
}

static int mk_network_state_connect_wifi(const struct mk_network_connect_request *request) {
    uint32_t index;

    if (request == 0 || !mk_network_string_eq(request->if_name, "wlan0")) {
        return -1;
    }

    for (index = 0u; index < g_network_state.scan_count; ++index) {
        if (!mk_network_string_eq(g_network_state.scans[index].ssid, request->ssid)) {
            continue;
        }
        if (g_network_state.scans[index].security == MK_NETWORK_SECURITY_WPA_PSK &&
            request->psk[0] == '\0') {
            return -1;
        }

        g_network_state.ethernet_pending = 0;
        g_network_state.ethernet_transition_polls = 0u;
        g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
        g_network_state.status.active_kind = MK_NETWORK_IF_WIFI;
        g_network_state.status.wifi_signal = g_network_state.scans[index].signal_strength;
        g_network_state.status.visible_network_count = g_network_state.scan_count;
        (void)mk_network_copy_string(g_network_state.status.active_if,
                                     sizeof(g_network_state.status.active_if),
                                     "wlan0");
        (void)mk_network_copy_string(g_network_state.status.current_ssid,
                                     sizeof(g_network_state.status.current_ssid),
                                     g_network_state.scans[index].ssid);
        (void)mk_network_copy_string(g_network_state.status.ip_address,
                                     sizeof(g_network_state.status.ip_address),
                                     "192.168.1.24");
        (void)mk_network_copy_string(g_network_state.status.gateway,
                                     sizeof(g_network_state.status.gateway),
                                     "192.168.1.1");
        (void)mk_network_copy_string(g_network_state.status.dns_server,
                                     sizeof(g_network_state.status.dns_server),
                                     "1.1.1.1");
        return 0;
    }

    return -1;
}

static int mk_network_state_disconnect(const char *if_name) {
    if (if_name == 0) {
        return -1;
    }

    if (mk_network_string_eq(if_name, "wlan0")) {
        mk_network_begin_ethernet_connect();
        return 0;
    }
    if (mk_network_string_eq(if_name, "em0")) {
        g_network_state.ethernet_pending = 0;
        g_network_state.ethernet_transition_polls = 0u;
        memset(&g_network_state.status, 0, sizeof(g_network_state.status));
        g_network_state.status.visible_network_count = g_network_state.scan_count;
        return 0;
    }
    return -1;
}

static int mk_network_state_connect_ethernet(const char *if_name) {
    if (if_name == 0 || !mk_network_string_eq(if_name, "em0")) {
        return -1;
    }
    if (g_network_state.status.link_state == MK_NETWORK_LINK_CONNECTED &&
        g_network_state.status.active_kind == MK_NETWORK_IF_ETHERNET) {
        return 0;
    }
    mk_network_begin_ethernet_connect();
    return 0;
}

static int mk_network_local_handler(const struct mk_message *request,
                                    struct mk_message *reply,
                                    void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_network_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_NET_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_network_reply_info(reply) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_network_progress_state();
        if (mk_message_set_payload(reply, &g_network_state.status, sizeof(g_network_state.status)) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_SCAN:
        if (request->payload_size != sizeof(struct mk_network_scan_request)) {
            return -1;
        }
        {
            struct mk_network_scan_request scan_request;
            struct mk_network_scan_info info;

            memcpy(&scan_request, request->payload, sizeof(scan_request));
            if (!mk_network_string_eq(scan_request.if_name, "wlan0") ||
                mk_network_fill_scan_info(scan_request.index, &info) != 0) {
                if (mk_network_reply_result(reply, -1) != 0) {
                    return -1;
                }
                g_last_network_reply = *reply;
                return 0;
            }
            if (mk_message_set_payload(reply, &info, sizeof(info)) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
    case MK_MSG_NET_CONNECT_WIFI:
        if (request->payload_size != sizeof(struct mk_network_connect_request)) {
            return -1;
        }
        if (mk_network_state_connect_wifi((const struct mk_network_connect_request *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_DISCONNECT:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_disconnect(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONNECT_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_connect_ethernet(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONFIGURE_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_ethernet_config)) {
            return -1;
        }
        if (mk_network_apply_ethernet_config((const struct mk_network_ethernet_config *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_SOCKET:
        if (request->payload_size != sizeof(struct mk_network_socket_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_BIND:
    case MK_MSG_NET_CONNECT:
        if (request->payload_size != sizeof(struct mk_network_name_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SEND:
    case MK_MSG_NET_RECV:
        if (request->payload_size != sizeof(struct mk_network_io_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SETSOCKOPT:
    case MK_MSG_NET_GETSOCKOPT:
        if (request->payload_size != sizeof(struct mk_network_option_request)) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (mk_network_reply_result(reply, -1) != 0) {
        return -1;
    }
    g_last_network_reply = *reply;
    return 0;
}

void mk_network_service_init(void) {
    memset(&g_last_network_request, 0, sizeof(g_last_network_request));
    memset(&g_last_network_reply, 0, sizeof(g_last_network_reply));
    memset(&g_network_state, 0, sizeof(g_network_state));

    g_network_state.info.flags = MK_NETWORK_CAPS_BSD_SOCKET_ABI |
                                 MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING |
                                 MK_NETWORK_CAPS_CONTROL_PLANE |
                                 MK_NETWORK_CAPS_LOOPBACK_READY |
                                 MK_NETWORK_CAPS_ETHERNET_STATUS |
                                 MK_NETWORK_CAPS_WIFI_SCAN |
                                 MK_NETWORK_CAPS_WIFI_CONNECT |
                                 MK_NETWORK_CAPS_DNS_STATUS;
    g_network_state.info.supported_families = MK_NETWORK_FAMILY_UNIX |
                                              MK_NETWORK_FAMILY_INET |
                                              MK_NETWORK_FAMILY_INET6;
    g_network_state.info.supported_socket_types = MK_NETWORK_SOCKET_STREAM |
                                                  MK_NETWORK_SOCKET_DGRAM;
    g_network_state.info.max_sockets = MK_NETWORK_SOCKET_MAX;
    g_network_state.info.max_packet_size = 1500u;

    g_network_state.scan_count = 4u;
    (void)mk_network_copy_string(g_network_state.scans[0].ssid, sizeof(g_network_state.scans[0].ssid), "VibeNet");
    g_network_state.scans[0].signal_strength = 4u;
    g_network_state.scans[0].security = MK_NETWORK_SECURITY_WPA_PSK;
    (void)mk_network_copy_string(g_network_state.scans[1].ssid, sizeof(g_network_state.scans[1].ssid), "Laboratorio");
    g_network_state.scans[1].signal_strength = 3u;
    g_network_state.scans[1].security = MK_NETWORK_SECURITY_WPA_PSK;
    (void)mk_network_copy_string(g_network_state.scans[2].ssid, sizeof(g_network_state.scans[2].ssid), "Visitantes");
    g_network_state.scans[2].signal_strength = 2u;
    g_network_state.scans[2].security = MK_NETWORK_SECURITY_OPEN;
    (void)mk_network_copy_string(g_network_state.scans[3].ssid, sizeof(g_network_state.scans[3].ssid), "CompatLab");
    g_network_state.scans[3].signal_strength = 4u;
    g_network_state.scans[3].security = MK_NETWORK_SECURITY_WPA_PSK;
    mk_network_apply_ethernet_profile();

    (void)mk_service_launch_task(MK_SERVICE_NETWORK,
                                 "network",
                                 mk_network_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN);
}

int mk_network_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_NETWORK) != 0;
}

int mk_network_service_get_info(struct mk_network_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_get_status(struct mk_network_status *status) {
    struct mk_message request;
    struct mk_message reply;

    if (status == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GET_STATUS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*status)) {
        return -1;
    }
    memcpy(status, reply.payload, sizeof(*status));
    return 0;
}

int mk_network_service_get_scan(uint32_t index, struct mk_network_scan_info *info) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_scan_request scan_request;

    if (info == 0) {
        return -1;
    }

    memset(&scan_request, 0, sizeof(scan_request));
    scan_request.index = index;
    (void)mk_network_copy_string(scan_request.if_name, sizeof(scan_request.if_name), "wlan0");

    if (mk_network_prepare_request(&request, MK_MSG_NET_SCAN, &scan_request, sizeof(scan_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_network_result)) {
        struct mk_network_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_connect_wifi(const struct mk_network_connect_request *request) {
    struct mk_message message;
    struct mk_message reply;
    struct mk_network_result result;

    if (request == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&message, MK_MSG_NET_CONNECT_WIFI, request, sizeof(*request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &message, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_connect_ethernet(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request connect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }

    memset(&connect_request, 0, sizeof(connect_request));
    (void)mk_network_copy_string(connect_request.if_name,
                                 sizeof(connect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONNECT_ETHERNET,
                                   &connect_request,
                                   sizeof(connect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_configure_ethernet(const struct mk_network_ethernet_config *config) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_result result;

    if (config == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONFIGURE_ETHERNET,
                                   config,
                                   sizeof(*config)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_disconnect(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request disconnect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }

    memset(&disconnect_request, 0, sizeof(disconnect_request));
    (void)mk_network_copy_string(disconnect_request.if_name,
                                 sizeof(disconnect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_DISCONNECT,
                                   &disconnect_request,
                                   sizeof(disconnect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    int handle;

    if (!mk_network_socket_valid_domain(domain) || !mk_network_socket_valid_type(type)) {
        return -1;
    }
    return mk_network_socket_allocate(mk_network_current_pid(), domain, type, protocol, &handle) != 0
               ? handle
               : -1;
}

int mk_network_service_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected || socket_state->bound) {
        return -1;
    }
    if ((uint32_t)address->sa_family != socket_state->domain) {
        return -1;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *other = &g_network_state.sockets[index];

        if (!other->used || other == socket_state) {
            continue;
        }
        if (other->domain != socket_state->domain || other->type != socket_state->type) {
            continue;
        }
        if (mk_network_socket_address_match(other, address, address_length)) {
            return -1;
        }
    }

    memset(&socket_state->address, 0, sizeof(socket_state->address));
    memcpy(&socket_state->address, address, address_length);
    socket_state->address_length = address_length;
    socket_state->bound = 1;
    return 0;
}

int mk_network_service_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected) {
        return -1;
    }
    if ((uint32_t)address->sa_family != socket_state->domain) {
        return -1;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *peer = &g_network_state.sockets[index];
        int accepted_handle;
        struct mk_network_socket_state *accepted_socket;

        if (!peer->used || peer == socket_state) {
            continue;
        }
        if (peer->domain != socket_state->domain || peer->type != socket_state->type) {
            continue;
        }
        if (!mk_network_socket_address_match(peer, address, address_length)) {
            continue;
        }
        if (peer->listening) {
            if (peer->pending_accept_handle > 0) {
                return -1;
            }
            accepted_socket = mk_network_socket_allocate(peer->owner_pid,
                                                         peer->domain,
                                                         peer->type,
                                                         peer->protocol,
                                                         &accepted_handle);
            if (accepted_socket == 0) {
                return -1;
            }
            accepted_socket->bound = 1;
            accepted_socket->address_length = peer->address_length;
            memcpy(&accepted_socket->address, &peer->address, peer->address_length);
            accepted_socket->connected = 1;
            accepted_socket->peer_handle = handle;
            socket_state->connected = 1;
            socket_state->peer_handle = accepted_handle;
            peer->pending_accept_handle = accepted_handle;
            return 0;
        }
        if (peer->connected) {
            continue;
        }
        socket_state->connected = 1;
        socket_state->peer_handle = (int)index + 1;
        peer->connected = 1;
        peer->peer_handle = handle;
        return 0;
    }

    return -1;
}

int mk_network_service_send(int handle, const void *data, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    struct mk_network_socket_state *peer;

    if (data == 0 || size == 0u || size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || !socket_state->connected || socket_state->peer_handle <= 0) {
        return -1;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer == 0 || !peer->used) {
        return -1;
    }
    if (peer->rx_size + size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    memcpy(peer->rx_buffer + peer->rx_size, data, size);
    peer->rx_size += size;
    return (int)size;
}

int mk_network_service_recv(int handle, void *buffer, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    uint32_t read_size;

    if (buffer == 0 || size == 0u) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0) {
        return -1;
    }

    read_size = socket_state->rx_size;
    if (read_size > size) {
        read_size = size;
    }

    memcpy(buffer, socket_state->rx_buffer, read_size);
    if (read_size < socket_state->rx_size) {
        memmove(socket_state->rx_buffer,
                socket_state->rx_buffer + read_size,
                socket_state->rx_size - read_size);
    }
    socket_state->rx_size -= read_size;
    return (int)read_size;
}

int mk_network_service_close(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    struct mk_network_socket_state *pending;

    if (socket_state == 0) {
        return -1;
    }

    if (socket_state->pending_accept_handle > 0) {
        pending = mk_network_socket_by_handle(socket_state->pending_accept_handle);
        if (pending != 0) {
            mk_network_socket_disconnect_peer(pending);
            memset(pending, 0, sizeof(*pending));
        }
    }
    mk_network_socket_disconnect_peer(socket_state);
    memset(socket_state, 0, sizeof(*socket_state));
    return 0;
}

int mk_network_service_listen(int handle, int backlog) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);

    (void)backlog;
    if (socket_state == 0 || !socket_state->bound || socket_state->connected) {
        return -1;
    }
    socket_state->listening = 1;
    socket_state->pending_accept_handle = -1;
    return 0;
}

int mk_network_service_accept(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    int accepted_handle;

    if (socket_state == 0 || !socket_state->listening || socket_state->pending_accept_handle <= 0) {
        return -1;
    }
    accepted_handle = socket_state->pending_accept_handle;
    socket_state->pending_accept_handle = -1;
    return accepted_handle;
}

int mk_network_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_network_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_network_request;
    return 0;
}
