#include <kernel/microkernel/service.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/ipc.h>
#include <kernel/lock.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <include/userland_api.h>

static struct mk_service_record g_services[MK_SERVICE_SLOTS];
static int g_console_request_trace_emitted = 0;
static int g_console_worker_entry_trace_emitted = 0;
static int g_console_worker_trace_emitted = 0;
static int g_console_reply_trace_emitted = 0;
static int g_transport_fallback_budget = 16;
static int g_request_send_fail_budget = 16;
static int g_unexpected_reply_budget = 16;
static int g_request_timeout_budget = 16;
static int g_storage_request_trace_budget = 8;
static int g_storage_worker_trace_budget = 8;
static int g_storage_reply_trace_budget = 8;
static int g_interactive_request_trace_budget = 24;
static spinlock_t g_service_event_lock;

#define MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS 32u
#define MK_SERVICE_VIDEO_CONTROL_TIMEOUT_TICKS 192u
#define MK_SERVICE_DEFERRED_REPLIES_MAX 8u

static void mk_service_worker_entry(void);
static void mk_service_publish_event(struct mk_service_record *service, uint32_t event_type);
static void mk_service_enqueue_event(struct mk_service_record *service,
                                     struct mk_service_event_subscription *subscription,
                                     uint32_t event_type);
static uint32_t mk_service_request_timeout_ticks(const struct mk_service_record *service,
                                                 const struct mk_message *request);
static void mk_service_restore_deferred_messages(process_t *destination,
                                                 const struct mk_message *messages,
                                                 uint32_t count);

static int mk_service_current_prefers_local_handler(const struct mk_service_record *service,
                                                    const struct mk_message *request) {
    const struct mk_launch_context *context;

    if (service == 0 || request == 0) {
        return 0;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return 0;
    }

    if ((context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) == 0u) {
        return 0;
    }

    if (service->type == MK_SERVICE_INPUT) {
        return 1;
    }

    if (service->type == MK_SERVICE_VIDEO) {
        /*
         * Keep the desktop-class fast path for lightweight video IPC, but let
         * full mode switches flow through the dedicated service task so the
         * control worker completes in the same context the runtime handoff was
         * designed around.
         */
        return request->type != MK_MSG_VIDEO_MODE_SET;
    }

    return 0;
}

static uint32_t mk_service_request_timeout_ticks(const struct mk_service_record *service,
                                                 const struct mk_message *request) {
    if (service == 0 || request == 0) {
        return MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS;
    }

    if (service->type == MK_SERVICE_VIDEO &&
        (request->type == MK_MSG_VIDEO_MODE_SET || request->type == MK_MSG_VIDEO_LEAVE)) {
        /*
         * Real mode transitions remap framebuffers and re-arm the backend; they
         * legitimately take longer than the lightweight IPC/control messages
         * that use the default budget.
         */
        return MK_SERVICE_VIDEO_CONTROL_TIMEOUT_TICKS;
    }

    return MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS;
}

static void mk_service_restore_deferred_messages(process_t *destination,
                                                 const struct mk_message *messages,
                                                 uint32_t count) {
    uint32_t index;

    if (destination == 0 || messages == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        if (ipc_send(destination, &messages[index], sizeof(messages[index])) != 0) {
            break;
        }
    }
}

void mk_service_init(void) {
    memset(g_services, 0, sizeof(g_services));
    spinlock_init(&g_service_event_lock);
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

static void mk_service_init_subscriptions(struct mk_service_record *service) {
    uint32_t index;

    if (service == 0) {
        return;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        subscription->pid = 0;
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_SERVICE_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_NEWEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            service->type);
    }
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
            mk_service_init_subscriptions(&g_services[slot]);
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

    if (service->pid > 0 || service->process != 0) {
        mk_service_publish_event(service, MK_SERVICE_EVENT_OFFLINE);
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
    mk_service_publish_event(service, force_restart ? MK_SERVICE_EVENT_RESTARTED : MK_SERVICE_EVENT_ONLINE);
    return 0;
}

static void mk_service_publish_event(struct mk_service_record *service, uint32_t event_type) {
    uint32_t index;
    uint32_t flags;

    if (service == 0 || service->type == MK_SERVICE_NONE || event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_SERVICE_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_SUPERVISION,
                                service->type);
            continue;
        }
        mk_service_enqueue_event(service, subscription, event_type);
    }
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
}

