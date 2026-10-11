#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/network/e1000.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define E1000_VENDOR_INTEL 0x8086u
#define E1000_DEVICE_82540EM 0x100Eu
#define E1000_DEVICE_82574L 0x10D3u
#define E1000_DEVICE_82571 0x105Eu
#define E1000_DEVICE_82572 0x107Du

#define E1000_NUM_TX_DESC 64u
#define E1000_NUM_RX_DESC 64u
#define E1000_TX_DESC_SIZE 16u
#define E1000_RX_DESC_SIZE 16u
#define E1000_ETH_MTU 1518u
#define E1000_RX_BUFSZ 2048u

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_EEPROM 0x0014u
#define E1000_REG_CTRL_EXT 0x0018u
#define E1000_REG_MDIC 0x0020u
#define E1000_REG_FCAL 0x0028u
#define E1000_REG_FCAH 0x002Cu
#define E1000_REG_FCT 0x0030u
#define E1000_REG_VET 0x0038u
#define E1000_REG_ICR 0x00C0u
#define E1000_REG_IMS 0x00D0u
#define E1000_REG_IMC 0x00D8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_TDBAL 0x0420u
#define E1000_REG_TDBAH 0x0424u
#define E1000_REG_TDLEN 0x0428u
#define E1000_REG_TDH 0x0430u
#define E1000_REG_TDT 0x0438u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_RDBAL1 0x0D00u
#define E1000_REG_RDBAH1 0x0D04u
#define E1000_REG_RA 0x5400u
#define E1000_REG_MTA 0x5200u
#define E1000_REG_RAL 0x5400u
#define E1000_REG_RAH 0x5404u
#define E1000_REG_FWSM 0x5B50u
#define E1000_REG_SWSM 0x5B54u
#define E1000_REG_SW_FW_SYNC 0x5B5Cu

#define E1000_CTRL_FD (1u << 0)
#define E1000_CTRL_LRST (1u << 3)
#define E1000_CTRL_ASDE (1u << 5)
#define E1000_CTRL_SLU (1u << 6)
#define E1000_CTRL_ILOS (1u << 7)
#define E1000_CTRL_SPEED_MASK (3u << 8)
#define E1000_CTRL_SPEED_1000 (2u << 8)
#define E1000_CTRL_FRCSPD (1u << 11)
#define E1000_CTRL_FRCDPLX (1u << 12)
#define E1000_CTRL_RST (1u << 26)
#define E1000_CTRL_VME (1u << 30)
#define E1000_CTRL_PHY_RST (1u << 31)

#define E1000_STATUS_LU (1u << 1)
#define E1000_STATUS_SPEED_MASK (3u << 6)
#define E1000_STATUS_SPEED_10 (0u << 6)
#define E1000_STATUS_SPEED_100 (1u << 6)
#define E1000_STATUS_SPEED_1000 (2u << 6)
#define E1000_STATUS_FD (1u << 2)

#define E1000_ICR_TXDW (1u << 0)
#define E1000_ICR_TXQE (1u << 1)
#define E1000_ICR_LSC (1u << 2)
#define E1000_ICR_RXSEQ (1u << 3)
#define E1000_ICR_RXDMT0 (1u << 4)
#define E1000_ICR_RXO (1u << 6)
#define E1000_ICR_RXT0 (1u << 7)
#define E1000_ICR_MDAC (1u << 9)
#define E1000_ICR_PHYSRQ (1u << 10)
#define E1000_ICR_PHYACK (1u << 11)

#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_SBP (1u << 2)
#define E1000_RCTL_UPE (1u << 3)
#define E1000_RCTL_MPE (1u << 4)
#define E1000_RCTL_LPE (1u << 5)
#define E1000_RCTL_LBM_MASK (3u << 6)
#define E1000_RCTL_RDMTS_MASK (3u << 8)
#define E1000_RCTL_RDMTS_1_2 (0u << 8)
#define E1000_RCTL_RDMTS_1_4 (1u << 8)
#define E1000_RCTL_RDMTS_1_8 (2u << 8)
#define E1000_RCTL_MO_MASK (3u << 12)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_BSIZE_MASK (3u << 16)
#define E1000_RCTL_BSIZE_256 (3u << 16)
#define E1000_RCTL_BSIZE_512 (2u << 16)
#define E1000_RCTL_BSIZE_1024 (1u << 16)
#define E1000_RCTL_BSIZE_2048 (0u << 16)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TCTL_CT_MASK (0xFFu << 4)
#define E1000_TCTL_COLD_MASK (0x3FFu << 12)
#define E1000_TCTL_COLD_DEFAULT (0x40u << 12)
#define E1000_TCTL_SWXOFF (1u << 22)
#define E1000_TCTL_RTLC (1u << 24)

