#include <kernel/kernel_string.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

#define MK_AUDIO_SOFT_BUFFER_SIZE 16384u
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_MULTIMEDIA_AUDIO 0x01u
#define PCI_SUBCLASS_MULTIMEDIA_HDA 0x03u
#define PCI_VENDOR_INTEL 0x8086u
#define PCI_VENDOR_AMD 0x1022u
#define PCI_VENDOR_ATI 0x1002u
#define PCI_VENDOR_NVIDIA 0x10DEu
#define PCI_VENDOR_ALI 0x10B9u
#define PCI_VENDOR_SIS 0x1039u
#define PCI_VENDOR_VIATECH 0x1106u

#define PCI_PRODUCT_INTEL_82801AA_ACA 0x2415u
#define PCI_PRODUCT_INTEL_82801AB_ACA 0x2425u
#define PCI_PRODUCT_INTEL_82801BA_ACA 0x2445u
#define PCI_PRODUCT_INTEL_82801CA_ACA 0x2485u
#define PCI_PRODUCT_INTEL_82801DB_ACA 0x24c5u
#define PCI_PRODUCT_INTEL_82801EB_ACA 0x24d5u
#define PCI_PRODUCT_INTEL_6300ESB_ACA 0x25a6u
#define PCI_PRODUCT_INTEL_82801FB_ACA 0x266eu
#define PCI_PRODUCT_INTEL_6321ESB_ACA 0x2698u
#define PCI_PRODUCT_INTEL_82801GB_ACA 0x27deu
#define PCI_PRODUCT_INTEL_82440MX_ACA 0x7195u
#define PCI_PRODUCT_AMD_768_ACA 0x7445u
#define PCI_PRODUCT_AMD_8111_ACA 0x746du
#define PCI_PRODUCT_ATI_SB200_AUDIO 0x4341u
#define PCI_PRODUCT_ATI_SB300_AUDIO 0x4361u
#define PCI_PRODUCT_ATI_SB400_AUDIO 0x4370u
#define PCI_PRODUCT_ATI_SB600_AUDIO 0x4382u
#define PCI_PRODUCT_NVIDIA_MCP04_AC97 0x003au
#define PCI_PRODUCT_NVIDIA_NFORCE4_AC97 0x0059u
#define PCI_PRODUCT_NVIDIA_NFORCE2_AC97 0x006au
#define PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97 0x008au
#define PCI_PRODUCT_NVIDIA_NFORCE3_AC97 0x00dau
#define PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97 0x00eau
#define PCI_PRODUCT_NVIDIA_NFORCE_AC97 0x01b1u
#define PCI_PRODUCT_NVIDIA_MCP51_AC97 0x026bu
#define PCI_PRODUCT_ALI_M5455_AC97 0x5455u
#define PCI_PRODUCT_SIS_7012_AC97 0x7012u
#define PCI_PRODUCT_VIATECH_VT82C686A_AC97 0x3058u
#define PCI_PRODUCT_VIATECH_VT8233_AC97 0x3059u

#define AUICH_NAMBAR_INDEX 0u
#define AUICH_NABMBAR_INDEX 1u
#define AUICH_MMBAR_INDEX 2u
#define AUICH_MBBAR_INDEX 3u
#define AUICH_CFG 0x41u
#define AUICH_CFG_IOSE 0x01u
#define AUICH_GCTRL 0x2cu
#define AUICH_GSTS 0x30u
#define AUICH_CAS 0x34u
#define AUICH_PCMI 0x00u
#define AUICH_PCMO 0x10u
#define AUICH_BDBAR 0x00u
#define AUICH_CIV 0x04u
#define AUICH_LVI 0x05u
#define AUICH_LVI_MASK 0x1fu
#define AUICH_STS 0x06u
#define AUICH_PICB 0x08u
#define AUICH_PIV 0x0au
#define AUICH_CTRL 0x0bu
#define AUICH_PCR 0x00100u
#define AUICH_ACLSO 0x08u
#define AUICH_WRESET 0x04u
#define AUICH_CRESET 0x02u
#define AUICH_IOCE 0x10u
#define AUICH_FEIE 0x08u
#define AUICH_LVBIE 0x04u
#define AUICH_RR 0x02u
#define AUICH_RPBM 0x01u
#define AUICH_FIFOE 0x10u
#define AUICH_BCIS 0x08u
#define AUICH_LVBCI 0x04u
#define AUICH_CELV 0x02u
#define AUICH_DCH 0x01u
#define AUICH_SEMATIMO 1000u
#define AUICH_RESETIMO 500000u
#define AUICH_DMALIST_MAX 32u
#define AUICH_DMAF_IOC 0x80000000u
#define AUICH_DMA_SLOT_SIZE 512u
#define ICH_SIS_NV_CTL 0x4cu
#define ICH_SIS_CTL_UNMUTE 0x01u

#define AC97_REG_RESET 0x00u
#define AC97_REG_MASTER_VOLUME 0x02u
#define AC97_REG_HEADPHONE_VOLUME 0x04u
#define AC97_REG_MIC_VOLUME 0x0eu
#define AC97_REG_LINEIN_VOLUME 0x10u
#define AC97_REG_PCMOUT_VOLUME 0x18u
#define AC97_REG_RECORD_SELECT 0x1au
#define AC97_REG_RECORD_GAIN 0x1cu
#define AC97_REG_EXT_AUDIO_ID 0x28u
#define AC97_REG_EXT_AUDIO_CTRL 0x2au
#define AC97_REG_PCM_FRONT_DAC_RATE 0x2cu
#define AC97_REG_PCM_SURR_DAC_RATE 0x2eu
#define AC97_REG_PCM_LFE_DAC_RATE 0x30u
#define AC97_REG_PCM_LR_ADC_RATE 0x32u
#define AC97_REG_CENTER_LFE_MASTER 0x36u
#define AC97_REG_SURR_MASTER 0x38u
#define AC97_CAPS_MICIN 0x0001u
#define AC97_CAPS_HEADPHONES 0x0010u
#define AC97_EXT_AUDIO_VRA 0x0001u
#define AC97_EXT_AUDIO_CDAC 0x0040u
#define AC97_EXT_AUDIO_SDAC 0x0080u
#define AC97_EXT_AUDIO_LDAC 0x0100u
#define AC97_RECORD_SOURCE_MIC 0u
#define AC97_RECORD_SOURCE_LINEIN 4u

#define HDA_BAR_INDEX 0u
#define HDA_GCAP 0x00u
#define HDA_GCAP_ISS_SHIFT 8u
#define HDA_GCAP_ISS_MASK 0x0fu
#define HDA_GCAP_OSS_SHIFT 12u
#define HDA_GCAP_OSS_MASK 0x0fu
#define HDA_VMIN 0x02u
#define HDA_VMAJ 0x03u
#define HDA_GCTL 0x08u
#define HDA_GCTL_CRST 0x00000001u
#define HDA_GCTL_UNSOL 0x00000100u
#define HDA_STATESTS 0x0eu
#define HDA_GSTS 0x10u
#define HDA_GSTS_FSTS 0x0002u
#define HDA_INTCTL 0x20u
#define HDA_INTCTL_GIE 0x80000000u
#define HDA_INTCTL_CIE 0x40000000u
#define HDA_INTSTS 0x24u
#define HDA_CORBLBASE 0x40u
#define HDA_CORBUBASE 0x44u
#define HDA_CORBWP 0x48u
#define HDA_CORBRP 0x4au
#define HDA_CORBRP_CORBRPRST 0x8000u
#define HDA_CORBCTL 0x4cu
#define HDA_CORBCTL_CORBRUN 0x02u
#define HDA_CORBSTS 0x4du
#define HDA_CORBSIZE 0x4eu
#define HDA_CORBSIZE_CAP_2 0x10u
#define HDA_CORBSIZE_CAP_16 0x20u
#define HDA_CORBSIZE_CAP_256 0x40u
#define HDA_CORBSIZE_SEL_2 0x00u
#define HDA_CORBSIZE_SEL_16 0x01u
#define HDA_CORBSIZE_SEL_256 0x02u
#define HDA_RIRBLBASE 0x50u
#define HDA_RIRBUBASE 0x54u
#define HDA_RIRBWP 0x58u
#define HDA_RIRBWP_RST 0x8000u
#define HDA_RINTCNT 0x5au
#define HDA_RIRBCTL 0x5cu
#define HDA_RIRBCTL_DMAEN 0x02u
#define HDA_RIRBSTS 0x5du
#define HDA_RIRBSTS_RINTFL 0x01u
#define HDA_RIRBSIZE 0x5eu
#define HDA_RIRBSIZE_CAP_2 0x10u
#define HDA_RIRBSIZE_CAP_16 0x20u
#define HDA_RIRBSIZE_CAP_256 0x40u
#define HDA_RIRBSIZE_SEL_2 0x00u
#define HDA_RIRBSIZE_SEL_16 0x01u
#define HDA_RIRBSIZE_SEL_256 0x02u
#define HDA_SD_BASE 0x80u
#define HDA_SD_CTL 0x00u
#define HDA_SD_CTL_DEIE 0x0010u
#define HDA_SD_CTL_FEIE 0x0008u
#define HDA_SD_CTL_IOCE 0x0004u
#define HDA_SD_CTL_RUN 0x0002u
#define HDA_SD_CTL_SRST 0x0001u
#define HDA_SD_CTL2 0x02u
#define HDA_SD_CTL2_STRM 0xf0u
#define HDA_SD_CTL2_STRM_SHIFT 4u
#define HDA_SD_STS 0x03u
#define HDA_SD_STS_DESE 0x10u
#define HDA_SD_STS_FIFOE 0x08u
#define HDA_SD_STS_BCIS 0x04u
#define HDA_SD_LPIB 0x04u
#define HDA_SD_CBL 0x08u
#define HDA_SD_LVI 0x0cu
#define HDA_SD_LVI_MASK 0x00ffu
#define HDA_SD_FMT 0x12u
#define HDA_SD_FMT_BASE_48 0x0000u
#define HDA_SD_FMT_BASE_44 0x4000u
#define HDA_SD_FMT_MULT_X1 0x0000u
#define HDA_SD_FMT_MULT_X2 0x0800u
#define HDA_SD_FMT_MULT_X4 0x1800u
#define HDA_SD_FMT_DIV_BY1 0x0000u
#define HDA_SD_FMT_DIV_BY2 0x0100u
#define HDA_SD_FMT_DIV_BY3 0x0200u
#define HDA_SD_FMT_DIV_BY4 0x0300u
#define HDA_SD_FMT_DIV_BY6 0x0500u
#define HDA_SD_FMT_BITS_8_16 0x0000u
#define HDA_SD_FMT_BITS_16_16 0x0010u
#define HDA_SD_FMT_BITS_20_32 0x0020u
#define HDA_SD_FMT_BITS_24_32 0x0030u
#define HDA_SD_FMT_BITS_32_32 0x0040u
#define HDA_SD_BDPL 0x18u
#define HDA_SD_BDPU 0x1cu
#define HDA_SD_SIZE 0x20u
#define HDA_RESET_TIMEOUT 100000u
#define HDA_CORB_TIMEOUT 100000u
#define HDA_MAX_CODECS 15u
#define HDA_BDL_MAX 32u
#define HDA_IC 0x60u
#define HDA_IR 0x64u
#define HDA_IRS 0x68u
#define HDA_IRS_VALID 0x02u
#define HDA_IRS_BUSY 0x01u

#define HDA_VERB_GET_PARAMETER 0xf00u
#define HDA_VERB_GET_CONFIG_DEFAULT 0xf1cu
#define HDA_VERB_SET_POWER_STATE 0x705u
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300u
#define HDA_VERB_SET_CONVERTER_FORMAT 0x2u
#define HDA_VERB_SET_STREAM_CHANNEL 0x706u
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707u
#define HDA_VERB_SET_EAPD_BTLENABLE 0x70cu
#define HDA_PARAM_VENDOR_ID 0x00u
#define HDA_PARAM_SUB_NODE_COUNT 0x04u
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05u
#define HDA_PARAM_AUDIO_WIDGET_CAP 0x09u
#define HDA_PARAM_PIN_CAP 0x0cu
#define HDA_PARAM_INPUT_AMP_CAP 0x0du
#define HDA_PARAM_CONN_LIST_LEN 0x0eu
#define HDA_PARAM_OUTPUT_AMP_CAP 0x12u
#define HDA_FGTYPE_AUDIO 0x01u
#define HDA_WCAP_INAMP 0x00000002u
#define HDA_WCAP_OUTAMP 0x00000004u
#define HDA_WCAP_TYPE_SHIFT 20u
#define HDA_WCAP_TYPE_MASK 0x0fu
#define HDA_WID_AUD_OUT 0x0u
#define HDA_WID_AUD_IN 0x1u
#define HDA_WID_AUD_MIXER 0x2u
#define HDA_WID_AUD_SELECTOR 0x3u
#define HDA_WID_PIN 0x4u
#define HDA_WCAP_CONNLIST 0x00000100u
#define HDA_CONNLIST_LONG 0x00000080u
#define HDA_CONNLIST_LEN_MASK 0x0000007fu
#define HDA_PINCAP_HEADPHONE 0x00000008u
#define HDA_PINCAP_OUTPUT 0x00000010u
#define HDA_PINCAP_INPUT 0x00000020u
#define HDA_PINCAP_HDMI 0x00000080u
#define HDA_PINCAP_EAPD 0x00010000u
#define HDA_CONFIG_DEVICE_SHIFT 20u
#define HDA_CONFIG_DEVICE_MASK 0x0fu
#define HDA_CONFIG_DEVICE_LINEOUT 0x0u
#define HDA_CONFIG_DEVICE_SPEAKER 0x1u
#define HDA_CONFIG_DEVICE_HEADPHONE 0x2u
#define HDA_CONFIG_DEVICE_SPDIFOUT 0x4u
#define HDA_CONFIG_DEVICE_DIGITALOUT 0x5u
#define HDA_CONFIG_DEVICE_LINEIN 0x8u
#define HDA_QEMU_VENDOR_ID 0x1af40000u
#define HDA_QEMU_CODEC_OUTPUT 0x1af40010u
#define HDA_QEMU_CODEC_DUPLEX 0x1af40020u
#define HDA_CONFIG_DEVICE_MICIN 0xau
#define HDA_POWER_STATE_D0 0x00u
#define HDA_VERB_SET_CONNECTION_SELECT 0x701u
#define HDA_VERB_GET_CONNECTION_LIST_ENTRY 0xf02u
#define HDA_PINCTL_OUT_EN 0x40u
#define HDA_PINCTL_HP_EN 0x80u
#define HDA_EAPD_ENABLE 0x02u
#define HDA_AMP_GAIN_MASK 0x7fu
#define HDA_AMP_GAIN_MUTE 0x80u
#define HDA_AMP_GAIN_INDEX_SHIFT 8u
#define HDA_AMP_GAIN_RIGHT 0x1000u
#define HDA_AMP_GAIN_LEFT 0x2000u
#define HDA_AMP_GAIN_INPUT 0x4000u
#define HDA_AMP_GAIN_OUTPUT 0x8000u

#define MK_AUDIO_STATUS_BACKEND_MASK 0x000000ffu
#define MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED 0x00000100u
#define MK_AUDIO_STATUS_FLAG_IRQ_SEEN 0x00000200u
#define MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ 0x00000400u
#define MK_AUDIO_STATUS_FLAG_STARVATION 0x00000800u
#define MK_AUDIO_STATUS_FLAG_UNDERRUN 0x00001000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_DATA 0x00002000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN 0x00004000u

enum mk_audio_backend_kind {
    MK_AUDIO_BACKEND_SOFT = 0,
    MK_AUDIO_BACKEND_COMPAT_AUICH = 1,
    MK_AUDIO_BACKEND_COMPAT_AZALIA = 2
};

struct mk_audio_backend_ops {
    int (*start)(void);
    int (*stop)(void);
    int (*write)(const uint8_t *data, uint32_t size);
};

struct mk_audio_auich_dmalist {
    uint32_t base;
    uint32_t len;
};

struct mk_audio_hda_rirb_entry {
    uint32_t response;
    uint32_t response_ex;
};

struct mk_audio_hda_bdl_entry {
    uint32_t low;
    uint32_t high;
    uint32_t length;
    uint32_t flags;
};

