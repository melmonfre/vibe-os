#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/network/rtl8139.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define RTL8139_VENDOR_REALTEK 0x10ECu
#define RTL8139_DEVICE_8139 0x8139u
#define RTL8139_DEVICE_8169 0x8169u

#define RTL8139_NUM_TX_DESC 4u
#define RTL8139_TX_BUFSZ 1536u
#define RTL8139_RX_BUFSZ 8192u
#define RTL8139_RX_BUF_MARKS 4u

#define RTL8139_RBSTART 0x30u
#define RTL8139_CMD 0x37u
#define RTL8139_CAPR 0x38u
#define RTL8139_CBR 0x3Au
#define RTL8139_IMR 0x3Cu
#define RTL8139_ISR 0x3Eu
#define RTL8139_TCR 0x40u
#define RTL8139_RCR 0x44u
#define RTL8139_CONFIG_1 0x52u
#define RTL8139_TX_STATUS_0 0x10u
#define RTL8139_TX_ADDR_0 0x20u
#define RTL8139_TX_NORMAL_POLLING 0x3Du
#define RTL8139_MAR0 0x44u
#define RTL8139_MAR4 0x48u
#define RTL8139_IDR0 0x00u
#define RTL8139_IDR4 0x04u

#define RTL8139_CMD_REQ 0x01u
#define RTL8139_CMD_TE 0x04u
#define RTL8139_CMD_RE 0x08u
#define RTL8139_CMD_RST 0x10u

#define RTL8139_ISR_ROK (1u << 0)
#define RTL8139_ISR_TOK (1u << 2)
#define RTL8139_ISR_RER (1u << 3)
#define RTL8139_ISR_TER (1u << 4)
#define RTL8139_ISR_RXOVW (1u << 7)

#define RTL8139_RCR_AAP (1u << 0)
#define RTL8139_RCR_APM (1u << 1)
#define RTL8139_RCR_AM (1u << 2)
#define RTL8139_RCR_AB (1u << 3)
#define RTL8139_RCR_WRAP (1u << 7)
#define RTL8139_RCR_RXFTH_MASK (3u << 8)
#define RTL8139_RCR_RXFTH_NONE (3u << 8)
#define RTL8139_RCR_RBLEN_MASK (3u << 11)
#define RTL8139_RCR_RBLEN_8K (0u << 11)
#define RTL8139_RCR_RBLEN_16K (1u << 11)
#define RTL8139_RCR_RBLEN_32K (2u << 11)
#define RTL8139_RCR_RBLEN_64K (3u << 11)

#define RTL8139_TCR_MXDMA_MASK (7u << 8)
#define RTL8139_TCR_MXDMA_256 (5u << 8)
#define RTL8139_TCR_MXDMA_512 (6u << 8)
#define RTL8139_TCR_MXDMA_1024 (7u << 8)

#define RTL8139_RX_STATUS_OK 0x0001u
#define RTL8139_RX_STATUS_LEN_MASK 0x1FFFu
#define RTL8139_RX_STATUS_ERR_MASK 0xE002u

#define RTL8139_TIMEOUT 10000u

#define RTL8139_TX_STATUS_OWN 0x2000u
#define RTL8139_TX_STATUS_TOK 0x8000u
#define RTL8139_TX_STATUS_LEN_MASK 0x1FFFu

struct rtl8139_device {
    uint8_t present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uint16_t io_base;

    uint8_t *tx_buffers[RTL8139_NUM_TX_DESC];
    uint8_t *rx_buffer;
    uint32_t rx_offset;
    uint32_t tx_cur;
    uint32_t tx_avail;
    uint8_t mac[6];
    spinlock_t lock;
};

static struct rtl8139_device g_rtl8139;
static int g_registered = 0;

static inline uint8_t rtl8139_read8(struct rtl8139_device *dev, uint16_t reg) {
    return inb((uint16_t)(dev->io_base + reg));
}

static inline uint16_t rtl8139_read16(struct rtl8139_device *dev, uint16_t reg) {
    return inw((uint16_t)(dev->io_base + reg));
}

static inline uint32_t rtl8139_read32(struct rtl8139_device *dev, uint16_t reg) {
    return inl((uint16_t)(dev->io_base + reg));
}

