#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stdint.h>
#include <stddef.h>

void kernel_mm_init(uintptr_t heap_start, size_t heap_size);
void *kernel_malloc(size_t size);
void *kernel_malloc_aligned(size_t size, size_t align);
void *kernel_realloc(void *ptr, size_t size);
void kernel_free(void *ptr);
uintptr_t kernel_heap_start(void);
uintptr_t kernel_heap_end(void);
size_t kernel_heap_used(void);
size_t kernel_heap_free(void);
int kernel_heap_is_initialized(void);
/* Membership probe: 1 iff ptr is the payload of a LIVE allocated block
 * (inside the heap window, 8-aligned, header magic ALLOC with sane size).
 * Used by the scheduler walk to reject wild pointers before following them. */
int kernel_heap_is_block(const void *ptr);

/* public-facing API (aliases) */
static inline void memory_init(uintptr_t start, size_t size) {
    kernel_mm_init(start, size);
}

static inline void *kmalloc(size_t sz) {
    return kernel_malloc(sz);
}

static inline void *kmalloc_aligned(size_t sz, size_t align) {
    return kernel_malloc_aligned(sz, align);
}

static inline void *krealloc(void *ptr, size_t sz) {
    return kernel_realloc(ptr, sz);
}

static inline void kfree(void *ptr) {
    kernel_free(ptr);
}

#endif /* KERNEL_MEMORY_HEAP_H */