struct mk_audio_service_state {
    struct mk_audio_info info;
    enum mk_audio_backend_kind backend_kind;
    struct kernel_pci_device_info pci;
    uintptr_t compat_mix_base;
    uintptr_t compat_aud_base;
    uintptr_t azalia_base;
    uint16_t compat_caps;
    uint16_t compat_ext_audio_id;
    uint16_t azalia_gcap;
    uint16_t azalia_codec_mask;
    uint16_t azalia_corb_entries;
    uint16_t azalia_rirb_entries;
    uint16_t azalia_output_fmt;
    uint8_t compat_ready;
    uint8_t compat_codec_ready;
    uint8_t compat_irq_registered;
    uint8_t compat_mix_is_mmio;
    uint8_t compat_aud_is_mmio;
    uint8_t compat_ignore_codecready;
    uint8_t azalia_ready;
    uint8_t azalia_irq_registered;
    uint8_t azalia_vmaj;
    uint8_t azalia_vmin;
    uint8_t azalia_codec_address;
    uint8_t azalia_afg_nid;
    uint8_t azalia_codec_probed;
    uint8_t azalia_corb_ready;
    uint8_t azalia_widget_probed;
    uint8_t azalia_path_programmed;
    uint8_t azalia_output_dac_nid;
    uint8_t azalia_output_pin_nid;
    uint8_t azalia_input_dac_nid;
    uint8_t azalia_input_pin_nid;
    uint8_t azalia_output_stream_index;
    uint8_t azalia_output_stream_number;
    uint8_t azalia_output_running;
    uint8_t azalia_output_pin_nids[4];
    uint8_t azalia_output_dac_nids[4];
    uint8_t azalia_input_pin_nids[2];
    int8_t azalia_output_priorities[4];
    uint8_t compat_output_running;
    uint8_t compat_output_producer;
    uint8_t compat_output_consumer;
    uint8_t compat_output_hw_civ;
    uint8_t compat_output_pending;
    uint8_t compat_input_running;
    uint8_t compat_input_producer;
    uint8_t compat_input_consumer;
    uint8_t compat_input_hw_civ;
    uint8_t compat_input_pending;
    uint8_t output_level;
    uint8_t input_level;
    uint8_t output_muted;
    uint8_t input_muted;
    uint8_t default_output;
    uint8_t default_input;
    uint8_t playback_buffer[MK_AUDIO_SOFT_BUFFER_SIZE];
    uint8_t capture_buffer[MK_AUDIO_SOFT_BUFFER_SIZE];
    uint32_t playback_head;
    uint32_t playback_tail;
    uint32_t playback_fill;
    uint32_t capture_head;
    uint32_t capture_tail;
    uint32_t capture_fill;
    uint32_t playback_bytes_written;
    uint32_t playback_write_calls;
    uint32_t playback_bytes_consumed;
    uint32_t playback_xruns;
    uint32_t playback_starvations;
    uint32_t playback_underruns;
    uint32_t compat_irq_count;
    uint32_t azalia_irq_count;
    uint32_t azalia_vendor_id;
    uint32_t azalia_output_mask;
    uint32_t azalia_input_mask;
    uint32_t azalia_output_regbase;
    uint32_t azalia_output_bytes;
    uint32_t azalia_output_pos;
    uint32_t azalia_output_start_tick;
    uint32_t azalia_output_deadline_tick;
    uint32_t capture_bytes_captured;
    uint32_t capture_read_calls;
    uint32_t capture_bytes_read;
    uint32_t capture_xruns;
};

static const struct mk_audio_control_info g_audio_controls[] = {
    {MK_AUDIO_MIXER_OUTPUT_LEVEL, MK_AUDIO_CONTROL_LEVEL, MK_AUDIO_MIXER_OUTPUT_MUTE, 0u, AudioNoutput, AudioNvolume},
    {MK_AUDIO_MIXER_OUTPUT_MUTE, MK_AUDIO_CONTROL_TOGGLE, MK_AUDIO_MIXER_OUTPUT_LEVEL, 0u, AudioNoutput, AudioNmute},
    {MK_AUDIO_MIXER_INPUT_LEVEL, MK_AUDIO_CONTROL_LEVEL, MK_AUDIO_MIXER_INPUT_MUTE, 0u, AudioNinput, AudioNvolume},
    {MK_AUDIO_MIXER_INPUT_MUTE, MK_AUDIO_CONTROL_TOGGLE, MK_AUDIO_MIXER_INPUT_LEVEL, 0u, AudioNinput, AudioNmute},
    {MK_AUDIO_MIXER_OUTPUT_DEFAULT, MK_AUDIO_CONTROL_ENUM, MK_AUDIO_MIXER_OUTPUT_LEVEL, 0u, AudioNoutput, AudioNsource},
    {MK_AUDIO_MIXER_INPUT_DEFAULT, MK_AUDIO_CONTROL_ENUM, MK_AUDIO_MIXER_INPUT_LEVEL, 0u, AudioNinput, AudioNsource}
};

static struct mk_message g_last_audio_request;
static struct mk_message g_last_audio_reply;
static struct mk_audio_service_state g_audio_state;
static int mk_audio_backend_soft_start(void);
static int mk_audio_backend_soft_stop(void);
static int mk_audio_backend_soft_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_compat_start(void);
static int mk_audio_backend_compat_stop(void);
static int mk_audio_backend_compat_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_compat_read(uint8_t *data, uint32_t size);
static void mk_audio_compat_irq_handler(void);
static int mk_audio_backend_azalia_start(void);
static int mk_audio_backend_azalia_stop(void);
static int mk_audio_backend_azalia_write(const uint8_t *data, uint32_t size);
static void mk_audio_azalia_irq_handler(void);
static int mk_audio_azalia_init_command_rings(void);
static int mk_audio_azalia_command_raw(uint8_t codec, uint8_t nid, uint32_t verb_payload, uint32_t *response_out);
static int mk_audio_azalia_command(uint8_t codec, uint8_t nid, uint16_t verb, uint8_t payload, uint32_t *response_out);
static int mk_audio_azalia_probe_codec(void);
static int mk_audio_azalia_probe_widgets(void);
static int mk_audio_azalia_format_from_params(const struct audio_swpar *params, uint16_t *fmt_out);
static int mk_audio_azalia_stream_reset(void);
static void mk_audio_azalia_stream_halt(void);
static int mk_audio_azalia_stream_start_buffer(uint32_t bytes);
static void mk_audio_azalia_update_output_progress(void);
static int mk_audio_azalia_program_output_path(void);
static void mk_audio_debug_azalia_route(uint8_t pin_nid, uint8_t dac_nid, int selected_bit);
static void mk_audio_azalia_power_widget(uint8_t nid);
static int mk_audio_azalia_power_output_path(uint8_t nid, uint8_t target_dac, uint32_t depth);
static void mk_audio_azalia_program_widget_amp(uint8_t nid, uint8_t input_amp, uint8_t index);
static void mk_audio_azalia_prime_output_pin(uint8_t pin_nid, int output_bit);
static int mk_audio_azalia_program_output_amps(uint8_t nid, uint8_t target_dac, uint32_t depth);
static int mk_audio_azalia_get_connections(uint8_t nid,
                                           uint8_t *connections,
                                           uint32_t max_connections,
                                           uint32_t *count_out);
static int mk_audio_azalia_find_output_dac(uint8_t nid, uint32_t depth);
static int mk_audio_azalia_select_output_route(uint8_t nid, uint8_t target_dac, uint32_t depth);
static int mk_audio_output_bit_from_ord(uint32_t mask, uint8_t ord);
static int mk_audio_azalia_register_output_path(uint32_t output_mask, uint8_t pin_nid, uint8_t dac_nid, int priority);
static void mk_audio_azalia_register_input_path(uint32_t input_mask, uint8_t pin_nid);
static uint32_t mk_audio_hda_config_device(uint32_t config_default);
static uint32_t mk_audio_hda_output_mask(uint32_t pin_caps, uint32_t config_default);
static uint32_t mk_audio_hda_input_mask(uint32_t pin_caps, uint32_t config_default);
static int mk_audio_hda_output_priority(uint32_t pin_caps, uint32_t config_default);
static void mk_audio_normalize_params(struct audio_swpar *params);
static void mk_audio_compat_sync_codec_caps(void);
static void mk_audio_compat_apply_device_quirks(void);
static uint32_t mk_audio_output_presence_mask(void);
static void mk_audio_refresh_topology_snapshot(void);
static void mk_audio_compat_update_input_progress(void);
static void mk_audio_capture_ring_write(const uint8_t *data, uint32_t size);
static int mk_audio_capture_ring_read(uint8_t *data, uint32_t size);
static int mk_audio_compat_capture_block(uint32_t requested_bytes);
static uint32_t mk_audio_estimated_playback_ticks(const struct audio_swpar *params, uint32_t data_size);
static const struct mk_audio_backend_ops g_audio_backend_soft = {
    mk_audio_backend_soft_start,
    mk_audio_backend_soft_stop,
    mk_audio_backend_soft_write
};
static const struct mk_audio_backend_ops g_audio_backend_compat = {
    mk_audio_backend_compat_start,
    mk_audio_backend_compat_stop,
    mk_audio_backend_compat_write
};
static const struct mk_audio_backend_ops g_audio_backend_azalia = {
    mk_audio_backend_azalia_start,
    mk_audio_backend_azalia_stop,
    mk_audio_backend_azalia_write
};
static const struct mk_audio_backend_ops *g_audio_backend = &g_audio_backend_soft;
static struct mk_audio_auich_dmalist g_audio_auich_pcmo_dmalist[AUICH_DMALIST_MAX]
    __attribute__((aligned(8)));
static uint8_t g_audio_auich_pcmo_buffers[AUICH_DMALIST_MAX][AUICH_DMA_SLOT_SIZE]
    __attribute__((aligned(2)));
static uint16_t g_audio_auich_pcmo_bytes[AUICH_DMALIST_MAX];
static struct mk_audio_auich_dmalist g_audio_auich_pcmi_dmalist[AUICH_DMALIST_MAX]
    __attribute__((aligned(8)));
static uint8_t g_audio_auich_pcmi_buffers[AUICH_DMALIST_MAX][AUICH_DMA_SLOT_SIZE]
    __attribute__((aligned(2)));
static uint16_t g_audio_auich_pcmi_bytes[AUICH_DMALIST_MAX];
static uint32_t g_audio_azalia_corb[256] __attribute__((aligned(128)));
static struct mk_audio_hda_rirb_entry g_audio_azalia_rirb[256] __attribute__((aligned(128)));
static struct mk_audio_hda_bdl_entry g_audio_azalia_bdl[HDA_BDL_MAX] __attribute__((aligned(128)));
static uint8_t g_audio_azalia_output_buffer[MK_AUDIO_SOFT_BUFFER_SIZE] __attribute__((aligned(128)));
static uint8_t mk_audio_compat_read8(uintptr_t base, uint8_t mmio, uint16_t reg);
static uint16_t mk_audio_compat_read16(uintptr_t base, uint8_t mmio, uint16_t reg);
static uint32_t mk_audio_compat_read32(uintptr_t base, uint8_t mmio, uint16_t reg);
static void mk_audio_compat_write8(uintptr_t base, uint8_t mmio, uint16_t reg, uint8_t value);
static void mk_audio_compat_write16(uintptr_t base, uint8_t mmio, uint16_t reg, uint16_t value);
static void mk_audio_compat_write32(uintptr_t base, uint8_t mmio, uint16_t reg, uint32_t value);
static uint8_t mk_audio_azalia_read8(uintptr_t base, uint16_t reg);
static uint16_t mk_audio_azalia_read16(uintptr_t base, uint16_t reg);
static uint32_t mk_audio_azalia_read32(uintptr_t base, uint16_t reg);
static void mk_audio_azalia_write8(uintptr_t base, uint16_t reg, uint8_t value);
static void mk_audio_azalia_write16(uintptr_t base, uint16_t reg, uint16_t value);
static void mk_audio_azalia_write32(uintptr_t base, uint16_t reg, uint32_t value);

static void mk_audio_compat_debug_pcmi(const char *tag) {
    uint8_t civ;
    uint8_t lvi;
    uint8_t piv;
    uint8_t ctrl;
    uint16_t sts;
    uint16_t picb;

    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CIV) & AUICH_LVI_MASK;
    lvi = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_LVI) & AUICH_LVI_MASK;
    piv = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PIV) & AUICH_LVI_MASK;
    ctrl = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CTRL);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    picb = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB);
    kernel_debug_printf("audio: %s pcmi civ=%d lvi=%d piv=%d picb=%d sts=%x ctrl=%x run=%d pending=%d capfill=%d\n",
                        tag,
                        (int)civ,
                        (int)lvi,
                        (int)piv,
                        (int)picb,
                        (unsigned int)sts,
                        (unsigned int)ctrl,
                        (int)g_audio_state.compat_input_running,
                        (int)g_audio_state.compat_input_pending,
                        (int)g_audio_state.capture_fill);
}

static void mk_audio_compat_delay(void) {
    for (uint32_t i = 0u; i < 4u; ++i) {
        io_wait();
    }
}

static uint8_t mk_audio_compat_read8(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inb((uint16_t)(base + (uintptr_t)reg));
}

static uint16_t mk_audio_compat_read16(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inw((uint16_t)(base + (uintptr_t)reg));
}

static uint32_t mk_audio_compat_read32(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inl((uint16_t)(base + (uintptr_t)reg));
}

static void mk_audio_compat_write8(uintptr_t base, uint8_t mmio, uint16_t reg, uint8_t value) {
    if (mmio) {
        volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outb((uint16_t)(base + (uintptr_t)reg), value);
}

static void mk_audio_compat_write16(uintptr_t base, uint8_t mmio, uint16_t reg, uint16_t value) {
    if (mmio) {
        volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outw((uint16_t)(base + (uintptr_t)reg), value);
}

static void mk_audio_compat_write32(uintptr_t base, uint8_t mmio, uint16_t reg, uint32_t value) {
    if (mmio) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outl((uint16_t)(base + (uintptr_t)reg), value);
}

static uint8_t mk_audio_azalia_read8(uintptr_t base, uint16_t reg) {
    volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static uint16_t mk_audio_azalia_read16(uintptr_t base, uint16_t reg) {
    volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static uint32_t mk_audio_azalia_read32(uintptr_t base, uint16_t reg) {
    volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static void mk_audio_azalia_write8(uintptr_t base, uint16_t reg, uint8_t value) {
    volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static void mk_audio_azalia_write16(uintptr_t base, uint16_t reg, uint16_t value) {
    volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static void mk_audio_azalia_write32(uintptr_t base, uint16_t reg, uint32_t value) {
    volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static uint32_t mk_audio_current_pid(void) {
    process_t *current = scheduler_current();

    return current != 0 ? (uint32_t)current->pid : 0u;
}

static int mk_audio_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_AUDIO);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_audio_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_audio_reply_result(struct mk_message *reply, int value) {
    struct mk_audio_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static void mk_audio_copy_limited(char *dst, const char *src, size_t dst_size) {
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static void mk_audio_debug_append_text(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (dst == 0 || dst_size == 0u || src == 0) {
        return;
    }
    len = strlen(dst);
    if (len >= dst_size - 1u) {
        return;
    }
    strncpy(dst + len, src, dst_size - len - 1u);
    dst[dst_size - 1u] = '\0';
}

static void mk_audio_debug_append_hex(char *dst, size_t dst_size, uint32_t value, unsigned digits) {
    static const char hex_digits[] = "0123456789abcdef";
    char text[9];
    unsigned count = digits > 8u ? 8u : digits;

    if (dst == 0 || dst_size == 0u || count == 0u) {
        return;
    }
    for (unsigned i = 0u; i < count; ++i) {
        unsigned shift = (count - 1u - i) * 4u;
        text[i] = hex_digits[(value >> shift) & 0x0fu];
    }
    text[count] = '\0';
    mk_audio_debug_append_text(dst, dst_size, text);
}

static void mk_audio_debug_append_uint(char *dst, size_t dst_size, uint32_t value) {
    char text[12];
    size_t len = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    if (value == 0u) {
        mk_audio_debug_append_text(dst, dst_size, "0");
        return;
    }
    while (value != 0u && len < sizeof(text) - 1u) {
        text[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len > 0u) {
        char ch[2];

        ch[0] = text[--len];
        ch[1] = '\0';
        mk_audio_debug_append_text(dst, dst_size, ch);
    }
}

static void mk_audio_debug_azalia_widget(uint8_t nid,
                                         uint32_t type,
                                         uint32_t caps,
                                         uint32_t pin_caps,
                                         uint32_t config_default,
                                         int candidate_dac) {
    char line[160];

    line[0] = '\0';
    mk_audio_debug_append_text(line, sizeof(line), "audio: hda widget nid=");
    mk_audio_debug_append_uint(line, sizeof(line), nid);
    mk_audio_debug_append_text(line, sizeof(line), " type=");
    mk_audio_debug_append_uint(line, sizeof(line), type);
    mk_audio_debug_append_text(line, sizeof(line), " caps=0x");
    mk_audio_debug_append_hex(line, sizeof(line), caps, 8u);
    if (pin_caps != 0u) {
        mk_audio_debug_append_text(line, sizeof(line), " pin=0x");
        mk_audio_debug_append_hex(line, sizeof(line), pin_caps, 8u);
        mk_audio_debug_append_text(line, sizeof(line), " cfg=0x");
        mk_audio_debug_append_hex(line, sizeof(line), config_default, 8u);
    }
    if (candidate_dac >= 0) {
        mk_audio_debug_append_text(line, sizeof(line), " dac=");
        mk_audio_debug_append_uint(line, sizeof(line), (uint32_t)candidate_dac);
    }
    mk_audio_debug_append_text(line, sizeof(line), "\n");
    kernel_debug_puts(line);
}

static void mk_audio_debug_azalia_route(uint8_t pin_nid, uint8_t dac_nid, int selected_bit) {
    char line[96];

    line[0] = '\0';
    mk_audio_debug_append_text(line, sizeof(line), "audio: hda route out=");
    if (selected_bit >= 0) {
        mk_audio_debug_append_uint(line, sizeof(line), (uint32_t)selected_bit);
    } else {
        mk_audio_debug_append_text(line, sizeof(line), "fallback");
    }
    mk_audio_debug_append_text(line, sizeof(line), " pin=");
    mk_audio_debug_append_uint(line, sizeof(line), pin_nid);
    mk_audio_debug_append_text(line, sizeof(line), " dac=");
    mk_audio_debug_append_uint(line, sizeof(line), dac_nid);
    mk_audio_debug_append_text(line, sizeof(line), "\n");
    kernel_debug_puts(line);
}

static void mk_audio_normalize_params(struct audio_swpar *params) {
    unsigned int rate;

    if (params == 0) {
        return;
    }

    params->sig = 1u;
    params->le = 1u;
    params->bits = 16u;
    params->bps = 2u;
    params->msb = 1u;

    if (params->pchan != 1u && params->pchan != 2u) {
        params->pchan = 2u;
    }
    params->rchan = params->pchan;

    rate = params->rate;
    if (rate <= 16537u) {
        params->rate = 11025u;
    } else if (rate <= 33075u) {
        params->rate = 22050u;
    } else if (rate <= 46050u) {
        params->rate = 44100u;
    } else {
        params->rate = 48000u;
    }

    if (params->round == 0u || params->round == 0xffffffffu) {
        params->round = 512u;
    }
    if (params->round > AUICH_DMA_SLOT_SIZE) {
        params->round = AUICH_DMA_SLOT_SIZE;
    }
    if ((params->round & 1u) != 0u) {
        params->round -= 1u;
    }
    if (params->round == 0u) {
        params->round = 512u;
    }

    if (params->nblks == 0u || params->nblks == 0xffffffffu) {
        params->nblks = 4u;
    }
    if (params->nblks > AUICH_DMALIST_MAX) {
        params->nblks = AUICH_DMALIST_MAX;
    }
}

static uint32_t mk_audio_estimated_playback_ticks(const struct audio_swpar *params, uint32_t data_size) {
    uint32_t bytes_per_second;
    uint32_t duration_ms;
    uint32_t ticks;

    if (params == 0 || params->rate == 0u || params->pchan == 0u || params->bps == 0u || data_size == 0u) {
        return 0u;
    }

    bytes_per_second = params->rate * params->pchan * params->bps;
    if (bytes_per_second == 0u) {
        return 0u;
    }

    duration_ms = (data_size * 1000u) / bytes_per_second;
    if ((data_size * 1000u) % bytes_per_second != 0u) {
        duration_ms += 1u;
    }

    ticks = (duration_ms + 9u) / 10u;
    if (ticks < 3u) {
        ticks = 3u;
    }
    return ticks;
}

static int mk_audio_is_auich_device(uint16_t vendor_id, uint16_t device_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        switch (device_id) {
        case PCI_PRODUCT_INTEL_82801AA_ACA:
        case PCI_PRODUCT_INTEL_82801AB_ACA:
        case PCI_PRODUCT_INTEL_82801BA_ACA:
        case PCI_PRODUCT_INTEL_82801CA_ACA:
        case PCI_PRODUCT_INTEL_82801DB_ACA:
        case PCI_PRODUCT_INTEL_82801EB_ACA:
        case PCI_PRODUCT_INTEL_6300ESB_ACA:
        case PCI_PRODUCT_INTEL_82801FB_ACA:
        case PCI_PRODUCT_INTEL_6321ESB_ACA:
        case PCI_PRODUCT_INTEL_82801GB_ACA:
        case PCI_PRODUCT_INTEL_82440MX_ACA:
            return 1;
        default:
            return 0;
        }
    case PCI_VENDOR_AMD:
        return device_id == PCI_PRODUCT_AMD_768_ACA ||
               device_id == PCI_PRODUCT_AMD_8111_ACA;
    case PCI_VENDOR_ATI:
        return device_id == PCI_PRODUCT_ATI_SB200_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB300_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB400_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB600_AUDIO;
    case PCI_VENDOR_NVIDIA:
        switch (device_id) {
        case PCI_PRODUCT_NVIDIA_MCP04_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE4_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE2_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE3_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE_AC97:
        case PCI_PRODUCT_NVIDIA_MCP51_AC97:
            return 1;
        default:
            return 0;
        }
    case PCI_VENDOR_ALI:
        return device_id == PCI_PRODUCT_ALI_M5455_AC97;
    case PCI_VENDOR_SIS:
        return device_id == PCI_PRODUCT_SIS_7012_AC97;
    case PCI_VENDOR_VIATECH:
        return device_id == PCI_PRODUCT_VIATECH_VT82C686A_AC97 ||
               device_id == PCI_PRODUCT_VIATECH_VT8233_AC97;
    default:
        return 0;
    }
}

static int mk_audio_auich_prefers_native_mmio(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != PCI_VENDOR_INTEL) {
        return 0;
    }

    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801DB_ACA:
    case PCI_PRODUCT_INTEL_82801EB_ACA:
    case PCI_PRODUCT_INTEL_82801FB_ACA:
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return 1;
    default:
        return 0;
    }
}

static int mk_audio_auich_quirk_ignore_codecready(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != PCI_VENDOR_INTEL) {
        return 0;
    }

    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801DB_ACA:
    case PCI_PRODUCT_INTEL_82801EB_ACA:
    case PCI_PRODUCT_INTEL_82801FB_ACA:
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return 1;
    default:
        return 0;
    }
}

static int mk_audio_compat_wait_cas_clear(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }

    for (uint32_t i = 0u; i < AUICH_SEMATIMO; ++i) {
        if ((mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_CAS) & 0x1u) == 0u) {
            return 0;
        }
        mk_audio_compat_delay();
    }
    return g_audio_state.compat_ignore_codecready ? 0 : -1;
}

static int mk_audio_compat_read_codec(uint8_t reg, uint16_t *value_out) {
    if (value_out == 0 || g_audio_state.compat_mix_base == 0u) {
        return -1;
    }
    if (mk_audio_compat_wait_cas_clear() != 0) {
        return -1;
    }

    *value_out = mk_audio_compat_read16(g_audio_state.compat_mix_base, g_audio_state.compat_mix_is_mmio, reg);
    return 0;
}

static int mk_audio_compat_write_codec(uint8_t reg, uint16_t value) {
    if (g_audio_state.compat_mix_base == 0u) {
        return -1;
    }
    if (mk_audio_compat_wait_cas_clear() != 0) {
        return -1;
    }

    mk_audio_compat_write16(g_audio_state.compat_mix_base, g_audio_state.compat_mix_is_mmio, reg, value);
    return 0;
}

static int mk_audio_compat_reset_codec(void) {
    uint32_t control;

    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }

    control = mk_audio_compat_read32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GCTRL);
    control &= ~(uint32_t)AUICH_ACLSO;
    control |= (control & AUICH_CRESET) != 0u ? AUICH_WRESET : AUICH_CRESET;
    mk_audio_compat_write32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GCTRL, control);

    for (uint32_t i = 0u; i < AUICH_RESETIMO; ++i) {
        if ((mk_audio_compat_read32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GSTS) & AUICH_PCR) != 0u) {
            g_audio_state.compat_codec_ready = 1u;
            return 0;
        }
        mk_audio_compat_delay();
    }

    if (g_audio_state.compat_ignore_codecready) {
        g_audio_state.compat_codec_ready = 1u;
        return 0;
    }

    g_audio_state.compat_codec_ready = 0u;
    return -1;
}

