#ifndef KERNEL_DRIVERS_PCI_PCI_DMA_H
#define KERNEL_DRIVERS_PCI_PCI_DMA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Device DMA service: physical-continuous buffers the kernel and the
 * device can both reach (identity mapping), plus the translation used to
 * publish a kernel buffer to a device. Failures are explicit (-1); no
 * call ever returns a wrong address.
 */

/* Allocate a physically continuous buffer. align <= 4096; zero requests a
 * zeroed buffer. Returns 0 on success and fills phys/vaddr. */
int kernel_dma_alloc(size_t size, size_t align, int zero,
                     uintptr_t *phys_out, void **vaddr_out);

/* Release a buffer obtained from kernel_dma_alloc. */
void kernel_dma_free(uintptr_t phys, size_t size);

/* Map/unmap a physical range for kernel access. */
int kernel_dma_map(uintptr_t phys, size_t size, void **vaddr_out);
void kernel_dma_unmap(void *vaddr, size_t size);

/* Translate a kernel address into a device-reachable physical address.
 * Returns -1 when the address is outside the identity-mapped window. */
int kernel_dma_translate(uintptr_t vaddr, uintptr_t *phys_out);

#endif /* KERNEL_DRIVERS_PCI_PCI_DMA_H */
