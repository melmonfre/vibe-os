#include <kernel/microkernel/service.h>
#include <kernel/kernel_string.h>
#include <kernel/ipc.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>

static struct mk_service_record g_services[MK_SERVICE_SLOTS];
static int g_console_request_trace_emitted = 0;
static int g_console_worker_entry_trace_emitted = 0;
static int g_console_worker_trace_emitted = 0;
static int g_console_reply_trace_emitted = 0;
static int g_transport_fallback_budget = 16;
static int g_request_send_fail_budget = 16;
static int g_unexpected_reply_budget = 16;
static int g_storage_request_trace_budget = 8;
static int g_storage_worker_trace_budget = 8;
static int g_storage_reply_trace_budget = 8;

static void mk_service_worker_entry(void);

void mk_service_init(void) {
    memset(g_services, 0, sizeof(g_services));
}

static int mk_service_name_equals(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    return strcmp(lhs, rhs) == 0;
}

static struct mk_service_record *mk_service_find_mutable_by_type(uint32_t type) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type) {
            return &g_services[slot];
        }
    }

    return 0;
}

static int mk_service_register_impl(uint32_t type,
                                    const char *name,
                                    int pid,
                                    struct process *process,
                                    mk_service_local_handler_fn local_handler,
                                    void *context) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE || name == 0) {
        return -1;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type || mk_service_name_equals(g_services[slot].name, name)) {
            g_services[slot].type = type;
            if (pid != 0 || process != 0) {
                g_services[slot].pid = pid;
                g_services[slot].process = process;
                g_services[slot].transport_degraded = 0u;
            }
            if (local_handler != 0) {
                g_services[slot].local_handler = local_handler;
                g_services[slot].context = context;
            }
            strncpy(g_services[slot].name, name, MK_SERVICE_NAME_MAX - 1u);
            g_services[slot].name[MK_SERVICE_NAME_MAX - 1u] = '\0';
            return 0;
        }
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == MK_SERVICE_NONE) {
            g_services[slot].type = type;
            g_services[slot].pid = pid;
            g_services[slot].process = process;
            g_services[slot].local_handler = local_handler;
            g_services[slot].context = context;
            g_services[slot].transport_degraded = 0u;
            strncpy(g_services[slot].name, name, MK_SERVICE_NAME_MAX - 1u);
            g_services[slot].name[MK_SERVICE_NAME_MAX - 1u] = '\0';
            return 0;
        }
    }

    return -1;
}

int mk_service_register(uint32_t type, const char *name, struct process *process) {
    if (process == 0) {
        return -1;
    }
    return mk_service_register_impl(type, name, process->pid, process, 0, 0);
}

int mk_service_register_local(uint32_t type, const char *name) {
    return mk_service_register_impl(type, name, 0, 0, 0, 0);
}

int mk_service_register_local_handler(uint32_t type,
                                      const char *name,
                                      mk_service_local_handler_fn handler,
                                      void *context) {
    if (handler == 0) {
        return -1;
    }
    return mk_service_register_impl(type, name, 0, 0, handler, context);
}

static int mk_service_process_online(const struct mk_service_record *service) {
    if (service == 0) {
        return 0;
    }
    if (service->process == 0 || service->pid <= 0) {
        return 1;
    }
    return scheduler_find_task_by_pid(service->pid) != 0;
}

static void mk_service_discard_worker_record(struct mk_service_record *service) {
    if (service == 0) {
        return;
    }

    if (service->process != 0) {
        scheduler_terminate_task(service->process);
    }
    if (service->pid > 0) {
        mk_launch_release_pid(service->pid);
    }

    service->pid = 0;
    service->process = 0;
}

