#include <kernel/memory/heap.h>
#include <kernel/lock.h>
#include <kernel/kernel_string.h>

struct free_block {
    size_t size;
    struct free_block *next;
};

#define HEAP_ALIGN 8u
#define HEAP_MIN_SPLIT 32u
#define ALIGNED_FLAG ((size_t)0x80000000u)

static uintptr_t g_heap_start = 0u;
static uintptr_t g_heap_end = 0u;
static struct free_block *g_free_list = NULL;
static size_t g_heap_used = 0u;
static spinlock_t g_heap_lock;
static volatile uint32_t g_heap_initialized = 0u;

static size_t heap_align_up(size_t size) {
    if (size & (HEAP_ALIGN - 1u)) {
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));
    }
    return size;
}

static size_t heap_total_free(void) {
    size_t total = 0u;
    struct free_block *curr = g_free_list;

    while (curr) {
        total += curr->size;
        curr = curr->next;
    }
    return total;
}

void kernel_mm_init(uintptr_t heap_start, size_t heap_size) {
    spinlock_init(&g_heap_lock);
    g_heap_start = heap_start;
    g_heap_end = heap_start + heap_size;
    g_heap_used = 0u;
    g_free_list = (struct free_block *)(uintptr_t)heap_start;
    g_free_list->size = heap_size;
    g_free_list->next = NULL;
    g_heap_initialized = 1u;
}

void *kernel_malloc(size_t size) {
    struct free_block *prev, *curr, *best_prev, *best;
    size_t best_size, needed, align_sz;
    uint32_t flags;

    if (size == 0u)
        return NULL;

    if (g_heap_initialized == 0u)
        return NULL;

    align_sz = heap_align_up(size);
    needed = sizeof(size_t) + align_sz;
    if (needed < sizeof(struct free_block))
        needed = sizeof(struct free_block);

    flags = spinlock_lock_irqsave(&g_heap_lock);

    best = NULL;
    best_prev = NULL;
    best_size = (size_t)-1;
    prev = NULL;
    curr = g_free_list;

    while (curr) {
        if (curr->size >= needed && curr->size < best_size) {
            best = curr;
            best_prev = prev;
            best_size = curr->size;
            if (best_size == needed)
                break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (!best) {
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return NULL;
    }

    if (best->size - needed > HEAP_MIN_SPLIT) {
        struct free_block *rem = (struct free_block *)((uint8_t *)best + needed);

        rem->size = best->size - needed;
        rem->next = best->next;
        best->size = needed;
        if (best_prev)
            best_prev->next = rem;
        else
            g_free_list = rem;
    } else {
        if (best_prev)
            best_prev->next = best->next;
        else
            g_free_list = best->next;
    }

    g_heap_used += best->size;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return (void *)((uint8_t *)best + sizeof(size_t));
}

void *kernel_malloc_aligned(size_t size, size_t align) {
    void *raw;
    uintptr_t raw_addr, aligned_addr;
    size_t shift;

    if (size == 0u)
        return NULL;
    if (align == 0u)
        align = 8u;
    if ((align & (align - 1u)) != 0u)
        return NULL;
    if (size & 0x7u)
        size += 8u - (size & 0x7u);

    raw = kernel_malloc(size + align + sizeof(size_t));
    if (!raw)
        return NULL;

    raw_addr = (uintptr_t)raw;
    aligned_addr = (raw_addr + align - 1u) & ~(align - 1u);
    shift = aligned_addr - raw_addr;

    *(size_t *)(aligned_addr - sizeof(size_t)) = shift | ALIGNED_FLAG;
    return (void *)aligned_addr;
}

void *kernel_realloc(void *ptr, size_t size) {
    struct free_block *block;
    size_t old_size, copy_size;
    void *new_ptr;

    if (!ptr)
        return kernel_malloc(size);
    if (size == 0u) {
        kernel_free(ptr);
        return NULL;
    }

    block = (struct free_block *)((uint8_t *)ptr - sizeof(size_t));
    old_size = block->size - sizeof(size_t);

    if (size & 0x7u)
        size += 8u - (size & 0x7u);

    new_ptr = kernel_malloc(size);
    if (!new_ptr)
        return NULL;

    copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    kernel_free(ptr);
    return new_ptr;
}

void kernel_free(void *ptr) {
    struct free_block *block, *prev, *curr;
    size_t stored, free_size;
    uint32_t flags;

    if (!ptr)
        return;
    if (g_heap_initialized == 0u)
        return;

    stored = *(size_t *)((uint8_t *)ptr - sizeof(size_t));

    if (stored & ALIGNED_FLAG) {
        size_t shift = stored & ~ALIGNED_FLAG;
        block = (struct free_block *)((uint8_t *)ptr - shift - sizeof(size_t));
    } else {
        block = (struct free_block *)((uint8_t *)ptr - sizeof(size_t));
    }

    free_size = block->size;
    flags = spinlock_lock_irqsave(&g_heap_lock);

    prev = NULL;
    curr = g_free_list;
    while (curr && (uintptr_t)curr < (uintptr_t)block) {
        prev = curr;
        curr = curr->next;
    }

    block->next = curr;
    if (prev)
        prev->next = block;
    else
        g_free_list = block;

    if (curr && (uint8_t *)block + block->size == (uint8_t *)curr) {
        block->size += curr->size;
        block->next = curr->next;
    }

    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)block) {
        prev->size += block->size;
        prev->next = block->next;
    }

    g_heap_used -= free_size;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
}

uintptr_t kernel_heap_start(void) {
    return g_heap_start;
}

uintptr_t kernel_heap_end(void) {
    return g_heap_end;
}

size_t kernel_heap_used(void) {
    size_t value;
    uint32_t flags;

    if (g_heap_initialized == 0u)
        return 0u;
    flags = spinlock_lock_irqsave(&g_heap_lock);
    value = g_heap_used;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return value;
}

size_t kernel_heap_free(void) {
    size_t value;
    uint32_t flags;

    if (g_heap_initialized == 0u)
        return 0u;
    flags = spinlock_lock_irqsave(&g_heap_lock);
    value = heap_total_free();
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return value;
}

int kernel_heap_is_initialized(void) {
    return g_heap_initialized != 0u;
}
