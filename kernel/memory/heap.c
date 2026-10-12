#include <kernel/memory/heap.h>
#include <kernel/lock.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/debug/debug.h>

/*
 * Block layout: 16-byte header {size, magic, next, reserved} followed by an
 * 8-byte-aligned payload. `next` is only meaningful while a block is free.
 *
 * The magic turns heap-ownership bugs into named O(1) events:
 *  - malloc taking a block already marked ALLOC  => DOUBLE-ALLOC trace
 *    (leading hypothesis for the SMP task-list corruption);
 *  - free of a block not marked ALLOC            => FREE-UNTRACKED trace,
 *    and the free is skipped so one bad pointer cannot poison the list.
 *
 * The previous 4-byte header aliased its free-list `next` link over the
 * payload's first word, so "block->next = NULL" during free silently zeroed
 * the owner's first field (observed under SMP as the storage host's pid
 * becoming 0 while kind/state stayed intact).
 */
struct free_block {
    size_t size;
    size_t magic;
    struct free_block *next;
    size_t reserved;
};

#define HEAP_ALIGN 8u
#define HEAP_MIN_SPLIT 32u
#define ALIGNED_FLAG ((size_t)0x80000000u)
#define HEAP_MAGIC_FREE  ((size_t)0x46524545u) /* "FREE" */
#define HEAP_MAGIC_ALLOC ((size_t)0x414C4C4Fu) /* "ALLO" */
#define HEAP_HEADER_SIZE ((size_t)16u)
#define HEAP_MIN_BLOCK   (HEAP_HEADER_SIZE + HEAP_ALIGN)

/*
 * Free-list integrity diagnostics (COMPLETION_ACTION_PLAN P4). Budgeted so
 * a systemic bug cannot flood the serial line.
 */
#define HEAP_DIAG_BUDGET 6u
#define HEAP_FREELIST_WALK_CAP 4096u
static volatile uint32_t g_heap_diag_budget = HEAP_DIAG_BUDGET;

static void heap_diag(const char *what, uintptr_t block, size_t size, uintptr_t other) {
    if (g_heap_diag_budget == 0u) {
        return;
    }
    g_heap_diag_budget--;
    kernel_debug_printf("heap: %s block=0x%x size=0x%x other=0x%x\n",
                        what, (unsigned int)block, (unsigned int)size,
                        (unsigned int)other);
}

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
    uint32_t steps = 0u;

    while (curr) {
        total += curr->size;
        curr = curr->next;
        if (++steps > HEAP_FREELIST_WALK_CAP) {
            break;
        }
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
    g_free_list->magic = HEAP_MAGIC_FREE;
    g_free_list->next = NULL;
    g_free_list->reserved = 0u;
    g_heap_initialized = 1u;
}

