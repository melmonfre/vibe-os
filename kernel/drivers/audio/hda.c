#include <kernel/driver_manager.h>
#include <kernel/drivers/audio/hda.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define HDA_VENDOR_INTEL 0x8086u
#define HDA_DEVICE_INTEL_GENERIC 0x2668u
#define HDA_DEVICE_INTEL_82801F 0x2668u
#define HDA_DEVICE_INTEL_82801I 0x293Eu
#define HDA_DEVICE_INTEL_82801JI 0x3A3Eu
#define HDA_DEVICE_INTEL_82801H 0x284Bu
#define HDA_DEVICE_INTEL_PCH 0x1C20u
#define HDA_DEVICE_INTEL_CPT 0x1C20u
#define HDA_DEVICE_INTEL_PPT 0x1E20u
#define HDA_DEVICE_INTEL_LPT 0x8C20u
#define HDA_DEVICE_INTEL_WPT 0x9C20u
#define HDA_DEVICE_INTEL_SKL 0x9D70u
#define HDA_DEVICE_INTEL_KBL 0xA170u
#define HDA_DEVICE_INTEL_CNL 0x9DC8u
#define HDA_DEVICE_INTEL_CFL 0xA348u
#define HDA_DEVICE_INTEL_ICL 0x34C8u
#define HDA_DEVICE_INTEL_TGL 0xA0C8u
#define HDA_DEVICE_NVIDIA_GENERIC 0x10DEu

#define HDA_PCI_CLASS 0x04u
#define HDA_PCI_SUBCLASS 0x03u

#define HDA_GCAP 0x00u
#define HDA_VMIN 0x02u
#define HDA_VMAJ 0x03u
#define HDA_OUTPAY 0x04u
#define HDA_INPAY 0x06u
#define HDA_GCTL 0x08u
#define HDA_WAKEEN 0x0Cu
#define HDA_STATESTS 0x0Eu
#define HDA_GSTS 0x10u
#define HDA_OUTSTRMPAY 0x18u
#define HDA_INSTRMPAY 0x1Au
#define HDA_LLCH 0x14u
#define HDA_CORB_BASE 0x40u
#define HDA_CORB_WP 0x48u
#define HDA_CORB_RP 0x4Au
#define HDA_CORB_CTL 0x4Cu
#define HDA_CORB_STS 0x4Du
#define HDA_CORB_SIZE 0x4Eu
#define HDA_RIRB_BASE 0x50u
#define HDA_RIRB_WP 0x58u
#define HDA_RIRB_INTCNT 0x5Au
#define HDA_RIRB_CTL 0x5Cu
#define HDA_RIRB_STS 0x5Du
#define HDA_RIRB_SIZE 0x5Eu
#define HDA_ICS 0x60u
#define HDA_ICC 0x64u
#define HDA_ICIS 0x68u
#define HDA_DPIBLBASE 0x70u
#define HDA_DPIBUBASE 0x74u

#define HDA_GCTL_CRST (1u << 0)
#define HDA_GCTL_FCNTRL (1u << 1)
#define HDA_GCTL_UNSOL (1u << 8)

#define HDA_CORB_CTL_RUN (1u << 1)
#define HDA_CORB_CTL_CMEIE (1u << 0)
#define HDA_CORB_STS_MEI (1u << 0)
#define HDA_CORB_SIZE_2ENT (0u)
#define HDA_CORB_SIZE_16ENT (1u)
#define HDA_CORB_SIZE_256ENT (2u)

#define HDA_RIRB_CTL_RUN (1u << 1)
#define HDA_RIRB_CTL_DMAR (1u << 0)
#define HDA_RIRB_STS_OIS (1u << 0)

#define HDA_ICS_BUSY (1u << 31)
#define HDA_ICS_VALID (1u << 30)

