#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/process.h>
#include <stdint.h>
#include <include/userland_api.h>

/* scheduler: cooperative round-robin over a linked list of processes */

/* initialize scheduler data structures - call once during boot */
void scheduler_init(void);

/* add a newly created process to the run queue */
void scheduler_add_task(process_t *proc);

/* perform a context switch to the next ready task */
void schedule(void);

/* voluntarily relinquish the CPU (syscall/yield) */
void yield(void);

/* return currently executing process (may be NULL) */
process_t *scheduler_current(void);
process_t *scheduler_current_for_cpu(uint32_t cpu_index);
process_t *scheduler_find_task_by_pid(int pid);
void scheduler_terminate_task(process_t *task);
uint32_t scheduler_snapshot(struct task_snapshot_entry *entries,
                            uint32_t max_entries,
                            struct task_snapshot_summary *summary);

#endif /* KERNEL_SCHEDULER_H */
