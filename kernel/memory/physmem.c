#include <kernel/bootinfo.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/memory/physmem.h>

#define PHYSMEM_FALLBACK_BASE 0x00500000u
#define PHYSMEM_FALLBACK_END 0x00900000u
#define PHYSMEM_MIN_BASE 0x00100000u
#define PHYSMEM_MAX_TRACKED_PAGES ((size_t)(PHYSMEM_DYNAMIC_CAP_BYTES / PHYSMEM_PAGE_SIZE))
#define PHYSMEM_BITMAP_WORD_BITS 32u

static uintptr_t g_physmem_base = PHYSMEM_FALLBACK_BASE;
static uintptr_t g_physmem_end = PHYSMEM_FALLBACK_END;
static size_t g_physmem_page_count = 0u;
static size_t g_physmem_free_page_count = 0u;
static uint32_t g_physmem_bitmap[(PHYSMEM_MAX_TRACKED_PAGES + PHYSMEM_BITMAP_WORD_BITS - 1u) /
                                 PHYSMEM_BITMAP_WORD_BITS];

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return value & ~(align - 1u);
}

static void physmem_mark_page_used(size_t page_index) {
    uint32_t *word;
    uint32_t mask;

    if (page_index >= g_physmem_page_count) {
        return;
    }
    word = &g_physmem_bitmap[page_index / PHYSMEM_BITMAP_WORD_BITS];
    mask = 1u << (page_index % PHYSMEM_BITMAP_WORD_BITS);
    if ((*word & mask) == 0u) {
        *word |= mask;
        if (g_physmem_free_page_count > 0u) {
            g_physmem_free_page_count -= 1u;
        }
    }
}

static void physmem_mark_page_free(size_t page_index) {
    uint32_t *word;
    uint32_t mask;

    if (page_index >= g_physmem_page_count) {
        return;
    }
    word = &g_physmem_bitmap[page_index / PHYSMEM_BITMAP_WORD_BITS];
    mask = 1u << (page_index % PHYSMEM_BITMAP_WORD_BITS);
    if ((*word & mask) != 0u) {
        *word &= ~mask;
        g_physmem_free_page_count += 1u;
    }
}

