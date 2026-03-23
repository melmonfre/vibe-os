#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>  /* memcpy/memset helpers */

#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <kernel/kernel.h>    /* for panic, if needed */
#include <kernel/lock.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>
#include <kernel/drivers/debug/debug.h>

/* prototype for the low‑level context switch routine (implemented in assembly) */
extern void context_switch(void *old_task, void *new_task);

/* simple singly‑linked list of processes */
static process_t *g_head = NULL;
static process_t *g_current[32];
static process_t *g_cursor[32];
static spinlock_t g_scheduler_lock;
static int g_scheduler_boot_trace_emitted = 0;
static int g_scheduler_switch_trace_budget = 24;

static void scheduler_trace_switch(uint32_t cpu_index,
                                   const process_t *old_task,
                                   const process_t *next_task) {
    if (g_scheduler_switch_trace_budget <= 0 || next_task == NULL) {
        return;
    }

    g_scheduler_switch_trace_budget -= 1;
    kernel_debug_printf("scheduler: cpu=%d old=%d/%d next=%d/%d state=%d\n",
                        (int)cpu_index,
                        old_task != NULL ? old_task->pid : -1,
                        old_task != NULL ? (int)old_task->service_type : -1,
                        next_task->pid,
                        (int)next_task->service_type,
                        (int)next_task->state);
}

static uint32_t scheduler_online_cpu_count(void) {
    uint32_t count = smp_started_cpu_count();

    if (count == 0u) {
        count = 1u;
    }
    if (count > kernel_cpu_count()) {
        count = kernel_cpu_count();
    }
    if (count == 0u) {
        count = 1u;
    }
    return count;
}

static uint32_t scheduler_cpu_load(uint32_t cpu_index) {
    process_t *task;
    uint32_t load = 0u;

    for (task = g_head; task != NULL; task = task->next) {
        if (task->state == PROCESS_TERMINATED) {
            continue;
        }
        if (task->current_cpu == (int)cpu_index || task->preferred_cpu == (int)cpu_index) {
            load += 1u;
        }
    }
    return load;
}

static int scheduler_pick_target_cpu(void) {
    uint32_t cpu_count = scheduler_online_cpu_count();
    uint32_t best_cpu = 0u;
    uint32_t best_load = 0u;
    uint32_t cpu;

    for (cpu = 0u; cpu < cpu_count; ++cpu) {
        uint32_t load = scheduler_cpu_load(cpu);

        if (cpu == 0u || load < best_load) {
            best_cpu = cpu;
            best_load = load;
        }
    }

    return (int)best_cpu;
}

static process_t *scheduler_first_runnable(void) {
    process_t *task;

    for (task = g_head; task != NULL; task = task->next) {
        if (task->state == PROCESS_READY && task->current_cpu < 0) {
            return task;
        }
    }
    return NULL;
}

static int scheduler_task_score(const process_t *task, uint32_t cpu_index) {
    if (task->preferred_cpu == (int)cpu_index) {
        return 0;
    }
    if (task->last_cpu == (int)cpu_index) {
        return 1;
    }
    if (task->preferred_cpu < 0) {
        return 2;
    }
    return 3;
}

void scheduler_init(void) {
    uint32_t i;

    g_head = NULL;
    for (i = 0; i < 32u; ++i) {
        g_current[i] = NULL;
        g_cursor[i] = NULL;
    }
    spinlock_init(&g_scheduler_lock);
}

