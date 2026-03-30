#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* process states for scheduler bookkeeping */
enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED,
};

enum process_kind {
    PROCESS_KIND_USER = 0,
    PROCESS_KIND_SERVICE = 1,
    PROCESS_KIND_KERNEL_TASK = 2,
};

enum process_priority_tier {
    PROCESS_PRIORITY_DESKTOP_USER = 0,
    PROCESS_PRIORITY_INPUT = 1,
    PROCESS_PRIORITY_VIDEO = 2,
    PROCESS_PRIORITY_STORAGE = 3,
    PROCESS_PRIORITY_AUDIO = 4,
    PROCESS_PRIORITY_NETWORK = 5,
    PROCESS_PRIORITY_APP = 6,
    PROCESS_PRIORITY_BACKGROUND = 7,
};

/*
 * Saved trap frame layout used by timer/yield preemption.
 * It matches the stack produced by:
 *   CPU interrupt gate push: EIP, CS, EFLAGS
 *   followed by the stub's pusha: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
 * When observed from the final ESP after pusha, the order below is correct.
 */
typedef struct kernel_trap_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} kernel_trap_frame_t;

typedef struct process {
    int pid;                /* process identifier */
    void *stack;            /* base pointer of allocated stack memory */
    uint32_t stack_size;    /* allocated stack size in bytes */
    kernel_trap_frame_t *context; /* saved trap frame on this process stack */
    int current_cpu;        /* CPU que esta executando este processo, -1 se nenhuma */
    int preferred_cpu;      /* CPU alvo para balanceamento inicial */
    int last_cpu;           /* ultimo CPU que executou a tarefa */
    enum process_state state;
    enum process_kind kind;
    uint32_t service_type;
    uint32_t task_class;
    uint32_t priority_tier;
    uint32_t runtime_ticks;
    uint32_t last_start_tick;
    uint32_t context_switches;
    uint32_t last_task_event_sequence;
    uint32_t last_task_event_type;
    uint32_t last_task_event_tick;
    const void *wait_channel;
    uint32_t wait_deadline;
    uint32_t wait_result;
    uint32_t wait_event_kind;
    uint32_t wait_event_class;
    uint32_t wait_owner_service;
    struct process *wait_next;
    struct process *next;   /* linked‑list pointer for scheduler */
} process_t;

/* create / destroy a process object.  the entry point will be installed
   in the register state and the stack allocated automatically. */
process_t *process_create(void (*entry)(void));
process_t *process_create_kind(void (*entry)(void), enum process_kind kind, uint32_t service_type);
process_t *process_create_with_stack(void (*entry)(void),
                                     enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class,
                                     uint32_t stack_size);
void process_setup_initial_context(process_t *proc, uintptr_t entry, uintptr_t stack_top);
void process_terminate(process_t *proc);
void process_destroy(process_t *proc);

static inline uint32_t process_saved_eip(const process_t *proc) {
    return (proc != NULL && proc->context != NULL) ? proc->context->eip : 0u;
}

static inline uint32_t process_saved_esp(const process_t *proc) {
    return (proc != NULL && proc->context != NULL)
               ? (uint32_t)((uintptr_t)proc->context + sizeof(kernel_trap_frame_t))
               : 0u;
}

#endif /* KERNEL_PROCESS_H */