#define HDA_VERB_GET_PARAM 0xF00u
#define HDA_VERB_GET_CONVERTER_FORMAT 0xA00u
#define HDA_VERB_SET_CONVERTER_FORMAT 0x200u
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300u
#define HDA_VERB_SET_POWER_STATE 0x705u
#define HDA_VERB_SET_PIN_WIDGET_CTRL 0x70Du
#define HDA_VERB_SET_CONVERTER_STREAM_CHAN 0x706u
#define HDA_VERB_GET_CONFIG_DEFAULT 0xF1Cu
#define HDA_VERB_GET_PIN_WIDGET_CTRL 0xF0Du
#define HDA_VERB_GET_CONVERTER_STREAM_CHAN 0xF06u

#define HDA_PARAM_VENDOR_ID 0x00u
#define HDA_PARAM_REVISION 0x02u
#define HDA_PARAM_SUBORDINATE_NODE_COUNT 0x04u
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05u
#define HDA_PARAM_AUDIO_WIDGET_CAPS 0x09u
#define HDA_PARAM_PIN_CAPS 0x0Cu
#define HDA_PARAM_AMP_CAPS 0x0Du
#define HDA_PARAM_CONVERTER_FORMATS 0x0Au
#define HDA_PARAM_POWER_STATE 0x0Fu
#define HDA_PARAM_GPIO_CAPS 0x11u

#define HDA_FGT_AUDIO_FUNCTION 0x01u
#define HDA_FGT_MODEM_FUNCTION 0x02u

#define HDA_AWC_OUTPUT 0x00u
#define HDA_AWC_INPUT 0x10u
#define HDA_AWC_MIXER 0x20u
#define HDA_AWC_SELECTOR 0x30u
#define HDA_AWC_PIN_COMPLEX 0x40u
#define HDA_AWC_POWER 0x50u
#define HDA_AWC_VOLUME_KNOB 0x60u
#define HDA_AWC_BEEP_GEN 0x70u
#define HDA_AWC_TYPE_MASK 0xF0u

#define HDA_PIN_CAP_OUTPUT (1u << 4)
#define HDA_PIN_CAP_INPUT (1u << 5)
#define HDA_PIN_CAP_HP_DRV (1u << 7)
#define HDA_PIN_CAP_EAPD (1u << 16)
#define HDA_PIN_CAP_PRES_DET (1u << 24)

#define HDA_PIN_WIDGET_CTRL_OUT_EN (1u << 6)
#define HDA_PIN_WIDGET_CTRL_IN_EN (1u << 5)
#define HDA_PIN_WIDGET_CTRL_HP_EN (1u << 7)
#define HDA_PIN_WIDGET_CTRL_EAPD (1u << 7)
#define HDA_PIN_WIDGET_CTRL_VREF_EN 0xCU

#define HDA_OUTPUT_BIT 0x01u
#define HDA_INPUT_BIT 0x02u

#define HDA_FORMAT_PCM 0x01u
#define HDA_FORMAT_16BIT (1u << 4)
#define HDA_FORMAT_STEREO (3u << 0)
#define HDA_FORMAT_RATE_48000 (0x0Cu << 8)

#define HDA_STREAM_TAG 1u
#define HDA_SD_CTL_OFF(stream) (0x80u + ((stream) - 1u) * 8u)
#define HDA_SD_STS_OFF(stream) (0x83u + ((stream) - 1u) * 8u)
#define HDA_SD_LPIB_OFF(stream) (0x84u + ((stream) - 1u) * 8u)
#define HDA_SD_CBL_OFF(stream) (0x84u + ((stream) - 1u) * 8u)
#define HDA_SD_LVI_OFF(stream) (0x86u + ((stream) - 1u) * 8u)
#define HDA_SD_FIFOW_OFF(stream) (0x87u + ((stream) - 1u) * 8u)
#define HDA_SD_FIFOSZ_OFF(stream) (0x88u + ((stream) - 1u) * 8u)
#define HDA_SD_FMT_OFF(stream) (0x8Cu + ((stream) - 1u) * 8u)
#define HDA_SD_BDLPL_OFF(stream) (0x80u + ((stream) - 1u) * 8u + 0x18u)
#define HDA_SD_BDLPU_OFF(stream) (0x80u + ((stream) - 1u) * 8u + 0x1Cu)

