#ifndef KERNEL_MEMORY_PAGING_H
#define KERNEL_MEMORY_PAGING_H

#include <stddef.h>
#include <stdint.h>

void paging_init(void);
int paging_is_enabled(void);
uintptr_t paging_page_directory_phys(void);
int paging_snapshot_large_region(uintptr_t virtual_start, size_t size, uint32_t *entries_out, size_t entry_capacity);
int paging_map_large_region(uintptr_t virtual_start, uintptr_t physical_start, size_t size);
int paging_set_large_region_user_access(uintptr_t virtual_start, size_t size, int user_accessible);
int paging_restore_large_region(uintptr_t virtual_start, size_t size, const uint32_t *entries, size_t entry_capacity);
int paging_pat_wc_enabled(void);
int paging_set_framebuffer_wc(uintptr_t fb_addr, size_t size);

#endif /* KERNEL_MEMORY_PAGING_H */
