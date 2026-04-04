#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/microkernel/service.h>
#include <kernel/kernel.h>    /* for panic if allocation fails */
#include <kernel/memory/heap.h>   /* kernel_malloc, kernel_free */

#define PROCESS_DEFAULT_STACK_SIZE 4096u
#define PROCESS_MIN_STACK_SIZE 1024u

__attribute__((noreturn)) static void process_entry_return_trampoline(void) {
    process_t *current = scheduler_current();

    if (current != NULL) {
        scheduler_terminate_task(current);
    }

    schedule();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static uint32_t process_normalize_stack_size(uint32_t stack_size) {
    if (stack_size == 0u) {
        stack_size = PROCESS_DEFAULT_STACK_SIZE;
    }
    if (stack_size < PROCESS_MIN_STACK_SIZE) {
        stack_size = PROCESS_MIN_STACK_SIZE;
    }
    return (stack_size + 15u) & ~15u;
}

static int g_next_pid = 1;

static uint32_t process_default_task_class_for(enum process_kind kind,
                                               uint32_t service_type,
                                               uint32_t launch_flags) {
    if (kind == PROCESS_KIND_USER) {
        if ((launch_flags & (MK_LAUNCH_FLAG_BOOTSTRAP | MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
            return MK_TASK_CLASS_SUPERVISION;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_DESKTOP) != 0u) {
            return MK_TASK_CLASS_DESKTOP;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_SHELL) != 0u) {
            return MK_TASK_CLASS_SHELL;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_APP) != 0u) {
            return MK_TASK_CLASS_APP_RUNTIME;
        }
        return MK_TASK_CLASS_APP_RUNTIME;
    }

    if (kind == PROCESS_KIND_SERVICE) {
        switch (service_type) {
        case MK_SERVICE_INIT:
            return MK_TASK_CLASS_SUPERVISION;
        case MK_SERVICE_STORAGE:
            return MK_TASK_CLASS_STORAGE_IO;
        case MK_SERVICE_FILESYSTEM:
            return MK_TASK_CLASS_FILESYSTEM_IO;
        case MK_SERVICE_VIDEO:
            return MK_TASK_CLASS_VIDEO_CONTROL;
        case MK_SERVICE_INPUT:
            return MK_TASK_CLASS_INPUT;
        case MK_SERVICE_CONSOLE:
            return MK_TASK_CLASS_CONSOLE_IO;
        case MK_SERVICE_NETWORK:
            return MK_TASK_CLASS_NETWORK_IO;
        case MK_SERVICE_AUDIO:
            return MK_TASK_CLASS_AUDIO_IO;
        default:
            return MK_TASK_CLASS_NONE;
        }
    }

    return MK_TASK_CLASS_NONE;
}

static uint32_t process_priority_for_task_class(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_SUPERVISION:
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_SHELL:
        return PROCESS_PRIORITY_DESKTOP_USER;
    case MK_TASK_CLASS_INPUT:
    case MK_TASK_CLASS_CONSOLE_IO:
        return PROCESS_PRIORITY_INPUT;
    case MK_TASK_CLASS_VIDEO_PRESENT:
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return PROCESS_PRIORITY_VIDEO;
    case MK_TASK_CLASS_STORAGE_IO:
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return PROCESS_PRIORITY_STORAGE;
    case MK_TASK_CLASS_AUDIO_IO:
        /*
         * Audio playback/control must be able to continue while the desktop is
         * animating and processing input; otherwise startup WAVs and player
         * output appear tied to UI activity.
         */
        return PROCESS_PRIORITY_DESKTOP_USER;
    case MK_TASK_CLASS_NETWORK_IO:
        return PROCESS_PRIORITY_NETWORK;
    case MK_TASK_CLASS_APP_RUNTIME:
        return PROCESS_PRIORITY_APP;
    default:
        return PROCESS_PRIORITY_BACKGROUND;
    }
}