static void mk_service_enqueue_event(struct mk_service_record *service,
                                     struct mk_service_event_subscription *subscription,
                                     uint32_t event_type) {
    struct mk_service_event event;

    if (service == 0 || subscription == 0 || event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.abi_version = 1u;
    event.service_type = service->type;
    event.event_type = event_type;
    event.pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    event.restart_count = service->restart_count;
    event.transport_degraded = service->transport_degraded;
    event.tick = kernel_timer_get_ticks();

    (void)kernel_mailbox_try_send(&subscription->mailbox, &event);
}

static struct mk_service_event_subscription *mk_service_find_subscription(
    struct mk_service_record *service,
    const struct process *subscriber) {
    uint32_t index;

    if (service == 0 || subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }

    return 0;
}

static struct mk_service_event_subscription *mk_service_alloc_subscription(
    struct mk_service_record *service,
    struct process *subscriber) {
    uint32_t index;

    if (service == 0 || subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_SERVICE_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_SUPERVISION,
                                service->type);
            return subscription;
        }
    }

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

    if (ipc_receive_wait(current, &request, sizeof(request)) != (int)sizeof(request)) {
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
    if ((service->type == MK_SERVICE_VIDEO ||
         service->type == MK_SERVICE_AUDIO ||
         service->type == MK_SERVICE_NETWORK ||
         service->type == MK_SERVICE_INPUT) &&
        g_interactive_request_trace_budget > 0) {
        g_interactive_request_trace_budget -= 1;
        kernel_debug_printf("service: worker svc=%d pid=%d type=%d src=%d\n",
                            (int)service->type,
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

int mk_service_restart(uint32_t type) {
    struct mk_service_record *service = mk_service_find_mutable_by_type(type);

    if (service == 0) {
        return -1;
    }
    if (service->type == MK_SERVICE_INIT) {
        return -1;
    }
    if (service->process == 0 && service->pid == 0) {
        return mk_service_restart_worker_record(service);
    }

    if (service->process != 0) {
        scheduler_publish_lifecycle_event(MK_TASK_EVENT_RESTART_REQUESTED,
                                          service->process);
    }
    service->transport_degraded = 1u;
    return mk_service_restart_worker_record(service);
}

static int mk_service_request_process(const struct mk_service_record *service,
                                      const struct mk_message *request,
                                      struct mk_message *reply) {
    process_t *current;
    struct mk_message request_copy;
    struct mk_message response;
    struct mk_message deferred_replies[MK_SERVICE_DEFERRED_REPLIES_MAX];
    uint32_t request_start_tick;
    uint32_t request_timeout_ticks;
    uint32_t waiting_pid;
    uint32_t deferred_reply_count = 0u;

    if (service == 0 || request == 0 || reply == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0) {
        return -1;
    }

    request_copy = *request;
    if (request_copy.abi_version == 0u) {
        request_copy.abi_version = MK_MESSAGE_ABI_VERSION;
    }
    request_copy.source_pid = (uint32_t)current->pid;
    if (service->pid > 0) {
        request_copy.target_pid = (uint32_t)service->pid;
    }

    if ((current == service->process || mk_service_current_prefers_local_handler(service, &request_copy)) &&
        service->local_handler != 0) {
        return service->local_handler(&request_copy, reply, service->context);
    }

    for (;;) {
        const struct mk_service_record *live_service = mk_service_find_by_type(service->type);

        if (live_service != 0) {
            service = live_service;
        }
        if (service == 0 || service->process == 0 || service->pid <= 0) {
            if (service != 0 && service->local_handler != 0) {
                return service->local_handler(&request_copy, reply, service->context);
            }
            return -1;
        }

        request_copy.target_pid = (uint32_t)service->pid;
        waiting_pid = request_copy.target_pid;
        request_start_tick = kernel_timer_get_ticks();
        request_timeout_ticks = mk_service_request_timeout_ticks(service, &request_copy);

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
        if ((service->type == MK_SERVICE_VIDEO ||
             service->type == MK_SERVICE_AUDIO ||
             service->type == MK_SERVICE_NETWORK ||
             service->type == MK_SERVICE_INPUT) &&
            g_interactive_request_trace_budget > 0) {
            g_interactive_request_trace_budget -= 1;
            kernel_debug_printf("service: request svc=%d type=%d src=%d dst=%d\n",
                                (int)service->type,
                                (int)request_copy.type,
                                current->pid,
                                service->pid);
        }

        if (ipc_send(service->process, &request_copy, sizeof(request_copy)) != 0) {
            if (service->local_handler != 0) {
                struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

                if (mutable_service != 0) {
                    if (mutable_service->transport_degraded == 0u) {
                        mutable_service->transport_degraded = 1u;
                        mk_service_publish_event(mutable_service, MK_SERVICE_EVENT_DEGRADED);
                    }
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
            uint32_t elapsed_ticks = kernel_timer_get_ticks() - request_start_tick;
            uint32_t remaining_ticks = 0u;
            int received;

            if (elapsed_ticks < request_timeout_ticks) {
                remaining_ticks = request_timeout_ticks - elapsed_ticks;
            }
            if (remaining_ticks == 0u) {
                received = -1;
            } else {
                received = ipc_receive_wait_timeout(current,
                                                    &response,
                                                    sizeof(response),
                                                    remaining_ticks);
            }

            if (received == (int)sizeof(response)) {
                if (response.source_pid == waiting_pid &&
                    response.target_pid == (uint32_t)current->pid) {
                    mk_service_restore_deferred_messages(current,
                                                         deferred_replies,
                                                         deferred_reply_count);
                    deferred_reply_count = 0u;
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
                            if (mutable_service->transport_degraded != 0u) {
                                mutable_service->transport_degraded = 0u;
                                mk_service_publish_event(mutable_service, MK_SERVICE_EVENT_RECOVERED);
                            }
                        }
                    }
                    if (service->type == MK_SERVICE_VIDEO ||
                        service->type == MK_SERVICE_AUDIO ||
                        service->type == MK_SERVICE_NETWORK ||
                        service->type == MK_SERVICE_INPUT) {
                        kernel_debug_printf("service: reply svc=%d type=%d src=%d dst=%d\n",
                                            (int)service->type,
                                            (int)response.type,
                                            (int)response.source_pid,
                                            (int)response.target_pid);
                    }
                    *reply = response;
                    return 0;
                }
                if (g_unexpected_reply_budget > 0) {
                    g_unexpected_reply_budget -= 1;
                    kernel_debug_printf("service: unexpected reply src=%d dst=%d waiting_src=%d waiting_dst=%d\n",
                                        (int)response.source_pid,
                                        (int)response.target_pid,
                                        (int)waiting_pid,
                                        current->pid);
                }
                if (deferred_reply_count >= MK_SERVICE_DEFERRED_REPLIES_MAX) {
                    mk_service_restore_deferred_messages(current,
                                                         deferred_replies,
                                                         deferred_reply_count);
                    deferred_reply_count = 0u;
                    (void)mk_service_requeue_message(current, &response);
                    return -1;
                }
                deferred_replies[deferred_reply_count++] = response;
                continue;
            }

            if ((uint32_t)(kernel_timer_get_ticks() - request_start_tick) >=
                request_timeout_ticks) {
                struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

                if (g_request_timeout_budget > 0) {
                    g_request_timeout_budget -= 1;
                    kernel_debug_printf("service: request timeout svc=%d type=%d src=%d dst=%d\n",
                                        (int)service->type,
                                        (int)request_copy.type,
                                        (int)request_copy.source_pid,
                                        (int)request_copy.target_pid);
                }

                if (mutable_service != 0) {
                    if (mutable_service->transport_degraded == 0u) {
                        mutable_service->transport_degraded = 1u;
                        mk_service_publish_event(mutable_service, MK_SERVICE_EVENT_DEGRADED);
                    }
                    (void)mk_service_restart_worker_record(mutable_service);
                    if (mutable_service->local_handler != 0) {
                        if (g_transport_fallback_budget > 0) {
                            g_transport_fallback_budget -= 1;
                            kernel_debug_printf("service: timeout fallback type=%d src=%d dst=%d service=%d\n",
                                                (int)request_copy.type,
                                                (int)request_copy.source_pid,
                                                (int)request_copy.target_pid,
                                                (int)service->type);
                        }
                        mk_service_restore_deferred_messages(current,
                                                             deferred_replies,
                                                             deferred_reply_count);
                        return mutable_service->local_handler(&request_copy, reply, mutable_service->context);
                    }
                }

                mk_service_restore_deferred_messages(current,
                                                     deferred_replies,
                                                     deferred_reply_count);
                return -1;
            }

            live_service = mk_service_find_by_type(service->type);
            if (live_service == 0) {
                mk_service_restore_deferred_messages(current,
                                                     deferred_replies,
                                                     deferred_reply_count);
                return -1;
            }
            if (live_service->transport_degraded != 0u && live_service->process != 0) {
                (void)mk_service_ensure(service->type);
                live_service = mk_service_find_by_type(service->type);
                if (live_service == 0) {
                    mk_service_restore_deferred_messages(current,
                                                         deferred_replies,
                                                         deferred_reply_count);
                    return -1;
                }
            }
            if (live_service->pid != (int)waiting_pid ||
                !mk_service_process_online(live_service)) {
                service = live_service;
                break;
            }
        }
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

int mk_service_subscribe(uint32_t type, struct process *subscriber) {
    struct mk_service_record *service;
    struct mk_service_event_subscription *subscription;
    uint32_t flags;
    int rc = -1;

    if (subscriber == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    subscription = mk_service_find_subscription(service, subscriber);
    if (subscription != 0) {
        rc = 0;
        goto done;
    }

    subscription = mk_service_alloc_subscription(service, subscriber);
    if (subscription == 0) {
        goto done;
    }

    if (service->transport_degraded != 0u) {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_DEGRADED);
    } else if (mk_service_process_online(service)) {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_ONLINE);
    } else {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_OFFLINE);
    }
    rc = 0;

done:
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
    return rc;
}

int mk_service_unsubscribe(uint32_t type, struct process *subscriber) {
    struct mk_service_record *service;
    struct mk_service_event_subscription *subscription;
    uint32_t flags;
    int rc = -1;

    if (subscriber == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    subscription = mk_service_find_subscription(service, subscriber);
    if (subscription == 0) {
        goto done;
    }

    memset(subscription, 0, sizeof(*subscription));
    rc = 0;

done:
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
    return rc;
}

int mk_service_event_receive(uint32_t type,
                             struct process *subscriber,
                             struct mk_service_event *event,
                             uint32_t timeout_ticks) {
    struct mk_service_record *service;
    int wait_rc;
    uint32_t flags;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    for (;;) {
        struct mk_service_event_subscription *subscription;

        flags = spinlock_lock_irqsave(&g_service_event_lock);
        subscription = mk_service_find_subscription(service, subscriber);
        if (subscription == 0) {
            spinlock_unlock_irqrestore(&g_service_event_lock, flags);
            return -1;
        }
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            spinlock_unlock_irqrestore(&g_service_event_lock, flags);
            return 0;
        }
        spinlock_unlock_irqrestore(&g_service_event_lock, flags);
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

void mk_service_fill_task_snapshot(struct task_snapshot_entry *entry) {
    const struct mk_service_record *service;

    if (entry == 0 || entry->service_type == MK_SERVICE_NONE) {
        return;
    }

    service = mk_service_find_by_type(entry->service_type);
    if (service == 0) {
        return;
    }

    if (mk_service_process_online(service)) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_ONLINE;
    }
    if (service->transport_degraded != 0u) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED;
    }
    if (service->local_handler != 0 &&
        (service->process != 0 || service->pid != 0 || service->launch_flags != 0u)) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_RESTARTABLE;
    }
    entry->service_restart_count = service->restart_count;
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
