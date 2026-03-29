#include <include/userland_api.h>
#include <kernel/drivers/input/input.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/input.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

static uint32_t mk_input_current_pid(void) {
    return scheduler_current_pid();
}

static int g_input_service_transport_degraded = 0;

static int mk_input_should_use_local_fallback(void) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_INPUT);

    if (service == 0) {
        return 1;
    }
    if (service->process == 0 || service->pid <= 0) {
        return 1;
    }

    if (g_input_service_transport_degraded ||
        service->transport_degraded != 0u ||
        !mk_service_is_online(MK_SERVICE_INPUT)) {
        if (mk_service_ensure(MK_SERVICE_INPUT) != 0) {
            return 1;
        }
        service = mk_service_find_by_type(MK_SERVICE_INPUT);
        if (service == 0 ||
            service->process == 0 ||
            service->pid <= 0 ||
            service->transport_degraded != 0u ||
            !mk_service_is_online(MK_SERVICE_INPUT)) {
            return 1;
        }
        g_input_service_transport_degraded = 0;
    }

    return 0;
}

static int mk_input_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_INPUT);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static int mk_input_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;
    process_t *current;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_INPUT);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    current = scheduler_current();
    message->source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_input_reply_result(struct mk_message *reply, int value) {
    struct mk_input_result result;

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_input_decode_result(const struct mk_message *reply) {
    struct mk_input_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_input_validate_string_transfer(uint32_t transfer_id,
                                             uint32_t length,
                                             char **text_out) {
    char *text;

    if (text_out == 0 || length == 0u) {
        return -1;
    }
    text = (char *)mk_transfer_data_read(transfer_id);
    if (text == 0 || mk_transfer_size(transfer_id) < length + 1u) {
        return -1;
    }
    if (text[length] != '\0') {
        return -1;
    }
    *text_out = text;
    return 0;
}

static int mk_input_reply_mouse(struct mk_message *reply,
                                int value,
                                const struct mouse_state *state) {
    struct mk_input_mouse_reply payload;

    payload.value = value;
    memset(&payload.state, 0, sizeof(payload.state));
    if (state != 0) {
        payload.state = *state;
    }
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_input_decode_mouse(const struct mk_message *reply,
                                 struct mouse_state *state) {
    struct mk_input_mouse_reply payload;

    if (reply == 0 || state == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }

    memcpy(&payload, reply->payload, sizeof(payload));
    *state = payload.state;
    return payload.value;
}

static int mk_input_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_INPUT_MOUSE_POLL: {
        struct mouse_state state;
        int x;
        int y;
        int dx;
        int dy;
        uint8_t buttons;

        if (request->payload_size != 0u) {
            return -1;
        }
        if (!kernel_mouse_has_data()) {
            return mk_input_reply_mouse(reply, 0, 0);
        }

        kernel_mouse_read(&x, &y, &dx, &dy, &buttons);
        state.x = x;
        state.y = y;
        state.dx = dx;
        state.dy = dy;
        state.buttons = buttons;
        return mk_input_reply_mouse(reply, 1, &state);
    }
    case MK_MSG_INPUT_KEY_READ:
        if (request->payload_size != 0u) {
            return -1;
        }
        return mk_input_reply_result(reply, kernel_keyboard_read());
    case MK_MSG_INPUT_SET_LAYOUT: {
        const struct mk_input_layout_set_request *payload;
        char *name;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_input_layout_set_request *)request->payload;
        if (mk_input_validate_string_transfer(payload->transfer_id, payload->name_length, &name) != 0) {
            return -1;
        }
        return mk_input_reply_result(reply, kernel_keyboard_set_layout(name));
    }
    case MK_MSG_INPUT_GET_LAYOUT: {
        const struct mk_input_transfer_request *payload;
        const char *name;
        uint32_t length;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_input_transfer_request *)request->payload;
        name = kernel_keyboard_get_layout();
        length = (uint32_t)strlen(name);
        if (payload->buffer_size <= length || mk_transfer_size(payload->transfer_id) < payload->buffer_size) {
            return mk_input_reply_result(reply, 0);
        }
        if (mk_transfer_copy_from(payload->transfer_id, name, length + 1u) != 0) {
            return -1;
        }
        return mk_input_reply_result(reply, (int)length);
    }
    case MK_MSG_INPUT_GET_AVAILABLE_LAYOUTS: {
        const struct mk_input_transfer_request *payload;
        char *buffer;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_input_transfer_request *)request->payload;
        buffer = (char *)mk_transfer_data_write(payload->transfer_id);
        if (buffer == 0 || mk_transfer_size(payload->transfer_id) < payload->buffer_size) {
            return -1;
        }
        kernel_keyboard_get_available_layouts(buffer, (int)payload->buffer_size);
        return mk_input_reply_result(reply, 0);
    }
    default:
        return -1;
    }
}

void mk_input_service_init(void) {
    g_input_service_transport_degraded = 0;
    (void)mk_service_launch_task(MK_SERVICE_INPUT,
                                 "input",
                                 mk_input_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN |
                                 MK_LAUNCH_FLAG_CRITICAL);
}

