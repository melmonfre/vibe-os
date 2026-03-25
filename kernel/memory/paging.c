#include <kernel/memory/paging.h>
#include <kernel/drivers/debug/debug.h>
#include <stddef.h>

#define PAGING_PAGE_DIR_ENTRIES 1024u
#define PAGING_4MB_PAGE_BYTES 0x00400000u
#define PAGING_PDE_PRESENT 0x001u
#define PAGING_PDE_WRITABLE 0x002u
#define PAGING_PDE_PAGE_SIZE 0x080u
#define PAGING_CR0_PG 0x80000000u
#define PAGING_CR4_PSE 0x00000010u

static uint32_t g_kernel_page_directory[PAGING_PAGE_DIR_ENTRIES] __attribute__((aligned(4096)));
static int g_paging_enabled = 0;

static void paging_flush_tlb(void) {
    uint32_t cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static int paging_large_region_params_valid(uintptr_t virtual_start, uintptr_t physical_start, size_t size) {
    return !(size == 0u ||
             (virtual_start & (PAGING_4MB_PAGE_BYTES - 1u)) != 0u ||
             (physical_start & (PAGING_4MB_PAGE_BYTES - 1u)) != 0u ||
             (size & (PAGING_4MB_PAGE_BYTES - 1u)) != 0u);
}

static int paging_large_region_virtual_valid(uintptr_t virtual_start, size_t size) {
    size_t page_count;

    if (size == 0u ||
        (virtual_start & (PAGING_4MB_PAGE_BYTES - 1u)) != 0u ||
        (size & (PAGING_4MB_PAGE_BYTES - 1u)) != 0u) {
        return 0;
    }
    page_count = size / PAGING_4MB_PAGE_BYTES;
    if (page_count == 0u) {
        return 0;
    }
    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        if (pde_index >= PAGING_PAGE_DIR_ENTRIES) {
            return 0;
        }
    }
    return 1;
}

void paging_init(void) {
    uint32_t cr0;
    uint32_t cr4;

    for (uint32_t i = 0; i < PAGING_PAGE_DIR_ENTRIES; ++i) {
        g_kernel_page_directory[i] = (i * PAGING_4MB_PAGE_BYTES) |
                                     PAGING_PDE_PRESENT |
                                     PAGING_PDE_WRITABLE |
                                     PAGING_PDE_PAGE_SIZE;
    }

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= PAGING_CR4_PSE;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)g_kernel_page_directory) : "memory");

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= PAGING_CR0_PG;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    g_paging_enabled = 1;
    kernel_debug_printf("paging: enabled cr3=%x span_mb=%d\n",
                        (uint32_t)(uintptr_t)g_kernel_page_directory,
                        (int)(PAGING_PAGE_DIR_ENTRIES * 4u));
}

int paging_is_enabled(void) {
    return g_paging_enabled;
}

uintptr_t paging_page_directory_phys(void) {
    return (uintptr_t)g_kernel_page_directory;
}

int paging_snapshot_large_region(uintptr_t virtual_start, size_t size, uint32_t *entries_out, size_t entry_capacity) {
    size_t page_count;

    if (!entries_out || !paging_large_region_virtual_valid(virtual_start, size)) {
        return -1;
    }
    page_count = size / PAGING_4MB_PAGE_BYTES;
    if (entry_capacity < page_count) {
        return -1;
    }

    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        entries_out[i] = g_kernel_page_directory[pde_index];
    }
    return 0;
}

int paging_map_large_region(uintptr_t virtual_start, uintptr_t physical_start, size_t size) {
    size_t page_count;

    if (!paging_large_region_params_valid(virtual_start, physical_start, size)) {
        return -1;
    }

    page_count = size / PAGING_4MB_PAGE_BYTES;
    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        if (pde_index >= PAGING_PAGE_DIR_ENTRIES) {
            return -1;
        }
    }

    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uintptr_t phys = physical_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        g_kernel_page_directory[pde_index] = (uint32_t)phys |
                                             PAGING_PDE_PRESENT |
                                             PAGING_PDE_WRITABLE |
                                             PAGING_PDE_PAGE_SIZE;
    }

    paging_flush_tlb();
    return 0;
}

int paging_restore_large_region(uintptr_t virtual_start, size_t size, const uint32_t *entries, size_t entry_capacity) {
    size_t page_count;

    if (!entries || !paging_large_region_virtual_valid(virtual_start, size)) {
        return -1;
    }
    page_count = size / PAGING_4MB_PAGE_BYTES;
    if (entry_capacity < page_count) {
        return -1;
    }

    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        g_kernel_page_directory[pde_index] = entries[i];
    }

    paging_flush_tlb();
    return 0;
}
