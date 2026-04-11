#include <kernel/driver_manager.h>
#include <kernel/drivers/network/virtio_net.h>
#include <kernel/hal/io.h>
#include <kernel/kernel.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define PCI_VENDOR_VIRTIO 0x1AF4u
#define PCI_DEVICE_VIRTIO_NET_LEGACY 0x1000u
#define PCI_DEVICE_VIRTIO_NET_MODERN 0x1041u

static int g_registered = 0;
static int g_present = 0;
static uint32_t g_mtu = 0u;

static uint32_t pci_config_read_u32(uint8_t bus,
                                    uint8_t slot,
                                    uint8_t function,
                                    uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)(offset & 0xFCu));

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static int pci_is_network_class(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t class_reg = pci_config_read_u32(bus, slot, function, 0x08u);
    uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);

    return class_code == 0x02u;
}

static int pci_is_virtio_net(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t vendor_device = pci_config_read_u32(bus, slot, function, 0x00u);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFu);
    uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);

    if (vendor != PCI_VENDOR_VIRTIO) {
        return 0;
    }

    if (device != PCI_DEVICE_VIRTIO_NET_LEGACY &&
        device != PCI_DEVICE_VIRTIO_NET_MODERN) {
        return 0;
    }

    return pci_is_network_class(bus, slot, function);
}

static void virtio_net_probe(void) {
    int found = 0;

    for (uint16_t bus = 0; bus < 256u && !found; ++bus) {
        for (uint16_t slot = 0; slot < 32u && !found; ++slot) {
            if (pci_is_virtio_net((uint8_t)bus, (uint8_t)slot, 0u)) {
                found = 1;
            }
        }
    }

    if (found) {
        g_present = 1;
        g_mtu = 1500u;
        kernel_debug_puts("network: virtio-net device detected\n");
    } else {
        g_present = 0;
        g_mtu = 0u;
        kernel_debug_puts("network: virtio-net not found\n");
    }
}

void kernel_virtio_net_init(void) {
    if (!g_registered) {
        register_driver("virtio-net", "network", kernel_virtio_net_init);
        g_registered = 1;
        return;
    }

    virtio_net_probe();
}

int kernel_virtio_net_present(void) {
    return g_present;
}

uint32_t kernel_virtio_net_mtu(void) {
    return g_mtu;
}