void scheduler_add_task(process_t *proc) {
    uint32_t flags;

    if (proc == NULL) {
        return;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    proc->next = NULL;
    proc->state = PROCESS_READY;
    proc->current_cpu = -1;
    proc->last_cpu = -1;
    proc->preferred_cpu = scheduler_pick_target_cpu();
    if (g_head == NULL) {
        g_head = proc;
        g_cursor[proc->preferred_cpu >= 0 ? (uint32_t)proc->preferred_cpu : 0u] = proc;
    } else {
        process_t *p = g_head;
        while (p->next) {
            p = p->next;
        }
        p->next = proc;
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
}

static process_t *find_next(uint32_t cpu_index, process_t *current) {
    process_t *start;
    process_t *task;
    process_t *best = NULL;
    int best_score = 0x7fffffff;

    if (g_head == NULL) {
        return NULL;
    }

    start = g_cursor[cpu_index];
    if (start == NULL) {
        start = current != NULL && current->next != NULL ? current->next : g_head;
    }
    if (start == NULL) {
        start = scheduler_first_runnable();
    }
    if (start == NULL) {
        return NULL;
    }

    task = start;
    do {
        if (task->state == PROCESS_READY && task->current_cpu < 0) {
            int score = scheduler_task_score(task, cpu_index);

            if (best == NULL || score < best_score) {
                best = task;
                best_score = score;
                if (score == 0) {
                    break;
                }
            }
        }

        task = task->next != NULL ? task->next : g_head;
    } while (task != NULL && task != start);

    return best;
}

void schedule(void) {
    uint32_t cpu_index = kernel_cpu_index();
    process_t *current;
    process_t *next;
    uint32_t flags = spinlock_lock_irqsave(&g_scheduler_lock);

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    current = g_current[cpu_index];
    if (current && current->state == PROCESS_RUNNING && current->current_cpu == (int)cpu_index) {
        current->state = PROCESS_READY;
        current->current_cpu = -1;
        current->last_cpu = (int)cpu_index;
    }
    next = find_next(cpu_index, current);
    if (next == NULL) {
        if (current && current->state == PROCESS_READY) {
            current->state = PROCESS_RUNNING;
            current->current_cpu = (int)cpu_index;
            current->last_cpu = (int)cpu_index;
            }
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return; /* nothing to do */
    }

    g_cursor[cpu_index] = next->next != NULL ? next->next : g_head;

    if (current == NULL) {
        /* first switch into user/task context */
        next->state = PROCESS_RUNNING;
        next->current_cpu = (int)cpu_index;
        next->last_cpu = (int)cpu_index;
        g_current[cpu_index] = next;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        if (!g_scheduler_boot_trace_emitted) {
            g_scheduler_boot_trace_emitted = 1;
            kernel_debug_printf("scheduler: first dispatch cpu=%d pid=%d kind=%d service=%d eip=%x esp=%x\n",
                                (int)cpu_index,
                                next->pid,
                                (int)next->kind,
                                (int)next->service_type,
                                (unsigned int)next->regs.eip,
                                (unsigned int)next->regs.esp);
        }
        scheduler_trace_switch(cpu_index, NULL, next);
        context_switch(NULL, next);
    } else if (next != current) {
        process_t *old = current;
        next->state = PROCESS_RUNNING;
        next->current_cpu = (int)cpu_index;
        next->last_cpu = (int)cpu_index;
        g_current[cpu_index] = next;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        scheduler_trace_switch(cpu_index, old, next);
        context_switch(old, next);
    } else {
        current->state = PROCESS_RUNNING;
        current->current_cpu = (int)cpu_index;
        current->last_cpu = (int)cpu_index;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    }
}

void yield(void) {
    schedule();
}

process_t *scheduler_current(void) {
    return scheduler_current_for_cpu(kernel_cpu_index());
}

process_t *scheduler_current_for_cpu(uint32_t cpu_index) {
    process_t *current;
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return current;
}

process_t *scheduler_find_task_by_pid(int pid) {
    process_t *task;
    uint32_t flags;

    if (pid <= 0) {
        return NULL;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        if (task->pid == pid && task->state != PROCESS_TERMINATED) {
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return task;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return NULL;
}

void scheduler_terminate_task(process_t *task) {
    uint32_t flags;
    uint32_t cpu;

    if (task == NULL) {
        return;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    task->state = PROCESS_TERMINATED;
    task->current_cpu = -1;
    task->preferred_cpu = -1;
    task->last_cpu = -1;

    for (cpu = 0u; cpu < 32u; ++cpu) {
        if (g_current[cpu] == task) {
            g_current[cpu] = NULL;
        }
        if (g_cursor[cpu] == task) {
            g_cursor[cpu] = task->next != NULL ? task->next : g_head;
            if (g_cursor[cpu] == task) {
                g_cursor[cpu] = NULL;
            }
        }
    }

    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
}
