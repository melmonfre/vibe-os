#ifndef KERNEL_DRIVERS_AUDIO_HDA_H
#define KERNEL_DRIVERS_AUDIO_HDA_H

#include <stdint.h>
#include <stddef.h>

int kernel_hda_init(void);
void kernel_hda_irq_handler(void);
int kernel_hda_present(void);
int kernel_hda_playback_write(const uint8_t *data, uint32_t size);
int kernel_hda_ioctl(int cmd, void *arg, size_t arg_size);

#endif
