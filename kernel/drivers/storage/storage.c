#include <kernel/drivers/debug/debug.h>

extern void kernel_text_puts(const char *s);
#include <kernel/drivers/storage/ahci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/storage/nvme.h>

static int g_storage_probed = 0;

void kernel_storage_init(void) {
    if (g_storage_probed) return;
    g_storage_probed = 1;

    kernel_block_device_reset();

    kernel_debug_puts("storage: probing...\n");

    /* Try ATA first (most common for IDE mode) */
    if (kernel_ata_init() == 0) {
        kernel_text_puts("storage: ata OK\n");
        return;
    }

    /* Try AHCI (SATA, common on laptops like T61) */
    kernel_debug_puts("storage: probing ahci...\n");
    if (kernel_ahci_init() == 0) {
        kernel_text_puts("storage: ahci OK\n");
        return;
    }

    kernel_text_puts("storage: none\n");
}

/* Called after USB init to probe USB/NVMe/AHCI storage */
void kernel_storage_probe_extra(void) {
    if (kernel_storage_ready()) return;

    kernel_block_device_reset();

    kernel_debug_puts("storage: probing nvme...\n");
    if (kernel_nvme_init() == 0) {
        kernel_text_puts("storage: nvme OK\n");
        return;
    }

    kernel_debug_puts("storage: probing ahci...\n");
    if (kernel_ahci_init() == 0) {
        kernel_text_puts("storage: ahci OK\n");
        return;
    }

    kernel_debug_puts("storage: extra none\n");
}