void *kernel_malloc(size_t size) {
    struct free_block *prev, *curr, *best_prev, *best;
    size_t best_size, needed, align_sz;
    uint32_t flags;
    /*
     * Allocation-site shadow (COMPLETION_ACTION_PLAN P4): the caller PC is
     * parked in the header's reserved word. The scheduler BAD-NODE trace
     * dumps it, so a corrupt task-list node names the allocation that
     * owns the block without relying on fixed-address watchpoints (heap
     * addresses shift between runs even under -icount).
     */
    uintptr_t call_pc = (uintptr_t)__builtin_return_address(0);

    if (size == 0u)
        return NULL;

    if (g_heap_initialized == 0u)
        return NULL;

    align_sz = heap_align_up(size);
    needed = HEAP_HEADER_SIZE + align_sz;
    if (needed < HEAP_MIN_BLOCK)
        needed = HEAP_MIN_BLOCK;

    flags = spinlock_lock_irqsave(&g_heap_lock);

    best = NULL;
    best_prev = NULL;
    best_size = (size_t)-1;
    prev = NULL;
    curr = g_free_list;

    while (curr) {
        if (curr->magic != HEAP_MAGIC_FREE) {
            heap_diag("BAD-FREE-BLOCK", (uintptr_t)curr, curr->size, curr->magic);
            break;
        }
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

    if (best->magic == HEAP_MAGIC_ALLOC) {
        heap_diag("DOUBLE-ALLOC", (uintptr_t)best, best->size, 0u);
    }

    if (best->size - needed > HEAP_MIN_SPLIT) {
        struct free_block *rem = (struct free_block *)((uint8_t *)best + needed);

        rem->size = best->size - needed;
        rem->magic = HEAP_MAGIC_FREE;
        rem->next = best->next;
        rem->reserved = 0u;
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

    best->magic = HEAP_MAGIC_ALLOC;
    best->next = NULL;
    best->reserved = call_pc;
    g_heap_used += best->size;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return (void *)((uint8_t *)best + HEAP_HEADER_SIZE);
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
    if (size & (HEAP_ALIGN - 1u))
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));

    /*
     * Reserve two alignment windows: if raw happens to already be aligned
     * we must still shift by one `align` so the link word written at
     * aligned_addr - sizeof(size_t) can never alias the block's own size
     * header (the old code clobbered the header with ALIGNED_FLAG when
     * shift==0, and the later free then treated a 0x80000000-byte block as
     * free space).
     */
    raw = kernel_malloc(size + 2u * align + sizeof(size_t));
    if (!raw)
        return NULL;

    raw_addr = (uintptr_t)raw;
    aligned_addr = (raw_addr + align - 1u) & ~(align - 1u);
    if (aligned_addr == raw_addr) {
        aligned_addr += align;
    }
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

    block = (struct free_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);
    old_size = block->size - HEAP_HEADER_SIZE;

    if (size & (HEAP_ALIGN - 1u))
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));

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
        block = (struct free_block *)((uint8_t *)ptr - shift - HEAP_HEADER_SIZE);
    } else {
        block = (struct free_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);
    }

    free_size = block->size;
    flags = spinlock_lock_irqsave(&g_heap_lock);

    if (block->magic != HEAP_MAGIC_ALLOC) {
        heap_diag("FREE-UNTRACKED", (uintptr_t)block, free_size, block->magic);
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return;
    }

    if ((uintptr_t)block < g_heap_start ||
        (uintptr_t)block + free_size > g_heap_end ||
        free_size < HEAP_MIN_BLOCK ||
        (free_size & (HEAP_ALIGN - 1u)) != 0u) {
        heap_diag("BAD-BLOCK", (uintptr_t)block, free_size, g_heap_start);
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return;
    }

    block->magic = HEAP_MAGIC_FREE;
    block->reserved = 0u;

    prev = NULL;
    curr = g_free_list;
    {
        uint32_t steps = 0u;
        while (curr && (uintptr_t)curr < (uintptr_t)block) {
            if (curr == block) {
                heap_diag("DOUBLE-FREE", (uintptr_t)block, free_size, 0u);
                spinlock_unlock_irqrestore(&g_heap_lock, flags);
                return;
            }
            if (++steps > HEAP_FREELIST_WALK_CAP) {
                heap_diag("LIST-CYCLE", (uintptr_t)block, free_size, (uintptr_t)curr);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    if (prev && (uint8_t *)prev + prev->size > (uint8_t *)block) {
        heap_diag("OVERLAP-PREV", (uintptr_t)block, free_size, (uintptr_t)prev);
    }
    if (curr && (uint8_t *)block + free_size > (uint8_t *)curr) {
        heap_diag("OVERLAP-NEXT", (uintptr_t)block, free_size, (uintptr_t)curr);
    }

    block->next = curr;
    if (prev)
        prev->next = block;
    else
        g_free_list = block;

    if (curr && (uint8_t *)block + block->size == (uint8_t *)curr &&
        curr->magic == HEAP_MAGIC_FREE) {
        block->size += curr->size;
        block->next = curr->next;
    }

    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)block &&
        prev->magic == HEAP_MAGIC_FREE) {
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
