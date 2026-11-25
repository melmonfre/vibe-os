/*
 * Device DMA service (PCI enabling layer).
 *
 * The BSD driver imports expect a bus_space/bus_dma pair: a buffer the
 * device can reach (physical address) plus a kernel-side pointer to the
 * same bytes. VibeOS maps physical memory 1:1 (physmem hands out identity
 * addresses), so a physically continuous buffer lives at the same address
 * for the kernel and for the device — that is the whole contract this
 * service offers today. There is no IOMMU and no bounce layer; when a
 * caller needs something this cannot provide (alignment beyond a page, a
 * virtual buffer outside the identity window), the call fails with an
 * explicit diagnostic instead of publishing a wrong address.
 *
 * This lives in the kernel (not inside the USB glue) so that every
 * extracted driver — USB, network, audio — shares one DMA policy instead
 * of each one inventing its own.
 */

#include <stddef.h>
#include <stdint.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>

static void kernel_dma_zero(void *p, size_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;

    while (n-- > 0u) {
        *b++ = 0u;
    }
}

static int kernel_dma_in_window(uintptr_t addr, size_t size) {
    uintptr_t base = physmem_usable_base();
    uintptr_t end = physmem_usable_end();

    if (addr < base || end <= addr) {
        return 0;
    }
    if (size > (size_t)(end - addr)) {
        return 0;
    }
    return 1;
}

/*
 * Allocate a physically continuous buffer for device use.
 *
 * align must not exceed a page: the allocator returns page-granular
 * physical memory, and without a bounce layer an over-aligned request
 * cannot be satisfied honestly.
 */
int kernel_dma_alloc(size_t size, size_t align, int zero,
                     uintptr_t *phys_out, void **vaddr_out) {
    size_t npages;
    void *p;

    if (size == 0u || phys_out == NULL || vaddr_out == NULL) {
        return -1;
    }
    if (align > PHYSMEM_PAGE_SIZE) {
        kernel_debug_printf("dma: alloc align=%u unsupported (max %u)\n",
            (unsigned int)align, (unsigned int)PHYSMEM_PAGE_SIZE);
        return -1;
    }

    npages = (size + PHYSMEM_PAGE_SIZE - 1u) / PHYSMEM_PAGE_SIZE;
    p = alloc_phys_pages(npages);
    if (p == NULL) {
        kernel_debug_printf("dma: alloc %u bytes failed\n",
            (unsigned int)size);
        return -1;
    }

    if (zero) {
        kernel_dma_zero(p, size);
    }

    *phys_out = (uintptr_t)p;
    *vaddr_out = p;
    return 0;
}

void kernel_dma_free(uintptr_t phys, size_t size) {
    size_t npages;

    if (phys == 0u) {
        return;
    }
    npages = (size + PHYSMEM_PAGE_SIZE - 1u) / PHYSMEM_PAGE_SIZE;
    free_phys_pages((void *)phys, npages);
}

/* Map a physical range for kernel access (identity mapping). */
int kernel_dma_map(uintptr_t phys, size_t size, void **vaddr_out) {
    if (vaddr_out == NULL || !kernel_dma_in_window(phys, size)) {
        return -1;
    }
    *vaddr_out = (void *)phys;
    return 0;
}

void kernel_dma_unmap(void *vaddr, size_t size) {
    (void)vaddr;
    (void)size;
}

/*
 * Translate a kernel address into the physical address a device can use.
 * Only the identity-mapped physical window is translatable today; anything
 * else is reported as untranslatable instead of being faked.
 */
int kernel_dma_translate(uintptr_t vaddr, uintptr_t *phys_out) {
    if (phys_out == NULL) {
        return -1;
    }
    if (!kernel_dma_in_window(vaddr, 1u)) {
        kernel_debug_printf("dma: vaddr %x outside the identity window\n",
            (unsigned int)vaddr);
        return -1;
    }
    *phys_out = vaddr;
    return 0;
}
