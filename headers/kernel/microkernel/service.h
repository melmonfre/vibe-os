#ifndef KERNEL_MICROKERNEL_SERVICE_H
#define KERNEL_MICROKERNEL_SERVICE_H

#include <kernel/microkernel/launch.h>
#include <stdint.h>

struct process;
struct mk_message;

#define MK_SERVICE_NAME_MAX 16u
#define MK_SERVICE_SLOTS 32u

typedef int (*mk_service_local_handler_fn)(const struct mk_message *request,
                                           struct mk_message *reply,
                                           void *context);
typedef void (*mk_service_entry_fn)(void);

enum mk_service_type {
    MK_SERVICE_NONE = 0,
    MK_SERVICE_INIT = 1,
    MK_SERVICE_STORAGE = 2,
    MK_SERVICE_FILESYSTEM = 3,
    MK_SERVICE_VIDEO = 4,
    MK_SERVICE_INPUT = 5,
    MK_SERVICE_CONSOLE = 6,
    MK_SERVICE_NETWORK = 7,
    MK_SERVICE_AUDIO = 8
};

struct mk_service_record {
    uint32_t type;
    int pid;
    char name[MK_SERVICE_NAME_MAX];
    struct process *process;
    mk_service_local_handler_fn local_handler;
    void *context;
    mk_service_entry_fn entry;
    uint32_t launch_flags;
    uint32_t stack_size;
    uint32_t restart_count;
    uint32_t transport_degraded;
};

void mk_service_init(void);
int mk_service_register(uint32_t type, const char *name, struct process *process);
int mk_service_register_local(uint32_t type, const char *name);
int mk_service_register_local_handler(uint32_t type,
                                      const char *name,
                                      mk_service_local_handler_fn handler,
                                      void *context);
int mk_service_launch_worker(uint32_t type,
                             const char *name,
                             mk_service_local_handler_fn handler,
                             void *context,
                             uint32_t stack_size);
int mk_service_launch_task(uint32_t type,
                           const char *name,
                           mk_service_local_handler_fn handler,
                           void *context,
                           mk_service_entry_fn entry,
                           uint32_t stack_size,
                           uint32_t launch_flags);
int mk_service_is_online(uint32_t type);
int mk_service_ensure(uint32_t type);
int mk_service_request(uint32_t type, const struct mk_message *request, struct mk_message *reply);
int mk_service_backend_handle_current(const struct mk_message *request, struct mk_message *reply);
const struct mk_service_record *mk_service_find_by_type(uint32_t type);
const struct mk_service_record *mk_service_find_by_name(const char *name);

#endif