static void mk_audio_compat_apply_device_quirks(void) {
    if (!g_audio_state.compat_ready) {
        return;
    }

    if (g_audio_state.pci.vendor_id == PCI_VENDOR_SIS &&
        g_audio_state.pci.device_id == PCI_PRODUCT_SIS_7012_AC97) {
        uint32_t control =
            mk_audio_compat_read32(g_audio_state.compat_aud_base,
                                   g_audio_state.compat_aud_is_mmio,
                                   ICH_SIS_NV_CTL);
        control |= ICH_SIS_CTL_UNMUTE;
        mk_audio_compat_write32(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                ICH_SIS_NV_CTL,
                                control);
    }
}

static uint16_t mk_audio_encode_output_gain(uint8_t level) {
    uint16_t atten;

    atten = (uint16_t)((255u - (uint16_t)level) * 31u / 255u);
    if (atten > 31u) {
        atten = 31u;
    }
    return (uint16_t)((atten << 8) | atten);
}

static uint16_t mk_audio_encode_input_gain(uint8_t level) {
    uint16_t gain;

    gain = (uint16_t)((uint16_t)level * 15u / 255u);
    if (gain > 15u) {
        gain = 15u;
    }
    return (uint16_t)((gain << 8) | gain);
}

static uint16_t mk_audio_encode_record_source(uint8_t input_default) {
    uint16_t source;

    source = input_default == 1u ? AC97_RECORD_SOURCE_LINEIN : AC97_RECORD_SOURCE_MIC;
    return (uint16_t)(source | (source << 8));
}

static void mk_audio_compat_sync_codec_caps(void) {
    uint16_t caps = 0u;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    if (mk_audio_compat_read_codec(AC97_REG_RESET, &caps) == 0) {
        g_audio_state.compat_caps = caps;
        mk_audio_refresh_topology_snapshot();
    }
}

static uint32_t mk_audio_output_count(void) {
    uint32_t mask = mk_audio_output_presence_mask();
    uint32_t count = 0u;

    while (mask != 0u) {
        count += mask & 0x1u;
        mask >>= 1;
    }
    return count != 0u ? count : 1u;
}

static uint32_t mk_audio_input_count(void) {
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) == 0u) {
        return 0u;
    }
    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH || !g_audio_state.compat_ready) {
        return 1u;
    }
    return 2u;
}

static uint32_t mk_audio_output_presence_mask(void) {
    uint32_t mask = 0x1u;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA &&
        g_audio_state.azalia_ready) {
        if (g_audio_state.azalia_output_mask != 0u) {
            return g_audio_state.azalia_output_mask;
        }
        return 0x1u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH &&
        g_audio_state.compat_ready) {
        if ((g_audio_state.compat_caps & AC97_CAPS_HEADPHONES) != 0u) {
            mask |= 0x2u;
        }
        if ((g_audio_state.compat_ext_audio_id & AC97_EXT_AUDIO_SDAC) != 0u) {
            mask |= 0x4u;
        }
        if ((g_audio_state.compat_ext_audio_id & (AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
            mask |= 0x8u;
        }
    }
    return mask;
}

static uint32_t mk_audio_input_presence_mask(void) {
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) == 0u) {
        return 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA &&
        g_audio_state.azalia_ready &&
        g_audio_state.azalia_input_mask != 0u) {
        return g_audio_state.azalia_input_mask;
    }
    return 0x3u;
}

static int mk_audio_output_bit_from_ord(uint32_t mask, uint8_t ord) {
    uint8_t seen = 0u;

    for (int bit = 0; bit < 4; ++bit) {
        if ((mask & (1u << bit)) == 0u) {
            continue;
        }
        if (seen == ord) {
            return bit;
        }
        ++seen;
    }
    return -1;
}

static int mk_audio_azalia_register_output_path(uint32_t output_mask,
                                                uint8_t pin_nid,
                                                uint8_t dac_nid,
                                                int priority) {
    int bit;

    if (output_mask == 0u || pin_nid == 0u || dac_nid == 0u) {
        return -1;
    }
    for (bit = 0; bit < 4; ++bit) {
        if ((output_mask & (1u << bit)) == 0u) {
            continue;
        }
        if (g_audio_state.azalia_output_pin_nids[bit] == 0u ||
            priority >= (int)g_audio_state.azalia_output_priorities[bit]) {
            g_audio_state.azalia_output_pin_nids[bit] = pin_nid;
            g_audio_state.azalia_output_dac_nids[bit] = dac_nid;
            g_audio_state.azalia_output_priorities[bit] = (int8_t)priority;
        }
    }
    return 0;
}

static void mk_audio_azalia_register_input_path(uint32_t input_mask, uint8_t pin_nid) {
    if ((input_mask & 0x1u) != 0u && g_audio_state.azalia_input_pin_nids[0] == 0u) {
        g_audio_state.azalia_input_pin_nids[0] = pin_nid;
    }
    if ((input_mask & 0x2u) != 0u && g_audio_state.azalia_input_pin_nids[1] == 0u) {
        g_audio_state.azalia_input_pin_nids[1] = pin_nid;
    }
}

static uint32_t mk_audio_hda_config_device(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_DEVICE_SHIFT) & HDA_CONFIG_DEVICE_MASK;
}

static uint32_t mk_audio_hda_output_mask(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device;

    if ((pin_caps & HDA_PINCAP_OUTPUT) == 0u) {
        return 0u;
    }

    device = mk_audio_hda_config_device(config_default);
    switch (device) {
    case HDA_CONFIG_DEVICE_HEADPHONE:
        return 0x2u;
    case HDA_CONFIG_DEVICE_SPEAKER:
        return 0x1u;
    case HDA_CONFIG_DEVICE_LINEOUT:
        return 0x4u;
    case HDA_CONFIG_DEVICE_SPDIFOUT:
    case HDA_CONFIG_DEVICE_DIGITALOUT:
        return 0x8u;
    default:
        break;
    }

    if ((pin_caps & HDA_PINCAP_HDMI) != 0u) {
        return 0x8u;
    }
    if ((pin_caps & HDA_PINCAP_HEADPHONE) != 0u) {
        return 0x2u;
    }
    return 0x1u;
}

static uint32_t mk_audio_hda_input_mask(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device;

    if ((pin_caps & HDA_PINCAP_INPUT) == 0u) {
        return 0u;
    }

    device = mk_audio_hda_config_device(config_default);
    if (device == HDA_CONFIG_DEVICE_LINEIN) {
        return 0x2u;
    }
    if (device == HDA_CONFIG_DEVICE_MICIN) {
        return 0x1u;
    }
    return 0x1u;
}

static int mk_audio_hda_output_priority(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device = mk_audio_hda_config_device(config_default);

    if ((pin_caps & HDA_PINCAP_OUTPUT) == 0u) {
        return -1;
    }
    switch (device) {
    case HDA_CONFIG_DEVICE_SPEAKER:
        return 50;
    case HDA_CONFIG_DEVICE_HEADPHONE:
        return 40;
    case HDA_CONFIG_DEVICE_LINEOUT:
        return 30;
    case HDA_CONFIG_DEVICE_SPDIFOUT:
    case HDA_CONFIG_DEVICE_DIGITALOUT:
        return 20;
    default:
        break;
    }
    if ((pin_caps & HDA_PINCAP_HDMI) != 0u) {
        return 20;
    }
    if ((pin_caps & HDA_PINCAP_HEADPHONE) != 0u) {
        return 40;
    }
    return 5;
}

static uint32_t mk_audio_backend_feature_flags(void) {
    uint32_t flags = 0u;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        flags |= 0x1u;
    }
    if (g_audio_state.compat_irq_registered || g_audio_state.azalia_irq_registered) {
        flags |= 0x2u;
    }
    if ((g_audio_state.compat_ready || g_audio_state.azalia_ready) &&
        (g_audio_state.pci.irq_line >= 16u ||
         (!g_audio_state.compat_irq_registered && !g_audio_state.azalia_irq_registered))) {
        flags |= 0x4u;
    }
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
        flags |= 0x8u;
    }
    if (g_audio_state.compat_irq_count != 0u || g_audio_state.azalia_irq_count != 0u) {
        flags |= 0x10u;
    }
    if (g_audio_state.playback_starvations != 0u) {
        flags |= 0x20u;
    }
    if (g_audio_state.playback_underruns != 0u) {
        flags |= 0x40u;
    }
    if (g_audio_state.compat_aud_is_mmio || g_audio_state.compat_mix_is_mmio || g_audio_state.azalia_ready) {
        flags |= 0x80u;
    }
    if (g_audio_state.compat_ignore_codecready) {
        flags |= 0x100u;
    }
    if ((g_audio_state.compat_ext_audio_id &
         (AC97_EXT_AUDIO_SDAC | AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
        flags |= 0x200u;
    }
    if (g_audio_state.compat_ready && g_audio_state.compat_codec_ready &&
        (g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
        flags |= 0x400u;
    }
    if (g_audio_state.azalia_corb_ready) {
        flags |= 0x800u;
    }
    if (g_audio_state.azalia_codec_probed) {
        flags |= 0x1000u;
    }
    if (g_audio_state.azalia_widget_probed) {
        flags |= 0x2000u;
    }
    if (g_audio_state.azalia_path_programmed) {
        flags |= 0x4000u;
    }
    return flags;
}

static void mk_audio_refresh_topology_snapshot(void) {
    g_audio_state.info.parameters._spare[0] = (unsigned int)(
        g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA ?
            g_audio_state.azalia_irq_count :
            g_audio_state.compat_irq_count);
    g_audio_state.info.parameters._spare[1] = mk_audio_output_count();
    g_audio_state.info.parameters._spare[2] = mk_audio_input_count();
    g_audio_state.info.parameters._spare[3] = mk_audio_output_presence_mask();
    g_audio_state.info.parameters._spare[4] = mk_audio_input_presence_mask();
    g_audio_state.info.parameters._spare[5] = mk_audio_backend_feature_flags();
    g_audio_state.info.controller_pci_id =
        ((uint32_t)g_audio_state.pci.vendor_id << 16) |
        (uint32_t)g_audio_state.pci.device_id;
    g_audio_state.info.controller_location =
        ((uint32_t)g_audio_state.pci.bus << 16) |
        ((uint32_t)g_audio_state.pci.slot << 8) |
        (uint32_t)g_audio_state.pci.function;
    g_audio_state.info.codec_vendor_id =
        g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA ?
            g_audio_state.azalia_vendor_id :
            0u;
    g_audio_state.info.output_route =
        ((uint32_t)g_audio_state.azalia_output_pin_nid << 8) |
        (uint32_t)g_audio_state.azalia_output_dac_nid;
}

static void mk_audio_compat_apply_mixer_state(void) {
    uint16_t output_value;
    uint16_t headphone_value;
    uint16_t surround_value;
    uint16_t center_lfe_value;
    uint16_t input_value;
    uint32_t output_mask;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    output_mask = mk_audio_output_presence_mask();
    output_value = mk_audio_encode_output_gain(g_audio_state.output_level);
    input_value = mk_audio_encode_input_gain(g_audio_state.input_level);
    if (g_audio_state.output_muted) {
        output_value |= 0x8000u;
    }
    if (g_audio_state.input_muted) {
        input_value |= 0x8000u;
    }

    headphone_value = output_value;
    surround_value = output_value;
    center_lfe_value = output_value;

    if ((output_mask & 0x2u) != 0u || (output_mask & 0x4u) != 0u || (output_mask & 0x8u) != 0u) {
        output_value |= 0x8000u;
        headphone_value |= 0x8000u;
        surround_value |= 0x8000u;
        center_lfe_value |= 0x8000u;

        switch (g_audio_state.default_output) {
        case 1u:
            if ((output_mask & 0x2u) != 0u) {
                headphone_value &= (uint16_t)~0x8000u;
            } else {
                output_value &= (uint16_t)~0x8000u;
            }
            break;
        case 2u:
            if ((output_mask & 0x4u) != 0u) {
                surround_value &= (uint16_t)~0x8000u;
            } else {
                output_value &= (uint16_t)~0x8000u;
            }
            break;
        case 3u:
            if ((output_mask & 0x8u) != 0u) {
                center_lfe_value &= (uint16_t)~0x8000u;
            } else {
                output_value &= (uint16_t)~0x8000u;
            }
            break;
        case 0u:
        default:
            output_value &= (uint16_t)~0x8000u;
            break;
        }
    }

    if ((g_audio_state.compat_caps & AC97_CAPS_HEADPHONES) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_HEADPHONE_VOLUME, headphone_value);
    }
    if ((output_mask & 0x4u) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_SURR_MASTER, surround_value);
    }
    if ((output_mask & 0x8u) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_CENTER_LFE_MASTER, center_lfe_value);
    }

    (void)mk_audio_compat_write_codec(AC97_REG_MASTER_VOLUME, output_value);
    (void)mk_audio_compat_write_codec(AC97_REG_PCMOUT_VOLUME, output_value);
    (void)mk_audio_compat_write_codec(AC97_REG_RECORD_SELECT,
                                      mk_audio_encode_record_source(g_audio_state.default_input));
    (void)mk_audio_compat_write_codec(AC97_REG_RECORD_GAIN, input_value);
    (void)mk_audio_compat_write_codec(AC97_REG_LINEIN_VOLUME,
                                      g_audio_state.input_muted ? 0x8000u : 0x0808u);
    (void)mk_audio_compat_write_codec(AC97_REG_MIC_VOLUME,
                                      g_audio_state.input_muted ? 0x8000u : 0x0000u);
}