static uint32_t process_priority_for(enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class) {
    if (task_class != MK_TASK_CLASS_NONE) {
        return process_priority_for_task_class(task_class);
    }
    if (kind == PROCESS_KIND_USER) {
        if ((launch_flags & (MK_LAUNCH_FLAG_USER_SHELL |
                             MK_LAUNCH_FLAG_USER_DESKTOP |
                             MK_LAUNCH_FLAG_BOOTSTRAP |
                             MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
            return PROCESS_PRIORITY_DESKTOP_USER;
        }
        return PROCESS_PRIORITY_APP;
    }
    if (kind == PROCESS_KIND_SERVICE) {
        switch (service_type) {
        case MK_SERVICE_INPUT:
        case MK_SERVICE_CONSOLE:
            return PROCESS_PRIORITY_INPUT;
        case MK_SERVICE_VIDEO:
            return PROCESS_PRIORITY_VIDEO;
        case MK_SERVICE_STORAGE:
        case MK_SERVICE_FILESYSTEM:
            return PROCESS_PRIORITY_STORAGE;
        case MK_SERVICE_AUDIO:
            return PROCESS_PRIORITY_DESKTOP_USER;
        case MK_SERVICE_NETWORK:
            return PROCESS_PRIORITY_NETWORK;
        case MK_SERVICE_INIT:
            return PROCESS_PRIORITY_DESKTOP_USER;
        default:
            return PROCESS_PRIORITY_BACKGROUND;
        }
    }
    return PROCESS_PRIORITY_BACKGROUND;
}

void process_setup_initial_context(process_t *proc, uintptr_t entry, uintptr_t stack_top) {
    kernel_trap_frame_t *frame;
    uintptr_t entry_stack_top;
    uint32_t *return_slot;

    if (proc == NULL || stack_top < (sizeof(kernel_trap_frame_t) + sizeof(uint32_t))) {
        return;
    }

    entry_stack_top = stack_top - sizeof(uint32_t);
    return_slot = (uint32_t *)entry_stack_top;
    *return_slot = (uint32_t)(uintptr_t)process_entry_return_trampoline;

    frame = (kernel_trap_frame_t *)(entry_stack_top - sizeof(kernel_trap_frame_t));
    memset(frame, 0, sizeof(*frame));
    frame->esp_dummy = (uint32_t)entry_stack_top;
    frame->eip = (uint32_t)entry;
    frame->cs = 0x08u;
    frame->eflags = 0x00000202u;
    proc->context = frame;
}

process_t *process_create(void (*entry)(void)) {
    return process_create_with_stack(entry,
                                     PROCESS_KIND_USER,
                                     0u,
                                     0u,
                                     MK_TASK_CLASS_NONE,
                                     PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_kind(void (*entry)(void), enum process_kind kind, uint32_t service_type) {
    return process_create_with_stack(entry,
                                     kind,
                                     service_type,
                                     0u,
                                     MK_TASK_CLASS_NONE,
                                     PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_with_stack(void (*entry)(void),
                                     enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class,
                                     uint32_t stack_size) {
    uint32_t normalized_stack_size;
    uint32_t effective_task_class;

    if (entry == NULL) {
        return NULL;
    }
    normalized_stack_size = process_normalize_stack_size(stack_size);

    process_t *p = kernel_malloc(sizeof(process_t));
    if (!p) {
        return NULL;
    }
    memset(p, 0, sizeof(*p));

    p->pid = g_next_pid++;
    p->current_cpu = -1;
    p->preferred_cpu = -1;
    p->last_cpu = -1;
    p->state = PROCESS_READY;
    p->kind = kind;
    p->service_type = service_type;
    effective_task_class = task_class != MK_TASK_CLASS_NONE
                               ? task_class
                               : process_default_task_class_for(kind, service_type, launch_flags);
    p->task_class = effective_task_class;
    p->priority_tier = process_priority_for(kind,
                                            service_type,
                                            launch_flags,
                                            effective_task_class);
    p->stack_size = normalized_stack_size;
    p->runtime_ticks = 0u;
    p->last_start_tick = 0u;
    p->context_switches = 0u;
    p->wait_channel = 0;
    p->wait_deadline = 0u;
    p->wait_result = TASK_WAIT_RESULT_NONE;
    p->wait_event_kind = TASK_WAIT_EVENT_NONE;
    p->wait_event_class = TASK_WAIT_CLASS_NONE;
    p->wait_owner_service = 0u;
    p->wake_boost_budget = 0u;
    p->wait_next = 0;
    p->next = NULL;

    /* allocate stack memory and set initial register state */
    p->stack = kernel_malloc(normalized_stack_size);
    if (!p->stack) {
        kernel_free(p);
        return NULL;
    }

    process_setup_initial_context(p,
                                  (uintptr_t)entry,
                                  (uintptr_t)p->stack + normalized_stack_size);
    return p;
}

void process_terminate(process_t *proc) {
    if (!proc) {
        return;
    }

    proc->state = PROCESS_TERMINATED;
    proc->current_cpu = -1;
    proc->preferred_cpu = -1;
    proc->last_start_tick = 0u;
}

void process_destroy(process_t *proc) {
    if (!proc) {
        return;
    }
    if (proc->stack) {
        kernel_free(proc->stack);
    }
    kernel_free(proc);
}
