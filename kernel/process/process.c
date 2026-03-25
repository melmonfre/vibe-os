#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/kernel.h>    /* for panic if allocation fails */
#include <kernel/memory/heap.h>   /* kernel_malloc, kernel_free */

#define PROCESS_DEFAULT_STACK_SIZE 4096u
#define PROCESS_MIN_STACK_SIZE 1024u

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

process_t *process_create(void (*entry)(void)) {
    return process_create_with_stack(entry, PROCESS_KIND_USER, 0u, PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_kind(void (*entry)(void), enum process_kind kind, uint32_t service_type) {
    return process_create_with_stack(entry, kind, service_type, PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_with_stack(void (*entry)(void),
                                     enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t stack_size) {
    uint32_t normalized_stack_size;

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
    p->stack_size = normalized_stack_size;
    p->runtime_ticks = 0u;
    p->last_start_tick = 0u;
    p->context_switches = 0u;
    p->next = NULL;

    /* allocate stack memory and set initial register state */
    p->stack = kernel_malloc(normalized_stack_size);
    if (!p->stack) {
        kernel_free(p);
        return NULL;
    }

    uintptr_t stack_top = (uintptr_t)p->stack + normalized_stack_size;
    p->regs.eip = (uint32_t)entry;
    p->regs.esp = (uint32_t)stack_top;
    p->regs.ebp = (uint32_t)stack_top;
    p->regs.eax = p->regs.ebx = p->regs.ecx = p->regs.edx = 0;
    p->regs.esi = p->regs.edi = 0;
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