static void mk_audio_compat_apply_params(void) {
    uint16_t ext_id = 0u;
    uint16_t ext_ctrl = 0u;
    uint16_t rate;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    if (mk_audio_compat_read_codec(AC97_REG_EXT_AUDIO_ID, &ext_id) == 0) {
        g_audio_state.compat_ext_audio_id = ext_id;
        if ((ext_id & AC97_EXT_AUDIO_VRA) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_VRA;
        }
        if ((ext_id & AC97_EXT_AUDIO_SDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_SDAC;
        }
        if ((ext_id & AC97_EXT_AUDIO_CDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_CDAC;
        }
        if ((ext_id & AC97_EXT_AUDIO_LDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_LDAC;
        }
        (void)mk_audio_compat_write_codec(AC97_REG_EXT_AUDIO_CTRL, ext_ctrl);
    }

    rate = (uint16_t)g_audio_state.info.parameters.rate;
    if (rate == 0u) {
        rate = 48000u;
    }
    (void)mk_audio_compat_write_codec(AC97_REG_PCM_FRONT_DAC_RATE, rate);
    if ((g_audio_state.compat_ext_audio_id & AC97_EXT_AUDIO_SDAC) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_PCM_SURR_DAC_RATE, rate);
    }
    if ((g_audio_state.compat_ext_audio_id & (AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_PCM_LFE_DAC_RATE, rate);
    }
    (void)mk_audio_compat_write_codec(AC97_REG_PCM_LR_ADC_RATE, rate);
}

static void mk_audio_compat_halt_output(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           0u);
    (void)mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           AUICH_RR);
    for (uint32_t i = 0u; i < 32u; ++i) {
        mk_audio_compat_delay();
    }
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           0u);
    g_audio_state.compat_output_running = 0u;
    g_audio_state.compat_output_pending = 0u;
    g_audio_state.compat_output_producer = 0u;
    g_audio_state.compat_output_consumer = 0u;
    g_audio_state.compat_output_hw_civ = 0u;
    memset(g_audio_auich_pcmo_bytes, 0, sizeof(g_audio_auich_pcmo_bytes));
}

static void mk_audio_compat_reset_output_ring(void) {
    memset(g_audio_auich_pcmo_dmalist, 0, sizeof(g_audio_auich_pcmo_dmalist));
    memset(g_audio_auich_pcmo_buffers, 0, sizeof(g_audio_auich_pcmo_buffers));
    memset(g_audio_auich_pcmo_bytes, 0, sizeof(g_audio_auich_pcmo_bytes));
    g_audio_state.compat_output_running = 0u;
    g_audio_state.compat_output_pending = 0u;
    g_audio_state.compat_output_producer = 0u;
    g_audio_state.compat_output_consumer = 0u;
    g_audio_state.compat_output_hw_civ = 0u;
}

static void mk_audio_compat_halt_input(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           0u);
    (void)mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           AUICH_RR);
    for (uint32_t i = 0u; i < 32u; ++i) {
        mk_audio_compat_delay();
    }
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           0u);
    g_audio_state.compat_input_running = 0u;
    g_audio_state.compat_input_pending = 0u;
    g_audio_state.compat_input_producer = 0u;
    g_audio_state.compat_input_consumer = 0u;
    g_audio_state.compat_input_hw_civ = 0u;
    memset(g_audio_auich_pcmi_bytes, 0, sizeof(g_audio_auich_pcmi_bytes));
}

static void mk_audio_compat_reset_input_ring(void) {
    memset(g_audio_auich_pcmi_dmalist, 0, sizeof(g_audio_auich_pcmi_dmalist));
    memset(g_audio_auich_pcmi_buffers, 0, sizeof(g_audio_auich_pcmi_buffers));
    memset(g_audio_auich_pcmi_bytes, 0, sizeof(g_audio_auich_pcmi_bytes));
    g_audio_state.compat_input_running = 0u;
    g_audio_state.compat_input_pending = 0u;
    g_audio_state.compat_input_producer = 0u;
    g_audio_state.compat_input_consumer = 0u;
    g_audio_state.compat_input_hw_civ = 0u;
}

static int mk_audio_compat_capture_block(uint32_t requested_bytes) {
    uint32_t dma_chunk_limit;
    uint32_t wait_count = 0u;
    uint16_t sts = 0u;
    uint16_t picb = 0xffffu;
    uint16_t captured_bytes;

    if (g_audio_state.compat_aud_base == 0u || !g_audio_state.compat_ready || !g_audio_state.compat_codec_ready) {
        return -1;
    }

    dma_chunk_limit = requested_bytes;
    if (dma_chunk_limit == 0u) {
        dma_chunk_limit = g_audio_state.info.parameters.round;
    }
    if (dma_chunk_limit == 0u || dma_chunk_limit > AUICH_DMA_SLOT_SIZE) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }
    if ((dma_chunk_limit & 1u) != 0u) {
        dma_chunk_limit -= 1u;
    }
    if (dma_chunk_limit < 2u) {
        dma_chunk_limit = 2u;
    }

    mk_audio_compat_halt_input();
    mk_audio_compat_reset_input_ring();
    memset(&g_audio_auich_pcmi_buffers[0][0], 0, AUICH_DMA_SLOT_SIZE);
    g_audio_auich_pcmi_dmalist[0].base = (uint32_t)(uintptr_t)&g_audio_auich_pcmi_buffers[0][0];
    g_audio_auich_pcmi_dmalist[0].len = ((dma_chunk_limit / 2u) & 0xffffu) | AUICH_DMAF_IOC;
    g_audio_auich_pcmi_bytes[0] = (uint16_t)dma_chunk_limit;
    g_audio_state.compat_input_running = 1u;
    g_audio_state.compat_input_pending = 1u;
    g_audio_state.compat_input_producer = 1u;
    g_audio_state.compat_input_consumer = 0u;

    mk_audio_compat_write32(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_BDBAR,
                            (uint32_t)(uintptr_t)&g_audio_auich_pcmi_dmalist[0]);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_LVI, 0u);
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           (uint8_t)(AUICH_IOCE | AUICH_FEIE | AUICH_RPBM));

    while (wait_count < 200000u) {
        sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
        picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB) & 0xffffu);
        if ((sts & (AUICH_BCIS | AUICH_LVBCI)) != 0u || ((sts & (AUICH_DCH | AUICH_CELV)) != 0u && picb == 0u)) {
            break;
        }
        mk_audio_compat_delay();
        wait_count++;
    }

    if (wait_count == 200000u) {
        mk_audio_compat_debug_pcmi("capture-timeout");
    }

    captured_bytes = (picb == 0u) ? (uint16_t)dma_chunk_limit : 0u;
    if (captured_bytes != 0u) {
        mk_audio_capture_ring_write(&g_audio_auich_pcmi_buffers[0][0], captured_bytes);
    }

    if ((sts & AUICH_FIFOE) != 0u) {
        g_audio_state.capture_xruns++;
    }
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_halt_input();
    return captured_bytes != 0u ? (int)captured_bytes : -1;
}

static void mk_audio_compat_update_output_progress(void) {
    uint8_t civ;
    uint8_t piv;
    uint16_t picb;
    uint16_t sts;
    uint32_t completed = 0u;

    if (!g_audio_state.compat_output_running || g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_CIV) & AUICH_LVI_MASK);
    piv = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_PIV) & AUICH_LVI_MASK);
    picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_PICB) & 0xffffu);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    g_audio_state.compat_output_hw_civ = civ;

    while (g_audio_state.compat_output_pending > 0u &&
           g_audio_state.compat_output_consumer != civ) {
        uint8_t completed_slot =
            (uint8_t)((g_audio_state.compat_output_consumer + 1u) & AUICH_LVI_MASK);

        g_audio_state.compat_output_consumer =
            completed_slot;
        g_audio_state.compat_output_pending--;
        g_audio_state.playback_bytes_consumed += g_audio_auich_pcmo_bytes[completed_slot];
        g_audio_auich_pcmo_bytes[completed_slot] = 0u;
        completed++;
    }

    if (g_audio_state.compat_output_pending == 1u &&
        picb == 0u &&
        (piv == civ || (sts & (AUICH_DCH | AUICH_CELV | AUICH_BCIS | AUICH_LVBCI)) != 0u)) {
        g_audio_state.playback_bytes_consumed += g_audio_auich_pcmo_bytes[civ];
        g_audio_auich_pcmo_bytes[civ] = 0u;
        g_audio_state.compat_output_pending = 0u;
        completed++;
    }

    if ((sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        if ((sts & AUICH_FIFOE) != 0u) {
            g_audio_state.playback_xruns++;
            g_audio_state.playback_underruns++;
        }
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMO + AUICH_STS,
                                (uint16_t)(sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
    }

    if (g_audio_state.compat_output_pending == 0u &&
        ((sts & (AUICH_DCH | AUICH_CELV)) != 0u || (picb == 0u && piv == civ))) {
        if (g_audio_state.playback_bytes_written > g_audio_state.playback_bytes_consumed) {
            g_audio_state.playback_starvations++;
        }
        g_audio_state.compat_output_running = 0u;
    }
}

static void mk_audio_compat_update_input_progress(void) {
    uint8_t civ;
    uint8_t piv;
    uint16_t picb;
    uint16_t sts;

    if (!g_audio_state.compat_input_running || g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CIV) & AUICH_LVI_MASK);
    piv = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PIV) & AUICH_LVI_MASK);
    picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB) & 0xffffu);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    g_audio_state.compat_input_hw_civ = civ;

    while (g_audio_state.compat_input_pending > 0u &&
           g_audio_state.compat_input_consumer != civ) {
        uint8_t completed_slot =
            (uint8_t)((g_audio_state.compat_input_consumer + 1u) & AUICH_LVI_MASK);
        uint16_t captured_bytes = g_audio_auich_pcmi_bytes[completed_slot];

        g_audio_state.compat_input_consumer = completed_slot;
        g_audio_state.compat_input_pending--;
        if (captured_bytes != 0u) {
            mk_audio_capture_ring_write(&g_audio_auich_pcmi_buffers[completed_slot][0], captured_bytes);
        }

        g_audio_state.compat_input_producer = completed_slot;
        g_audio_state.compat_input_pending++;
        mk_audio_compat_write8(g_audio_state.compat_aud_base,
                               g_audio_state.compat_aud_is_mmio,
                               AUICH_PCMI + AUICH_LVI,
                               completed_slot);
    }

    if ((sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        if ((sts & AUICH_FIFOE) != 0u) {
            g_audio_state.capture_xruns++;
        }
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMI + AUICH_STS,
                                (uint16_t)(sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
    }

    if ((sts & (AUICH_DCH | AUICH_CELV)) != 0u && picb == 0u && piv == civ) {
        g_audio_state.compat_input_running = 0u;
    }
}

static void mk_audio_compat_irq_handler(void) {
    uint16_t status;
    uint16_t input_status;

    if (g_audio_state.compat_aud_base == 0u) {
        kernel_pic_send_eoi(g_audio_state.pci.irq_line);
        return;
    }

    status = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    if ((status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        g_audio_state.compat_irq_count++;
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMO + AUICH_STS,
                                (uint16_t)(status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
        if ((status & AUICH_FIFOE) != 0u) {
            g_audio_state.playback_xruns++;
            g_audio_state.playback_underruns++;
        }
    }

    input_status = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    if ((input_status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        g_audio_state.compat_irq_count++;
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMI + AUICH_STS,
                                (uint16_t)(input_status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
        if ((input_status & AUICH_FIFOE) != 0u) {
            g_audio_state.capture_xruns++;
        }
    }

    mk_audio_compat_update_output_progress();
    mk_audio_compat_update_input_progress();
    kernel_pic_send_eoi(g_audio_state.pci.irq_line);
}

static void mk_audio_azalia_irq_handler(void) {
    uint32_t int_status;
    uint32_t stream_mask;
    uint16_t wake_status;
    uint16_t state_status;
    uint32_t base;
    uint8_t stream_status;

    if (g_audio_state.azalia_base == 0u) {
        kernel_pic_send_eoi(g_audio_state.pci.irq_line);
        return;
    }

    int_status = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTSTS);
    stream_mask = (1u << g_audio_state.azalia_output_stream_index);
    wake_status = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_GSTS);
    state_status = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_STATESTS);
    if (int_status != 0u || wake_status != 0u || state_status != 0u) {
        g_audio_state.azalia_irq_count++;
    }
    if (state_status != 0u) {
        g_audio_state.azalia_codec_mask |= state_status;
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_STATESTS, state_status);
    }
    if (wake_status != 0u) {
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_GSTS, wake_status);
    }
    if ((int_status & stream_mask) != 0u && g_audio_state.azalia_output_regbase != 0u) {
        base = g_audio_state.azalia_output_regbase;
        stream_status = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS));
        if ((stream_status & (HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS)) != 0u) {
            mk_audio_azalia_write8(g_audio_state.azalia_base,
                                   (uint16_t)(base + HDA_SD_STS),
                                   (uint8_t)(stream_status |
                                             HDA_SD_STS_DESE |
                                             HDA_SD_STS_FIFOE |
                                             HDA_SD_STS_BCIS));
            if ((stream_status & HDA_SD_STS_FIFOE) != 0u) {
                g_audio_state.playback_xruns++;
                g_audio_state.playback_underruns++;
            }
        }
        mk_audio_azalia_update_output_progress();
    }
    kernel_pic_send_eoi(g_audio_state.pci.irq_line);
}

static int mk_audio_compat_start_output_if_needed(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }
    if (g_audio_state.compat_output_running || g_audio_state.compat_output_pending == 0u) {
        return 0;
    }

    mk_audio_compat_write32(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_BDBAR,
                            (uint32_t)(uintptr_t)&g_audio_auich_pcmo_dmalist[0]);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    g_audio_state.compat_output_hw_civ =
        (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_CIV) & AUICH_LVI_MASK);
    g_audio_state.compat_output_consumer = g_audio_state.compat_output_hw_civ;
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_LVI,
                           (uint8_t)((g_audio_state.compat_output_producer - 1u) & AUICH_LVI_MASK));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           (uint8_t)(AUICH_IOCE | AUICH_FEIE | AUICH_RPBM));
    g_audio_state.compat_output_running = 1u;
    return 0;
}

static const char *mk_audio_auich_family_name(uint16_t device_id) {
    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801AA_ACA:
        return "ich-aa";
    case PCI_PRODUCT_INTEL_82801AB_ACA:
        return "ich-ab";
    case PCI_PRODUCT_INTEL_82801BA_ACA:
        return "ich-ba";
    case PCI_PRODUCT_INTEL_82801CA_ACA:
        return "ich-ca";
    case PCI_PRODUCT_INTEL_82801DB_ACA:
        return "ich-db";
    case PCI_PRODUCT_INTEL_82801EB_ACA:
        return "ich-eb";
    case PCI_PRODUCT_INTEL_6300ESB_ACA:
        return "esb6300";
    case PCI_PRODUCT_INTEL_82801FB_ACA:
        return "ich-fb";
    case PCI_PRODUCT_INTEL_6321ESB_ACA:
        return "esb6321";
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return "ich-gb";
    case PCI_PRODUCT_INTEL_82440MX_ACA:
        return "440mx";
    case PCI_PRODUCT_AMD_768_ACA:
        return "amd-768";
    case PCI_PRODUCT_AMD_8111_ACA:
        return "amd-8111";
    case PCI_PRODUCT_ATI_SB200_AUDIO:
        return "ati-sb200";
    case PCI_PRODUCT_ATI_SB300_AUDIO:
        return "ati-sb300";
    case PCI_PRODUCT_ATI_SB400_AUDIO:
        return "ati-sb400";
    case PCI_PRODUCT_ATI_SB600_AUDIO:
        return "ati-sb600";
    case PCI_PRODUCT_NVIDIA_MCP04_AC97:
        return "mcp04";
    case PCI_PRODUCT_NVIDIA_NFORCE4_AC97:
        return "nforce4";
    case PCI_PRODUCT_NVIDIA_NFORCE2_AC97:
        return "nforce2";
    case PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97:
        return "nf2-400";
    case PCI_PRODUCT_NVIDIA_NFORCE3_AC97:
        return "nforce3";
    case PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97:
        return "nf3-250";
    case PCI_PRODUCT_NVIDIA_NFORCE_AC97:
        return "nforce";
    case PCI_PRODUCT_NVIDIA_MCP51_AC97:
        return "mcp51";
    case PCI_PRODUCT_ALI_M5455_AC97:
        return "ali-m5455";
    case PCI_PRODUCT_SIS_7012_AC97:
        return "sis-7012";
    case PCI_PRODUCT_VIATECH_VT82C686A_AC97:
        return "via-686";
    case PCI_PRODUCT_VIATECH_VT8233_AC97:
        return "via-8233";
    default:
        return "ac97";
    }
}

