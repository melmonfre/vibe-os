#ifndef KERNEL_MEMORY_PAGING_H
#define KERNEL_MEMORY_PAGING_H

#include <stddef.h>
#include <stdint.h>

void paging_init(void);
int paging_is_enabled(void);
uintptr_t paging_page_directory_phys(void);
int paging_snapshot_large_region(uintptr_t virtual_start, size_t size, uint32_t *entries_out, size_t entry_capacity);
int paging_map_large_region(uintptr_t virtual_start, uintptr_t physical_start, size_t size);
int paging_restore_large_region(uintptr_t virtual_start, size_t size, const uint32_t *entries, size_t entry_capacity);

#endif /* KERNEL_MEMORY_PAGING_H */
