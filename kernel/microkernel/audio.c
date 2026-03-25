#include <kernel/kernel_string.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

struct mk_audio_service_state {
    struct mk_audio_info info;
};

static struct mk_message g_last_audio_request;
static struct mk_message g_last_audio_reply;
static struct mk_audio_service_state g_audio_state;

static uint32_t mk_audio_current_pid(void) {
    process_t *current = scheduler_current();

    return current != 0 ? (uint32_t)current->pid : 0u;
}

static int mk_audio_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_AUDIO);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_audio_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_audio_reply_result(struct mk_message *reply, int value) {
    struct mk_audio_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_audio_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_audio_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_AUDIO_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_message_set_payload(reply, &g_audio_state.info, sizeof(g_audio_state.info)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.status,
                                   sizeof(g_audio_state.info.status)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_PARAMS:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.parameters,
                                   sizeof(g_audio_state.info.parameters)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_SET_PARAMS:
        if (request->payload_size != sizeof(g_audio_state.info.parameters)) {
            return -1;
        }
        memcpy(&g_audio_state.info.parameters,
               request->payload,
               sizeof(g_audio_state.info.parameters));
        break;
    case MK_MSG_AUDIO_START:
        if (request->payload_size != 0u) {
            return -1;
        }
        g_audio_state.info.status.pause = 0;
        g_audio_state.info.status.active = 1;
        break;
    case MK_MSG_AUDIO_STOP:
        if (request->payload_size != 0u) {
            return -1;
        }
        g_audio_state.info.status.pause = 0;
        g_audio_state.info.status.active = 0;
        break;
    case MK_MSG_AUDIO_WRITE:
    case MK_MSG_AUDIO_READ:
        if (request->payload_size != sizeof(struct mk_audio_transfer_request)) {
            return -1;
        }
        if (mk_audio_reply_result(reply, -1) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_MIXER_READ:
    case MK_MSG_AUDIO_MIXER_WRITE:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        if (mk_audio_reply_result(reply, -1) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    default:
        return -1;
    }

    if (mk_audio_reply_result(reply, 0) != 0) {
        return -1;
    }
    g_last_audio_reply = *reply;
    return 0;
}

void mk_audio_service_init(void) {
    memset(&g_last_audio_request, 0, sizeof(g_last_audio_request));
    memset(&g_last_audio_reply, 0, sizeof(g_last_audio_reply));
    memset(&g_audio_state, 0, sizeof(g_audio_state));

    g_audio_state.info.flags = MK_AUDIO_CAPS_QUERY_ONLY |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    strncpy(g_audio_state.info.device.name, "stub", MAX_AUDIO_DEV_LEN - 1u);
    strncpy(g_audio_state.info.device.version, "0", MAX_AUDIO_DEV_LEN - 1u);
    strncpy(g_audio_state.info.device.config, "service", MAX_AUDIO_DEV_LEN - 1u);
    g_audio_state.info.status.mode = AUMODE_PLAY;
    AUDIO_INITPAR(&g_audio_state.info.parameters);
    g_audio_state.info.parameters.rate = 48000u;
    g_audio_state.info.parameters.bits = 16u;
    g_audio_state.info.parameters.bps = 2u;
    g_audio_state.info.parameters.sig = 1u;
    g_audio_state.info.parameters.le = 1u;
    g_audio_state.info.parameters.pchan = 2u;
    g_audio_state.info.parameters.rchan = 2u;
    g_audio_state.info.parameters.nblks = 4u;
    g_audio_state.info.parameters.round = 512u;

    (void)mk_service_launch_task(MK_SERVICE_AUDIO,
                                 "audio",
                                 mk_audio_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN);
}

int mk_audio_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_AUDIO) != 0;
}

int mk_audio_service_get_info(struct mk_audio_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_audio_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_audio_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_audio_request;
    return 0;
}
