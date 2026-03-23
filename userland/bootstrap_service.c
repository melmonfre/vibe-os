#include <kernel/microkernel/message.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

static void service_log_online(const struct userland_launch_info *info) {
    char buffer[64];

    if (info == 0) {
        return;
    }

    buffer[0] = '\0';
    str_append(buffer, "service-host: online ", (int)sizeof(buffer));
    str_append(buffer, info->name, (int)sizeof(buffer));
    str_append(buffer, "\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static void service_prepare_error_reply(struct mk_message *reply,
                                        const struct mk_message *request,
                                        uint32_t source_pid) {
    int i;

    if (reply == 0 || request == 0) {
        return;
    }

    reply->abi_version = MK_MESSAGE_ABI_VERSION;
    reply->type = request->type;
    reply->source_pid = source_pid;
    reply->target_pid = request->source_pid;
    reply->payload_size = 0u;
    for (i = 0; i < (int)sizeof(reply->payload); ++i) {
        reply->payload[i] = 0u;
    }
}

__attribute__((section(".entry"))) void userland_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            sys_yield();
            continue;
        }

        if (sys_service_backend(&request, &reply) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}