#define HDA_SD_CTL_IOCE (1u << 0)
#define HDA_SD_CTL_FEIE (1u << 1)
#define HDA_SD_CTL_DEIE (1u << 2)
#define HDA_SD_CTL_RUN (1u << 3)
#define HDA_SD_CTL_RESET (1u << 4)
#define HDA_SD_CTL_STRIPE_MASK (3u << 5)
#define HDA_SD_CTL_TP (1u << 7)
#define HDA_SD_CTL_STREAM_TAG_SHIFT 16u
#define HDA_SD_CTL_DIR_SHIFT 20u
#define HDA_SD_CTL_DIR_OUT (0u << 20)
#define HDA_SD_CTL_DIR_IN (1u << 20)

#define HDA_SD_STS_BCIS (1u << 2)
#define HDA_SD_STS_FIFOE (1u << 3)
#define HDA_SD_STS_DESE (1u << 4)
#define HDA_SD_STS_FIFORDY (1u << 5)
#define HDA_SD_STS_ACK (0x1Eu)

#define HDA_CORB_ENTRIES 256u
#define HDA_RIRB_ENTRIES 256u
#define HDA_BDL_ENTRIES 32u
#define HDA_DMA_BUF_SZ (HDA_BDL_ENTRIES * 4096u)

#define HDA_TIMEOUT 100000u
#define HDA_SAMPLE_RATE 48000u
#define HDA_BPS 16u
#define HDA_CHANNELS 2u
#define HDA_FRAME_SIZE ((HDA_BPS / 8u) * HDA_CHANNELS)
#define HDA_PERIOD_SIZE 512u
#define HDA_PERIOD_BYTES (HDA_PERIOD_SIZE * HDA_FRAME_SIZE)

struct hda_bdl_entry {
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t length;
    uint32_t ioc;
} __attribute__((packed));

struct hda_device {
    uint8_t present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uintptr_t mmio_base;
    size_t mmio_size;

    uint8_t *corb;
    uint8_t *rirb;
    uint16_t corb_wp;
    uint16_t rirb_rp;
    uint16_t rirb_wp_cache;

    uint16_t afg_nid;
    uint16_t audio_out_nid;
    uint16_t pin_out_nid;
    uint8_t dma_buf_valid;
    uint8_t *dma_buf;
    struct hda_bdl_entry *bdl;

    uint32_t playback_pos;
    uint32_t playback_filled;
    spinlock_t lock;
};

static struct hda_device g_hda;
static int g_registered = 0;

static inline uint16_t hda_read16(struct hda_device *dev, uint16_t reg) {
    return *(volatile uint16_t *)(dev->mmio_base + reg);
}

static inline void hda_write16(struct hda_device *dev, uint16_t reg, uint16_t val) {
    *(volatile uint16_t *)(dev->mmio_base + reg) = val;
}

static inline uint32_t hda_read32(struct hda_device *dev, uint16_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static inline void hda_write32(struct hda_device *dev, uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

static inline uint8_t hda_read8(struct hda_device *dev, uint16_t reg) {
    return *(volatile uint8_t *)(dev->mmio_base + reg);
}

static inline void hda_write8(struct hda_device *dev, uint16_t reg, uint8_t val) {
    *(volatile uint8_t *)(dev->mmio_base + reg) = val;
}

__attribute__((unused))
static int hda_send_verb(struct hda_device *dev, uint16_t nid, uint16_t verb, uint16_t payload) {
    uint32_t cmd = ((uint32_t)nid << 20) | ((uint32_t)verb << 8) | payload;
    uint32_t icis;

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_ICS) & HDA_ICS_BUSY) == 0u) {
            break;
        }
        io_wait();
    }

    hda_write32(dev, HDA_ICC, cmd);
    hda_write32(dev, HDA_ICS, HDA_ICS_BUSY);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        icis = hda_read32(dev, HDA_ICIS);
        if ((icis & HDA_ICS_VALID) != 0u) {
            hda_write32(dev, HDA_ICIS, HDA_ICS_VALID);
            return 0;
        }
        io_wait();
    }

    return -1;
}