static inline void rtl8139_write8(struct rtl8139_device *dev, uint16_t reg, uint8_t val) {
    outb((uint16_t)(dev->io_base + reg), val);
}

static inline void rtl8139_write16(struct rtl8139_device *dev, uint16_t reg, uint16_t val) {
    outw((uint16_t)(dev->io_base + reg), val);
}

static inline void rtl8139_write32(struct rtl8139_device *dev, uint16_t reg, uint32_t val) {
    outl((uint16_t)(dev->io_base + reg), val);
}

static void rtl8139_read_mac(struct rtl8139_device *dev) {
    uint32_t mac_lo = rtl8139_read32(dev, RTL8139_IDR0);
    uint32_t mac_hi = rtl8139_read32(dev, RTL8139_IDR4);

    dev->mac[0] = (uint8_t)(mac_lo & 0xFFu);
    dev->mac[1] = (uint8_t)((mac_lo >> 8) & 0xFFu);
    dev->mac[2] = (uint8_t)((mac_lo >> 16) & 0xFFu);
    dev->mac[3] = (uint8_t)((mac_lo >> 24) & 0xFFu);
    dev->mac[4] = (uint8_t)(mac_hi & 0xFFu);
    dev->mac[5] = (uint8_t)((mac_hi >> 8) & 0xFFu);
}

static int rtl8139_probe(struct rtl8139_device *dev) {
    uint32_t flags;
    uint8_t cmd_reg;

    rtl8139_write8(dev, RTL8139_CMD, RTL8139_CMD_RST);
    for (uint32_t i = 0; i < RTL8139_TIMEOUT; i++) {
        io_wait();
        if ((rtl8139_read8(dev, RTL8139_CMD) & RTL8139_CMD_RST) == 0u) {
            break;
        }
    }
    if ((rtl8139_read8(dev, RTL8139_CMD) & RTL8139_CMD_RST) != 0u) {
        kernel_debug_printf("rtl8139: reset timed out\n");
        return -1;
    }

    rtl8139_read_mac(dev);
    kernel_debug_printf("rtl8139: mac=%02x:%02x:%02x:%02x:%02x:%02x io=%x\n",
                        dev->mac[0], dev->mac[1], dev->mac[2],
                        dev->mac[3], dev->mac[4], dev->mac[5],
                        (unsigned int)dev->io_base);

    for (uint32_t i = 0; i < RTL8139_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)kernel_malloc(RTL8139_TX_BUFSZ);
        if (dev->tx_buffers[i] == 0) {
            return -1;
        }
        memset(dev->tx_buffers[i], 0, RTL8139_TX_BUFSZ);
    }

    dev->rx_buffer = (uint8_t *)kernel_malloc(RTL8139_RX_BUFSZ);
    if (dev->rx_buffer == 0) {
        return -1;
    }
    memset(dev->rx_buffer, 0, RTL8139_RX_BUFSZ);

    rtl8139_write32(dev, RTL8139_RBSTART, (uint32_t)(uintptr_t)dev->rx_buffer);

    flags = RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB;
    flags |= RTL8139_RCR_RXFTH_NONE;
    flags |= RTL8139_RCR_RBLEN_8K;
    flags |= RTL8139_RCR_WRAP;
    rtl8139_write32(dev, RTL8139_RCR, flags);

    rtl8139_write32(dev, RTL8139_TCR, RTL8139_TCR_MXDMA_1024);

    rtl8139_write16(dev, RTL8139_IMR, RTL8139_ISR_ROK | RTL8139_ISR_TOK | RTL8139_ISR_RER | RTL8139_ISR_TER);

    cmd_reg = RTL8139_CMD_RE | RTL8139_CMD_TE | RTL8139_CMD_REQ;
    rtl8139_write8(dev, RTL8139_CMD, cmd_reg);

    if (dev->irq_line != 0u) {
        kernel_irq_register_handler(dev->irq_line, kernel_rtl8139_irq_handler);
        kernel_irq_unmask(dev->irq_line);
    }

    dev->tx_cur = 0;
    dev->tx_avail = RTL8139_NUM_TX_DESC;
    dev->rx_offset = 0;

    kernel_debug_printf("rtl8139: device initialized\n");
    dev->present = 1;
    return 0;
}

