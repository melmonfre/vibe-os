#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

/* Initialize PIT timer at given frequency (Hz) */
void kernel_timer_init(uint32_t freq_hz);

/* Get current system ticks */
uint32_t kernel_timer_get_ticks(void);

/* IRQ0 handler (called from assembly) */
void kernel_timer_irq_handler(void);

/* Legacy PC speaker / buzzer control */
int kernel_timer_pc_speaker_available(void);
void kernel_timer_pc_speaker_set_frequency(uint32_t freq_hz);
void kernel_timer_pc_speaker_disable(void);

#endif /* KERNEL_TIMER_H */
