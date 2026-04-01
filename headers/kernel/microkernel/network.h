#ifndef KERNEL_MICROKERNEL_NETWORK_H
#define KERNEL_MICROKERNEL_NETWORK_H

#include <include/userland_api.h>
#include <stdint.h>
#include <sys/socket.h>

struct mk_message;
struct process;

enum mk_network_capability_flags {
    MK_NETWORK_CAPS_QUERY_ONLY = 1u << 0,
    MK_NETWORK_CAPS_LOOPBACK_READY = 1u << 1,
    MK_NETWORK_CAPS_BSD_SOCKET_ABI = 1u << 2,
    MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING = 1u << 3,
    MK_NETWORK_CAPS_CONTROL_PLANE = 1u << 4,
    MK_NETWORK_CAPS_ETHERNET_STATUS = 1u << 5,
    MK_NETWORK_CAPS_WIFI_SCAN = 1u << 6,
    MK_NETWORK_CAPS_WIFI_CONNECT = 1u << 7,
    MK_NETWORK_CAPS_DNS_STATUS = 1u << 8,
    MK_NETWORK_CAPS_EVENT_STREAM_READY = 1u << 9,
    MK_NETWORK_CAPS_BACKEND_ACTIVITY_EVENTS = 1u << 10,
    MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST = 1u << 11,
    MK_NETWORK_CAPS_NETMGR_POLICY_ONLY = 1u << 12,
    MK_NETWORK_CAPS_LOCAL_FALLBACK_RESCUE_ONLY = 1u << 13,
    MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR = 1u << 14,
    MK_NETWORK_CAPS_REAL_PACKET_DATAPATH = 1u << 15,
    MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY = 1u << 16
};

enum mk_network_family_bits {
    MK_NETWORK_FAMILY_UNIX = 1u << 0,
    MK_NETWORK_FAMILY_INET = 1u << 1,
    MK_NETWORK_FAMILY_INET6 = 1u << 2
};

enum mk_network_socket_type_bits {
    MK_NETWORK_SOCKET_STREAM = 1u << 0,
    MK_NETWORK_SOCKET_DGRAM = 1u << 1,
    MK_NETWORK_SOCKET_RAW = 1u << 2
};

struct mk_network_socket_request {
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
};

struct mk_network_name_request {
    int32_t handle;
    uint32_t address_length;
    uint32_t transfer_id;
};

struct mk_network_io_request {
    int32_t handle;
    int32_t flags;
    uint32_t size;
    uint32_t transfer_id;
    uint32_t address_length;
    uint32_t address_transfer_id;
};

struct mk_network_option_request {
    int32_t handle;
    int32_t level;
    int32_t option_name;
    uint32_t value_size;
    uint32_t transfer_id;
};

struct mk_network_result {
    int32_t value;
};

#define MK_NETWORK_IF_NAME_MAX 16
#define MK_NETWORK_ADDR_MAX 40
#define MK_NETWORK_SSID_MAX 32
#define MK_NETWORK_PSK_MAX 64

enum mk_network_link_state {
    MK_NETWORK_LINK_DISCONNECTED = 0,
    MK_NETWORK_LINK_CONNECTING = 1,
    MK_NETWORK_LINK_CONNECTED = 2
};

enum mk_network_interface_kind {
    MK_NETWORK_IF_LOOPBACK = 1,
    MK_NETWORK_IF_ETHERNET = 2,
    MK_NETWORK_IF_WIFI = 3
};

enum mk_network_security_kind {
    MK_NETWORK_SECURITY_OPEN = 0,
    MK_NETWORK_SECURITY_WPA_PSK = 1
};

struct mk_network_info {
    uint32_t flags;
    uint32_t supported_families;
    uint32_t supported_socket_types;
    uint32_t max_sockets;
    uint32_t max_packet_size;
};

struct mk_network_status {
    uint32_t link_state;
    uint32_t active_kind;
    uint32_t wifi_signal;
    uint32_t visible_network_count;
    char active_if[MK_NETWORK_IF_NAME_MAX];
    char current_ssid[MK_NETWORK_SSID_MAX + 1];
    char ip_address[MK_NETWORK_ADDR_MAX];
    char gateway[MK_NETWORK_ADDR_MAX];
    char dns_server[MK_NETWORK_ADDR_MAX];
};

struct mk_network_scan_request {
    uint32_t index;
    char if_name[MK_NETWORK_IF_NAME_MAX];
};

struct mk_network_scan_info {
    uint32_t index;
    uint32_t signal_strength;
    uint32_t security;
    uint32_t connected;
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ssid[MK_NETWORK_SSID_MAX + 1];
};

struct mk_network_connect_request {
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

struct mk_network_disconnect_request {
    char if_name[MK_NETWORK_IF_NAME_MAX];
};

struct mk_network_ethernet_config {
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ip_address[MK_NETWORK_ADDR_MAX];
    char gateway[MK_NETWORK_ADDR_MAX];
    char dns_server[MK_NETWORK_ADDR_MAX];
};

void mk_network_service_init(void);
int mk_network_service_ready(void);
int mk_network_service_get_info(struct mk_network_info *info);
int mk_network_service_get_status(struct mk_network_status *status);
int mk_network_service_get_scan(uint32_t index, struct mk_network_scan_info *info);
int mk_network_service_connect_wifi(const struct mk_network_connect_request *request);
int mk_network_service_connect_ethernet(const char *if_name);
int mk_network_service_configure_ethernet(const struct mk_network_ethernet_config *config);
int mk_network_service_disconnect(const char *if_name);
int mk_network_service_socket(uint32_t domain, uint32_t type, uint32_t protocol);
int mk_network_service_bind(int handle, const struct sockaddr *address, uint32_t address_length);
int mk_network_service_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length);
int mk_network_service_send(int handle, const void *data, uint32_t size);
int mk_network_service_recv(int handle, void *buffer, uint32_t size);
int mk_network_service_close(int handle);
int mk_network_service_listen(int handle, int backlog);
int mk_network_service_accept(int handle);
int mk_network_service_subscribe(struct process *subscriber);
int mk_network_service_event_receive(struct process *subscriber,
                                     struct mk_network_event *event,
                                     uint32_t timeout_ticks);
int mk_network_service_last_request(struct mk_message *message);

#endif
