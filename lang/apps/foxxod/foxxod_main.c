#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>
#include <userland/modules/include/syscalls.h>

#define FOXXOD_PID_PATH "/runtime/foxxod.pid"
#define FOXXOD_STATUS_PATH "/runtime/foxxod-status.txt"
#define FOXXOD_EVENTS_PATH "/runtime/foxxod-events.log"
#define FOXXOD_DEFERRED_QUEUE_MAX 16
#define FOXXOD_PENDING_MAX 16
#define FOXXOD_REPLY_TIMEOUT_TICKS 200u
#define FOXXOD_POLL_TIMEOUT_TICKS 8u

struct foxxod_pending_request {
    int used;
    uint32_t correlation_id;
    uint32_t requester_pid;
};

struct foxxod_state {
    uint32_t pid;
    uint32_t next_correlation_id;
    uint32_t received_messages;
    uint32_t handled_messages;
    uint32_t deferred_enqueued;
    uint32_t deferred_dropped;
    uint32_t forwarded_requests;
    uint32_t forwarded_replies;
    struct mk_async_message deferred_queue[FOXXOD_DEFERRED_QUEUE_MAX];
    int deferred_count;
    struct foxxod_pending_request pending[FOXXOD_PENDING_MAX];
};

static void foxxod_usage(void) {
    printf("usage: foxxod <command> [args]\n");
    printf("commands:\n");
    printf("  serve\n");
    printf("  status\n");
    printf("  ping [texto]\n");
    printf("  event <texto>\n");
    printf("  request <service-id> <texto>\n");
}

static int foxxod_copy_text(uint8_t *dst, uint32_t dst_size, const char *text) {
    uint32_t len = 0u;

    if (dst == 0 || dst_size == 0u) {
        return 0;
    }
    if (text == 0) {
        dst[0] = '\0';
        return 0;
    }
    while (text[len] != '\0' && len + 1u < dst_size) {
        dst[len] = (uint8_t)text[len];
        len += 1u;
    }
    dst[len] = '\0';
    return (int)len;
}

static int foxxod_parse_uint(const char *text, uint32_t *value_out) {
    uint32_t value = 0u;
    int digits = 0;

    if (text == 0 || value_out == 0) {
        return -1;
    }
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text - '0');
        digits += 1;
        ++text;
    }
    if (digits == 0) {
        return -1;
    }
    *value_out = value;
    return 0;
}

static uint32_t foxxod_copy_packet_text(struct mk_foxxo_packet *packet, const char *text) {
    int written;

    if (packet == 0) {
        return 0u;
    }
    written = foxxod_copy_text(packet->body, MK_FOXXO_PACKET_BODY_MAX, text);
    packet->body_size = written > 0 ? (uint32_t)written : 0u;
    return packet->body_size;
}

static void foxxod_packet_init(struct mk_foxxo_packet *packet, uint32_t opcode) {
    if (packet == 0) {
        return;
    }
    memset(packet, 0, sizeof(*packet));
    packet->abi_version = MK_FOXXO_PACKET_ABI_VERSION;
    packet->opcode = opcode;
}

static int foxxod_packet_validate(const struct mk_foxxo_packet *packet) {
    if (packet == 0) {
        return -1;
    }
    if (packet->abi_version != MK_FOXXO_PACKET_ABI_VERSION) {
        return -1;
    }
    if (packet->opcode == MK_FOXXO_OP_NONE) {
        return -1;
    }
    if (packet->body_size > MK_FOXXO_PACKET_BODY_MAX) {
        return -1;
    }
    return 0;
}

static int foxxod_read_pid_file(uint32_t *pid_out) {
    FILE *handle;
    char line[32];
    uint32_t value;

    if (pid_out == 0) {
        return -1;
    }
    handle = fopen(FOXXOD_PID_PATH, "r");
    if (handle == 0) {
        return -1;
    }
    line[0] = '\0';
    if (fgets(line, (int)sizeof(line), handle) == 0) {
        fclose(handle);
        return -1;
    }
    fclose(handle);
    if (foxxod_parse_uint(line, &value) != 0 || value == 0u) {
        return -1;
    }
    *pid_out = value;
    return 0;
}