void kernel_rtl8139_irq_handler(void) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint16_t isr;

    isr = rtl8139_read16(dev, RTL8139_ISR);

    if ((isr & RTL8139_ISR_ROK) != 0u) {
        uint16_t rx_status;
        uint16_t rx_len;

        while (1) {
            rx_status = *(volatile uint16_t *)(dev->rx_buffer + dev->rx_offset);
            rx_len = rx_status & RTL8139_RX_STATUS_LEN_MASK;

            if ((rx_status & RTL8139_RX_STATUS_OK) == 0) {
                if (dev->rx_offset + rx_len < RTL8139_RX_BUFSZ) {
                    break;
                }
            }

            if ((rx_status & RTL8139_RX_STATUS_OK) != 0u) {
                kernel_debug_printf("rtl8139: rx packet len=%u\n", (unsigned int)rx_len);
            }

            dev->rx_offset += rx_len + 4u;
            if (dev->rx_offset >= RTL8139_RX_BUFSZ) {
                dev->rx_offset -= RTL8139_RX_BUFSZ;
            }

            rtl8139_write16(dev, RTL8139_CAPR, (uint16_t)(dev->rx_offset - 16u));
        }
    }

    if ((isr & RTL8139_ISR_TOK) != 0u) {
        uint32_t flags = spinlock_lock_irqsave(&dev->lock);
        dev->tx_avail = RTL8139_NUM_TX_DESC;
        spinlock_unlock_irqrestore(&dev->lock, flags);
    }

    if ((isr & (RTL8139_ISR_RER | RTL8139_ISR_TER | RTL8139_ISR_RXOVW)) != 0u) {
        kernel_debug_printf("rtl8139: error isr=%x\n", (unsigned int)isr);
    }

    rtl8139_write16(dev, RTL8139_ISR, isr);
}

static const pci_id_t rtl8139_ids[] = {
    { RTL8139_VENDOR_REALTEK, RTL8139_DEVICE_8139 },
    { RTL8139_VENDOR_REALTEK, RTL8139_DEVICE_8169 },
    { 0, 0 }
};

static int rtl8139_probe_pci(void) {
    for (int i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_get(i);
        uint32_t cmd;

        if (!pci_match_table(dev, rtl8139_ids)) {
            continue;
        }
        if (dev->class_code != 0x02u) {
            continue;
        }

        memset(&g_rtl8139, 0, sizeof(g_rtl8139));
        g_rtl8139.bus = dev->bus;
        g_rtl8139.slot = dev->slot;
        g_rtl8139.function = dev->function;
        g_rtl8139.irq_line = dev->irq_line;
        spinlock_init(&g_rtl8139.lock);

        for (int bar = 0; bar < 6; bar++) {
            if (pci_bar_type(dev, bar) == PCI_BAR_TYPE_IO) {
                g_rtl8139.io_base = (uint16_t)pci_bar_addr(dev, bar);
                break;
            }
        }

        if (g_rtl8139.io_base == 0) {
            continue;
        }

        cmd = kernel_pci_config_read_u32(dev->bus, dev->slot, dev->function, 0x04u);
        cmd |= PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER;
        kernel_pci_config_write_u32(dev->bus, dev->slot, dev->function, 0x04u, cmd);

        kernel_debug_printf("rtl8139: found device %d:%d.%d io=%x irq=%d\n",
                            (int)dev->bus, (int)dev->slot, (int)dev->function,
                            (unsigned int)g_rtl8139.io_base, (int)g_rtl8139.irq_line);

        if (rtl8139_probe(&g_rtl8139) == 0) {
            kernel_debug_printf("rtl8139: realtek nic initialized at %d:%d.%d\n",
                                (int)dev->bus, (int)dev->slot, (int)dev->function);
            return 0;
        }

        g_rtl8139.io_base = 0;
    }

    return -1;
}

