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
void scheduler_rsp_witness(uint32_t cpu_index);

/* Face-1 alias witness (class #5): returns the pid of a LIVE task whose
 * declared stack range [stack, stack+size) overlaps [lo, hi), or -1.
 * Called from kernel_malloc; read-only, capped walk, no locks. */
int scheduler_stack_overlap(uintptr_t lo, uintptr_t hi, uintptr_t *os_out,
                            uintptr_t *oe_out, int *state_out);
/* Face-1 per-cpu schedule_frame entry counter + death dump (class #5,
 * adendo 17): pair with smp_tick_counts_dump to locate the stall stage. */
uint32_t scheduler_frame_entry_count(uint32_t cpu_index);
void scheduler_frame_counts_dump(void);
/* Last-saved-frame ledger per cpu (adendo 28): dump at panic/#UD-halt to
 * compare the poisoned resume value against the last legit saves. */
void scheduler_save_ledger_dump(void);

/* enter the scheduler cooperatively from the current CPU context */
void schedule(void);

/* voluntarily relinquish the CPU (syscall/yield) */
void yield(void);

/* mark when timer-driven preemption may safely switch tasks */
void scheduler_set_preemption_ready(int ready);
void scheduler_set_task_events_ready(int ready);

/* schedule from an interrupt/yield trap and return the frame to resume */
kernel_trap_frame_t *scheduler_schedule_frame(kernel_trap_frame_t *frame, int preemptive);

/* return currently executing process (may be NULL) */
process_t *scheduler_current(void);
process_t *scheduler_current_for_cpu(uint32_t cpu_index);
uint32_t scheduler_current_pid(void);
uint32_t scheduler_current_pid_for_cpu(uint32_t cpu_index);
process_t *scheduler_find_task_by_pid(int pid);
void scheduler_terminate_task(process_t *task);
int scheduler_block_current(const void *wait_channel);
int scheduler_block_current_ex(const void *wait_channel,
                               uint32_t wait_deadline,
                               uint32_t wait_event_kind,
                               uint32_t wait_event_class,
                               uint32_t wait_owner_service);
int scheduler_wake_task(process_t *task);
int scheduler_task_is_linked(const process_t *proc);
int scheduler_complete_wait(process_t *task, uint32_t wait_result);
int scheduler_task_event_subscribe(process_t *subscriber,
                                   uint32_t event_mask,
                                   uint32_t task_class_mask);
int scheduler_task_event_receive(process_t *subscriber,
                                 struct mk_task_event *event,
                                 uint32_t timeout_ticks);
void scheduler_publish_lifecycle_event(uint32_t event_type, const process_t *task);
uint32_t scheduler_snapshot(struct task_snapshot_entry *entries,
                            uint32_t max_entries,
                            struct task_snapshot_summary *summary);

#endif /* KERNEL_SCHEDULER_H */
