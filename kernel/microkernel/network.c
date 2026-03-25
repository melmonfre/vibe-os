#include <kernel/kernel_string.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

static struct mk_message g_last_network_request;
static struct mk_message g_last_network_reply;

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
    struct mk_network_info info;

    memset(&info, 0, sizeof(info));
    info.flags = MK_NETWORK_CAPS_QUERY_ONLY |
                 MK_NETWORK_CAPS_BSD_SOCKET_ABI |
                 MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING;
    info.supported_families = MK_NETWORK_FAMILY_UNIX |
                              MK_NETWORK_FAMILY_INET |
                              MK_NETWORK_FAMILY_INET6;
    info.supported_socket_types = MK_NETWORK_SOCKET_STREAM |
                                  MK_NETWORK_SOCKET_DGRAM;
    info.max_sockets = 0u;
    info.max_packet_size = 0u;
    return mk_message_set_payload(reply, &info, sizeof(info));
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