static int hda_read_verb(struct hda_device *dev, uint16_t nid, uint16_t verb, uint16_t payload, uint32_t *resp) {
    uint32_t cmd = ((uint32_t)nid << 20) | ((uint32_t)verb << 8) | payload;
    uint32_t icis;

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_ICS) & HDA_ICS_BUSY) == 0u) {
            break;
        }
        io_wait();
    }

    hda_write32(dev, HDA_ICC, cmd);
    hda_write32(dev, HDA_ICS, HDA_ICS_BUSY);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        icis = hda_read32(dev, HDA_ICIS);
        if ((icis & HDA_ICS_VALID) != 0u) {
            hda_write32(dev, HDA_ICIS, HDA_ICS_VALID);
            *resp = hda_read32(dev, HDA_ICC);
            return 0;
        }
        io_wait();
    }

    return -1;
}

static int hda_corb_start(struct hda_device *dev) {
    uintptr_t corb_phys = (uintptr_t)dev->corb;
    uintptr_t rirb_phys = (uintptr_t)dev->rirb;

    hda_write32(dev, HDA_CORB_BASE, (uint32_t)(corb_phys & 0xFFFFFFFFu));
    hda_write32(dev, HDA_CORB_BASE + 4u, 0u);
    hda_write32(dev, HDA_RIRB_BASE, (uint32_t)(rirb_phys & 0xFFFFFFFFu));
    hda_write32(dev, HDA_RIRB_BASE + 4u, 0u);

    hda_write8(dev, HDA_CORB_SIZE, HDA_CORB_SIZE_256ENT);
    hda_write16(dev, HDA_CORB_WP, 0u);
    hda_write16(dev, HDA_RIRB_WP, 0u);
    hda_write8(dev, HDA_RIRB_INTCNT, 1u);

    hda_write8(dev, HDA_CORB_CTL, HDA_CORB_CTL_RUN);
    hda_write8(dev, HDA_RIRB_CTL, HDA_RIRB_CTL_RUN);

    dev->corb_wp = 0;
    dev->rirb_rp = 0;
    dev->rirb_wp_cache = 0;

    return 0;
}

static int hda_corb_send(struct hda_device *dev, uint32_t verb) {
    uint32_t *corb32 = (uint32_t *)dev->corb;
    uint16_t wp;

    wp = dev->corb_wp;
    corb32[wp] = verb;
    wp = (wp + 1u) % HDA_CORB_ENTRIES;
    dev->corb_wp = wp;
    hda_write16(dev, HDA_CORB_WP, wp);

    return 0;
}

static int hda_rirb_poll(struct hda_device *dev, uint64_t *resp) {
    uint64_t *rirb64 = (uint64_t *)dev->rirb;

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        uint16_t wp = hda_read16(dev, HDA_RIRB_WP);

        if (wp != dev->rirb_wp_cache) {
            uint16_t rp = dev->rirb_rp;
            *resp = rirb64[rp];
            rp = (rp + 1u) % HDA_RIRB_ENTRIES;
            dev->rirb_rp = rp;
            dev->rirb_wp_cache = wp;
            hda_write16(dev, HDA_RIRB_WP, wp);
            return 0;
        }

        io_wait();
    }

    return -1;
}

static int hda_command(struct hda_device *dev, uint16_t nid, uint16_t verb,
                       uint16_t payload, uint32_t *resp) {
    uint32_t cmd = ((uint32_t)nid << 20) | ((uint32_t)verb << 8) | payload;
    uint64_t rirb_resp;

    hda_corb_send(dev, cmd);

    if (resp != 0) {
        if (hda_rirb_poll(dev, &rirb_resp) != 0) {
            return -1;
        }
        *resp = (uint32_t)(rirb_resp & 0xFFFFFFFFu);
    }

    return 0;
}