static const char *mk_audio_ac97_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        return "intel-ac97";
    case PCI_VENDOR_AMD:
        return "amd-ac97";
    case PCI_VENDOR_ATI:
        return "ati-ac97";
    case PCI_VENDOR_NVIDIA:
        return "nvidia-ac97";
    case PCI_VENDOR_ALI:
        return "ali-ac97";
    case PCI_VENDOR_SIS:
        return "sis-ac97";
    case PCI_VENDOR_VIATECH:
        return "via-ac97";
    default:
        return "compat-ac97";
    }
}

static const char *mk_audio_hda_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        return "intel-hda";
    case PCI_VENDOR_AMD:
        return "amd-hda";
    case PCI_VENDOR_ATI:
        return "ati-hda";
    case PCI_VENDOR_NVIDIA:
        return "nvidia-hda";
    case PCI_VENDOR_SIS:
        return "sis-hda";
    case PCI_VENDOR_VIATECH:
        return "via-hda";
    default:
        return "compat-hda";
    }
}

static void mk_audio_format_pci_location(char *dst,
                                         size_t dst_size,
                                         const struct kernel_pci_device_info *pci) {
    static const char hex_digits[] = "0123456789abcdef";
    size_t pos;

    if (dst == 0 || dst_size < 12u || pci == 0) {
        return;
    }

    dst[0] = hex_digits[(pci->bus >> 4) & 0xFu];
    dst[1] = hex_digits[pci->bus & 0xFu];
    dst[2] = ':';
    dst[3] = hex_digits[(pci->slot >> 4) & 0xFu];
    dst[4] = hex_digits[pci->slot & 0xFu];
    dst[5] = '.';
    dst[6] = hex_digits[pci->function & 0xFu];
    dst[7] = ' ';
    dst[8] = 'i';
    dst[9] = 'r';
    dst[10] = 'q';
    pos = 11u;
    if (pci->irq_line == 0xFFu) {
        if (pos + 2u < dst_size) {
            dst[pos++] = '?';
        }
    } else {
        if (pci->irq_line >= 100u && pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + ((pci->irq_line / 100u) % 10u));
        }
        if (pci->irq_line >= 10u && pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + ((pci->irq_line / 10u) % 10u));
        }
        if (pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + (pci->irq_line % 10u));
        }
    }
    dst[pos] = '\0';
}

static void mk_audio_enable_pci_device(const struct kernel_pci_device_info *pci) {
    uint32_t command_status;

    if (pci == 0) {
        return;
    }

    command_status = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, 0x04u);
    command_status |= (uint32_t)(PCI_COMMAND_IO_SPACE |
                                 PCI_COMMAND_MEMORY_SPACE |
                                 PCI_COMMAND_BUS_MASTER);
    kernel_pci_config_write_u32(pci->bus, pci->slot, pci->function, 0x04u, command_status);
}

struct mk_audio_probe_ctx {
    struct kernel_pci_device_info *out;
    int found;
};

static int mk_audio_backend_current_is_usable(void) {
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        return g_audio_state.azalia_ready &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        return g_audio_state.compat_ready &&
               g_audio_state.compat_codec_ready &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    return 0;
}

static int mk_audio_probe_compat_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_probe_ctx *ctx = (struct mk_audio_probe_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0 || ctx->out == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_AUDIO) {
        return 0;
    }
    if (!mk_audio_is_auich_device(info->vendor_id, info->device_id)) {
        return 0;
    }

    *ctx->out = *info;
    ctx->found = 1;
    return 1;
}

static int mk_audio_probe_compat_backend(struct kernel_pci_device_info *pci_out) {
    struct mk_audio_probe_ctx ctx;

    if (pci_out == 0) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = pci_out;
    if (kernel_pci_enumerate(mk_audio_probe_compat_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.found ? 0 : -1;
}

static int mk_audio_probe_azalia_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_probe_ctx *ctx = (struct mk_audio_probe_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0 || ctx->out == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_HDA) {
        return 0;
    }
    if (!kernel_pci_bar_is_mmio(info->bars[HDA_BAR_INDEX])) {
        return 0;
    }

    *ctx->out = *info;
    ctx->found = 1;
    return 1;
}

static int mk_audio_probe_azalia_backend(struct kernel_pci_device_info *pci_out) {
    struct mk_audio_probe_ctx ctx;

    if (pci_out == 0) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = pci_out;
    if (kernel_pci_enumerate(mk_audio_probe_azalia_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.found ? 0 : -1;
}

static void mk_audio_select_compat_backend(const struct kernel_pci_device_info *pci);
static void mk_audio_select_azalia_backend(const struct kernel_pci_device_info *pci);

struct mk_audio_try_ctx {
    int matched;
    int selected;
};

static int mk_audio_try_azalia_backend_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_try_ctx *ctx = (struct mk_audio_try_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_HDA) {
        return 0;
    }
    if (!kernel_pci_bar_is_mmio(info->bars[HDA_BAR_INDEX])) {
        return 0;
    }

    ctx->matched = 1;
    mk_audio_select_azalia_backend(info);
    if (mk_audio_backend_current_is_usable()) {
        ctx->selected = 1;
        return 1;
    }
    return 0;
}

static int mk_audio_try_compat_backend_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_try_ctx *ctx = (struct mk_audio_try_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_AUDIO) {
        return 0;
    }
    if (!mk_audio_is_auich_device(info->vendor_id, info->device_id)) {
        return 0;
    }

    ctx->matched = 1;
    mk_audio_select_compat_backend(info);
    if (mk_audio_backend_current_is_usable()) {
        ctx->selected = 1;
        return 1;
    }
    return 0;
}

static int mk_audio_try_azalia_backends(void) {
    struct mk_audio_try_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    if (kernel_pci_enumerate(mk_audio_try_azalia_backend_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.selected ? 0 : -1;
}

static int mk_audio_try_compat_backends(void) {
    struct mk_audio_try_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    if (kernel_pci_enumerate(mk_audio_try_compat_backend_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.selected ? 0 : -1;
}

static int mk_audio_probe_any_hardware_backend(void) {
    struct kernel_pci_device_info detected_pci;

    if (mk_audio_probe_azalia_backend(&detected_pci) == 0) {
        return 1;
    }
    if (mk_audio_probe_compat_backend(&detected_pci) == 0) {
        return 1;
    }
    return 0;
}

static void mk_audio_select_soft_backend(void) {
    g_audio_state.backend_kind = MK_AUDIO_BACKEND_SOFT;
    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER |
                               MK_AUDIO_CAPS_PLAYBACK |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = AUMODE_PLAY;
    memset(&g_audio_state.pci, 0, sizeof(g_audio_state.pci));
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready = 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    memset(g_audio_state.azalia_output_pin_nids, 0, sizeof(g_audio_state.azalia_output_pin_nids));
    memset(g_audio_state.azalia_output_dac_nids, 0, sizeof(g_audio_state.azalia_output_dac_nids));
    memset(g_audio_state.azalia_input_pin_nids, 0, sizeof(g_audio_state.azalia_input_pin_nids));
    memset(g_audio_state.azalia_output_priorities, -1, sizeof(g_audio_state.azalia_output_priorities));
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_backend = &g_audio_backend_soft;
    mk_audio_copy_limited(g_audio_state.info.device.name, "compat", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "softmix", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.config, "compat-pcm", MAX_AUDIO_DEV_LEN);
    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_set_softmix_reason(const char *reason) {
    if (reason == 0 || reason[0] == '\0') {
        return;
    }
    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_SOFT) {
        return;
    }
    mk_audio_copy_limited(g_audio_state.info.device.config, reason, MAX_AUDIO_DEV_LEN);
}

static void mk_audio_select_compat_backend(const struct kernel_pci_device_info *pci) {
    char location[MAX_AUDIO_DEV_LEN];

    if (pci == 0) {
        mk_audio_select_soft_backend();
        return;
    }

    g_audio_state.backend_kind = MK_AUDIO_BACKEND_COMPAT_AUICH;
    g_audio_state.pci = *pci;
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready =
        mk_audio_auich_quirk_ignore_codecready(pci->vendor_id, pci->device_id) ? 1u : 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_backend = &g_audio_backend_compat;
    mk_audio_enable_pci_device(pci);
    if (mk_audio_auich_prefers_native_mmio(pci->vendor_id, pci->device_id) &&
        kernel_pci_bar_is_mmio(pci->bars[AUICH_MMBAR_INDEX]) &&
        kernel_pci_bar_is_mmio(pci->bars[AUICH_MBBAR_INDEX])) {
        g_audio_state.compat_mix_base = kernel_pci_bar_base(pci->bars[AUICH_MMBAR_INDEX]);
        g_audio_state.compat_aud_base = kernel_pci_bar_base(pci->bars[AUICH_MBBAR_INDEX]);
        g_audio_state.compat_mix_is_mmio = 1u;
        g_audio_state.compat_aud_is_mmio = 1u;
        g_audio_state.compat_ready = 1u;
    } else if ((pci->bars[AUICH_NAMBAR_INDEX] & 0x1u) != 0u &&
               (pci->bars[AUICH_NABMBAR_INDEX] & 0x1u) != 0u) {
        g_audio_state.compat_mix_base = (uintptr_t)(pci->bars[AUICH_NAMBAR_INDEX] & 0xFFFCu);
        g_audio_state.compat_aud_base = (uintptr_t)(pci->bars[AUICH_NABMBAR_INDEX] & 0xFFFCu);
        g_audio_state.compat_ready = 1u;
    } else if (mk_audio_auich_prefers_native_mmio(pci->vendor_id, pci->device_id)) {
        uint32_t cfg = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, 0x40u);

        kernel_pci_config_write_u32(pci->bus,
                                    pci->slot,
                                    pci->function,
                                    0x40u,
                                    cfg | AUICH_CFG_IOSE);
        if ((pci->bars[AUICH_NAMBAR_INDEX] & 0x1u) != 0u &&
            (pci->bars[AUICH_NABMBAR_INDEX] & 0x1u) != 0u) {
            g_audio_state.compat_mix_base = (uintptr_t)(pci->bars[AUICH_NAMBAR_INDEX] & 0xFFFCu);
            g_audio_state.compat_aud_base = (uintptr_t)(pci->bars[AUICH_NABMBAR_INDEX] & 0xFFFCu);
            g_audio_state.compat_ready = 1u;
        }
    }
    mk_audio_copy_limited(g_audio_state.info.device.name,
                          mk_audio_ac97_vendor_name(pci->vendor_id),
                          MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version,
                          mk_audio_auich_family_name(pci->device_id),
                          MAX_AUDIO_DEV_LEN);
    memset(location, 0, sizeof(location));
    mk_audio_format_pci_location(location, sizeof(location), pci);
    if (g_audio_state.compat_ready) {
        g_audio_state.info.flags |= MK_AUDIO_CAPS_CAPTURE;
        g_audio_state.info.status.mode = AUMODE_PLAY | AUMODE_RECORD;
        mk_audio_copy_limited(g_audio_state.info.device.config, location, MAX_AUDIO_DEV_LEN);
        mk_audio_compat_apply_device_quirks();
        (void)mk_audio_compat_reset_codec();
        if (g_audio_state.compat_codec_ready) {
            /* Preserve the software defaults/current mixer state instead of
             * inheriting the codec reset state, which may come up muted. */
            mk_audio_compat_sync_codec_caps();
            mk_audio_compat_apply_params();
            mk_audio_compat_apply_mixer_state();
        }
        if (pci->irq_line < 16u) {
            (void)kernel_irq_register_handler(pci->irq_line, mk_audio_compat_irq_handler);
            kernel_irq_unmask(pci->irq_line);
            g_audio_state.compat_irq_registered = 1u;
        }
    } else {
        mk_audio_copy_limited(g_audio_state.info.device.config, "bar-unavailable", MAX_AUDIO_DEV_LEN);
    }
    mk_audio_refresh_topology_snapshot();
}

static int mk_audio_azalia_reset_controller(void) {
    uint32_t control;

    if (g_audio_state.azalia_base == 0u) {
        return -1;
    }

    control = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_GCTL, control & ~HDA_GCTL_CRST);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        if ((mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL) & HDA_GCTL_CRST) == 0u) {
            break;
        }
        mk_audio_compat_delay();
    }

    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_GCTL, control | HDA_GCTL_CRST);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        if ((mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL) & HDA_GCTL_CRST) != 0u) {
            g_audio_state.azalia_ready = 1u;
            return 0;
        }
        mk_audio_compat_delay();
    }

    g_audio_state.azalia_ready = 0u;
    return -1;
}

static int mk_audio_azalia_select_output_stream(void) {
    uint8_t iss;
    uint8_t oss;

    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_regbase = 0u;

    iss = (uint8_t)((g_audio_state.azalia_gcap >> HDA_GCAP_ISS_SHIFT) & HDA_GCAP_ISS_MASK);
    oss = (uint8_t)((g_audio_state.azalia_gcap >> HDA_GCAP_OSS_SHIFT) & HDA_GCAP_OSS_MASK);
    if (oss == 0u) {
        return -1;
    }

    g_audio_state.azalia_output_stream_index = iss;
    g_audio_state.azalia_output_stream_number = 1u;
    g_audio_state.azalia_output_regbase =
        HDA_SD_BASE + ((uint32_t)g_audio_state.azalia_output_stream_index * HDA_SD_SIZE);
    return 0;
}

static int mk_audio_azalia_ring_size(uint8_t caps, uint8_t *selector_out, uint16_t *entries_out) {
    if (selector_out == 0 || entries_out == 0) {
        return -1;
    }

    if ((caps & HDA_CORBSIZE_CAP_256) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_256;
        *entries_out = 256u;
        return 0;
    }
    if ((caps & HDA_CORBSIZE_CAP_16) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_16;
        *entries_out = 16u;
        return 0;
    }
    if ((caps & HDA_CORBSIZE_CAP_2) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_2;
        *entries_out = 2u;
        return 0;
    }
    return -1;
}

static int mk_audio_azalia_init_command_rings(void) {
    uint8_t corb_sel;
    uint8_t rirb_sel;
    uint16_t corb_entries;
    uint16_t rirb_entries;
    uint8_t corb_caps;
    uint8_t rirb_caps;

    if (g_audio_state.azalia_base == 0u || !g_audio_state.azalia_ready) {
        return -1;
    }

    corb_caps = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_CORBSIZE) & 0xf0u;
    rirb_caps = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_RIRBSIZE) & 0xf0u;
    if (mk_audio_azalia_ring_size(corb_caps, &corb_sel, &corb_entries) != 0 ||
        mk_audio_azalia_ring_size(rirb_caps, &rirb_sel, &rirb_entries) != 0) {
        return -1;
    }

    memset(g_audio_azalia_corb, 0, sizeof(g_audio_azalia_corb));
    memset(g_audio_azalia_rirb, 0, sizeof(g_audio_azalia_rirb));
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBCTL, 0u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBCTL, 0u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBSTS, 0xffu);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBSTS, 0xffu);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBSIZE, corb_sel);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBSIZE, rirb_sel);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_CORBLBASE,
                            (uint32_t)(uintptr_t)&g_audio_azalia_corb[0]);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_CORBUBASE, 0u);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_RIRBLBASE,
                            (uint32_t)(uintptr_t)&g_audio_azalia_rirb[0]);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_RIRBUBASE, 0u);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBRP, HDA_CORBRP_CORBRPRST);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_RIRBWP, HDA_RIRBWP_RST);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBWP, 0u);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_RINTCNT, 1u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBCTL, HDA_CORBCTL_CORBRUN);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBCTL, HDA_RIRBCTL_DMAEN);

    g_audio_state.azalia_corb_entries = corb_entries;
    g_audio_state.azalia_rirb_entries = rirb_entries;
    g_audio_state.azalia_corb_ready = 1u;
    return 0;
}

static int mk_audio_azalia_command_raw(uint8_t codec,
                                       uint8_t nid,
                                       uint32_t verb_payload,
                                       uint32_t *response_out) {
    uint16_t corb_wp;
    uint16_t rirb_wp;
    uint16_t next_wp;
    uint16_t old_rirb_wp;
    uint32_t command = 0u;

    if (!g_audio_state.azalia_corb_ready ||
        g_audio_state.azalia_corb_entries == 0u ||
        g_audio_state.azalia_rirb_entries == 0u) {
        goto immediate_fallback;
    }

    corb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_CORBWP) & 0x00ffu);
    old_rirb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
    next_wp = (uint16_t)((corb_wp + 1u) % g_audio_state.azalia_corb_entries);
    command = ((uint32_t)(codec & 0x0fu) << 28) |
              ((uint32_t)nid << 20) |
              (verb_payload & 0x000fffffu);
    g_audio_azalia_corb[next_wp] = command;
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBWP, next_wp);

    for (uint32_t i = 0u; i < HDA_CORB_TIMEOUT; ++i) {
        rirb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
        if (rirb_wp != old_rirb_wp) {
            if (response_out != 0) {
                *response_out = g_audio_azalia_rirb[rirb_wp % g_audio_state.azalia_rirb_entries].response;
            }
            mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBSTS, HDA_RIRBSTS_RINTFL);
            return 0;
        }
        mk_audio_compat_delay();
    }

immediate_fallback:
    if (g_audio_state.azalia_base == 0u) {
        return -1;
    }
    for (uint32_t i = 0u; i < HDA_CORB_TIMEOUT; ++i) {
        if ((mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_IRS) & HDA_IRS_BUSY) == 0u) {
            uint16_t irs = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_IRS);

            mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_IRS, (uint16_t)(irs | HDA_IRS_VALID));
            mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_IC, command);
            mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_IRS, (uint16_t)(irs | HDA_IRS_BUSY));
            for (uint32_t j = 0u; j < HDA_CORB_TIMEOUT; ++j) {
                irs = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_IRS);
                if ((irs & HDA_IRS_VALID) != 0u) {
                    if (response_out != 0) {
                        *response_out = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_IR);
                    }
                    return 0;
                }
                mk_audio_compat_delay();
            }
            return -1;
        }
        mk_audio_compat_delay();
    }

    return -1;
}