int kernel_rtl8139_send_packet(const uint8_t *data, uint32_t len) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint32_t flags;
    uint32_t desc_idx;

    if (!dev->present || data == 0 || len == 0 || len > RTL8139_TX_BUFSZ) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    if (dev->tx_avail == 0) {
        uint32_t tx_status;
        for (uint32_t i = 0; i < RTL8139_NUM_TX_DESC; i++) {
            tx_status = rtl8139_read32(dev, (uint16_t)(RTL8139_TX_STATUS_0 + i * 4));
            if ((tx_status & RTL8139_TX_STATUS_OWN) == 0u) {
                dev->tx_avail = RTL8139_NUM_TX_DESC - i;
                break;
            }
        }
        if (dev->tx_avail == 0) {
            spinlock_unlock_irqrestore(&dev->lock, flags);
            return -1;
        }
    }

    desc_idx = dev->tx_cur;
    memcpy(dev->tx_buffers[desc_idx], data, len);

    rtl8139_write32(dev, (uint16_t)(RTL8139_TX_ADDR_0 + desc_idx * 4),
                    (uint32_t)(uintptr_t)dev->tx_buffers[desc_idx]);
    rtl8139_write32(dev, (uint16_t)(RTL8139_TX_STATUS_0 + desc_idx * 4),
                    len | RTL8139_TX_STATUS_OWN);

    dev->tx_cur = (desc_idx + 1u) % RTL8139_NUM_TX_DESC;
    dev->tx_avail--;

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return 0;
}

int kernel_rtl8139_recv_packet(uint8_t *buf, uint32_t max_len, uint32_t *len_out) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint16_t rx_status;
    uint16_t rx_len;
    uint32_t packet_offset;

    if (!dev->present || buf == 0 || max_len == 0 || len_out == 0) {
        return -1;
    }

    rx_status = *(volatile uint16_t *)(dev->rx_buffer + dev->rx_offset);
    rx_len = rx_status & RTL8139_RX_STATUS_LEN_MASK;

    if ((rx_status & RTL8139_RX_STATUS_OK) == 0u) {
        return -1;
    }

    packet_offset = dev->rx_offset + 4u;
    if (packet_offset + rx_len > RTL8139_RX_BUFSZ) {
        uint32_t first_part = RTL8139_RX_BUFSZ - packet_offset;
        uint32_t second_part = rx_len - first_part;

        if (first_part > max_len) {
            first_part = max_len;
        }
        memcpy(buf, dev->rx_buffer + packet_offset, first_part);
        if (second_part > 0 && first_part < max_len) {
            uint32_t remaining = max_len - first_part;
            if (remaining > second_part) {
                remaining = second_part;
            }
            memcpy(buf + first_part, dev->rx_buffer, remaining);
        }
        *len_out = (first_part + rx_len > max_len) ? max_len : rx_len;
    } else {
        uint16_t copy_len = rx_len;
        if (copy_len > max_len) {
            copy_len = (uint16_t)max_len;
        }
        memcpy(buf, dev->rx_buffer + packet_offset, copy_len);
        *len_out = copy_len;
    }

    dev->rx_offset += rx_len + 4u + 3u;
    dev->rx_offset &= ~3u;
    if (dev->rx_offset >= RTL8139_RX_BUFSZ) {
        dev->rx_offset -= RTL8139_RX_BUFSZ;
    }

    rtl8139_write16(dev, RTL8139_CAPR, (uint16_t)(dev->rx_offset - 16u));
    return 0;
}

int kernel_rtl8139_present(void) {
    return g_rtl8139.present;
}

int kernel_rtl8139_ioctl(int cmd, void *arg, size_t arg_size) {
    struct rtl8139_device *dev = &g_rtl8139;
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
        st->link_up = 1u;
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
        *link = 1;
        return 0;
    }
    case NET_IOCTL_SEND_FRAME: {
        struct { const uint8_t *data; uint32_t len; } *p = (void *)arg;
        if (!p) return -1;
        return kernel_rtl8139_send_packet(p->data, p->len);
    }
    default:
        return -1;
    }
}

void kernel_rtl8139_init(void) {
    if (!g_registered) {
        register_driver("rtl8139", "network", kernel_rtl8139_init);
        g_registered = 1;
        return;
    }

    kernel_debug_puts("network: probing realtek rtl8139/8169\n");
    if (rtl8139_probe_pci() != 0) {
        kernel_debug_puts("network: rtl8139 not found\n");
        return;
    }
}