static int hda_probe_codec(struct hda_device *dev) {
    uint32_t resp;
    uint32_t fgt;
    uint16_t nid;
    uint32_t wcaps;
    uint16_t sub_count;
    uint16_t start_nid;

    for (uint32_t cad = 0u; cad < 16u; cad++) {
        if (hda_read_verb(dev, cad, HDA_VERB_GET_PARAM, HDA_PARAM_VENDOR_ID, &resp) == 0) {
            if (resp != 0 && resp != 0xFFFFFFFFu) {
                kernel_debug_printf("hda: codec %u vid=%x\n", (unsigned int)cad, (unsigned int)resp);

                if (hda_read_verb(dev, cad, HDA_VERB_GET_PARAM, HDA_PARAM_FUNCTION_GROUP_TYPE, &resp) == 0) {
                    fgt = (resp >> 8) & 0xFFu;
                    if (fgt == HDA_FGT_AUDIO_FUNCTION) {
                        dev->afg_nid = (uint16_t)cad;
                        break;
                    }
                }
            }
        }
    }

    if (dev->afg_nid == 0) {
        kernel_debug_printf("hda: no audio function group found\n");
        return -1;
    }

    if (hda_read_verb(dev, dev->afg_nid, HDA_VERB_GET_PARAM,
                       HDA_PARAM_SUBORDINATE_NODE_COUNT, &resp) != 0) {
        return -1;
    }

    sub_count = (uint16_t)(resp & 0xFFu);
    start_nid = (uint16_t)((resp >> 16) & 0xFFu);

    kernel_debug_printf("hda: afg nid=%u subordinate start=%u count=%u\n",
                        (unsigned int)dev->afg_nid,
                        (unsigned int)start_nid,
                        (unsigned int)sub_count);

    for (nid = start_nid; nid < start_nid + sub_count; nid++) {
        if (hda_read_verb(dev, nid, HDA_VERB_GET_PARAM,
                           HDA_PARAM_AUDIO_WIDGET_CAPS, &resp) != 0) {
            continue;
        }

        wcaps = resp;
        switch (wcaps & HDA_AWC_TYPE_MASK) {
        case HDA_AWC_OUTPUT:
            if (dev->audio_out_nid == 0) {
                dev->audio_out_nid = nid;
                kernel_debug_printf("hda: audio output nid=%u\n", (unsigned int)nid);
            }
            break;
        case HDA_AWC_PIN_COMPLEX: {
            uint32_t pin_caps;

            if (hda_read_verb(dev, nid, HDA_VERB_GET_PARAM,
                               HDA_PARAM_PIN_CAPS, &pin_caps) != 0) {
                break;
            }
            if ((pin_caps & HDA_PIN_CAP_OUTPUT) != 0u || (pin_caps & HDA_PIN_CAP_HP_DRV) != 0u) {
                if (dev->pin_out_nid == 0) {
                    dev->pin_out_nid = nid;
                    kernel_debug_printf("hda: pin output nid=%u\n", (unsigned int)nid);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    if (dev->audio_out_nid == 0 || dev->pin_out_nid == 0) {
        kernel_debug_printf("hda: required widgets not found (out=%u pin=%u)\n",
                            (unsigned int)dev->audio_out_nid,
                            (unsigned int)dev->pin_out_nid);
        return -1;
    }

    return 0;
}

static int hda_setup_dma(struct hda_device *dev) {
    uintptr_t buf_phys;
    uintptr_t bdl_phys;
    uint32_t sd_ctl;
    uint32_t fmt;

    dev->dma_buf = (uint8_t *)kernel_malloc_aligned(HDA_DMA_BUF_SZ, 4096);
    dev->bdl = (struct hda_bdl_entry *)kernel_malloc_aligned(sizeof(struct hda_bdl_entry) * HDA_BDL_ENTRIES, 128);

    if (dev->dma_buf == 0 || dev->bdl == 0) {
        return -1;
    }

    memset(dev->dma_buf, 0, HDA_DMA_BUF_SZ);
    memset(dev->bdl, 0, sizeof(struct hda_bdl_entry) * HDA_BDL_ENTRIES);

    buf_phys = (uintptr_t)dev->dma_buf;
    bdl_phys = (uintptr_t)dev->bdl;

    for (uint32_t i = 0; i < HDA_BDL_ENTRIES; i++) {
        dev->bdl[i].addr_low = (uint32_t)((buf_phys + i * 4096u) & 0xFFFFFFFFu);
        dev->bdl[i].addr_high = 0u;
        dev->bdl[i].length = 4096u;
        dev->bdl[i].ioc = (i == HDA_BDL_ENTRIES - 1u) ? 0x01u : 0x00u;
    }

    sd_ctl = hda_read32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG));
    sd_ctl |= HDA_SD_CTL_RESET;
    hda_write32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG), sd_ctl);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG)) & HDA_SD_CTL_RESET) != 0u) {
            break;
        }
        io_wait();
    }

    sd_ctl = hda_read32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG));
    sd_ctl &= ~HDA_SD_CTL_RESET;
    hda_write32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG), sd_ctl);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG)) & HDA_SD_CTL_RESET) == 0u) {
            break;
        }
        io_wait();
    }

    hda_write32(dev, HDA_SD_BDLPL_OFF(HDA_STREAM_TAG), (uint32_t)(bdl_phys & 0xFFFFFFFFu));
    hda_write32(dev, HDA_SD_BDLPU_OFF(HDA_STREAM_TAG), 0u);
    hda_write32(dev, HDA_SD_CBL_OFF(HDA_STREAM_TAG), HDA_DMA_BUF_SZ);
    hda_write16(dev, HDA_SD_LVI_OFF(HDA_STREAM_TAG), (uint16_t)(HDA_BDL_ENTRIES - 1u));

    fmt = HDA_FORMAT_PCM | HDA_FORMAT_16BIT | HDA_FORMAT_STEREO;
    fmt |= (HDA_FORMAT_RATE_48000);
    hda_write16(dev, HDA_SD_FMT_OFF(HDA_STREAM_TAG), (uint16_t)fmt);

    hda_command(dev, dev->audio_out_nid, HDA_VERB_SET_CONVERTER_FORMAT, (uint16_t)fmt, 0);
    hda_command(dev, dev->audio_out_nid, HDA_VERB_SET_CONVERTER_STREAM_CHAN,
                (uint16_t)(((uint32_t)HDA_STREAM_TAG) << 4), 0);

    hda_command(dev, dev->pin_out_nid, HDA_VERB_SET_PIN_WIDGET_CTRL,
                HDA_PIN_WIDGET_CTRL_OUT_EN | HDA_PIN_WIDGET_CTRL_EAPD, 0);
    hda_command(dev, dev->pin_out_nid, HDA_VERB_SET_POWER_STATE, 0x00u, 0);

    dev->dma_buf_valid = 1;
    dev->playback_pos = 0;
    dev->playback_filled = 0;

    return 0;
}