static int mk_audio_azalia_command(uint8_t codec,
                                   uint8_t nid,
                                   uint16_t verb,
                                   uint8_t payload,
                                   uint32_t *response_out) {
    return mk_audio_azalia_command_raw(codec,
                                       nid,
                                       (((uint32_t)verb & 0x0fffu) << 8) | (uint32_t)payload,
                                       response_out);
}

static int mk_audio_azalia_probe_codec(void) {
    uint32_t response;
    uint32_t subnodes;
    uint8_t first_nid;
    uint8_t count;

    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_vendor_id = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    if (!g_audio_state.azalia_corb_ready) {
        return -1;
    }

    for (uint8_t codec = 0u; codec < HDA_MAX_CODECS; ++codec) {
        if ((g_audio_state.azalia_codec_mask & (1u << codec)) == 0u) {
            continue;
        }
        if (mk_audio_azalia_command(codec, 0u, HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID, &response) != 0 ||
            response == 0u || response == 0xffffffffu) {
            continue;
        }
        g_audio_state.azalia_vendor_id = response;
        g_audio_state.azalia_codec_address = codec;
        if (mk_audio_azalia_command(codec, 0u, HDA_VERB_GET_PARAMETER, HDA_PARAM_SUB_NODE_COUNT, &subnodes) == 0) {
            first_nid = (uint8_t)((subnodes >> 16) & 0xffu);
            count = (uint8_t)(subnodes & 0xffu);
            for (uint8_t i = 0u; i < count; ++i) {
                if (mk_audio_azalia_command(codec,
                                            (uint8_t)(first_nid + i),
                                            HDA_VERB_GET_PARAMETER,
                                            HDA_PARAM_FUNCTION_GROUP_TYPE,
                                            &response) == 0 &&
                    (response & 0xffu) == HDA_FGTYPE_AUDIO) {
                    g_audio_state.azalia_afg_nid = (uint8_t)(first_nid + i);
                    break;
                }
            }
        }
        g_audio_state.azalia_codec_probed = 1u;
        return 0;
    }

    return -1;
}

static void mk_audio_azalia_apply_known_codec_topology(void) {
    if (g_audio_state.azalia_vendor_id == HDA_QEMU_CODEC_OUTPUT) {
        kernel_debug_puts("audio: hda fallback qemu output topology\n");
        g_audio_state.azalia_output_dac_nid = 2u;
        g_audio_state.azalia_output_pin_nid = 3u;
        g_audio_state.azalia_output_mask |= 0x1u;
        (void)mk_audio_azalia_register_output_path(0x1u, 3u, 2u, 100);
        return;
    }
    if (g_audio_state.azalia_vendor_id == HDA_QEMU_CODEC_DUPLEX) {
        kernel_debug_puts("audio: hda fallback qemu duplex topology\n");
        g_audio_state.azalia_output_dac_nid = 2u;
        g_audio_state.azalia_output_pin_nid = 3u;
        g_audio_state.azalia_input_dac_nid = 4u;
        g_audio_state.azalia_input_pin_nid = 5u;
        g_audio_state.azalia_output_mask |= 0x1u;
        g_audio_state.azalia_input_mask |= 0x2u;
        (void)mk_audio_azalia_register_output_path(0x1u, 3u, 2u, 100);
        mk_audio_azalia_register_input_path(0x2u, 5u);
    }
}

static int mk_audio_azalia_probe_widgets(void) {
    uint32_t subnodes;
    uint8_t first_nid;
    uint8_t count;
    int best_output_priority = -1;

    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                g_audio_state.azalia_afg_nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_SUB_NODE_COUNT,
                                &subnodes) != 0) {
        kernel_debug_puts("audio: hda widget probe failed reading afg subnodes\n");
        return -1;
    }

    first_nid = (uint8_t)((subnodes >> 16) & 0xffu);
    count = (uint8_t)(subnodes & 0xffu);
    for (uint8_t i = 0u; i < count; ++i) {
        uint8_t nid = (uint8_t)(first_nid + i);
        uint32_t caps;
        uint32_t type;

        if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                    nid,
                                    HDA_VERB_GET_PARAMETER,
                                    HDA_PARAM_AUDIO_WIDGET_CAP,
                                    &caps) != 0) {
            mk_audio_debug_azalia_widget(nid, 0xffu, 0u, 0u, 0u, -1);
            continue;
        }
        type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
        if (type == HDA_WID_AUD_OUT && g_audio_state.azalia_output_dac_nid == 0u) {
            g_audio_state.azalia_output_dac_nid = nid;
            mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, nid);
            continue;
        }
        if (type == HDA_WID_AUD_IN && g_audio_state.azalia_input_dac_nid == 0u) {
            g_audio_state.azalia_input_dac_nid = nid;
            mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, -1);
            continue;
        }
        if (type == HDA_WID_PIN) {
            uint32_t pin_caps = 0u;
            uint32_t config_default = 0u;
            uint32_t output_mask = 0u;
            uint32_t input_mask = 0u;
            int candidate_dac = -1;
            int priority;

            if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                        nid,
                                        HDA_VERB_GET_PARAMETER,
                                        HDA_PARAM_PIN_CAP,
                                        &pin_caps) != 0 ||
                pin_caps == 0u ||
                pin_caps == 0xffffffffu) {
                mk_audio_debug_azalia_widget(nid, type, caps, pin_caps, 0u, -1);
                continue;
            }

            (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                          nid,
                                          HDA_VERB_GET_CONFIG_DEFAULT,
                                          0u,
                                          &config_default);
            output_mask = mk_audio_hda_output_mask(pin_caps, config_default);
            input_mask = mk_audio_hda_input_mask(pin_caps, config_default);
            g_audio_state.azalia_output_mask |= output_mask;
            g_audio_state.azalia_input_mask |= input_mask;
            if (output_mask != 0u) {
                candidate_dac = mk_audio_azalia_find_output_dac(nid, 0u);
            }
            priority = mk_audio_hda_output_priority(pin_caps, config_default);
            if (candidate_dac >= 0 && priority > best_output_priority) {
                g_audio_state.azalia_output_pin_nid = nid;
                g_audio_state.azalia_output_dac_nid = (uint8_t)candidate_dac;
                best_output_priority = priority;
            }
            if (candidate_dac >= 0 && output_mask != 0u) {
                (void)mk_audio_azalia_register_output_path(output_mask,
                                                           nid,
                                                           (uint8_t)candidate_dac,
                                                           priority);
            }
            if (g_audio_state.azalia_input_pin_nid == 0u && input_mask != 0u) {
                g_audio_state.azalia_input_pin_nid = nid;
            }
            if (input_mask != 0u) {
                mk_audio_azalia_register_input_path(input_mask, nid);
            }
            mk_audio_debug_azalia_widget(nid, type, caps, pin_caps, config_default, candidate_dac);
        } else {
            mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, -1);
        }
    }

    if (g_audio_state.azalia_output_mask == 0u &&
        (g_audio_state.azalia_output_dac_nid != 0u || g_audio_state.azalia_output_pin_nid != 0u)) {
        g_audio_state.azalia_output_mask = 0x1u;
    }
    if (g_audio_state.azalia_output_dac_nid == 0u && g_audio_state.azalia_output_pin_nid == 0u) {
        mk_audio_azalia_apply_known_codec_topology();
    }
    if (g_audio_state.azalia_output_dac_nid != 0u || g_audio_state.azalia_output_pin_nid != 0u) {
        g_audio_state.azalia_widget_probed = 1u;
        kernel_debug_puts("audio: hda widget probe ok\n");
        return 0;
    }
    kernel_debug_puts("audio: hda widget probe found no usable output\n");
    return -1;
}

static int mk_audio_azalia_format_from_params(const struct audio_swpar *params, uint16_t *fmt_out) {
    uint16_t fmt = 0u;
    unsigned channels;
    unsigned bits;
    unsigned rate;

    if (params == 0 || fmt_out == 0) {
        return -1;
    }

    channels = params->pchan;
    bits = params->bits;
    rate = params->rate;
    if (channels == 0u || channels > 16u) {
        return -1;
    }
    fmt |= (uint16_t)(channels - 1u);

    switch (bits) {
    case 8u: fmt |= HDA_SD_FMT_BITS_8_16; break;
    case 16u: fmt |= HDA_SD_FMT_BITS_16_16; break;
    case 20u: fmt |= HDA_SD_FMT_BITS_20_32; break;
    case 24u: fmt |= HDA_SD_FMT_BITS_24_32; break;
    case 32u: fmt |= HDA_SD_FMT_BITS_32_32; break;
    default: return -1;
    }

    switch (rate) {
    case 192000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1; break;
    case 176400u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1; break;
    case 96000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1; break;
    case 88200u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1; break;
    case 48000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1; break;
    case 44100u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1; break;
    case 32000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY3; break;
    case 22050u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY2; break;
    case 16000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY3; break;
    case 11025u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY4; break;
    case 8000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY6; break;
    default: return -1;
    }

    *fmt_out = fmt;
    return 0;
}

static int mk_audio_azalia_program_output_path(void) {
    uint32_t response;
    uint32_t output_mask;
    int selected_bit;
    uint8_t pin_nid;
    uint8_t dac_nid;

    g_audio_state.azalia_path_programmed = 0u;
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_output_dac_nid == 0u) {
        return -1;
    }
    output_mask = mk_audio_output_presence_mask();
    selected_bit = mk_audio_output_bit_from_ord(output_mask, g_audio_state.default_output);
    if (selected_bit < 0 ||
        g_audio_state.azalia_output_dac_nids[selected_bit] == 0u) {
        for (selected_bit = 0; selected_bit < 4; ++selected_bit) {
            if (g_audio_state.azalia_output_dac_nids[selected_bit] != 0u) {
                break;
            }
        }
    }
    if (selected_bit < 0 || selected_bit >= 4 || g_audio_state.azalia_output_dac_nids[selected_bit] == 0u) {
        pin_nid = g_audio_state.azalia_output_pin_nid;
        dac_nid = g_audio_state.azalia_output_dac_nid;
    } else {
        pin_nid = g_audio_state.azalia_output_pin_nids[selected_bit];
        dac_nid = g_audio_state.azalia_output_dac_nids[selected_bit];
    }
    if (dac_nid == 0u) {
        return -1;
    }
    g_audio_state.azalia_output_pin_nid = pin_nid;
    g_audio_state.azalia_output_dac_nid = dac_nid;
    mk_audio_debug_azalia_route(pin_nid, dac_nid, selected_bit);
    mk_audio_azalia_power_widget(g_audio_state.azalia_afg_nid);
    if (pin_nid != 0u &&
        mk_audio_azalia_select_output_route(pin_nid,
                                            dac_nid,
                                            0u) != 0) {
        return -1;
    }
    if (pin_nid != 0u) {
        (void)mk_audio_azalia_power_output_path(pin_nid, dac_nid, 0u);
    } else {
        mk_audio_azalia_power_widget(dac_nid);
    }
    if (pin_nid != 0u) {
        (void)mk_audio_azalia_program_output_amps(pin_nid, dac_nid, 0u);
    } else {
        mk_audio_azalia_program_widget_amp(dac_nid, 0u, 0u);
    }
    if (mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                    dac_nid,
                                    ((uint32_t)HDA_VERB_SET_CONVERTER_FORMAT << 16) |
                                        (uint32_t)g_audio_state.azalia_output_fmt,
                                    &response) != 0) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                dac_nid,
                                HDA_VERB_SET_STREAM_CHANNEL,
                                (uint8_t)(g_audio_state.azalia_output_stream_number << 4),
                                &response) != 0) {
        return -1;
    }
    if (pin_nid != 0u) {
        uint8_t pin_ctl = HDA_PINCTL_OUT_EN;

        if ((selected_bit == 1) || (output_mask & 0x2u) != 0u) {
            pin_ctl |= HDA_PINCTL_HP_EN;
        }
        (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                      pin_nid,
                                      HDA_VERB_SET_PIN_WIDGET_CONTROL,
                                      pin_ctl,
                                      &response);
        (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                      pin_nid,
                                      HDA_VERB_SET_EAPD_BTLENABLE,
                                      HDA_EAPD_ENABLE,
                                      &response);
    }
    for (int bit = 0; bit < 4; ++bit) {
        if (bit == selected_bit ||
            g_audio_state.azalia_output_pin_nids[bit] == 0u) {
            continue;
        }
        mk_audio_azalia_prime_output_pin(g_audio_state.azalia_output_pin_nids[bit], bit);
    }
    g_audio_state.azalia_path_programmed = 1u;
    return 0;
}

static void mk_audio_azalia_program_widget_amp(uint8_t nid, uint8_t input_amp, uint8_t index) {
    uint32_t caps = 0u;
    uint32_t amp_caps = 0u;
    uint32_t gain = 0u;
    uint32_t payload;
    uint16_t verb = input_amp ? HDA_PARAM_INPUT_AMP_CAP : HDA_PARAM_OUTPUT_AMP_CAP;
    uint32_t response = 0u;

    if (nid == 0u) {
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return;
    }
    if (input_amp) {
        if ((caps & HDA_WCAP_INAMP) == 0u) {
            return;
        }
    } else if ((caps & HDA_WCAP_OUTAMP) == 0u) {
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                verb,
                                &amp_caps) != 0) {
        return;
    }

    gain = (amp_caps >> 8) & HDA_AMP_GAIN_MASK;
    payload = gain & HDA_AMP_GAIN_MASK;
    payload |= ((uint32_t)index << HDA_AMP_GAIN_INDEX_SHIFT);
    payload |= HDA_AMP_GAIN_LEFT | HDA_AMP_GAIN_RIGHT;
    payload |= input_amp ? HDA_AMP_GAIN_INPUT : HDA_AMP_GAIN_OUTPUT;
    payload &= ~HDA_AMP_GAIN_MUTE;
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_AMP_GAIN_MUTE,
                                  (uint8_t)payload,
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      nid,
                                      ((uint32_t)HDA_VERB_SET_AMP_GAIN_MUTE << 16) | payload,
                                      &response);
}

static void mk_audio_azalia_power_widget(uint8_t nid) {
    uint32_t response = 0u;

    if (nid == 0u) {
        return;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_POWER_STATE,
                                  HDA_POWER_STATE_D0,
                                  &response);
}

static int mk_audio_azalia_power_output_path(uint8_t nid, uint8_t target_dac, uint32_t depth) {
    uint32_t caps = 0u;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (depth >= 8u || nid == 0u || target_dac == 0u) {
        return -1;
    }
    mk_audio_azalia_power_widget(nid);
    if (nid == target_dac) {
        return 0;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

        if (found != (int)target_dac) {
            continue;
        }
        return mk_audio_azalia_power_output_path(connections[i], target_dac, depth + 1u);
    }
    return -1;
}

static void mk_audio_azalia_prime_output_pin(uint8_t pin_nid, int output_bit) {
    uint8_t pin_ctl = HDA_PINCTL_OUT_EN;
    uint32_t response = 0u;

    if (pin_nid == 0u) {
        return;
    }
    mk_audio_azalia_power_widget(pin_nid);
    if (output_bit == 1) {
        pin_ctl |= HDA_PINCTL_HP_EN;
    }
    mk_audio_azalia_program_widget_amp(pin_nid, 0u, 0u);
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  pin_nid,
                                  HDA_VERB_SET_PIN_WIDGET_CONTROL,
                                  pin_ctl,
                                  &response);
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  pin_nid,
                                  HDA_VERB_SET_EAPD_BTLENABLE,
                                  HDA_EAPD_ENABLE,
                                  &response);
}

static int mk_audio_azalia_program_output_amps(uint8_t nid, uint8_t target_dac, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (depth >= 8u || nid == 0u || target_dac == 0u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }

    mk_audio_azalia_program_widget_amp(nid, 0u, 0u);
    if (nid == target_dac) {
        return 0;
    }

    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

        if (found != (int)target_dac) {
            continue;
        }
        if (type != HDA_WID_AUD_MIXER && connection_count > 1u) {
            mk_audio_azalia_program_widget_amp(nid, 1u, (uint8_t)i);
        } else {
            mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        }
        return mk_audio_azalia_program_output_amps(connections[i], target_dac, depth + 1u);
    }
    return -1;
}

static int mk_audio_azalia_get_connections(uint8_t nid,
                                           uint8_t *connections,
                                           uint32_t max_connections,
                                           uint32_t *count_out) {
    uint32_t widget_caps;
    uint32_t list_len_info;
    uint32_t count = 0u;
    uint32_t last = 0u;
    uint32_t bits;
    uint32_t length;

    if (count_out == 0) {
        return -1;
    }
    *count_out = 0u;
    if (connections == 0 || max_connections == 0u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &widget_caps) != 0) {
        return -1;
    }
    if ((widget_caps & HDA_WCAP_CONNLIST) == 0u) {
        return 0;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_CONN_LIST_LEN,
                                &list_len_info) != 0) {
        return -1;
    }

    bits = (list_len_info & HDA_CONNLIST_LONG) != 0u ? 16u : 8u;
    length = list_len_info & HDA_CONNLIST_LEN_MASK;
    for (uint32_t entry_index = 0u; entry_index < length;) {
        uint32_t response = 0u;
        uint32_t per_word = 32u / bits;

        if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                    nid,
                                    HDA_VERB_GET_CONNECTION_LIST_ENTRY,
                                    (uint8_t)entry_index,
                                    &response) != 0) {
            return -1;
        }
        for (uint32_t k = 0u; k < per_word && entry_index < length; ++k, ++entry_index) {
            uint32_t conn = (response >> (k * bits)) & ((1u << bits) - 1u);
            uint32_t range_flag = 1u << (bits - 1u);

            if (count > 0u && (conn & range_flag) != 0u) {
                uint32_t end = conn & ~range_flag;

                while (last < end && count < max_connections) {
                    last += 1u;
                    connections[count++] = (uint8_t)last;
                }
            } else if (count < max_connections) {
                connections[count++] = (uint8_t)conn;
                last = conn;
            } else {
                last = conn;
            }
        }
    }
    *count_out = count;
    return 0;
}