int mk_input_service_poll_mouse(struct mouse_state *state) {
    struct mk_message request;
    struct mk_message reply;
    int rc;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;

    if (state == 0) {
        return 0;
    }
    if (mk_input_should_use_local_fallback() ||
        mk_input_prepare_request(&request, MK_MSG_INPUT_MOUSE_POLL, 0, 0u) != 0) {
        if (!kernel_mouse_has_data()) {
            memset(state, 0, sizeof(*state));
            return 0;
        }
        kernel_mouse_read(&x, &y, &dx, &dy, &buttons);
        state->x = x;
        state->y = y;
        state->dx = dx;
        state->dy = dy;
        state->buttons = buttons;
        return 1;
    }
    rc = mk_service_request(MK_SERVICE_INPUT, &request, &reply);
    if (rc != 0) {
        g_input_service_transport_degraded = 1;
        if (!kernel_mouse_has_data()) {
            memset(state, 0, sizeof(*state));
            return 0;
        }
        kernel_mouse_read(&x, &y, &dx, &dy, &buttons);
        state->x = x;
        state->y = y;
        state->dx = dx;
        state->dy = dy;
        state->buttons = buttons;
        return 1;
    }
    g_input_service_transport_degraded = 0;
    return mk_input_decode_mouse(&reply, state);
}

int mk_input_service_read_key(void) {
    struct mk_message request;
    struct mk_message reply;
    int rc;

    if (mk_input_should_use_local_fallback() ||
        mk_input_prepare_request(&request, MK_MSG_INPUT_KEY_READ, 0, 0u) != 0) {
        return kernel_keyboard_read();
    }
    rc = mk_service_request(MK_SERVICE_INPUT, &request, &reply);
    if (rc != 0) {
        g_input_service_transport_degraded = 1;
        return kernel_keyboard_read();
    }
    g_input_service_transport_degraded = 0;
    return mk_input_decode_result(&reply);
}

int mk_input_service_set_layout(const char *name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_input_layout_set_request payload;
    uint32_t transfer_id;
    uint32_t length;

    if (name == 0) {
        return -1;
    }
    length = (uint32_t)strlen(name);
    if (mk_transfer_create(mk_input_current_pid(), length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_input_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, name, length + 1u) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.name_length = length;
    payload.transfer_id = transfer_id;
    if (mk_input_should_use_local_fallback() ||
        mk_input_prepare_request(&request, MK_MSG_INPUT_SET_LAYOUT, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return kernel_keyboard_set_layout(name);
    }
    if (mk_service_request(MK_SERVICE_INPUT, &request, &reply) != 0) {
        g_input_service_transport_degraded = 1;
        (void)mk_transfer_destroy(transfer_id);
        return kernel_keyboard_set_layout(name);
    }
    g_input_service_transport_degraded = 0;
    (void)mk_transfer_destroy(transfer_id);
    return mk_input_decode_result(&reply);
}

static int mk_input_service_fill_buffer_request(uint32_t type, char *buffer, int size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_input_transfer_request payload;
    uint32_t transfer_id;
    int rc;

    if (buffer == 0 || size <= 0) {
        return -1;
    }
    if (mk_transfer_create(mk_input_current_pid(), (uint32_t)size, &transfer_id) != 0) {
        return -1;
    }
    if (mk_input_share_transfer(transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.buffer_size = (uint32_t)size;
    payload.transfer_id = transfer_id;
    if (mk_input_should_use_local_fallback() ||
        mk_input_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        if (type == MK_MSG_INPUT_GET_LAYOUT) {
            const char *name = kernel_keyboard_get_layout();
            int length = (int)strlen(name);

            if (size <= length) {
                return 0;
            }
            memcpy(buffer, name, (size_t)length + 1u);
            return length;
        }
        kernel_keyboard_get_available_layouts(buffer, size);
        return 0;
    }
    if (mk_service_request(MK_SERVICE_INPUT, &request, &reply) != 0) {
        g_input_service_transport_degraded = 1;
        (void)mk_transfer_destroy(transfer_id);
        if (type == MK_MSG_INPUT_GET_LAYOUT) {
            const char *name = kernel_keyboard_get_layout();
            int length = (int)strlen(name);

            if (size <= length) {
                return 0;
            }
            memcpy(buffer, name, (size_t)length + 1u);
            return length;
        }
        kernel_keyboard_get_available_layouts(buffer, size);
        return 0;
    }

    g_input_service_transport_degraded = 0;
    rc = mk_input_decode_result(&reply);
    if (rc >= 0 && mk_transfer_copy_to(transfer_id, buffer, (uint32_t)size) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

int mk_input_service_get_layout(char *buffer, int size) {
    return mk_input_service_fill_buffer_request(MK_MSG_INPUT_GET_LAYOUT, buffer, size);
}

int mk_input_service_get_available_layouts(char *buffer, int size) {
    return mk_input_service_fill_buffer_request(MK_MSG_INPUT_GET_AVAILABLE_LAYOUTS, buffer, size);
}