static void foxxod_append_event_log(const char *prefix, const struct mk_foxxo_packet *packet) {
    FILE *handle;
    char body[MK_FOXXO_PACKET_BODY_MAX + 1];

    handle = fopen(FOXXOD_EVENTS_PATH, "a");
    if (handle == 0) {
        return;
    }
    memset(body, 0, sizeof(body));
    if (packet != 0 && packet->body_size > 0u) {
        memcpy(body, packet->body, packet->body_size);
        body[packet->body_size] = '\0';
    }
    fprintf(handle,
            "%s opcode=%u correlation=%u service=%u status=%u body=%s\n",
            prefix != 0 ? prefix : "foxxod",
            packet != 0 ? packet->opcode : 0u,
            packet != 0 ? packet->correlation_id : 0u,
            packet != 0 ? packet->route_service : 0u,
            packet != 0 ? packet->status : 0u,
            body);
    fclose(handle);
}

static void foxxod_write_status(const struct foxxod_state *state) {
    FILE *handle;

    if (state == 0) {
        return;
    }
    handle = fopen(FOXXOD_STATUS_PATH, "w");
    if (handle == 0) {
        return;
    }
    fprintf(handle, "pid=%u\n", state->pid);
    fprintf(handle, "received=%u\n", state->received_messages);
    fprintf(handle, "handled=%u\n", state->handled_messages);
    fprintf(handle, "deferred_queue=%d\n", state->deferred_count);
    fprintf(handle, "deferred_enqueued=%u\n", state->deferred_enqueued);
    fprintf(handle, "deferred_dropped=%u\n", state->deferred_dropped);
    fprintf(handle, "forwarded_requests=%u\n", state->forwarded_requests);
    fprintf(handle, "forwarded_replies=%u\n", state->forwarded_replies);
    fclose(handle);
}

static int foxxod_send_packet(uint32_t target_pid,
                              uint32_t message_type,
                              uint32_t flags,
                              const struct mk_foxxo_packet *packet) {
    struct mk_async_message message;

    if (target_pid == 0u || packet == 0) {
        return -1;
    }
    memset(&message, 0, sizeof(message));
    message.abi_version = MK_ASYNC_MESSAGE_ABI_VERSION;
    message.flags = flags | MK_ASYNC_MESSAGE_TO_PID;
    message.type = message_type;
    message.target_pid = target_pid;
    message.payload_size = sizeof(*packet);
    memcpy(message.payload, packet, sizeof(*packet));
    return sys_message_post(&message);
}

static int foxxod_send_service_packet(uint32_t service,
                                      uint32_t message_type,
                                      uint32_t flags,
                                      const struct mk_foxxo_packet *packet) {
    struct mk_async_message message;

    if (service == 0u || packet == 0) {
        return -1;
    }
    memset(&message, 0, sizeof(message));
    message.abi_version = MK_ASYNC_MESSAGE_ABI_VERSION;
    message.flags = flags | MK_ASYNC_MESSAGE_TO_SERVICE | MK_ASYNC_MESSAGE_REQUIRE_TARGET;
    message.type = message_type;
    message.target_service = service;
    message.payload_size = sizeof(*packet);
    memcpy(message.payload, packet, sizeof(*packet));
    return sys_message_post(&message);
}

static int foxxod_send_error(uint32_t target_pid, uint32_t correlation_id, const char *text) {
    struct mk_foxxo_packet packet;

    foxxod_packet_init(&packet, MK_FOXXO_OP_ERROR);
    packet.correlation_id = correlation_id;
    packet.status = 1u;
    foxxod_copy_packet_text(&packet, text);
    return foxxod_send_packet(target_pid, MK_FOXXO_MESSAGE_ERROR, 0u, &packet);
}

static int foxxod_pending_store(struct foxxod_state *state,
                                uint32_t correlation_id,
                                uint32_t requester_pid) {
    int i;

    if (state == 0 || correlation_id == 0u || requester_pid == 0u) {
        return -1;
    }
    for (i = 0; i < FOXXOD_PENDING_MAX; ++i) {
        if (!state->pending[i].used) {
            state->pending[i].used = 1;
            state->pending[i].correlation_id = correlation_id;
            state->pending[i].requester_pid = requester_pid;
            return 0;
        }
    }
    return -1;
}

static int foxxod_pending_take(struct foxxod_state *state,
                               uint32_t correlation_id,
                               uint32_t *requester_pid_out) {
    int i;

    if (state == 0 || requester_pid_out == 0) {
        return -1;
    }
    for (i = 0; i < FOXXOD_PENDING_MAX; ++i) {
        if (state->pending[i].used &&
            state->pending[i].correlation_id == correlation_id) {
            *requester_pid_out = state->pending[i].requester_pid;
            memset(&state->pending[i], 0, sizeof(state->pending[i]));
            return 0;
        }
    }
    return -1;
}

