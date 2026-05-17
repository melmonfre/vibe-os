#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ahci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/storage/usb_compat.h>
#include <kernel/drivers/storage/usb_mass_storage.h>
#include <kernel/drivers/video/video.h>

void kernel_storage_init(void) {
    kernel_block_device_reset();

    kernel_text_puts("  storage: ahci?\n");
    if (kernel_ahci_init() == 0) {
        kernel_text_puts("  storage: ahci ok\n");
        kernel_debug_puts("storage: using ahci backend\n");
        return;
    }
    kernel_text_puts("  storage: ata?\n");
    if (kernel_ata_init() == 0) {
        kernel_text_puts("  storage: ata ok\n");
        kernel_debug_puts("storage: using ata backend\n");
        return;
    }
    kernel_text_puts("  storage: usb-ms?\n");
    if (kernel_usb_mass_storage_init() == 0) {
        kernel_text_puts("  storage: usb-ms ok\n");
        kernel_debug_puts("storage: using usb backend\n");
        return;
    }
    kernel_text_puts("  storage: usb-compat?\n");
    (void)kernel_usb_storage_compat_probe();

    kernel_text_puts("  storage: none\n");
    kernel_debug_puts("storage: no block device backend available\n");
}