static int mk_audio_azalia_find_output_dac(uint8_t nid, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (depth >= 8u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (type == HDA_WID_AUD_OUT) {
        return (int)nid;
    }
    if (depth > 0u && (type == HDA_WID_PIN || type == HDA_WID_AUD_IN)) {
        return -1;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

static int mk_audio_azalia_select_output_route(uint8_t nid, uint8_t target_dac, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (depth >= 8u || nid == 0u || target_dac == 0u) {
        return -1;
    }
    if (nid == target_dac) {
        return 0;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

        if (found != (int)target_dac) {
            continue;
        }
        if (type != HDA_WID_AUD_MIXER && connection_count > 1u) {
            uint32_t response = 0u;

            if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                        nid,
                                        HDA_VERB_SET_CONNECTION_SELECT,
                                        (uint8_t)i,
                                        &response) != 0) {
                return -1;
            }
        }
        if (connections[i] == target_dac) {
            return 0;
        }
        return mk_audio_azalia_select_output_route(connections[i], target_dac, depth + 1u);
    }
    return -1;
}

static int mk_audio_azalia_stream_reset(void) {
    uint32_t base;
    uint16_t ctl;
    uint8_t sts;

    base = g_audio_state.azalia_output_regbase;
    if (g_audio_state.azalia_base == 0u || base == 0u) {
        return -1;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl &= (uint16_t)~HDA_SD_CTL_RUN;
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_RUN) == 0u) {
            break;
        }
        mk_audio_compat_delay();
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), (uint16_t)(ctl | HDA_SD_CTL_SRST));
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_SRST) != 0u) {
            break;
        }
        mk_audio_compat_delay();
    }

    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), (uint16_t)(ctl & ~HDA_SD_CTL_SRST));
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_SRST) == 0u) {
            break;
        }
        mk_audio_compat_delay();
    }

    sts = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS));
    sts |= (uint8_t)(HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS);
    mk_audio_azalia_write8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS), sts);
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    return 0;
}

static void mk_audio_azalia_stream_halt(void) {
    uint32_t base;
    uint16_t ctl;
    uint32_t intctl;

    base = g_audio_state.azalia_output_regbase;
    if (g_audio_state.azalia_base == 0u || base == 0u) {
        return;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl &= (uint16_t)~(HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    intctl = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTCTL);
    intctl &= ~(1u << g_audio_state.azalia_output_stream_index);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, intctl);
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
}

static int mk_audio_azalia_stream_start_buffer(uint32_t bytes) {
    uint32_t base;
    uint32_t bdl_addr;
    uint32_t intctl;
    uint32_t offset = 0u;
    uint32_t blk;
    uint16_t lvi = 0u;

    if (g_audio_state.azalia_base == 0u || g_audio_state.azalia_output_regbase == 0u || bytes == 0u) {
        return -1;
    }
    kernel_debug_puts("audio: hda stream start begin\n");
    if (mk_audio_azalia_stream_reset() != 0) {
        kernel_debug_puts("audio: hda stream reset failed\n");
        return -1;
    }

    base = g_audio_state.azalia_output_regbase;
    blk = g_audio_state.info.parameters.round;
    if (blk == 0u || blk > AUICH_DMA_SLOT_SIZE) {
        blk = AUICH_DMA_SLOT_SIZE;
    }
    if ((blk & 1u) != 0u) {
        blk -= 1u;
    }
    if (blk == 0u) {
        blk = AUICH_DMA_SLOT_SIZE;
    }

    memset(g_audio_azalia_bdl, 0, sizeof(g_audio_azalia_bdl));
    for (lvi = 0u; lvi < HDA_BDL_MAX && offset < bytes; ++lvi) {
        uint32_t chunk = bytes - offset;
        if (chunk > blk) {
            chunk = blk;
        }
        g_audio_azalia_bdl[lvi].low = (uint32_t)(uintptr_t)&g_audio_azalia_output_buffer[offset];
        g_audio_azalia_bdl[lvi].high = 0u;
        g_audio_azalia_bdl[lvi].length = chunk;
        g_audio_azalia_bdl[lvi].flags = 0x00000001u;
        offset += chunk;
    }
    if (lvi == 0u) {
        return -1;
    }
    lvi = (uint16_t)(lvi - 1u);

    bdl_addr = (uint32_t)(uintptr_t)&g_audio_azalia_bdl[0];
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPL), bdl_addr);
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPU), 0u);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_LVI), (uint16_t)(lvi & HDA_SD_LVI_MASK));
    mk_audio_azalia_write8(g_audio_state.azalia_base,
                           (uint16_t)(base + HDA_SD_CTL2),
                           (uint8_t)((g_audio_state.azalia_output_stream_number << HDA_SD_CTL2_STRM_SHIFT) &
                                     HDA_SD_CTL2_STRM));
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CBL), bytes);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_FMT), g_audio_state.azalia_output_fmt);
    intctl = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTCTL);
    intctl |= HDA_INTCTL_GIE | HDA_INTCTL_CIE | (1u << g_audio_state.azalia_output_stream_index);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, intctl);
    mk_audio_azalia_write16(g_audio_state.azalia_base,
                            (uint16_t)(base + HDA_SD_CTL),
                            (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL)) |
                                       HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN));
    g_audio_state.azalia_output_running = 1u;
    g_audio_state.azalia_output_bytes = bytes;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = kernel_timer_get_ticks();
    g_audio_state.azalia_output_deadline_tick =
        g_audio_state.azalia_output_start_tick +
        mk_audio_estimated_playback_ticks(&g_audio_state.info.parameters, bytes);
    kernel_debug_puts("audio: hda stream start ok\n");
    return 0;
}

static void mk_audio_azalia_update_output_progress(void) {
    uint32_t base;
    uint32_t pos;
    uint32_t now_ticks;

    base = g_audio_state.azalia_output_regbase;
    if (!g_audio_state.azalia_output_running || g_audio_state.azalia_base == 0u || base == 0u) {
        return;
    }
    pos = mk_audio_azalia_read32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_LPIB));
    if (pos > g_audio_state.azalia_output_bytes) {
        pos = g_audio_state.azalia_output_bytes;
    }
    if (pos > g_audio_state.azalia_output_pos) {
        g_audio_state.playback_bytes_consumed += pos - g_audio_state.azalia_output_pos;
        g_audio_state.azalia_output_pos = pos;
    }
    now_ticks = kernel_timer_get_ticks();
    if (g_audio_state.azalia_output_deadline_tick != 0u &&
        (int32_t)(now_ticks - g_audio_state.azalia_output_deadline_tick) >= 0) {
        g_audio_state.azalia_output_pos = g_audio_state.azalia_output_bytes;
    }
    if (g_audio_state.azalia_output_pos >= g_audio_state.azalia_output_bytes) {
        if (g_audio_state.playback_bytes_consumed < g_audio_state.playback_bytes_written) {
            g_audio_state.playback_bytes_consumed = g_audio_state.playback_bytes_written;
        }
        g_audio_state.azalia_output_running = 0u;
        g_audio_state.azalia_output_start_tick = 0u;
        g_audio_state.azalia_output_deadline_tick = 0u;
        kernel_debug_puts("audio: hda stream idle\n");
    }
}

static void mk_audio_select_azalia_backend(const struct kernel_pci_device_info *pci) {
    char location[MAX_AUDIO_DEV_LEN];

    if (pci == 0) {
        mk_audio_select_soft_backend();
        return;
    }

    g_audio_state.backend_kind = MK_AUDIO_BACKEND_COMPAT_AZALIA;
    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER | MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = 0u;
    g_audio_state.pci = *pci;
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready = 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    g_audio_backend = &g_audio_backend_azalia;

    mk_audio_enable_pci_device(pci);
    if (kernel_pci_bar_is_mmio(pci->bars[HDA_BAR_INDEX])) {
        g_audio_state.azalia_base = kernel_pci_bar_base(pci->bars[HDA_BAR_INDEX]);
    }

    mk_audio_copy_limited(g_audio_state.info.device.name,
                          mk_audio_hda_vendor_name(pci->vendor_id),
                          MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "compat-azalia", MAX_AUDIO_DEV_LEN);
    memset(location, 0, sizeof(location));
    mk_audio_format_pci_location(location, sizeof(location), pci);

    if (g_audio_state.azalia_base != 0u && mk_audio_azalia_reset_controller() == 0) {
        g_audio_state.azalia_gcap = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_GCAP);
        g_audio_state.azalia_vmin = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_VMIN);
        g_audio_state.azalia_vmaj = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_VMAJ);
        g_audio_state.azalia_codec_mask = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_STATESTS);
        if (mk_audio_azalia_select_output_stream() != 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-output-stream", MAX_AUDIO_DEV_LEN);
            mk_audio_refresh_topology_snapshot();
            return;
        }
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_STATESTS, g_audio_state.azalia_codec_mask);
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_GSTS, 0xffffu);
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, 0u);
        (void)mk_audio_azalia_init_command_rings();
        (void)mk_audio_azalia_probe_codec();
        (void)mk_audio_azalia_probe_widgets();
        if (mk_audio_azalia_format_from_params(&g_audio_state.info.parameters,
                                               &g_audio_state.azalia_output_fmt) == 0) {
            g_audio_state.info.flags |= MK_AUDIO_CAPS_PLAYBACK;
            g_audio_state.info.status.mode |= AUMODE_PLAY;
        }
        if (g_audio_state.azalia_codec_probed) {
            mk_audio_azalia_write32(g_audio_state.azalia_base,
                                    HDA_GCTL,
                                    mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL) | HDA_GCTL_UNSOL);
        }
        if (pci->irq_line < 16u) {
            (void)kernel_irq_register_handler(pci->irq_line, mk_audio_azalia_irq_handler);
            kernel_irq_unmask(pci->irq_line);
            g_audio_state.azalia_irq_registered = 1u;
        }
        mk_audio_copy_limited(g_audio_state.info.device.config, location, MAX_AUDIO_DEV_LEN);
    } else if (g_audio_state.azalia_base == 0u) {
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-bar-unavailable", MAX_AUDIO_DEV_LEN);
    } else {
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-reset-failed", MAX_AUDIO_DEV_LEN);
    }

    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_state_reset_playback(void) {
    g_audio_state.playback_head = 0u;
    g_audio_state.playback_tail = 0u;
    g_audio_state.playback_fill = 0u;
    g_audio_state.playback_bytes_consumed = 0u;
    memset(g_audio_state.playback_buffer, 0, sizeof(g_audio_state.playback_buffer));
}

static void mk_audio_state_reset_capture(void) {
    g_audio_state.capture_head = 0u;
    g_audio_state.capture_tail = 0u;
    g_audio_state.capture_fill = 0u;
    g_audio_state.capture_bytes_read = 0u;
    memset(g_audio_state.capture_buffer, 0, sizeof(g_audio_state.capture_buffer));
}

static void mk_audio_capture_ring_write(const uint8_t *data, uint32_t size) {
    uint32_t i;

    if (data == 0 || size == 0u) {
        return;
    }

    if (size > MK_AUDIO_SOFT_BUFFER_SIZE) {
        data += size - MK_AUDIO_SOFT_BUFFER_SIZE;
        size = MK_AUDIO_SOFT_BUFFER_SIZE;
    }

    if ((g_audio_state.capture_fill + size) > MK_AUDIO_SOFT_BUFFER_SIZE) {
        uint32_t overflow = (g_audio_state.capture_fill + size) - MK_AUDIO_SOFT_BUFFER_SIZE;

        g_audio_state.capture_head =
            (g_audio_state.capture_head + overflow) % MK_AUDIO_SOFT_BUFFER_SIZE;
        g_audio_state.capture_fill -= overflow;
        g_audio_state.capture_xruns++;
    }

    for (i = 0u; i < size; ++i) {
        g_audio_state.capture_buffer[g_audio_state.capture_tail] = data[i];
        g_audio_state.capture_tail = (g_audio_state.capture_tail + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    g_audio_state.capture_fill += size;
    g_audio_state.capture_bytes_captured += size;
}

static int mk_audio_capture_ring_read(uint8_t *data, uint32_t size) {
    uint32_t i;
    uint32_t to_read;

    if (data == 0 || size == 0u) {
        return -1;
    }

    to_read = size;
    if (to_read > g_audio_state.capture_fill) {
        to_read = g_audio_state.capture_fill;
    }
    if (to_read == 0u) {
        return 0;
    }

    for (i = 0u; i < to_read; ++i) {
        data[i] = g_audio_state.capture_buffer[g_audio_state.capture_head];
        g_audio_state.capture_head = (g_audio_state.capture_head + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    g_audio_state.capture_fill -= to_read;
    g_audio_state.capture_bytes_read += to_read;
    g_audio_state.capture_read_calls++;
    return (int)to_read;
}

static void mk_audio_refresh_status_snapshot(void) {
    uint32_t runtime_flags = (uint32_t)g_audio_state.backend_kind & MK_AUDIO_STATUS_BACKEND_MASK;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        mk_audio_compat_update_output_progress();
        mk_audio_compat_update_input_progress();
        g_audio_state.info.status.active =
            (g_audio_state.compat_output_running ||
             g_audio_state.compat_output_pending > 0u ||
             g_audio_state.compat_input_running) ? 1 : 0;
    } else if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        mk_audio_azalia_update_output_progress();
        g_audio_state.info.status.active = g_audio_state.azalia_output_running ? 1 : 0;
    } else {
        g_audio_state.info.status.active =
            (g_audio_state.playback_fill > 0u || g_audio_state.capture_fill > 0u) ? 1 : 0;
    }

    if (g_audio_state.compat_irq_registered || g_audio_state.azalia_irq_registered) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED;
    }
    if (g_audio_state.compat_irq_count != 0u || g_audio_state.azalia_irq_count != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_IRQ_SEEN;
    }
    if ((g_audio_state.compat_ready || g_audio_state.azalia_ready) &&
        (g_audio_state.pci.irq_line >= 16u ||
         (!g_audio_state.compat_irq_registered && !g_audio_state.azalia_irq_registered))) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ;
    }
    if (g_audio_state.playback_starvations != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_STARVATION;
    }
    if (g_audio_state.playback_underruns != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_UNDERRUN;
    }
    if (g_audio_state.capture_bytes_captured != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_CAPTURE_DATA;
    }
    if (g_audio_state.capture_xruns != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN;
    }

    g_audio_state.info.status._spare[0] = (int)runtime_flags;
    g_audio_state.info.status._spare[1] = (int)(
        g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA ?
            (g_audio_state.azalia_output_running ? 1u : 0u) :
            g_audio_state.compat_output_pending);
    g_audio_state.info.status._spare[2] = (int)g_audio_state.playback_bytes_written;
    g_audio_state.info.status._spare[3] = (int)g_audio_state.playback_bytes_consumed;
    g_audio_state.info.status._spare[4] = (int)g_audio_state.playback_xruns;
    mk_audio_refresh_topology_snapshot();
}

static int mk_audio_backend_soft_start(void) {
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 1;
    g_audio_state.playback_xruns = 0u;
    g_audio_state.playback_starvations = 0u;
    g_audio_state.playback_underruns = 0u;
    g_audio_state.compat_irq_count = 0u;
    g_audio_state.azalia_irq_count = 0u;
    return 0;
}

static int mk_audio_backend_soft_stop(void) {
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    mk_audio_state_reset_playback();
    return 0;
}