static int foxxod_enqueue_deferred(struct foxxod_state *state,
                                   const struct mk_async_message *message) {
    if (state == 0 || message == 0) {
        return -1;
    }
    if (state->deferred_count >= FOXXOD_DEFERRED_QUEUE_MAX) {
        state->deferred_dropped += 1u;
        return -1;
    }
    state->deferred_queue[state->deferred_count] = *message;
    state->deferred_count += 1;
    state->deferred_enqueued += 1u;
    return 0;
}

static int foxxod_dequeue_deferred(struct foxxod_state *state,
                                   struct mk_async_message *message_out) {
    int i;

    if (state == 0 || message_out == 0 || state->deferred_count <= 0) {
        return -1;
    }
    *message_out = state->deferred_queue[0];
    for (i = 1; i < state->deferred_count; ++i) {
        state->deferred_queue[i - 1] = state->deferred_queue[i];
    }
    state->deferred_count -= 1;
    return 0;
}

static int foxxod_handle_packet(struct foxxod_state *state,
                                const struct mk_async_message *message,
                                const struct mk_foxxo_packet *packet) {
    struct mk_foxxo_packet reply;
    uint32_t requester_pid;
    int rc;

    if (state == 0 || message == 0 || packet == 0) {
        return -1;
    }
    state->handled_messages += 1u;
    switch (packet->opcode) {
    case MK_FOXXO_OP_PING:
        foxxod_packet_init(&reply, MK_FOXXO_OP_ACK);
        reply.correlation_id = packet->correlation_id;
        reply.status = 0u;
        reply.body_size = packet->body_size;
        if (packet->body_size > 0u) {
            memcpy(reply.body, packet->body, packet->body_size);
        }
        return foxxod_send_packet(message->source_pid, MK_FOXXO_MESSAGE_REPLY, 0u, &reply);
    case MK_FOXXO_OP_EVENT:
        foxxod_append_event_log("foxxod-event", packet);
        foxxod_packet_init(&reply, MK_FOXXO_OP_ACK);
        reply.correlation_id = packet->correlation_id;
        foxxod_copy_packet_text(&reply, "event accepted");
        return foxxod_send_packet(message->source_pid, MK_FOXXO_MESSAGE_REPLY, 0u, &reply);
    case MK_FOXXO_OP_SERVICE_REQUEST:
        if (packet->route_service == 0u) {
            return foxxod_send_error(message->source_pid,
                                     packet->correlation_id,
                                     "missing route_service");
        }
        if (foxxod_pending_store(state, packet->correlation_id, message->source_pid) != 0) {
            return foxxod_send_error(message->source_pid,
                                     packet->correlation_id,
                                     "pending table full");
        }
        rc = foxxod_send_service_packet(packet->route_service,
                                        MK_FOXXO_MESSAGE_REQUEST,
                                        (message->flags & MK_ASYNC_MESSAGE_DEFERRED),
                                        packet);
        if (rc != 0) {
            (void)foxxod_pending_take(state, packet->correlation_id, &requester_pid);
            return foxxod_send_error(message->source_pid,
                                     packet->correlation_id,
                                     "service delivery failed");
        }
        state->forwarded_requests += 1u;
        return 0;
    case MK_FOXXO_OP_SERVICE_REPLY:
    case MK_FOXXO_OP_ERROR:
        if (foxxod_pending_take(state, packet->correlation_id, &requester_pid) != 0) {
            return 0;
        }
        state->forwarded_replies += 1u;
        return foxxod_send_packet(requester_pid,
                                  packet->opcode == MK_FOXXO_OP_ERROR ? MK_FOXXO_MESSAGE_ERROR
                                                                       : MK_FOXXO_MESSAGE_REPLY,
                                  0u,
                                  packet);
    default:
        return foxxod_send_error(message->source_pid,
                                 packet->correlation_id,
                                 "unsupported opcode");
    }
}

static int foxxod_process_message(struct foxxod_state *state,
                                  const struct mk_async_message *message) {
    struct mk_foxxo_packet packet;

    if (state == 0 || message == 0) {
        return -1;
    }
    state->received_messages += 1u;
    if (message->payload_size < sizeof(packet)) {
        return foxxod_send_error(message->source_pid, 0u, "payload too small");
    }
    memcpy(&packet, message->payload, sizeof(packet));
    if (foxxod_packet_validate(&packet) != 0) {
        return foxxod_send_error(message->source_pid, 0u, "invalid foxxo packet");
    }
    return foxxod_handle_packet(state, message, &packet);
}

