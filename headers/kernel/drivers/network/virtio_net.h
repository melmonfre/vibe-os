#ifndef KERNEL_DRIVERS_NETWORK_VIRTIO_NET_H
#define KERNEL_DRIVERS_NETWORK_VIRTIO_NET_H

#include <stdint.h>

void kernel_virtio_net_init(void);
int kernel_virtio_net_present(void);
uint32_t kernel_virtio_net_mtu(void);

#endif