static int mk_audio_backend_soft_write(const uint8_t *data, uint32_t size) {
    uint32_t i;

    if (data == 0 || size == 0u) {
        return -1;
    }

    for (i = 0u; i < size; ++i) {
        g_audio_state.playback_buffer[g_audio_state.playback_tail] = data[i];
        g_audio_state.playback_tail = (g_audio_state.playback_tail + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
        if (g_audio_state.playback_fill < MK_AUDIO_SOFT_BUFFER_SIZE) {
            g_audio_state.playback_fill++;
        } else {
            g_audio_state.playback_head = (g_audio_state.playback_head + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
        }
    }

    g_audio_state.playback_bytes_written += size;
    g_audio_state.playback_write_calls++;
    g_audio_state.info.status.active = 1;
    return (int)size;
}

static int mk_audio_backend_compat_start(void) {
    if (!g_audio_state.compat_ready) {
        return -1;
    }
    if (!g_audio_state.compat_codec_ready && mk_audio_compat_reset_codec() != 0) {
        return -1;
    }
    mk_audio_compat_reset_output_ring();
    mk_audio_compat_reset_input_ring();
    mk_audio_compat_apply_params();
    mk_audio_compat_apply_mixer_state();
    return mk_audio_backend_soft_start();
}

static int mk_audio_backend_compat_stop(void) {
    uint32_t wait_count = 0u;

    while (g_audio_state.compat_output_pending > 0u && wait_count < 200000u) {
        mk_audio_compat_update_output_progress();
        mk_audio_compat_delay();
        wait_count++;
    }
    mk_audio_compat_halt_output();
    mk_audio_compat_halt_input();
    return mk_audio_backend_soft_stop();
}

static int mk_audio_backend_compat_write(const uint8_t *data, uint32_t size) {
    uint32_t offset = 0u;
    uint32_t dma_chunk_limit;

    if (!g_audio_state.compat_ready || !g_audio_state.compat_codec_ready || data == 0 || size == 0u) {
        return -1;
    }

    dma_chunk_limit = g_audio_state.info.parameters.round;
    if (dma_chunk_limit == 0u || dma_chunk_limit > AUICH_DMA_SLOT_SIZE) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }
    if ((dma_chunk_limit & 1u) != 0u) {
        dma_chunk_limit -= 1u;
    }
    if (dma_chunk_limit == 0u) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }

    while (offset < size) {
        uint8_t slot;
        uint32_t chunk_size = size - offset;
        uint32_t wait_count = 0u;

        if (chunk_size > dma_chunk_limit) {
            chunk_size = dma_chunk_limit;
        }
        if ((chunk_size & 1u) != 0u) {
            chunk_size -= 1u;
        }
        if (chunk_size == 0u) {
            break;
        }

        while (g_audio_state.compat_output_pending >= (AUICH_DMALIST_MAX - 1u) &&
               wait_count < 200000u) {
            mk_audio_compat_update_output_progress();
            mk_audio_compat_delay();
            wait_count++;
        }
        if (g_audio_state.compat_output_pending >= (AUICH_DMALIST_MAX - 1u)) {
            break;
        }

        slot = g_audio_state.compat_output_producer;
        memcpy(&g_audio_auich_pcmo_buffers[slot][0], data + offset, chunk_size);
        if (chunk_size < AUICH_DMA_SLOT_SIZE) {
            memset(&g_audio_auich_pcmo_buffers[slot][chunk_size], 0, AUICH_DMA_SLOT_SIZE - chunk_size);
        }
        g_audio_auich_pcmo_dmalist[slot].base = (uint32_t)(uintptr_t)&g_audio_auich_pcmo_buffers[slot][0];
        g_audio_auich_pcmo_dmalist[slot].len = ((chunk_size / 2u) & 0xffffu) | AUICH_DMAF_IOC;
        g_audio_auich_pcmo_bytes[slot] = (uint16_t)chunk_size;

        g_audio_state.compat_output_producer =
            (uint8_t)((g_audio_state.compat_output_producer + 1u) & 0x1fu);
        g_audio_state.compat_output_pending++;
        offset += chunk_size;

        if (mk_audio_compat_start_output_if_needed() != 0) {
            break;
        }
        mk_audio_compat_write8(g_audio_state.compat_aud_base,
                               g_audio_state.compat_aud_is_mmio,
                               AUICH_PCMO + AUICH_LVI,
                               (uint8_t)((g_audio_state.compat_output_producer - 1u) & AUICH_LVI_MASK));
        mk_audio_compat_update_output_progress();
    }

    if (offset != 0u) {
        (void)mk_audio_backend_soft_write(data, offset);
    }
    return offset != 0u ? (int)offset : -1;
}

static int mk_audio_backend_compat_read(uint8_t *data, uint32_t size) {
    if (!g_audio_state.compat_ready || !g_audio_state.compat_codec_ready || data == 0 || size == 0u) {
        return -1;
    }

    if (g_audio_state.capture_fill == 0u) {
        if (mk_audio_compat_capture_block(size) < 0) {
            return -1;
        }
    }

    return mk_audio_capture_ring_read(data, size);
}

static int mk_audio_backend_azalia_start(void) {
    if (!g_audio_state.azalia_ready) {
        return -1;
    }
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    if (mk_audio_azalia_format_from_params(&g_audio_state.info.parameters,
                                           &g_audio_state.azalia_output_fmt) != 0) {
        return -1;
    }
    if (mk_audio_azalia_program_output_path() != 0) {
        return -1;
    }
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    return 0;
}

static int mk_audio_backend_azalia_stop(void) {
    mk_audio_azalia_stream_halt();
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    return 0;
}

static int mk_audio_backend_azalia_write(const uint8_t *data, uint32_t size) {
    uint32_t bytes;

    if (!g_audio_state.azalia_ready || data == 0 || size == 0u) {
        return -1;
    }
    kernel_debug_puts("audio: hda write enter\n");
    if (g_audio_state.azalia_output_running) {
        kernel_debug_puts("audio: hda write recycle\n");
        mk_audio_azalia_update_output_progress();
        if (g_audio_state.azalia_output_running) {
            kernel_debug_puts("audio: hda write force-halt\n");
            mk_audio_azalia_stream_halt();
            g_audio_state.playback_bytes_consumed = g_audio_state.playback_bytes_written;
            g_audio_state.azalia_output_pos = g_audio_state.azalia_output_bytes;
            kernel_debug_puts("audio: hda stream idle\n");
        }
    }
    bytes = size;
    if (bytes > sizeof(g_audio_azalia_output_buffer)) {
        bytes = sizeof(g_audio_azalia_output_buffer);
    }
    if ((bytes & 1u) != 0u) {
        bytes -= 1u;
    }
    if (bytes == 0u) {
        return -1;
    }

    kernel_debug_puts("audio: hda write copy\n");
    memcpy(g_audio_azalia_output_buffer, data, bytes);
    if (mk_audio_azalia_stream_start_buffer(bytes) != 0) {
        return -1;
    }
    g_audio_state.playback_bytes_written += bytes;
    g_audio_state.playback_write_calls++;
    g_audio_state.info.status.active = 1;
    return (int)bytes;
}

static uint8_t mk_audio_clamp_gain(int value) {
    if (value < AUDIO_MIN_GAIN) {
        return AUDIO_MIN_GAIN;
    }
    if (value > AUDIO_MAX_GAIN) {
        return AUDIO_MAX_GAIN;
    }
    return (uint8_t)value;
}

static uint8_t mk_audio_level_from_ctrl(const mixer_ctrl_t *control) {
    uint32_t sum = 0u;
    int channels;

    if (control == 0 || control->un.value.num_channels <= 0) {
        return 0u;
    }

    channels = control->un.value.num_channels;
    if (channels > 8) {
        channels = 8;
    }
    for (int i = 0; i < channels; ++i) {
        sum += control->un.value.level[i];
    }
    return (uint8_t)(sum / (uint32_t)channels);
}

static void mk_audio_fill_level_ctrl(mixer_ctrl_t *control, uint8_t level) {
    if (control == 0) {
        return;
    }

    control->type = AUDIO_MIXER_VALUE;
    control->un.value.num_channels = 2;
    control->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = level;
    control->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = level;
}

static int mk_audio_default_output_valid(int value) {
    return value >= 0 && (uint32_t)value < mk_audio_output_count();
}

static int mk_audio_default_input_valid(int value) {
    return value >= 0 && (uint32_t)value < mk_audio_input_count();
}

static int mk_audio_state_read_control(mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }

    switch (control->dev) {
    case MK_AUDIO_MIXER_OUTPUT_LEVEL:
        mk_audio_fill_level_ctrl(control, g_audio_state.output_level);
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_MUTE:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = g_audio_state.output_muted ? 1 : 0;
        return 0;
    case MK_AUDIO_MIXER_INPUT_LEVEL:
        mk_audio_fill_level_ctrl(control, g_audio_state.input_level);
        return 0;
    case MK_AUDIO_MIXER_INPUT_MUTE:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = g_audio_state.input_muted ? 1 : 0;
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_DEFAULT:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = (int)g_audio_state.default_output;
        return 0;
    case MK_AUDIO_MIXER_INPUT_DEFAULT:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = (int)g_audio_state.default_input;
        return 0;
    default:
        return -1;
    }
}

static int mk_audio_state_write_control(const mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }

    switch (control->dev) {
    case MK_AUDIO_MIXER_OUTPUT_LEVEL:
        if (control->type != AUDIO_MIXER_VALUE) {
            return -1;
        }
        g_audio_state.output_level = mk_audio_clamp_gain((int)mk_audio_level_from_ctrl(control));
        g_audio_state.output_muted = g_audio_state.output_level == 0u ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_MUTE:
        if (control->type != AUDIO_MIXER_ENUM) {
            return -1;
        }
        g_audio_state.output_muted = control->un.ord != 0 ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_LEVEL:
        if (control->type != AUDIO_MIXER_VALUE) {
            return -1;
        }
        g_audio_state.input_level = mk_audio_clamp_gain((int)mk_audio_level_from_ctrl(control));
        g_audio_state.input_muted = g_audio_state.input_level == 0u ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_MUTE:
        if (control->type != AUDIO_MIXER_ENUM) {
            return -1;
        }
        g_audio_state.input_muted = control->un.ord != 0 ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_DEFAULT:
        if (control->type != AUDIO_MIXER_ENUM || !mk_audio_default_output_valid(control->un.ord)) {
            return -1;
        }
        g_audio_state.default_output = (uint8_t)control->un.ord;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_DEFAULT:
        if (control->type != AUDIO_MIXER_ENUM || !mk_audio_default_input_valid(control->un.ord)) {
            return -1;
        }
        g_audio_state.default_input = (uint8_t)control->un.ord;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    default:
        return -1;
    }
}

static int mk_audio_state_get_control_info(uint32_t index, struct mk_audio_control_info *info) {
    if (info == 0 || index >= (sizeof(g_audio_controls) / sizeof(g_audio_controls[0]))) {
        return -1;
    }

    *info = g_audio_controls[index];
    return 0;
}

static int mk_audio_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_audio_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_AUDIO_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_audio_refresh_topology_snapshot();
        mk_audio_refresh_status_snapshot();
        if (mk_message_set_payload(reply, &g_audio_state.info, sizeof(g_audio_state.info)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_audio_refresh_status_snapshot();
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.status,
                                   sizeof(g_audio_state.info.status)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_PARAMS:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.parameters,
                                   sizeof(g_audio_state.info.parameters)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_SET_PARAMS:
        if (request->payload_size != sizeof(g_audio_state.info.parameters)) {
            return -1;
        }
        memcpy(&g_audio_state.info.parameters,
               request->payload,
               sizeof(g_audio_state.info.parameters));
        mk_audio_normalize_params(&g_audio_state.info.parameters);
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_params();
        }
        break;
    case MK_MSG_AUDIO_START:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (g_audio_backend->start == 0 || g_audio_backend->start() != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_STOP:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (g_audio_backend->stop == 0 || g_audio_backend->stop() != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_WRITE:
        if (request->payload_size != sizeof(struct mk_audio_write_request)) {
            return -1;
        }
        {
            const struct mk_audio_write_request *write_request;
            int rc;

            write_request = (const struct mk_audio_write_request *)request->payload;
            if (write_request->size > MK_AUDIO_INLINE_WRITE_MAX) {
                return -1;
            }
            if (g_audio_backend->write == 0) {
                rc = -1;
            } else {
                rc = g_audio_backend->write(write_request->data, write_request->size);
            }
            if (mk_audio_reply_result(reply, rc) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_READ:
        if (request->payload_size != sizeof(struct mk_audio_transfer_request)) {
            return -1;
        }
        {
            const struct mk_audio_transfer_request *read_request;
            struct mk_audio_read_reply payload;
            int rc;

            read_request = (const struct mk_audio_transfer_request *)request->payload;
            if (read_request->size > MK_AUDIO_INLINE_READ_MAX) {
                return -1;
            }
            memset(&payload, 0, sizeof(payload));
            if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH) {
                rc = -1;
            } else {
                rc = mk_audio_backend_compat_read(payload.data, read_request->size);
            }
            if (rc < 0) {
                if (mk_audio_reply_result(reply, rc) != 0) {
                    return -1;
                }
            } else {
                payload.size = (uint32_t)rc;
                if (mk_message_set_payload(reply, &payload, sizeof(payload)) != 0) {
                    return -1;
                }
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_MIXER_READ:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        mixer_ctrl_t control;

        memcpy(&control, request->payload, sizeof(control));
        if (mk_audio_state_read_control(&control) != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        if (mk_message_set_payload(reply, &control, sizeof(control)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_MIXER_WRITE:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        if (mk_audio_state_write_control((const mixer_ctrl_t *)request->payload) != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_CONTROL_INFO:
        if (request->payload_size != sizeof(uint32_t)) {
            return -1;
        }
        {
            uint32_t index = 0u;
            struct mk_audio_control_info info;

            memcpy(&index, request->payload, sizeof(index));
            if (mk_audio_state_get_control_info(index, &info) != 0) {
                if (mk_audio_reply_result(reply, -1) != 0) {
                    return -1;
                }
                g_last_audio_reply = *reply;
                return 0;
            }
            if (mk_message_set_payload(reply, &info, sizeof(info)) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    default:
        return -1;
    }

    if (mk_audio_reply_result(reply, 0) != 0) {
        return -1;
    }
    g_last_audio_reply = *reply;
    return 0;
}

void mk_audio_service_init(void) {
    struct kernel_pci_device_info detected_pci;

    memset(&g_last_audio_request, 0, sizeof(g_last_audio_request));
    memset(&g_last_audio_reply, 0, sizeof(g_last_audio_reply));
    memset(&g_audio_state, 0, sizeof(g_audio_state));

    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER |
                               MK_AUDIO_CAPS_PLAYBACK |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = AUMODE_PLAY;
    AUDIO_INITPAR(&g_audio_state.info.parameters);
    g_audio_state.info.parameters.rate = 48000u;
    g_audio_state.info.parameters.bits = 16u;
    g_audio_state.info.parameters.bps = 2u;
    g_audio_state.info.parameters.sig = 1u;
    g_audio_state.info.parameters.le = 1u;
    g_audio_state.info.parameters.pchan = 2u;
    g_audio_state.info.parameters.rchan = 2u;
    g_audio_state.info.parameters.nblks = 4u;
    g_audio_state.info.parameters.round = 512u;
    g_audio_state.info.parameters.msb = 1u;
    mk_audio_normalize_params(&g_audio_state.info.parameters);
    g_audio_state.output_level = 192u;
    g_audio_state.input_level = 160u;
    g_audio_state.output_muted = 0u;
    g_audio_state.input_muted = 0u;
    g_audio_state.default_output = 0u;
    g_audio_state.default_input = 0u;
    mk_audio_state_reset_playback();
    mk_audio_state_reset_capture();
    mk_audio_select_soft_backend();
    if (mk_audio_try_azalia_backends() != 0 &&
        mk_audio_try_compat_backends() != 0) {
        int hardware_detected = mk_audio_probe_any_hardware_backend();

        if (mk_audio_probe_azalia_backend(&detected_pci) == 0) {
            mk_audio_select_azalia_backend(&detected_pci);
        } else if (mk_audio_probe_compat_backend(&detected_pci) == 0) {
            mk_audio_select_compat_backend(&detected_pci);
        }
        if (!mk_audio_backend_current_is_usable()) {
            mk_audio_select_soft_backend();
            if (hardware_detected > 0) {
                mk_audio_set_softmix_reason("no-usable-hw-backend");
            } else {
                mk_audio_set_softmix_reason("no-pci-audio");
            }
            kernel_debug_puts("audio: no usable hardware backend, staying on softmix\n");
        }
    }
    mk_audio_refresh_topology_snapshot();

    (void)mk_service_launch_task(MK_SERVICE_AUDIO,
                                 "audio",
                                 mk_audio_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN);
}

int mk_audio_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_AUDIO) != 0;
}

int mk_audio_service_get_info(struct mk_audio_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_audio_service_get_status(struct audio_status *status) {
    struct mk_message request;
    struct mk_message reply;

    if (status == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_GET_STATUS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*status)) {
        return -1;
    }
    memcpy(status, reply.payload, sizeof(*status));
    return 0;
}

int mk_audio_service_set_params(const struct audio_swpar *params) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (params == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_SET_PARAMS, params, sizeof(*params)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_start(void) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_START, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_stop(void) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_STOP, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_write(const void *data, uint32_t size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;
    struct mk_audio_write_request chunk;
    const uint8_t *src;
    uint32_t total_written;

    if (data == 0 || size == 0u) {
        return -1;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA &&
        g_audio_backend != 0 &&
        g_audio_backend->write != 0) {
        kernel_debug_puts("audio: syscall write azalia fastpath\n");
        int value = g_audio_backend->write((const uint8_t *)data, size);

        kernel_debug_puts("audio: syscall write returned\n");
        return value;
    }

    src = (const uint8_t *)data;
    total_written = 0u;
    while (total_written < size) {
        uint32_t chunk_size = size - total_written;

        if (chunk_size > MK_AUDIO_INLINE_WRITE_MAX) {
            chunk_size = MK_AUDIO_INLINE_WRITE_MAX;
        }
        memset(&chunk, 0, sizeof(chunk));
        chunk.size = chunk_size;
        memcpy(chunk.data, src + total_written, chunk_size);
        if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_WRITE, &chunk, sizeof(chunk)) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (reply.payload_size != sizeof(result)) {
            return total_written != 0u ? (int)total_written : -1;
        }
        memcpy(&result, reply.payload, sizeof(result));
        if (result.value <= 0) {
            return total_written != 0u ? (int)total_written : result.value;
        }
        total_written += (uint32_t)result.value;
    }

    return (int)total_written;
}

int mk_audio_service_read(void *data, uint32_t size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_transfer_request transfer;

    if (data == 0 || size == 0u || size > MK_AUDIO_INLINE_READ_MAX) {
        return -1;
    }

    transfer.size = size;
    transfer.transfer_id = 0u;
    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_READ, &transfer, sizeof(transfer)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_audio_result)) {
        struct mk_audio_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(struct mk_audio_read_reply)) {
        return -1;
    }
    {
        struct mk_audio_read_reply payload;

        memcpy(&payload, reply.payload, sizeof(payload));
        if (payload.size > size || payload.size > MK_AUDIO_INLINE_READ_MAX) {
            return -1;
        }
        memcpy(data, payload.data, payload.size);
        return (int)payload.size;
    }
}

int mk_audio_service_get_control_info(uint32_t index, struct mk_audio_control_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_CONTROL_INFO, &index, sizeof(index)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_audio_result)) {
        struct mk_audio_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_audio_service_mixer_read(mixer_ctrl_t *control) {
    struct mk_message request;
    struct mk_message reply;

    if (control == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_MIXER_READ, control, sizeof(*control)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*control)) {
        return -1;
    }
    memcpy(control, reply.payload, sizeof(*control));
    return 0;
}

int mk_audio_service_mixer_write(const mixer_ctrl_t *control) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (control == 0) {
        return -1;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_MIXER_WRITE, control, sizeof(*control)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_audio_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_audio_request;
    return 0;
}