static void hda_start_playback(struct hda_device *dev) {
    uint32_t sd_ctl;

    sd_ctl = hda_read32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG));
    sd_ctl &= ~HDA_SD_CTL_DIR_IN;
    sd_ctl |= (HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE | HDA_SD_CTL_FEIE | HDA_SD_CTL_DEIE);
    sd_ctl |= (uint32_t)HDA_STREAM_TAG << HDA_SD_CTL_STREAM_TAG_SHIFT;
    hda_write32(dev, HDA_SD_CTL_OFF(HDA_STREAM_TAG), sd_ctl);
}

static int hda_probe(struct hda_device *dev) {
    uint16_t gcap;
    uint32_t gctl;

    gcap = hda_read16(dev, HDA_GCAP);
    kernel_debug_printf("hda: gcap oss=%u iss=%u bss=%u nsdo=%u\n",
                        (unsigned int)(gcap & 0xF),
                        (unsigned int)((gcap >> 4) & 0xF),
                        (unsigned int)((gcap >> 8) & 0xF),
                        (unsigned int)((gcap >> 12) & 0x3));

    gctl = hda_read32(dev, HDA_GCTL);
    gctl &= ~HDA_GCTL_CRST;
    hda_write32(dev, HDA_GCTL, gctl);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_GCTL) & HDA_GCTL_CRST) == 0u) {
            break;
        }
        io_wait();
    }

    gctl = hda_read32(dev, HDA_GCTL);
    gctl |= HDA_GCTL_CRST;
    hda_write32(dev, HDA_GCTL, gctl);

    for (uint32_t i = 0; i < HDA_TIMEOUT; i++) {
        if ((hda_read32(dev, HDA_GCTL) & HDA_GCTL_CRST) != 0u) {
            break;
        }
        io_wait();
    }

    dev->corb = (uint8_t *)kernel_malloc_aligned(HDA_CORB_ENTRIES * 4u, 128);
    dev->rirb = (uint8_t *)kernel_malloc_aligned(HDA_RIRB_ENTRIES * 8u, 128);
    if (dev->corb == 0 || dev->rirb == 0) {
        return -1;
    }

    memset(dev->corb, 0, HDA_CORB_ENTRIES * 4u);
    memset(dev->rirb, 0, HDA_RIRB_ENTRIES * 8u);

    hda_corb_start(dev);

    if (hda_probe_codec(dev) != 0) {
        kernel_debug_printf("hda: codec probe failed\n");
        return -1;
    }

    if (hda_setup_dma(dev) != 0) {
        kernel_debug_printf("hda: dma setup failed\n");
        return -1;
    }

    hda_start_playback(dev);

    if (dev->irq_line != 0u) {
        kernel_irq_register_handler(dev->irq_line, kernel_hda_irq_handler);
        kernel_irq_unmask(dev->irq_line);
    }

    dev->present = 1;
    return 0;
}