#define E1000_TIPG_IPGT_MASK 0x1Fu
#define E1000_TIPG_IPGR1_MASK (0x1Fu << 10)
#define E1000_TIPG_IPGR2_MASK (0x1Fu << 20)
#define E1000_TIPG_DEFAULT 0x00702008u

#define E1000_EEPROM_READ_OPCODE 0x2u
#define E1000_EEPROM_START 0x1u
#define E1000_EEPROM_REQ 0x2u
#define E1000_EEPROM_GNT 0x4u
#define E1000_EEPROM_DEV_ADDR 0x7u

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_device {
    uint8_t present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uintptr_t mmio_base;
    size_t mmio_size;

    struct e1000_tx_desc tx_desc_ring[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
    struct e1000_rx_desc rx_desc_ring[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
    uint8_t *tx_buffers[E1000_NUM_TX_DESC];
    uint8_t *rx_buffers[E1000_NUM_RX_DESC];
    volatile uint32_t tx_cur;
    volatile uint32_t tx_avail;
    volatile uint32_t rx_cur;

    uint8_t mac[6];
    spinlock_t lock;
};

static struct e1000_device g_e1000;
static int g_registered = 0;

static inline uint32_t e1000_read32(struct e1000_device *dev, uint16_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static inline void e1000_write32(struct e1000_device *dev, uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

static int e1000_eeprom_read(struct e1000_device *dev, uint8_t addr, uint16_t *val) {
    uint32_t eecd;
    uint32_t tmp;
    uint32_t w;
    int bit;

    eecd = e1000_read32(dev, E1000_REG_EEPROM);
    eecd = (eecd & ~0xFFFFu) | (E1000_EEPROM_START | E1000_EEPROM_REQ);
    e1000_write32(dev, E1000_REG_EEPROM, eecd);

    for (w = 0; w < 1000; w++) {
        eecd = e1000_read32(dev, E1000_REG_EEPROM);
        if ((eecd & E1000_EEPROM_GNT) != 0u) {
            break;
        }
    }
    if ((eecd & E1000_EEPROM_GNT) == 0u) {
        return -1;
    }

    tmp = (uint32_t)(E1000_EEPROM_READ_OPCODE & 0x3u) << 8;
    tmp |= (uint32_t)(addr & E1000_EEPROM_DEV_ADDR) << 4;
    tmp |= 1u;

    for (bit = 15; bit >= 0; bit--) {
        uint32_t out = (tmp >> bit) & 1u;
        eecd &= ~((1u << 2) | (1u << 1));
        eecd |= (out << 1);
        eecd &= ~(1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);

        eecd |= (1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);
        io_wait();
        eecd &= ~(1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);
    }

    *val = 0;
    for (bit = 15; bit >= 0; bit--) {
        eecd &= ~(1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);
        eecd |= (1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);
        io_wait();
        eecd = e1000_read32(dev, E1000_REG_EEPROM);
        if ((eecd & (1u << 2)) != 0u) {
            *val |= (1u << bit);
        }
        eecd &= ~(1u << 0);
        e1000_write32(dev, E1000_REG_EEPROM, eecd);
    }

    eecd &= ~(E1000_EEPROM_REQ | E1000_EEPROM_START);
    e1000_write32(dev, E1000_REG_EEPROM, eecd);

    return 0;
}

static int e1000_read_mac(struct e1000_device *dev) {
    uint16_t mac_lo;
    uint16_t mac_mid;
    uint16_t mac_hi;

    if (e1000_eeprom_read(dev, 0, &mac_lo) != 0 ||
        e1000_eeprom_read(dev, 1, &mac_mid) != 0 ||
        e1000_eeprom_read(dev, 2, &mac_hi) != 0) {
        return -1;
    }

    dev->mac[0] = (uint8_t)(mac_lo & 0xFFu);
    dev->mac[1] = (uint8_t)((mac_lo >> 8) & 0xFFu);
    dev->mac[2] = (uint8_t)(mac_mid & 0xFFu);
    dev->mac[3] = (uint8_t)((mac_mid >> 8) & 0xFFu);
    dev->mac[4] = (uint8_t)(mac_hi & 0xFFu);
    dev->mac[5] = (uint8_t)((mac_hi >> 8) & 0xFFu);

    return 0;
}

static void e1000_link_up(struct e1000_device *dev) {
    uint32_t ctrl;

    ctrl = e1000_read32(dev, E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU;
    ctrl &= ~E1000_CTRL_ILOS;
    e1000_write32(dev, E1000_REG_CTRL, ctrl);
}

static int e1000_setup_tx_resources(struct e1000_device *dev) {
    uintptr_t ring_phys;

    memset(dev->tx_desc_ring, 0, sizeof(dev->tx_desc_ring));
    for (uint32_t i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)kernel_malloc(E1000_ETH_MTU);
        if (dev->tx_buffers[i] == 0) {
            return -1;
        }
        dev->tx_desc_ring[i].addr = (uintptr_t)dev->tx_buffers[i];
        dev->tx_desc_ring[i].status = 0u;
    }

    ring_phys = (uintptr_t)dev->tx_desc_ring;
    e1000_write32(dev, E1000_REG_TDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFu));
    e1000_write32(dev, E1000_REG_TDBAH, 0u);
    e1000_write32(dev, E1000_REG_TDLEN, E1000_NUM_TX_DESC * E1000_TX_DESC_SIZE);
    e1000_write32(dev, E1000_REG_TDH, 0u);
    e1000_write32(dev, E1000_REG_TDT, 0u);

    dev->tx_cur = 0;
    dev->tx_avail = E1000_NUM_TX_DESC;

    return 0;
}

static int e1000_setup_rx_resources(struct e1000_device *dev) {
    uintptr_t ring_phys;
    uint32_t rctl;

    memset(dev->rx_desc_ring, 0, sizeof(dev->rx_desc_ring));
    for (uint32_t i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)kernel_malloc(E1000_RX_BUFSZ);
        if (dev->rx_buffers[i] == 0) {
            return -1;
        }
        dev->rx_desc_ring[i].addr = (uintptr_t)dev->rx_buffers[i];
        dev->rx_desc_ring[i].status = 0u;
    }

    ring_phys = (uintptr_t)dev->rx_desc_ring;
    e1000_write32(dev, E1000_REG_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFu));
    e1000_write32(dev, E1000_REG_RDBAH, 0u);
    e1000_write32(dev, E1000_REG_RDLEN, E1000_NUM_RX_DESC * E1000_RX_DESC_SIZE);
    e1000_write32(dev, E1000_REG_RDH, 0u);
    e1000_write32(dev, E1000_REG_RDT, E1000_NUM_RX_DESC - 1u);

    dev->rx_cur = 0;

    rctl = e1000_read32(dev, E1000_REG_RCTL);
    rctl |= E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE;
    rctl |= E1000_RCTL_BAM | E1000_RCTL_SECRC;
    rctl &= ~E1000_RCTL_LBM_MASK;
    rctl &= ~E1000_RCTL_BSIZE_MASK;
    e1000_write32(dev, E1000_REG_RCTL, rctl);

    return 0;
}

static void e1000_setup_transmit(struct e1000_device *dev) {
    uint32_t tctl;

    tctl = e1000_read32(dev, E1000_REG_TCTL);
    tctl |= E1000_TCTL_EN;
    tctl |= E1000_TCTL_PSP;
    tctl |= E1000_TCTL_RTLC;
    tctl &= ~E1000_TCTL_SWXOFF;
    tctl |= (0x10u << 4);
    tctl |= E1000_TCTL_COLD_DEFAULT;
    e1000_write32(dev, E1000_REG_TCTL, tctl);

    e1000_write32(dev, E1000_REG_TIPG, E1000_TIPG_DEFAULT);
}

static void e1000_setup_interrupts(struct e1000_device *dev) {
    e1000_write32(dev, E1000_REG_IMS, E1000_ICR_TXDW | E1000_ICR_RXT0 | E1000_ICR_LSC);
}

static void e1000_irq_handler(void) {
    struct e1000_device *dev = &g_e1000;
    uint32_t icr;

    icr = e1000_read32(dev, E1000_REG_ICR);
    if ((icr & E1000_ICR_LSC) != 0u) {
        kernel_debug_printf("e1000: link status changed\n");
    }
    if ((icr & E1000_ICR_RXT0) != 0u) {
        uint32_t rdh = e1000_read32(dev, E1000_REG_RDH);
        while (dev->rx_cur != rdh) {
            struct e1000_rx_desc *desc = &dev->rx_desc_ring[dev->rx_cur];
            if ((desc->status & 0x01u) != 0u) {
                uint16_t pkt_len = desc->length;
                kernel_debug_printf("e1000: rx packet len=%u\n", (unsigned int)pkt_len);
                desc->status = 0u;
                desc->length = E1000_RX_BUFSZ;
            }
            dev->rx_cur = (dev->rx_cur + 1u) % E1000_NUM_RX_DESC;
            rdh = e1000_read32(dev, E1000_REG_RDH);
        }
        e1000_write32(dev, E1000_REG_RDT, (dev->rx_cur + E1000_NUM_RX_DESC - 1u) % E1000_NUM_RX_DESC);
    }
    if ((icr & E1000_ICR_TXDW) != 0u) {
        uint32_t tdh = e1000_read32(dev, E1000_REG_TDH);
        uint32_t released = (tdh - dev->tx_avail + E1000_NUM_TX_DESC) % E1000_NUM_TX_DESC;
        dev->tx_avail = released;
    }
}

static int e1000_probe(struct e1000_device *dev) {
    uint32_t ctrl;
    uint32_t status_reg;

    e1000_write32(dev, E1000_REG_IMC, 0xFFFFFFFFu);

    ctrl = e1000_read32(dev, E1000_REG_CTRL);
    ctrl |= E1000_CTRL_RST;
    e1000_write32(dev, E1000_REG_CTRL, ctrl);
    for (uint32_t i = 0; i < 10000; i++) {
        io_wait();
        if ((e1000_read32(dev, E1000_REG_CTRL) & E1000_CTRL_RST) == 0u) {
            break;
        }
    }
    if ((e1000_read32(dev, E1000_REG_CTRL) & E1000_CTRL_RST) != 0u) {
        kernel_debug_printf("e1000: reset timed out\n");
        return -1;
    }

    if (e1000_read_mac(dev) != 0) {
        kernel_debug_printf("e1000: failed to read mac\n");
        return -1;
    }

    kernel_debug_printf("e1000: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                        dev->mac[0], dev->mac[1], dev->mac[2],
                        dev->mac[3], dev->mac[4], dev->mac[5]);

    e1000_link_up(dev);

    if (e1000_setup_tx_resources(dev) != 0) {
        kernel_debug_printf("e1000: tx setup failed\n");
        return -1;
    }
    if (e1000_setup_rx_resources(dev) != 0) {
        kernel_debug_printf("e1000: rx setup failed\n");
        return -1;
    }

    e1000_setup_transmit(dev);

    if (dev->irq_line != 0u) {
        kernel_irq_register_handler(dev->irq_line, e1000_irq_handler);
        kernel_irq_unmask(dev->irq_line);
    }

    e1000_setup_interrupts(dev);

    status_reg = e1000_read32(dev, E1000_REG_STATUS);
    kernel_debug_printf("e1000: link=%s speed=%s duplex=%s\n",
                        (status_reg & E1000_STATUS_LU) ? "up" : "down",
                        ((status_reg & E1000_STATUS_SPEED_MASK) == E1000_STATUS_SPEED_1000) ? "1000" :
                        ((status_reg & E1000_STATUS_SPEED_MASK) == E1000_STATUS_SPEED_100) ? "100" : "10",
                        (status_reg & E1000_STATUS_FD) ? "full" : "half");

    dev->present = 1;
    return 0;
}

static const pci_id_t e1000_ids[] = {
    { E1000_VENDOR_INTEL, E1000_DEVICE_82540EM },
    { E1000_VENDOR_INTEL, E1000_DEVICE_82574L },
    { E1000_VENDOR_INTEL, E1000_DEVICE_82571 },
    { E1000_VENDOR_INTEL, E1000_DEVICE_82572 },
    { 0, 0 }
};

static int e1000_probe_pci(void) {
    for (int i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_get(i);
        uint32_t cmd;

        if (!pci_match_table(dev, e1000_ids)) {
            continue;
        }
        if (dev->class_code != 0x02u) {
            continue;
        }

        memset(&g_e1000, 0, sizeof(g_e1000));
        g_e1000.bus = dev->bus;
        g_e1000.slot = dev->slot;
        g_e1000.function = dev->function;
        g_e1000.irq_line = dev->irq_line;
        spinlock_init(&g_e1000.lock);

        for (int bar = 0; bar < 6; bar++) {
            if (pci_bar_type(dev, bar) == PCI_BAR_TYPE_MEM32) {
                uintptr_t base = pci_bar_addr(dev, bar);
                size_t size = pci_bar_size(dev, bar);
                if (base != 0u && size != 0u) {
                    g_e1000.mmio_base = base;
                    g_e1000.mmio_size = size;
                    break;
                }
            }
        }

        if (g_e1000.mmio_base == 0) {
            continue;
        }

        cmd = kernel_pci_config_read_u32(dev->bus, dev->slot, dev->function, 0x04u);
        cmd |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
        kernel_pci_config_write_u32(dev->bus, dev->slot, dev->function, 0x04u, cmd);

        kernel_debug_printf("e1000: found device %d:%d.%d mmio=%p irq=%d\n",
                            (int)dev->bus, (int)dev->slot, (int)dev->function,
                            (void *)g_e1000.mmio_base, (int)g_e1000.irq_line);

        if (e1000_probe(&g_e1000) == 0) {
            kernel_debug_printf("e1000: intel pro/1000 initialized at %d:%d.%d\n",
                                (int)dev->bus, (int)dev->slot, (int)dev->function);
            return 0;
        }

        g_e1000.mmio_base = 0;
    }

    return -1;
}

int kernel_e1000_send_packet(const uint8_t *data, uint32_t len) {
    struct e1000_device *dev = &g_e1000;
    uint32_t flags;
    uint32_t desc_idx;
    struct e1000_tx_desc *desc;

    if (!dev->present || data == 0 || len == 0 || len > E1000_ETH_MTU) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    if (dev->tx_avail == 0) {
        spinlock_unlock_irqrestore(&dev->lock, flags);
        return -1;
    }

    desc_idx = dev->tx_cur;
    desc = &dev->tx_desc_ring[desc_idx];
    memcpy(dev->tx_buffers[desc_idx], data, len);
    desc->length = (uint16_t)len;
    desc->cmd = 0x1u | 0x2u;
    desc->status = 0u;

    dev->tx_cur = (desc_idx + 1u) % E1000_NUM_TX_DESC;
    dev->tx_avail--;

    e1000_write32(dev, E1000_REG_TDT, dev->tx_cur);

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return 0;
}

int kernel_e1000_recv_packet(uint8_t *buf, uint32_t max_len, uint32_t *len_out) {
    struct e1000_device *dev = &g_e1000;
    uint32_t flags;

    if (!dev->present || buf == 0 || max_len == 0 || len_out == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    if ((dev->rx_desc_ring[dev->rx_cur].status & 0x01u) == 0u) {
        spinlock_unlock_irqrestore(&dev->lock, flags);
        return -1;
    }

    {
        struct e1000_rx_desc *desc = &dev->rx_desc_ring[dev->rx_cur];
        uint16_t pkt_len = desc->length;

        if (pkt_len > max_len) {
            pkt_len = (uint16_t)max_len;
        }
        memcpy(buf, dev->rx_buffers[dev->rx_cur], pkt_len);
        *len_out = pkt_len;

        desc->status = 0u;
        desc->length = E1000_RX_BUFSZ;

        dev->rx_cur = (dev->rx_cur + 1u) % E1000_NUM_RX_DESC;
        e1000_write32(dev, E1000_REG_RDT, (dev->rx_cur + E1000_NUM_RX_DESC - 1u) % E1000_NUM_RX_DESC);
    }

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return 0;
}

int kernel_e1000_present(void) {
    return g_e1000.present;
}

int kernel_e1000_ioctl(int cmd, void *arg, size_t arg_size) {
    struct e1000_device *dev = &g_e1000;
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)dev->present;
        st->active = (uint32_t)dev->present;
        st->mtu = 1500u;
        memcpy(st->mac, dev->mac, 6);
        {
            uint32_t status_reg = e1000_read32(dev, E1000_REG_STATUS);
            st->link_up = (status_reg & E1000_STATUS_LU) ? 1u : 0u;
        }
        return 0;
    }
    case NET_IOCTL_GET_MAC: {
        if (!arg || !dev->present) return -1;
        memcpy(arg, dev->mac, 6);
        return 0;
    }
    case NET_IOCTL_GET_LINK: {
        int *link = (int *)arg;
        if (!link || !dev->present) return -1;
        *link = (e1000_read32(dev, E1000_REG_STATUS) & E1000_STATUS_LU) ? 1 : 0;
        return 0;
    }
    case NET_IOCTL_SEND_FRAME: {
        struct { const uint8_t *data; uint32_t len; } *p = (void *)arg;
        if (!p) return -1;
        return kernel_e1000_send_packet(p->data, p->len);
    }
    default:
        return -1;
    }
}

void kernel_e1000_init(void) {
    if (!g_registered) {
        register_driver("e1000", "network", kernel_e1000_init);
        g_registered = 1;
        return;
    }

    kernel_debug_puts("network: probing intel e1000 pro/1000\n");
    if (e1000_probe_pci() != 0) {
        kernel_debug_puts("network: e1000 not found\n");
        return;
    }
}