static int mk_service_restart_worker_record(struct mk_service_record *service) {
    struct mk_launch_descriptor descriptor;
    int force_restart;
    int pid;

    if (service == 0 || service->type == MK_SERVICE_NONE) {
        return -1;
    }
    if (service->local_handler == 0) {
        return -1;
    }
    if (service->name[0] == '\0') {
        return -1;
    }
    if (service->process == 0 && service->pid == 0 && service->launch_flags == 0u) {
        return 0;
    }
    force_restart = service->transport_degraded != 0u;
    if (!force_restart && mk_service_process_online(service)) {
        return 0;
    }

    if (service->pid > 0) {
        kernel_debug_printf("service: restarting type=%d old_pid=%d restarts=%d\n",
                            (int)service->type,
                            service->pid,
                            (int)service->restart_count);
    }
    mk_service_discard_worker_record(service);

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_SERVICE;
    descriptor.service_type = service->type;
    descriptor.flags = service->launch_flags;
    descriptor.stack_size = service->stack_size;
    strncpy(descriptor.name, service->name, MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = service->entry != 0 ? service->entry : mk_service_worker_entry;

    pid = mk_launch_bootstrap(&descriptor);
    if (pid < 0) {
        return -1;
    }

    service->transport_degraded = 0u;
    service->restart_count += 1u;
    return 0;
}

static int mk_service_reply_process(const struct mk_message *reply) {
    process_t *destination;

    if (reply == 0 || reply->target_pid == 0u) {
        return -1;
    }

    destination = scheduler_find_task_by_pid((int)reply->target_pid);
    if (destination == 0) {
        return -1;
    }

    if (reply->source_pid != 0u && g_storage_reply_trace_budget > 0 &&
        scheduler_find_task_by_pid((int)reply->source_pid) != 0) {
        process_t *source = scheduler_find_task_by_pid((int)reply->source_pid);
        if (source != 0 && source->service_type == MK_SERVICE_STORAGE) {
            g_storage_reply_trace_budget -= 1;
            kernel_debug_printf("service: storage reply src=%d dst=%d type=%d\n",
                                (int)reply->source_pid,
                                (int)reply->target_pid,
                                (int)reply->type);
        }
    }

    if (ipc_send(destination, reply, sizeof(*reply)) != 0) {
        kernel_debug_printf("service: reply send failed src=%d dst=%d type=%d\n",
                            (int)reply->source_pid,
                            (int)reply->target_pid,
                            (int)reply->type);
        return -1;
    }

    return 0;
}

static int mk_service_requeue_message(process_t *destination, const struct mk_message *message) {
    if (destination == 0 || message == 0) {
        return -1;
    }

    return ipc_send(destination, message, sizeof(*message));
}

static void mk_service_worker_step(const struct mk_service_record *service) {
    process_t *current;
    struct mk_message request;
    struct mk_message reply;

    current = scheduler_current();
    if (current == 0 || service == 0 || service->local_handler == 0) {
        yield();
        return;
    }

    if (ipc_receive(current, &request, sizeof(request)) != (int)sizeof(request)) {
        yield();
        return;
    }

    if (service->type == MK_SERVICE_CONSOLE && !g_console_worker_trace_emitted) {
        g_console_worker_trace_emitted = 1;
        kernel_debug_printf("service: console worker pid=%d received request type=%d from pid=%d\n",
                            current->pid,
                            (int)request.type,
                            (int)request.source_pid);
    }
    if (service->type == MK_SERVICE_STORAGE && g_storage_worker_trace_budget > 0) {
        g_storage_worker_trace_budget -= 1;
        kernel_debug_printf("service: storage worker pid=%d received type=%d from pid=%d\n",
                            current->pid,
                            (int)request.type,
                            (int)request.source_pid);
    }

    if (service->local_handler(&request, &reply, service->context) != 0) {
        mk_message_init(&reply, request.type);
        reply.source_pid = (uint32_t)current->pid;
        reply.target_pid = request.source_pid;
    }

    if (reply.source_pid == 0u) {
        reply.source_pid = (uint32_t)current->pid;
    }
    if (reply.target_pid == 0u) {
        reply.target_pid = request.source_pid;
    }

    (void)mk_service_reply_process(&reply);
}

static void mk_service_worker_entry(void) {
    for (;;) {
        const struct mk_launch_context *launch_context = mk_launch_context_current();
        const struct mk_service_record *service = 0;

        if (launch_context != 0) {
            service = mk_service_find_by_type(launch_context->service_type);
            if (launch_context->service_type == MK_SERVICE_CONSOLE &&
                !g_console_worker_entry_trace_emitted) {
                process_t *current = scheduler_current();

                g_console_worker_entry_trace_emitted = 1;
                kernel_debug_printf("service: console worker entry pid=%d launch_service=%d\n",
                                    current != 0 ? current->pid : -1,
                                    (int)launch_context->service_type);
            }
        }

        if (service == 0 || service->process == 0 || service->local_handler == 0) {
            yield();
            continue;
        }

        mk_service_worker_step(service);
    }
}

int mk_service_launch_worker(uint32_t type,
                             const char *name,
                             mk_service_local_handler_fn handler,
                             void *context,
                             uint32_t stack_size) {
    return mk_service_launch_task(type,
                                  name,
                                  handler,
                                  context,
                                  mk_service_worker_entry,
                                  stack_size,
                                  MK_LAUNCH_FLAG_BOOTSTRAP | MK_LAUNCH_FLAG_BUILTIN);
}

int mk_service_launch_task(uint32_t type,
                           const char *name,
                           mk_service_local_handler_fn handler,
                           void *context,
                           mk_service_entry_fn entry,
                           uint32_t stack_size,
                           uint32_t launch_flags) {
    struct mk_service_record *service;
    struct mk_launch_descriptor descriptor;

    if (type == MK_SERVICE_NONE || name == 0 || handler == 0 || entry == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service != 0 && service->process != 0) {
        return service->pid;
    }

    if (mk_service_register_local_handler(type, name, handler, context) != 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_SERVICE;
    descriptor.service_type = type;
    descriptor.flags = launch_flags;
    descriptor.stack_size = stack_size;
    strncpy(descriptor.name, name, MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = entry;
    service->entry = entry;
    service->launch_flags = descriptor.flags;
    service->stack_size = descriptor.stack_size;
    return mk_launch_bootstrap(&descriptor);
}

int mk_service_is_online(uint32_t type) {
    const struct mk_service_record *service = mk_service_find_by_type(type);

    if (service == 0) {
        return 0;
    }
    return mk_service_process_online(service);
}

int mk_service_ensure(uint32_t type) {
    struct mk_service_record *service = mk_service_find_mutable_by_type(type);

    if (service == 0) {
        return -1;
    }
    return mk_service_restart_worker_record(service);
}

static int mk_service_request_process(const struct mk_service_record *service,
                                      const struct mk_message *request,
                                      struct mk_message *reply) {
    process_t *current;
    struct mk_message request_copy;
    struct mk_message response;

    if (service == 0 || request == 0 || reply == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0 || current == service->process) {
        if (service->local_handler != 0) {
            return service->local_handler(request, reply, service->context);
        }
        return -1;
    }

    request_copy = *request;
    if (request_copy.abi_version == 0u) {
        request_copy.abi_version = MK_MESSAGE_ABI_VERSION;
    }
    request_copy.source_pid = (uint32_t)current->pid;
    request_copy.target_pid = (uint32_t)service->pid;

    if (service->type == MK_SERVICE_CONSOLE && !g_console_request_trace_emitted) {
        g_console_request_trace_emitted = 1;
        kernel_debug_printf("service: send request type=%d from pid=%d to console pid=%d\n",
                            (int)request_copy.type,
                            current->pid,
                            service->pid);
    }
    if (service->type == MK_SERVICE_STORAGE && g_storage_request_trace_budget > 0) {
        g_storage_request_trace_budget -= 1;
        kernel_debug_printf("service: storage request type=%d from pid=%d to pid=%d\n",
                            (int)request_copy.type,
                            current->pid,
                            service->pid);
    }

    if (ipc_send(service->process, &request_copy, sizeof(request_copy)) != 0) {
        if (service->local_handler != 0) {
            struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

            if (mutable_service != 0) {
                mutable_service->transport_degraded = 1u;
            }
            if (g_transport_fallback_budget > 0) {
                g_transport_fallback_budget -= 1;
                kernel_debug_printf("service: transport fallback type=%d src=%d dst=%d service=%d\n",
                                    (int)request_copy.type,
                                    (int)request_copy.source_pid,
                                    (int)request_copy.target_pid,
                                    (int)service->type);
            }
            return service->local_handler(&request_copy, reply, service->context);
        }
        if (g_request_send_fail_budget > 0) {
            g_request_send_fail_budget -= 1;
            kernel_debug_printf("service: request send failed type=%d src=%d dst=%d\n",
                                (int)request_copy.type,
                                (int)request_copy.source_pid,
                                (int)request_copy.target_pid);
        }
        return -1;
    }

    for (;;) {
        int received = ipc_receive(current, &response, sizeof(response));

        if (received == (int)sizeof(response)) {
            if (response.source_pid == (uint32_t)service->pid &&
                response.target_pid == (uint32_t)current->pid) {
                if (service->type == MK_SERVICE_CONSOLE && !g_console_reply_trace_emitted) {
                    g_console_reply_trace_emitted = 1;
                    kernel_debug_printf("service: console reply type=%d src=%d dst=%d\n",
                                        (int)response.type,
                                        (int)response.source_pid,
                                        (int)response.target_pid);
                }
                {
                    struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

                    if (mutable_service != 0) {
                        mutable_service->transport_degraded = 0u;
                    }
                }
                *reply = response;
                return 0;
            }
            if (g_unexpected_reply_budget > 0) {
                g_unexpected_reply_budget -= 1;
                kernel_debug_printf("service: unexpected reply src=%d dst=%d waiting_src=%d waiting_dst=%d\n",
                                    (int)response.source_pid,
                                    (int)response.target_pid,
                                    service->pid,
                                    current->pid);
            }
            (void)mk_service_requeue_message(current, &response);
        }
        yield();
    }
}

int mk_service_request(uint32_t type, const struct mk_message *request, struct mk_message *reply) {
    const struct mk_service_record *service;

    if (request == 0 || reply == 0) {
        return -1;
    }

    service = mk_service_find_by_type(type);
    if (service == 0) {
        return -1;
    }

    if (service->transport_degraded != 0u && service->process != 0) {
        (void)mk_service_ensure(type);
        service = mk_service_find_by_type(type);
        if (service == 0) {
            return -1;
        }
    }

    if (service->transport_degraded != 0u && service->local_handler != 0) {
        return service->local_handler(request, reply, service->context);
    }

    if (service->process != 0) {
        if (!mk_service_process_online(service) && mk_service_ensure(type) != 0) {
            return -1;
        }
        service = mk_service_find_by_type(type);
        if (service == 0 || service->process == 0 || !mk_service_process_online(service)) {
            return -1;
        }
        return mk_service_request_process(service, request, reply);
    }

    if (service->local_handler != 0) {
        return service->local_handler(request, reply, service->context);
    }

    return -1;
}

int mk_service_backend_handle_current(const struct mk_message *request, struct mk_message *reply) {
    process_t *current;
    const struct mk_service_record *service;

    if (request == 0 || reply == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0 || current->kind != PROCESS_KIND_SERVICE ||
        current->service_type == MK_SERVICE_NONE) {
        return -1;
    }

    service = mk_service_find_by_type(current->service_type);
    if (service == 0 || service->local_handler == 0) {
        return -1;
    }

    return service->local_handler(request, reply, service->context);
}

const struct mk_service_record *mk_service_find_by_type(uint32_t type) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type) {
            return &g_services[slot];
        }
    }

    return 0;
}

const struct mk_service_record *mk_service_find_by_name(const char *name) {
    uint32_t slot;

    if (name == 0) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (mk_service_name_equals(g_services[slot].name, name)) {
            return &g_services[slot];
        }
    }

    return 0;
}