void kernel_hda_irq_handler(void) {
    struct hda_device *dev = &g_hda;
    uint8_t sd_sts;
    uint32_t flags;

    sd_sts = hda_read8(dev, HDA_SD_STS_OFF(HDA_STREAM_TAG));
    if ((sd_sts & (HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) != 0u) {
        flags = spinlock_lock_irqsave(&dev->lock);
        if ((sd_sts & HDA_SD_STS_BCIS) != 0u) {
            uint32_t lpib = hda_read32(dev, HDA_SD_LPIB_OFF(HDA_STREAM_TAG));
            if (lpib <= dev->playback_filled) {
                dev->playback_pos = lpib;
            }
        }
        if ((sd_sts & HDA_SD_STS_FIFOE) != 0u || (sd_sts & HDA_SD_STS_DESE) != 0u) {
            dev->playback_filled = 0;
            dev->playback_pos = 0;
        }
        spinlock_unlock_irqrestore(&dev->lock, flags);
        hda_write8(dev, HDA_SD_STS_OFF(HDA_STREAM_TAG), sd_sts & HDA_SD_STS_ACK);
    }
}

int kernel_hda_playback_write(const uint8_t *data, uint32_t size) {
    struct hda_device *dev = &g_hda;
    uint32_t flags;
    uint32_t written = 0;

    if (!dev->present || !dev->dma_buf_valid || data == 0 || size == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    while (written < size) {
        uint32_t buf_avail = HDA_DMA_BUF_SZ - dev->playback_filled;
        uint32_t chunk = size - written;

        if (buf_avail == 0) {
            break;
        }

        if (chunk > buf_avail) {
            chunk = buf_avail;
        }

        memcpy(dev->dma_buf + dev->playback_filled, data + written, chunk);
        dev->playback_filled += chunk;
        dev->playback_pos = (dev->playback_pos + chunk) % HDA_DMA_BUF_SZ;
        written += chunk;

        {
            uint32_t lpib = hda_read32(dev, HDA_SD_LPIB_OFF(HDA_STREAM_TAG));
            if (lpib < dev->playback_filled - HDA_PERIOD_BYTES) {
                break;
            }
        }
    }

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return (int)written;
}

int kernel_hda_present(void) {
    return g_hda.present;
}

int kernel_hda_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_hda.present;
        st->active = (uint32_t)(g_hda.present && g_hda.dma_buf_valid);
        return 0;
    }
    case AUDIO_IOCTL_GET_INFO: {
        struct { uint32_t rate; uint8_t channels; uint8_t bits; } *info = (void *)arg;
        if (!info || !g_hda.present) return -1;
        info->rate = HDA_SAMPLE_RATE;
        info->channels = HDA_CHANNELS;
        info->bits = HDA_BPS;
        return 0;
    }
    case AUDIO_IOCTL_PLAYBACK: {
        struct { const uint8_t *data; uint32_t size; } *p = (void *)arg;
        if (!p) return -1;
        return kernel_hda_playback_write(p->data, p->size);
    }
    default:
        return -1;
    }
}

