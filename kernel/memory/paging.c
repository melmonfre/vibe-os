#include <kernel/memory/paging.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <stddef.h>

#define PAGING_PAGE_DIR_ENTRIES 1024u
#define PAGING_4MB_PAGE_BYTES 0x00400000u
#define PAGING_PDE_PRESENT 0x001u
#define PAGING_PDE_WRITABLE 0x002u
#define PAGING_PDE_USER 0x004u
#define PAGING_PDE_PWT 0x008u
#define PAGING_PDE_PCD 0x010u
#define PAGING_PDE_PAGE_SIZE 0x080u
#define PAGING_PDE_PAT_LARGE 0x1000u
#define PAGING_CR0_PG 0x80000000u
#define PAGING_CR4_PSE 0x00000010u
#define PAGING_MSR_IA32_PAT 0x277u
#define PAGING_PAT_ENTRY_WB 0x06u
#define PAGING_PAT_ENTRY_WC 0x01u

static uint32_t g_kernel_page_directory[PAGING_PAGE_DIR_ENTRIES] __attribute__((aligned(4096)));
static int g_paging_enabled = 0;
static int g_pat_wc_enabled = 0;
static uint32_t g_fb_wc_first_pde = PAGING_PAGE_DIR_ENTRIES;
static uint32_t g_fb_wc_pde_count = 0u;

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

static uintptr_t paging_align_down(uintptr_t value, uintptr_t align) {
    return value & ~(align - 1u);
}

static uintptr_t paging_align_up(uintptr_t value, uintptr_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static uint64_t paging_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdmsr"
                     : "=a"(low), "=d"(high)
                     : "c"(msr)
                     : "memory");
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static void paging_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t high = (uint32_t)(value >> 32);

    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(low), "d"(high)
                     : "memory");
}

static void paging_restore_default_pde_range(uint32_t first_pde, uint32_t page_count) {
    for (uint32_t i = 0u; i < page_count; ++i) {
        uint32_t pde_index = first_pde + i;

        if (pde_index >= PAGING_PAGE_DIR_ENTRIES) {
            break;
        }

        g_kernel_page_directory[pde_index] =
            (pde_index * PAGING_4MB_PAGE_BYTES) |
            PAGING_PDE_PRESENT |
            PAGING_PDE_WRITABLE |
            PAGING_PDE_PAGE_SIZE;
    }
}

static void paging_init_pat(void) {
    uint64_t pat_value;
    uint64_t updated_value;

    if (!kernel_cpu_has_pat()) {
        kernel_debug_puts("paging: PAT unsupported, leaving framebuffer cache policy default\n");
        return;
    }

    pat_value = paging_rdmsr(PAGING_MSR_IA32_PAT);
    updated_value = (pat_value & ~(0xFFull << 8)) | ((uint64_t)PAGING_PAT_ENTRY_WC << 8);
    if (updated_value != pat_value) {
        paging_wrmsr(PAGING_MSR_IA32_PAT, updated_value);
    }

    g_pat_wc_enabled = 1;
    kernel_debug_printf("paging: pat wc slot=%d msr=%x\n",
                        1,
                        (uint32_t)updated_value);
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
    paging_init_pat();
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

int paging_set_large_region_user_access(uintptr_t virtual_start, size_t size, int user_accessible) {
    size_t page_count;

    if (!paging_large_region_virtual_valid(virtual_start, size)) {
        return -1;
    }

    page_count = size / PAGING_4MB_PAGE_BYTES;
    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        if ((g_kernel_page_directory[pde_index] & PAGING_PDE_PRESENT) == 0u) {
            return -1;
        }
    }

    for (size_t i = 0; i < page_count; ++i) {
        uintptr_t virt = virtual_start + (i * PAGING_4MB_PAGE_BYTES);
        uint32_t pde_index = (uint32_t)(virt / PAGING_4MB_PAGE_BYTES);

        if (user_accessible) {
            g_kernel_page_directory[pde_index] |= PAGING_PDE_USER;
        } else {
            g_kernel_page_directory[pde_index] &= ~PAGING_PDE_USER;
        }
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

int paging_pat_wc_enabled(void) {
    return g_pat_wc_enabled;
}

int paging_set_framebuffer_wc(uintptr_t fb_addr, size_t size) {
    uintptr_t start;
    uintptr_t end;
    uint32_t first_pde;
    uint32_t page_count;

    if (!g_paging_enabled || !g_pat_wc_enabled || fb_addr == 0u || size == 0u) {
        return -1;
    }

    start = paging_align_down(fb_addr, PAGING_4MB_PAGE_BYTES);
    end = paging_align_up(fb_addr + size, PAGING_4MB_PAGE_BYTES);
    if (end <= start) {
        return -1;
    }

    first_pde = (uint32_t)(start / PAGING_4MB_PAGE_BYTES);
    page_count = (uint32_t)((end - start) / PAGING_4MB_PAGE_BYTES);
    if (first_pde >= PAGING_PAGE_DIR_ENTRIES || (first_pde + page_count) > PAGING_PAGE_DIR_ENTRIES) {
        return -1;
    }

    if (g_fb_wc_pde_count != 0u) {
        paging_restore_default_pde_range(g_fb_wc_first_pde, g_fb_wc_pde_count);
    }

    for (uint32_t i = 0u; i < page_count; ++i) {
        uint32_t pde_index = first_pde + i;

        g_kernel_page_directory[pde_index] =
            (pde_index * PAGING_4MB_PAGE_BYTES) |
            PAGING_PDE_PRESENT |
            PAGING_PDE_WRITABLE |
            PAGING_PDE_PAGE_SIZE |
            PAGING_PDE_PWT;
    }

    g_fb_wc_first_pde = first_pde;
    g_fb_wc_pde_count = page_count;
    paging_flush_tlb();
    kernel_debug_printf("paging: framebuffer wc fb=%x size=%d pde=%d count=%d\n",
                        (uint32_t)fb_addr,
                        (int)size,
                        (int)first_pde,
                        (int)page_count);
    return 0;
}
