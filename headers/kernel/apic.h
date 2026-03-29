#ifndef KERNEL_APIC_H
#define KERNEL_APIC_H

#include <stdint.h>

void local_apic_init(void);
int local_apic_present(void);
int local_apic_enabled(void);
uint32_t local_apic_base(void);
uint32_t local_apic_id(void);
void local_apic_eoi(void);
int local_apic_send_ipi(uint32_t apic_id, uint8_t vector);
int local_apic_broadcast_ipi(uint8_t vector);
int local_apic_send_init(uint32_t apic_id);
int local_apic_send_startup(uint32_t apic_id, uint8_t vector);

#endif