int kernel_hda_init(void) {
    pci_device_t *hda_dev;

    if (!g_registered) {
        register_driver("hda", "audio", (void (*)(void))kernel_hda_init);
        g_registered = 1;
        return 0;
    }

    kernel_debug_puts("audio: probing intel hda\n");

    hda_dev = pci_find_by_class(HDA_PCI_CLASS, HDA_PCI_SUBCLASS);
    if (hda_dev == 0) {
        kernel_debug_puts("audio: hda controller not found\n");
        return -1;
    }

    {
        uint16_t vendor_check = hda_dev->vendor_id;
        uint16_t device_check = hda_dev->device_id;
        int unsupported = 1;

        if (vendor_check == HDA_VENDOR_INTEL) {
            switch (device_check) {
            case HDA_DEVICE_INTEL_82801F:
            case HDA_DEVICE_INTEL_82801I:
            case HDA_DEVICE_INTEL_82801JI:
            case HDA_DEVICE_INTEL_82801H:
            case HDA_DEVICE_INTEL_PCH:
            case HDA_DEVICE_INTEL_SKL:
            case HDA_DEVICE_INTEL_KBL:
            case HDA_DEVICE_INTEL_CNL:
            case HDA_DEVICE_INTEL_CFL:
            case HDA_DEVICE_INTEL_ICL:
            case HDA_DEVICE_INTEL_TGL:
                unsupported = 0;
                break;
            default:
                if (device_check == HDA_DEVICE_INTEL_GENERIC) {
                    unsupported = 0;
                }
                break;
            }
        }
        if (vendor_check == HDA_DEVICE_NVIDIA_GENERIC) {
            unsupported = 0;
        }
        if (unsupported) {
            kernel_debug_printf("audio: unsupported hda vendor=%x device=%x\n",
                                (unsigned int)vendor_check, (unsigned int)device_check);
            return -1;
        }
    }

    memset(&g_hda, 0, sizeof(g_hda));
    g_hda.bus = hda_dev->bus;
    g_hda.slot = hda_dev->slot;
    g_hda.function = hda_dev->function;
    g_hda.irq_line = hda_dev->irq_line;
    spinlock_init(&g_hda.lock);

    for (int bar = 0; bar < 6; bar++) {
        if (pci_bar_type(hda_dev, bar) == PCI_BAR_TYPE_MEM32) {
            uintptr_t base = pci_bar_addr(hda_dev, bar);
            size_t size = pci_bar_size(hda_dev, bar);
            if (base != 0u && size != 0u) {
                g_hda.mmio_base = base;
                g_hda.mmio_size = size;
                break;
            }
        }
    }

    if (g_hda.mmio_base == 0) {
        kernel_debug_printf("audio: hda no mmio bar\n");
        return -1;
    }

    {
        uint32_t cmd = kernel_pci_config_read_u32(hda_dev->bus, hda_dev->slot, hda_dev->function, 0x04u);
        cmd |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
        kernel_pci_config_write_u32(hda_dev->bus, hda_dev->slot, hda_dev->function, 0x04u, cmd);
    }

    kernel_debug_printf("hda: controller %d:%d.%d mmio=%p irq=%d\n",
                        (int)hda_dev->bus, (int)hda_dev->slot, (int)hda_dev->function,
                        (void *)g_hda.mmio_base, (int)g_hda.irq_line);

    if (hda_probe(&g_hda) != 0) {
        kernel_debug_puts("audio: hda probe failed\n");
        return -1;
    }

    kernel_debug_printf("audio: hda ready at %d:%d.%d\n",
                        (int)hda_dev->bus, (int)hda_dev->slot, (int)hda_dev->function);
    return 0;
}
