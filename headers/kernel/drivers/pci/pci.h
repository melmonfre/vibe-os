#ifndef KERNEL_DRIVERS_PCI_H
#define KERNEL_DRIVERS_PCI_H

#include <stddef.h>
#include <stdint.h>

#define PCI_VENDOR_NONE 0xFFFFu

#define PCI_CLASS_DISPLAY 0x03u
#define PCI_CLASS_MASS_STORAGE 0x01u

#define PCI_SUBCLASS_DISPLAY_VGA 0x00u
#define PCI_SUBCLASS_DISPLAY_XGA 0x01u
#define PCI_SUBCLASS_DISPLAY_3D 0x02u

#define PCI_COMMAND_IO_SPACE 0x0001u
#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER 0x0004u

struct kernel_pci_device_info {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
    uint8_t irq_line;
    uint8_t secondary_bus;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint32_t bars[6];
};

typedef int (*kernel_pci_enum_cb)(const struct kernel_pci_device_info *info, void *ctx);

uint32_t kernel_pci_config_read_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t kernel_pci_config_read_u16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint8_t kernel_pci_config_read_u8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void kernel_pci_config_write_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
int kernel_pci_get_device_info(uint8_t bus,
                               uint8_t slot,
                               uint8_t function,
                               struct kernel_pci_device_info *info_out);
int kernel_pci_enumerate(kernel_pci_enum_cb cb, void *ctx);
int kernel_pci_find_by_class(uint8_t class_code,
                             uint8_t subclass,
                             int subclass_any,
                             struct kernel_pci_device_info *info_out);
int kernel_pci_bar_is_io(uint32_t bar_value);
int kernel_pci_bar_is_mmio(uint32_t bar_value);
uintptr_t kernel_pci_io_bar_base(uint32_t bar_value);
uintptr_t kernel_pci_bar_base(uint32_t bar_value);
size_t kernel_pci_bar_size(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t bar_index);

#endif
