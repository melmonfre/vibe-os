#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdint.h>

#define SMP_IPI_WAKE_VECTOR 0x82u
#define SMP_IPI_SCHED_VECTOR 0x83u
#define SMP_IPI_TLB_SHOOTDOWN_VECTOR 0x84u

void smp_init(void);
uint32_t smp_started_cpu_count(void);
uint32_t smp_cpu_count(void);
void smp_ap_entry(void);
void smp_scheduler_enable(void);
int smp_scheduler_enabled(void);
void smp_wakeup_ipi_handler(void);
void smp_wake_sleeping_cpus(void);
uint32_t smp_cpu_stage(uint32_t cpu_index);
uint8_t smp_trampoline_debug_stage(void);
void smp_persist_trace_arm(void);
void smp_persist_trace_mark(uint8_t code);
void smp_persist_trace_disarm(void);
void smp_print_topology(void);

typedef void (*smp_ipi_handler_t)(void);
int smp_register_ipi_handler(uint8_t vector, smp_ipi_handler_t handler);
int smp_unregister_ipi_handler(uint8_t vector);
void smp_ipi_dispatch(uint8_t vector);

#endif
