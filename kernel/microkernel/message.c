#include <kernel/microkernel/message.h>
#include <kernel/kernel_string.h>

void mk_message_init(struct mk_message *message, uint32_t type) {
    if (message == 0) {
        return;
    }

    memset(message, 0, sizeof(*message));
    message->abi_version = MK_MESSAGE_ABI_VERSION;
    message->type = type;
}

int mk_message_validate(const struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (message->abi_version != MK_MESSAGE_ABI_VERSION) {
        return -1;
    }
    if (message->type == MK_MSG_NONE) {
        return -1;
    }
    if (message->payload_size > MK_MESSAGE_PAYLOAD_MAX) {
        return -1;
    }
    return 0;
}

int mk_message_set_payload(struct mk_message *message, const void *payload, size_t payload_size) {
    if (message == 0) {
        return -1;
    }
    if (payload_size > MK_MESSAGE_PAYLOAD_MAX) {
        return -1;
    }
    if (payload_size != 0u && payload == 0) {
        return -1;
    }

    if (payload_size != 0u) {
        memcpy(message->payload, payload, payload_size);
    }
    message->payload_size = (uint32_t)payload_size;
    return 0;
}
