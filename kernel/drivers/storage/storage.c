#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ahci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>

void kernel_storage_init(void) {
    kernel_block_device_reset();

    if (kernel_ahci_init() == 0) {
        kernel_debug_puts("storage: using ahci backend\n");
        return;
    }
    if (kernel_ata_init() == 0) {
        kernel_debug_puts("storage: using ata backend\n");
        return;
    }

    kernel_debug_puts("storage: no block device backend available\n");
}