static int foxxod_command_serve(void) {
    struct foxxod_state state;
    struct mk_async_message message;
    FILE *handle;

    memset(&state, 0, sizeof(state));
    state.pid = (uint32_t)sys_getpid();
    state.next_correlation_id = 1u;
    handle = fopen(FOXXOD_PID_PATH, "w");
    if (handle != 0) {
        fprintf(handle, "%u\n", state.pid);
        fclose(handle);
    }
    foxxod_write_status(&state);
    printf("foxxod: listening pid=%u\n", state.pid);
    for (;;) {
        int rc = sys_message_receive(&message, FOXXOD_POLL_TIMEOUT_TICKS);
        if (rc == 0) {
            if ((message.flags & MK_ASYNC_MESSAGE_DEFERRED) != 0u) {
                if (foxxod_enqueue_deferred(&state, &message) != 0) {
                    (void)foxxod_send_error(message.source_pid, 0u, "deferred queue full");
                }
            } else {
                (void)foxxod_process_message(&state, &message);
            }
            foxxod_write_status(&state);
        }
        if (foxxod_dequeue_deferred(&state, &message) == 0) {
            (void)foxxod_process_message(&state, &message);
            foxxod_write_status(&state);
        }
        vibe_app_yield();
    }
    return 0;
}

static int foxxod_request_broker(uint32_t message_type,
                                 uint32_t opcode,
                                 uint32_t route_service,
                                 const char *text,
                                 int wait_reply) {
    uint32_t broker_pid;
    struct mk_foxxo_packet packet;
    struct mk_async_message reply;
    int rc;

    if (foxxod_read_pid_file(&broker_pid) != 0) {
        printf("foxxod: broker not running\n");
        return 1;
    }
    foxxod_packet_init(&packet, opcode);
    packet.correlation_id = (uint32_t)sys_ticks();
    if (packet.correlation_id == 0u) {
        packet.correlation_id = 1u;
    }
    packet.route_service = route_service;
    foxxod_copy_packet_text(&packet, text);
    rc = foxxod_send_packet(broker_pid, message_type, 0u, &packet);
    if (rc != 0) {
        printf("foxxod: send failed\n");
        return 1;
    }
    if (!wait_reply) {
        printf("foxxod: sent\n");
        return 0;
    }
    rc = sys_message_receive(&reply, FOXXOD_REPLY_TIMEOUT_TICKS);
    if (rc != 0) {
        printf("foxxod: timeout waiting reply\n");
        return 1;
    }
    if (reply.payload_size >= sizeof(packet)) {
        memcpy(&packet, reply.payload, sizeof(packet));
        if (foxxod_packet_validate(&packet) == 0) {
            printf("foxxod: reply opcode=%u status=%u body=%s\n",
                   packet.opcode,
                   packet.status,
                   packet.body_size > 0u ? (const char *)packet.body : "");
            return packet.opcode == MK_FOXXO_OP_ERROR ? 1 : 0;
        }
    }
    printf("foxxod: invalid reply\n");
    return 1;
}

static int foxxod_command_status(void) {
    FILE *handle;
    char line[160];

    handle = fopen(FOXXOD_STATUS_PATH, "r");
    if (handle == 0) {
        printf("foxxod: no status exported\n");
        return 1;
    }
    while (fgets(line, (int)sizeof(line), handle) != 0) {
        fputs(line, stdout);
    }
    fclose(handle);
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    if (argc <= 1 || strcmp(argv[1], "serve") == 0) {
        return foxxod_command_serve();
    }
    if (strcmp(argv[1], "status") == 0) {
        return foxxod_command_status();
    }
    if (strcmp(argv[1], "ping") == 0) {
        const char *text = argc > 2 ? argv[2] : "pong-check";
        return foxxod_request_broker(MK_FOXXO_MESSAGE_REQUEST,
                                     MK_FOXXO_OP_PING,
                                     0u,
                                     text,
                                     1);
    }
    if (strcmp(argv[1], "event") == 0) {
        if (argc <= 2) {
            foxxod_usage();
            return 1;
        }
        return foxxod_request_broker(MK_FOXXO_MESSAGE_EVENT,
                                     MK_FOXXO_OP_EVENT,
                                     0u,
                                     argv[2],
                                     1);
    }
    if (strcmp(argv[1], "request") == 0) {
        uint32_t service_id;

        if (argc <= 3) {
            foxxod_usage();
            return 1;
        }
        if (foxxod_parse_uint(argv[2], &service_id) != 0 || service_id == 0u) {
            printf("foxxod: invalid service id\n");
            return 1;
        }
        return foxxod_request_broker(MK_FOXXO_MESSAGE_REQUEST,
                                     MK_FOXXO_OP_SERVICE_REQUEST,
                                     (uint32_t)service_id,
                                     argv[3],
                                     1);
    }
    foxxod_usage();
    return 1;
}
