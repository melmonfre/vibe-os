#include <kernel/drivers/storage/usb_compat.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/usb/usb_host.h>

static const char *kernel_usb_storage_mass_protocol_name(uint8_t protocol) {
    switch (protocol) {
    case 0x00u:
        return "cbi";
    case 0x01u:
        return "cbi-cci";
    case 0x50u:
        return "bulk-only";
    default:
        return "unknown";
    }
}

int kernel_usb_storage_compat_probe(void) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index;
    uint32_t count;

    count = kernel_usb_mass_storage_probe_count();
    if (count == 0u) {
        return -1;
    }

    if (kernel_usb_mass_storage_probe_first_configured(&snapshot, &snapshot_index) != 0) {
        kernel_debug_printf("storage: usb compat mass-storage candidates=%u but none are configured yet\n",
                            (unsigned int)count);
        return -1;
    }

    kernel_debug_printf("storage: usb compat mass-storage candidate idx=%u addr=%u if=%u subclass=%u protocol=%s bulk_in=%u/%u bulk_out=%u/%u\n",
                        (unsigned int)snapshot_index,
                        (unsigned int)snapshot.assigned_address,
                        snapshot.mass_storage_interface_number == 0xffu ?
                            0xffffffffu :
                            (unsigned int)snapshot.mass_storage_interface_number,
                        (unsigned int)snapshot.mass_storage_subclass,
                        kernel_usb_storage_mass_protocol_name(snapshot.mass_storage_protocol),
                        snapshot.mass_storage_bulk_in_endpoint_address == 0xffu ?
                            0xffffffffu :
                            (unsigned int)snapshot.mass_storage_bulk_in_endpoint_address,
                        (unsigned int)snapshot.mass_storage_bulk_in_max_packet,
                        snapshot.mass_storage_bulk_out_endpoint_address == 0xffu ?
                            0xffffffffu :
                            (unsigned int)snapshot.mass_storage_bulk_out_endpoint_address,
                        (unsigned int)snapshot.mass_storage_bulk_out_max_packet);
    kernel_debug_puts("storage: compat umass bridge not wired yet; native usb runtime storage still unavailable\n");
    return -1;
}