void physmem_init(void) {
    const struct bootinfo *info = (const struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    uintptr_t base = PHYSMEM_FALLBACK_BASE;
    uintptr_t end = PHYSMEM_FALLBACK_END;
    int used_bootinfo = 0;
    size_t word_count;

    if (info->magic == BOOTINFO_MAGIC &&
        info->version == BOOTINFO_VERSION &&
        (info->flags & BOOTINFO_FLAG_MEMINFO_VALID) != 0u) {
        base = (uintptr_t)info->meminfo.largest_base;
        end = (uintptr_t)info->meminfo.largest_end;

        if (base < PHYSMEM_MIN_BASE) {
            base = PHYSMEM_MIN_BASE;
        }
        if (end > PHYSMEM_MAX_PHYS_ADDR) {
            end = PHYSMEM_MAX_PHYS_ADDR;
        }
        base = align_up_uintptr(base, 0x1000u);
        end = align_down_uintptr(end, 0x1000u);
        if (end <= base) {
            base = PHYSMEM_FALLBACK_BASE;
            end = PHYSMEM_FALLBACK_END;
        } else {
            used_bootinfo = 1;
        }
    }

    g_physmem_base = base;
    g_physmem_end = end;
    g_physmem_page_count = (size_t)((g_physmem_end - g_physmem_base) / PHYSMEM_PAGE_SIZE);
    if (g_physmem_page_count > PHYSMEM_MAX_TRACKED_PAGES) {
        g_physmem_page_count = PHYSMEM_MAX_TRACKED_PAGES;
        g_physmem_end = g_physmem_base + (g_physmem_page_count * PHYSMEM_PAGE_SIZE);
    }
    word_count = (g_physmem_page_count + PHYSMEM_BITMAP_WORD_BITS - 1u) / PHYSMEM_BITMAP_WORD_BITS;
    for (size_t i = 0; i < word_count; ++i) {
        g_physmem_bitmap[i] = 0u;
    }
    g_physmem_free_page_count = g_physmem_page_count;
    kernel_debug_printf("physmem: base=%x end=%x size_mb=%d pages=%d source=%s\n",
                        (uint32_t)g_physmem_base,
                        (uint32_t)g_physmem_end,
                        (int)(physmem_usable_size() / (1024u * 1024u)),
                        (int)g_physmem_page_count,
                        used_bootinfo ? "bootinfo" : "fallback");
}

uintptr_t physmem_usable_base(void) {
    return g_physmem_base;
}

uintptr_t physmem_usable_end(void) {
    return g_physmem_end;
}

size_t physmem_usable_size(void) {
    if (g_physmem_end <= g_physmem_base) {
        return 0u;
    }
    return (size_t)(g_physmem_end - g_physmem_base);
}

size_t physmem_total_pages(void) {
    return g_physmem_page_count;
}

size_t physmem_free_pages(void) {
    return g_physmem_free_page_count;
}

void physmem_reserve_range(uintptr_t start, size_t size) {
    uintptr_t begin;
    uintptr_t end;

    if (size == 0u) {
        return;
    }
    begin = align_down_uintptr(start, PHYSMEM_PAGE_SIZE);
    end = align_up_uintptr(start + size, PHYSMEM_PAGE_SIZE);
    if (end <= g_physmem_base || begin >= g_physmem_end) {
        return;
    }
    if (begin < g_physmem_base) {
        begin = g_physmem_base;
    }
    if (end > g_physmem_end) {
        end = g_physmem_end;
    }
    for (uintptr_t addr = begin; addr < end; addr += PHYSMEM_PAGE_SIZE) {
        physmem_mark_page_used((size_t)((addr - g_physmem_base) / PHYSMEM_PAGE_SIZE));
    }
}

void physmem_release_range(uintptr_t start, size_t size) {
    uintptr_t begin;
    uintptr_t end;

    if (size == 0u) {
        return;
    }
    begin = align_down_uintptr(start, PHYSMEM_PAGE_SIZE);
    end = align_up_uintptr(start + size, PHYSMEM_PAGE_SIZE);
    if (end <= g_physmem_base || begin >= g_physmem_end) {
        return;
    }
    if (begin < g_physmem_base) {
        begin = g_physmem_base;
    }
    if (end > g_physmem_end) {
        end = g_physmem_end;
    }
    for (uintptr_t addr = begin; addr < end; addr += PHYSMEM_PAGE_SIZE) {
        physmem_mark_page_free((size_t)((addr - g_physmem_base) / PHYSMEM_PAGE_SIZE));
    }
}

void *alloc_phys_page(void) {
    size_t word_count = (g_physmem_page_count + PHYSMEM_BITMAP_WORD_BITS - 1u) / PHYSMEM_BITMAP_WORD_BITS;

    for (size_t word_index = 0; word_index < word_count; ++word_index) {
        uint32_t word = g_physmem_bitmap[word_index];
        if (word == 0xFFFFFFFFu) {
            continue;
        }
        for (size_t bit = 0; bit < PHYSMEM_BITMAP_WORD_BITS; ++bit) {
            size_t page_index = (word_index * PHYSMEM_BITMAP_WORD_BITS) + bit;
            if (page_index >= g_physmem_page_count) {
                break;
            }
            if ((word & (1u << bit)) == 0u) {
                physmem_mark_page_used(page_index);
                return (void *)(g_physmem_base + (page_index * PHYSMEM_PAGE_SIZE));
            }
        }
    }
    return NULL;
}

void free_phys_page(void *page) {
    uintptr_t addr = (uintptr_t)page;

    if (addr < g_physmem_base || addr >= g_physmem_end) {
        return;
    }
    if ((addr & (PHYSMEM_PAGE_SIZE - 1u)) != 0u) {
        return;
    }
    physmem_mark_page_free((size_t)((addr - g_physmem_base) / PHYSMEM_PAGE_SIZE));
}
