#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/scheduler.h>

static volatile uint32_t g_kernel_ticks = 0u;
static volatile uint32_t g_timer_trace_budget = 8u;

uint32_t kernel_timer_get_ticks(void) {
    uint32_t flags = kernel_irq_save();
    uint32_t ticks = g_kernel_ticks;
    kernel_irq_restore(flags);
    return ticks;
}

kernel_trap_frame_t *kernel_timer_irq_handler(kernel_trap_frame_t *frame) {
    g_kernel_ticks += 1u;
    if (g_timer_trace_budget != 0u && (g_kernel_ticks % 500u) == 0u) {
        g_timer_trace_budget -= 1u;
        kernel_debug_printf("timer: tick=%d\n", (int)g_kernel_ticks);
    }
    kernel_pic_send_eoi(0);
    return scheduler_schedule_frame(frame, 1);
}

int kernel_timer_pc_speaker_available(void) {
    return 1;
}

void kernel_timer_pc_speaker_set_frequency(uint32_t freq_hz) {
    uint32_t divisor;
    uint8_t speaker_state;

    if (freq_hz < 20u || freq_hz > 20000u) {
        kernel_timer_pc_speaker_disable();
        return;
    }

    divisor = 1193182u / freq_hz;
    if (divisor == 0u) {
        divisor = 1u;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    /* Channel 2 drives the legacy PC speaker / notebook buzzer. */
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFFu));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFFu));
    speaker_state = inb(0x61);
    outb(0x61, (uint8_t)(speaker_state | 0x03u));
}

void kernel_timer_pc_speaker_disable(void) {
    uint8_t speaker_state = inb(0x61);

    outb(0x61, (uint8_t)(speaker_state & (uint8_t)~0x03u));
}

void kernel_timer_init(uint32_t freq_hz) {
    uint32_t divisor;

    if (freq_hz == 0u) {
        freq_hz = 100u;
    }

    divisor = 1193182u / freq_hz;
    if (divisor == 0u) {
        divisor = 1u;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    /* Program the PIT (Programmable Interval Timer) */
    outb(0x43, 0x36);  /* Control word: channel 0, lobyte/hibyte, mode 3, binary */
    outb(0x40, (uint8_t)(divisor & 0xFFu));       /* LSB */
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu)); /* MSB */
}
