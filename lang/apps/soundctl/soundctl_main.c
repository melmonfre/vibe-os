#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>
#include <include/userland_api.h>

#define SOUNDCTL_WAV_STREAM_CHUNK 960u
#define SOUNDCTL_WAV_AZALIA_CHUNK 16384u
#define SOUNDCTL_WAV_UAUDIO_CHUNK 16384u
#define MK_AUDIO_STATUS_BACKEND_MASK 0x000000ffu
#define MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED 0x00000100u
#define MK_AUDIO_STATUS_FLAG_IRQ_SEEN 0x00000200u
#define MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ 0x00000400u
#define MK_AUDIO_STATUS_FLAG_STARVATION 0x00000800u
#define MK_AUDIO_STATUS_FLAG_UNDERRUN 0x00001000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_DATA 0x00002000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN 0x00004000u
static unsigned soundctl_feature_flags(const struct mk_audio_info *info);

static void soundctl_debug(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

static unsigned short soundctl_read_u16_le(const unsigned char *src) {
    return (unsigned short)((unsigned short)src[0] | ((unsigned short)src[1] << 8));
}

static unsigned soundctl_read_u32_le(const unsigned char *src) {
    return (unsigned)src[0] |
           ((unsigned)src[1] << 8) |
           ((unsigned)src[2] << 16) |
           ((unsigned)src[3] << 24);
}

static void soundctl_write_u16_le(unsigned char *dst, unsigned value) {
    dst[0] = (unsigned char)(value & 0xffu);
    dst[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void soundctl_write_u32_le(unsigned char *dst, unsigned value) {
    dst[0] = (unsigned char)(value & 0xffu);
    dst[1] = (unsigned char)((value >> 8) & 0xffu);
    dst[2] = (unsigned char)((value >> 16) & 0xffu);
    dst[3] = (unsigned char)((value >> 24) & 0xffu);
}

static int soundctl_chunk_id_eq(const unsigned char *src, const char *id) {
    return src[0] == (unsigned char)id[0] &&
           src[1] == (unsigned char)id[1] &&
           src[2] == (unsigned char)id[2] &&
           src[3] == (unsigned char)id[3];
}

static void soundctl_usage(void) {
    printf("usage: soundctl <command> [args]\n");
    printf("commands:\n");
    printf("  list\n");
    printf("  status\n");
    printf("  set <output|input> <0-100>\n");
    printf("  mute <output|input>\n");
    printf("  unmute <output|input>\n");
    printf("  default <output|input> <choice>\n");
    printf("  tone [ms] [hz]\n");
    printf("  play [path]\n");
    printf("  record [ms] [path]\n");
}

static const char *soundctl_backend_name(int backend_kind) {
    switch (backend_kind) {
    case 4:
        return "compat-uaudio";
    case 3:
        return "pcspkr";
    case 2:
        return "compat-azalia";
    case 1:
        return "compat-ac97";
    case 0:
    default:
        return "softmix";
    }
}

static int soundctl_status_backend(const struct audio_status *status) {
    if (status == 0) {
        return 0;
    }
    return status->_spare[0] & (int)MK_AUDIO_STATUS_BACKEND_MASK;
}

static int soundctl_current_backend(void) {
    struct audio_status status;

    if (vibe_app_audio_get_status(&status) != 0) {
        return -1;
    }
    return soundctl_status_backend(&status);
}

static int soundctl_wait_for_playback_idle(unsigned expected_ticks) {
    struct audio_status status;
    unsigned start = 0u;
    unsigned timeout = expected_ticks + 40u;

    if (timeout < 40u) {
        timeout = 40u;
    }
    start = vibe_app_ticks();

    for (;;) {
        if (vibe_app_audio_get_status(&status) == 0) {
            if (status.active == 0 && status._spare[1] == 0) {
                return 0;
            }
        }
        if ((unsigned)(vibe_app_ticks() - start) >= timeout) {
            return -1;
        }
        vibe_app_yield();
    }
}

static void soundctl_debug_uint_line(const char *prefix, unsigned value);
static int soundctl_handle_capture_event(const struct mk_audio_event *event);
static void soundctl_drain_capture_events(int subscribed);
static int soundctl_wait_for_capture_ready(int subscribed);
static void soundctl_emit_record_capture_debug(void);
static void soundctl_write_record_summary(const char *target, unsigned duration_ms, unsigned captured);

static __attribute__((unused)) unsigned soundctl_status_flags(const struct audio_status *status) {
    if (status == 0) {
        return 0u;
    }
    return (unsigned)status->_spare[0] & ~MK_AUDIO_STATUS_BACKEND_MASK;
}

static void soundctl_append_char(char *buf, int max_len, int *len, char ch) {
    if (buf == 0 || len == 0 || max_len <= 0) {
        return;
    }
    if (*len >= max_len - 1) {
        return;
    }
    buf[*len] = ch;
    *len += 1;
    buf[*len] = '\0';
}

static void soundctl_append_text(char *buf, int max_len, int *len, const char *text) {
    if (buf == 0 || len == 0 || text == 0 || max_len <= 0) {
        return;
    }
    while (*text != '\0' && *len < max_len - 1) {
        buf[*len] = *text;
        *len += 1;
        ++text;
    }
    buf[*len] = '\0';
}

static void soundctl_append_uint(char *buf, int max_len, int *len, unsigned value) {
    char digits[10];
    int digit_count = 0;
    int i;

    if (buf == 0 || len == 0 || max_len <= 0) {
        return;
    }

    if (value == 0u) {
        soundctl_append_char(buf, max_len, len, '0');
        return;
    }

    while (value != 0u && digit_count < (int)sizeof(digits)) {
        digits[digit_count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (i = digit_count - 1; i >= 0; --i) {
        soundctl_append_char(buf, max_len, len, digits[i]);
    }
}

static void soundctl_append_hex(char *buf, int max_len, int *len, unsigned value, int digits) {
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        unsigned nibble = (value >> (unsigned)shift) & 0xfu;
        soundctl_append_char(buf, max_len, len, (char)(nibble < 10u ? ('0' + nibble) : ('a' + (nibble - 10u))));
    }
}

static void soundctl_debug_uint_line(const char *prefix, unsigned value) {
    char line[64];
    int len = 0;

    if (prefix == 0) {
        return;
    }
    line[0] = '\0';
    soundctl_append_text(line, (int)sizeof(line), &len, prefix);
    soundctl_append_uint(line, (int)sizeof(line), &len, value);
    soundctl_append_char(line, (int)sizeof(line), &len, '\n');
    soundctl_debug(line);
}

static int soundctl_handle_capture_event(const struct mk_audio_event *event) {
    if (event == 0) {
        return 0;
    }

    switch (event->event_type) {
    case MK_AUDIO_EVENT_CAPTURE_READY:
        soundctl_debug_uint_line("soundctl: capture-ready=", (unsigned)event->queued_bytes);
        return 1;
    case MK_AUDIO_EVENT_CAPTURE_XRUN:
        soundctl_debug_uint_line("soundctl: capture-xrun=", (unsigned)event->underruns);
        break;
    case MK_AUDIO_EVENT_OVERFLOW:
        if (event->dropped_events != 0u) {
            soundctl_debug_uint_line("soundctl: capture-overflow=", (unsigned)event->dropped_events);
        }
        break;
    default:
        break;
    }

    return 0;
}

static void soundctl_drain_capture_events(int subscribed) {
    struct mk_audio_event event;

    if (!subscribed) {
        return;
    }

    while (vibe_app_audio_event_receive(&event, 0u) == 0) {
        (void)soundctl_handle_capture_event(&event);
    }
}

static int soundctl_wait_for_capture_ready(int subscribed) {
    struct mk_audio_event event;
    unsigned timeout_ticks;

    if (!subscribed) {
        return -1;
    }

    timeout_ticks = vibe_app_clock_hz() / 4u;
    if (timeout_ticks == 0u) {
        timeout_ticks = 1u;
    }

    for (;;) {
        if (vibe_app_audio_event_receive(&event, timeout_ticks) != 0) {
            return -1;
        }
        if (soundctl_handle_capture_event(&event)) {
            return 0;
        }
    }
}

static void soundctl_emit_record_capture_debug(void) {
    struct mk_audio_info info;
    unsigned feature_flags;

    if (vibe_app_audio_get_info(&info) != 0) {
        return;
    }

    feature_flags = soundctl_feature_flags(&info);
    soundctl_debug((feature_flags & 0x400u) != 0u ? "soundctl: capture-dma=1\n"
                                                  : "soundctl: capture-dma=0\n");
}

static void soundctl_write_record_summary(const char *target, unsigned duration_ms, unsigned captured) {
    char line[160];
    int len = 0;

    line[0] = '\0';
    soundctl_append_text(line, (int)sizeof(line), &len, "record: ");
    soundctl_append_uint(line, (int)sizeof(line), &len, duration_ms);
    soundctl_append_text(line, (int)sizeof(line), &len, " ms -> ");
    soundctl_append_text(line, (int)sizeof(line), &len, target != 0 ? target : "/capture.wav");
    soundctl_append_text(line, (int)sizeof(line), &len, " (");
    soundctl_append_uint(line, (int)sizeof(line), &len, captured);
    soundctl_append_text(line, (int)sizeof(line), &len, " bytes)");
    puts(line);
}

static void soundctl_format_hardware_diag(const struct mk_audio_info *info, char *buf, int max_len) {
    unsigned feature_flags;

    if (buf == 0 || max_len <= 0) {
        return;
    }
    buf[0] = '\0';
    if (info == 0 || info->controller_pci_id == 0u) {
        return;
    }
    feature_flags = soundctl_feature_flags(info);
    if ((feature_flags & MK_AUDIO_FEATURE_USB_ATTACH_READY) != 0u) {
        snprintf(buf,
                 (size_t)max_len,
                 "pci %04x:%04x b%02x:s%02x:f%u  usb-audio as=%02x alt=%02x ep=%02x cfg=%02x",
                 (unsigned)((info->controller_pci_id >> 16) & 0xffffu),
                 (unsigned)(info->controller_pci_id & 0xffffu),
                 (unsigned)((info->controller_location >> 16) & 0xffu),
                 (unsigned)((info->controller_location >> 8) & 0xffu),
                 (unsigned)(info->controller_location & 0xffu),
                 (unsigned)((info->output_route >> 24) & 0xffu),
                 (unsigned)((info->output_route >> 16) & 0xffu),
                 (unsigned)((info->output_route >> 8) & 0xffu),
                 (unsigned)(info->output_route & 0xffu));
        return;
    }
    if (info->codec_vendor_id != 0u || info->output_route != 0u) {
        snprintf(buf,
                 (size_t)max_len,
                 "pci %04x:%04x b%02x:s%02x:f%u  codec %08x  route pin=%02x dac=%02x",
                 (unsigned)((info->controller_pci_id >> 16) & 0xffffu),
                 (unsigned)(info->controller_pci_id & 0xffffu),
                 (unsigned)((info->controller_location >> 16) & 0xffu),
                 (unsigned)((info->controller_location >> 8) & 0xffu),
                 (unsigned)(info->controller_location & 0xffu),
                 (unsigned)info->codec_vendor_id,
                 (unsigned)((info->output_route >> 8) & 0xffu),
                 (unsigned)(info->output_route & 0xffu));
        return;
    }
    snprintf(buf,
             (size_t)max_len,
             "pci %04x:%04x b%02x:s%02x:f%u",
             (unsigned)((info->controller_pci_id >> 16) & 0xffffu),
             (unsigned)(info->controller_pci_id & 0xffffu),
             (unsigned)((info->controller_location >> 16) & 0xffu),
             (unsigned)((info->controller_location >> 8) & 0xffu),
             (unsigned)(info->controller_location & 0xffu));
}

static void soundctl_emit_hardware_debug(const struct mk_audio_info *info) {
    char line[96];
    int len = 0;
    unsigned feature_flags;

    if (info == 0) {
        return;
    }
    feature_flags = soundctl_feature_flags(info);
    line[0] = '\0';
    soundctl_append_text(line, (int)sizeof(line), &len, "soundctl: pci=");
    soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->controller_pci_id >> 16) & 0xffffu), 4);
    soundctl_append_char(line, (int)sizeof(line), &len, ':');
    soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)(info->controller_pci_id & 0xffffu), 4);
    soundctl_append_char(line, (int)sizeof(line), &len, '\n');
    soundctl_debug(line);
    len = 0;
    line[0] = '\0';
    soundctl_append_text(line, (int)sizeof(line), &len, "soundctl: codec=");
    soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)info->codec_vendor_id, 8);
    soundctl_append_char(line, (int)sizeof(line), &len, '\n');
    soundctl_debug(line);
    len = 0;
    line[0] = '\0';
    soundctl_append_text(line, (int)sizeof(line), &len, "soundctl: route=");
    if ((feature_flags & MK_AUDIO_FEATURE_USB_ATTACH_READY) != 0u) {
        soundctl_append_text(line, (int)sizeof(line), &len, "as");
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->output_route >> 24) & 0xffu), 2);
        soundctl_append_text(line, (int)sizeof(line), &len, "/alt");
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->output_route >> 16) & 0xffu), 2);
        soundctl_append_text(line, (int)sizeof(line), &len, "/ep");
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->output_route >> 8) & 0xffu), 2);
        soundctl_append_text(line, (int)sizeof(line), &len, "/cfg");
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)(info->output_route & 0xffu), 2);
    } else {
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->output_route >> 8) & 0xffu), 2);
        soundctl_append_char(line, (int)sizeof(line), &len, '/');
        soundctl_append_hex(line, (int)sizeof(line), &len, (unsigned)(info->output_route & 0xffu), 2);
    }
    soundctl_append_char(line, (int)sizeof(line), &len, '\n');
    soundctl_debug(line);
}

static int soundctl_output_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 1;
    }
    if ((int)info->parameters._spare[1] > 0) {
        return (int)info->parameters._spare[1];
    }
    return 1;
}

static int soundctl_input_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (int)info->parameters._spare[2];
}

static unsigned soundctl_output_mask(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0x1u;
    }
    return info->parameters._spare[3] != 0u ? info->parameters._spare[3] : 0x1u;
}

static unsigned soundctl_feature_flags(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[5];
}

static const char *soundctl_device_hint(const audio_device_t *device) {
    const char *config;

    if (device == 0) {
        return "";
    }
    config = device->config;
    if (strcmp(config, "hda-no-output-stream") == 0) {
        return "no output stream announced by HDA controller";
    }
    if (strcmp(config, "hda-reset-failed") == 0) {
        return "HDA controller reset failed";
    }
    if (strcmp(config, "hda-bar-unavailable") == 0) {
        return "HDA MMIO BAR unavailable";
    }
    if (strcmp(config, "no-usable-hw-backend") == 0) {
        return "hardware audio found, but no backend became usable";
    }
    if (strcmp(config, "no-pci-audio") == 0) {
        return "no PCI audio controller detected";
    }
    if (strcmp(config, "bar-unavailable") == 0) {
        return "AC97 BAR unavailable";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attached") == 0) {
        return "USB Audio Class ja teve SET_INTERFACE de playback; falta plugar o caminho de write/isoc do compat-uaudio";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attach-ready") == 0) {
        return "USB Audio Class ja anexavel; falta o backend compat-uaudio sair do attach para playback";
    }
    if (strcmp(config, "pcspkr-fallback-usb-configured-audio") == 0) {
        return "USB Audio Class configurado; falta fechar o attach minimo do compat-uaudio";
    }
    return "";
}

static const char *soundctl_output_name_from_bit(int bit) {
    switch (bit) {
    case 1:
        return "headphones";
    case 2:
        return "surround";
    case 3:
        return "center-lfe";
    case 0:
    default:
        return "main";
    }
}

static const char *soundctl_hda_output_name_from_bit(int bit) {
    switch (bit) {
    case 1:
        return "headphones";
    case 2:
        return "line-out";
    case 3:
        return "digital";
    case 0:
    default:
        return "speaker";
    }
}

static const char *soundctl_output_default_name(const struct mk_audio_info *info, int ord) {
    unsigned mask = soundctl_output_mask(info);
    int backend = info != 0 ? soundctl_status_backend(&info->status) : 0;
    int seen = 0;

    for (int bit = 0; bit < 4; ++bit) {
        if ((mask & (1u << bit)) == 0u) {
            continue;
        }
        if (seen == ord) {
            return backend == 2 ? soundctl_hda_output_name_from_bit(bit)
                                : soundctl_output_name_from_bit(bit);
        }
        ++seen;
    }
    return backend == 2 ? soundctl_hda_output_name_from_bit(0)
                        : soundctl_output_name_from_bit(0);
}

static const char *soundctl_input_default_name(const struct mk_audio_info *info, int ord) {
    if (ord == 1 && soundctl_input_count(info) > 1) {
        return info != 0 && soundctl_status_backend(&info->status) == 2 ? "line-in" : "line";
    }
    return "mic";
}

static int soundctl_target_ids(const char *target, int *level_id, int *mute_id) {
    if (target == 0 || level_id == 0 || mute_id == 0) {
        return -1;
    }

    if (strcmp(target, "output") == 0) {
        *level_id = MK_AUDIO_MIXER_OUTPUT_LEVEL;
        *mute_id = MK_AUDIO_MIXER_OUTPUT_MUTE;
        return 0;
    }
    if (strcmp(target, "output-volume") == 0 || strcmp(target, "output-main") == 0) {
        *level_id = MK_AUDIO_MIXER_OUTPUT_LEVEL;
        *mute_id = MK_AUDIO_MIXER_OUTPUT_MUTE;
        return 0;
    }
    if (strcmp(target, "input") == 0) {
        *level_id = MK_AUDIO_MIXER_INPUT_LEVEL;
        *mute_id = MK_AUDIO_MIXER_INPUT_MUTE;
        return 0;
    }
    if (strcmp(target, "input-volume") == 0 || strcmp(target, "mic") == 0) {
        *level_id = MK_AUDIO_MIXER_INPUT_LEVEL;
        *mute_id = MK_AUDIO_MIXER_INPUT_MUTE;
        return 0;
    }
    return -1;
}

static int soundctl_default_control_id(const char *target) {
    if (target == 0) {
        return -1;
    }
    if (strcmp(target, "output") == 0 || strcmp(target, "output-default") == 0) {
        return MK_AUDIO_MIXER_OUTPUT_DEFAULT;
    }
    if (strcmp(target, "input") == 0 || strcmp(target, "input-default") == 0 || strcmp(target, "mic") == 0) {
        return MK_AUDIO_MIXER_INPUT_DEFAULT;
    }
    return -1;
}

static int soundctl_default_choice_value(const struct mk_audio_info *info,
                                         int control_id,
                                         const char *value_text) {
    if (value_text == 0) {
        return -1;
    }

    if (control_id == MK_AUDIO_MIXER_OUTPUT_DEFAULT) {
        int output_count = soundctl_output_count(info);

        for (int ord = 0; ord < output_count; ++ord) {
            const char *name = soundctl_output_default_name(info, ord);

            if (strcmp(value_text, name) == 0) {
                return ord;
            }
            if (ord == 0 && (strcmp(value_text, "speaker") == 0 || strcmp(value_text, "speakers") == 0)) {
                return ord;
            }
            if (strcmp(name, "line-out") == 0 &&
                (strcmp(value_text, "lineout") == 0 || strcmp(value_text, "line") == 0)) {
                return ord;
            }
            if (strcmp(name, "digital") == 0 &&
                (strcmp(value_text, "spdif") == 0 || strcmp(value_text, "hdmi") == 0)) {
                return ord;
            }
            if (strcmp(name, "headphones") == 0 && strcmp(value_text, "hp") == 0) {
                return ord;
            }
            if (strcmp(name, "center-lfe") == 0 &&
                (strcmp(value_text, "center") == 0 || strcmp(value_text, "lfe") == 0)) {
                return ord;
            }
            if (strcmp(name, "surround") == 0 && strcmp(value_text, "rear") == 0) {
                return ord;
            }
        }
        return -1;
    }
    if (control_id == MK_AUDIO_MIXER_INPUT_DEFAULT) {
        if (strcmp(value_text, "mic") == 0 || strcmp(value_text, "microphone") == 0) {
            return 0;
        }
        if ((strcmp(value_text, "line") == 0 || strcmp(value_text, "line-in") == 0 ||
             strcmp(value_text, "linein") == 0) &&
            soundctl_input_count(info) > 1) {
            return 1;
        }
        return -1;
    }
    return -1;
}

static int soundctl_parse_percent(const char *text, int *value_out) {
    int value = 0;

    if (text == 0 || *text == '\0' || value_out == 0) {
        return -1;
    }

    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        value = (value * 10) + (*text - '0');
        if (value > 100) {
            return -1;
        }
        ++text;
    }

    *value_out = value;
    return 0;
}

static int soundctl_parse_uint(const char *text, unsigned *value_out) {
    unsigned value = 0u;

    if (text == 0 || *text == '\0' || value_out == 0) {
        return -1;
    }

    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        value = (value * 10u) + (unsigned)(*text - '0');
        ++text;
    }

    *value_out = value;
    return 0;
}

static unsigned soundctl_estimated_playback_ticks(const struct audio_swpar *params, unsigned data_size) {
    unsigned bytes_per_second;
    unsigned duration_ms;
    unsigned ticks;

    if (params == 0 || params->rate == 0u || params->pchan == 0u || params->bps == 0u || data_size == 0u) {
        return 0u;
    }

    bytes_per_second = params->rate * params->pchan * params->bps;
    if (bytes_per_second == 0u) {
        return 0u;
    }

    duration_ms = (data_size * 1000u) / bytes_per_second;
    if (((data_size * 1000u) % bytes_per_second) != 0u) {
        duration_ms += 1u;
    }

    ticks = (duration_ms + 9u) / 10u;
    if (ticks < 3u) {
        ticks = 3u;
    }
    return ticks;
}

static int soundctl_read_level_percent(int control_id, int *percent_out) {
    mixer_ctrl_t control = {0};
    int level;

    if (percent_out == 0) {
        return -1;
    }

    control.dev = control_id;
    control.type = AUDIO_MIXER_VALUE;
    if (vibe_app_audio_mixer_read(&control) != 0) {
        return -1;
    }
    if (control.type != AUDIO_MIXER_VALUE || control.un.value.num_channels <= 0) {
        return -1;
    }

    level = control.un.value.level[0];
    *percent_out = (level * 100) / AUDIO_MAX_GAIN;
    return 0;
}

static int soundctl_read_mute(int control_id, int *muted_out) {
    mixer_ctrl_t control = {0};

    if (muted_out == 0) {
        return -1;
    }

    control.dev = control_id;
    control.type = AUDIO_MIXER_ENUM;
    if (vibe_app_audio_mixer_read(&control) != 0) {
        return -1;
    }
    if (control.type != AUDIO_MIXER_ENUM) {
        return -1;
    }

    *muted_out = control.un.ord != 0;
    return 0;
}

static int soundctl_read_enum(int control_id, int *value_out) {
    mixer_ctrl_t control = {0};

    if (value_out == 0) {
        return -1;
    }

    control.dev = control_id;
    control.type = AUDIO_MIXER_ENUM;
    if (vibe_app_audio_mixer_read(&control) != 0) {
        return -1;
    }
    if (control.type != AUDIO_MIXER_ENUM) {
        return -1;
    }

    *value_out = control.un.ord;
    return 0;
}

static int soundctl_write_level_percent(int control_id, int percent) {
    mixer_ctrl_t control = {0};
    int gain;

    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    gain = (percent * AUDIO_MAX_GAIN) / 100;
    control.dev = control_id;
    control.type = AUDIO_MIXER_VALUE;
    control.un.value.num_channels = 2;
    control.un.value.level[AUDIO_MIXER_LEVEL_LEFT] = (uint8_t)gain;
    control.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = (uint8_t)gain;
    return vibe_app_audio_mixer_write(&control);
}

static int soundctl_write_mute(int control_id, int muted) {
    mixer_ctrl_t control = {0};

    control.dev = control_id;
    control.type = AUDIO_MIXER_ENUM;
    control.un.ord = muted ? 1 : 0;
    return vibe_app_audio_mixer_write(&control);
}

static int soundctl_write_enum(int control_id, int value) {
    mixer_ctrl_t control = {0};

    control.dev = control_id;
    control.type = AUDIO_MIXER_ENUM;
    control.un.ord = value;
    return vibe_app_audio_mixer_write(&control);
}

static void soundctl_print_target(const char *label, int level_id, int mute_id) {
    int percent = 0;
    int muted = 0;

    if (soundctl_read_level_percent(level_id, &percent) != 0 ||
        soundctl_read_mute(mute_id, &muted) != 0) {
        printf("%s: unavailable\n", label);
        return;
    }

    printf("%s: %d%% %s\n", label, percent, muted ? "(muted)" : "(active)");
}

static void soundctl_print_default_target(const struct mk_audio_info *info,
                                          const char *label,
                                          int control_id) {
    int value = 0;
    const char *name = "unknown";

    if (soundctl_read_enum(control_id, &value) != 0) {
        printf("%s default: unavailable\n", label);
        return;
    }

    if (control_id == MK_AUDIO_MIXER_OUTPUT_DEFAULT) {
        name = soundctl_output_default_name(info, value);
    } else if (control_id == MK_AUDIO_MIXER_INPUT_DEFAULT) {
        name = soundctl_input_default_name(info, value);
    }
    printf("%s default: %s\n", label, name);
}

static int soundctl_command_list(void) {
    struct mk_audio_info info;
    struct mk_audio_control_info control_info;
    uint32_t index = 0u;
    int output_count;
    int input_count;
    unsigned feature_flags;
    char hardware_diag[160];
    if (vibe_app_audio_get_info(&info) != 0) {
        printf("soundctl: audio service unavailable\n");
        return 1;
    }

    printf("device: %s %s (%s)\n", info.device.name, info.device.version, info.device.config);
    if (soundctl_device_hint(&info.device)[0] != '\0') {
        printf("hint: %s\n", soundctl_device_hint(&info.device));
    }
    soundctl_format_hardware_diag(&info, hardware_diag, (int)sizeof(hardware_diag));
    if (hardware_diag[0] != '\0') {
        printf("hardware: %s\n", hardware_diag);
    }
    printf("flags: 0x%x\n", (unsigned int)info.flags);
    output_count = soundctl_output_count(&info);
    input_count = soundctl_input_count(&info);
    feature_flags = soundctl_feature_flags(&info);
    soundctl_print_target("output", MK_AUDIO_MIXER_OUTPUT_LEVEL, MK_AUDIO_MIXER_OUTPUT_MUTE);
    soundctl_print_default_target(&info, "output", MK_AUDIO_MIXER_OUTPUT_DEFAULT);
    printf("output endpoints: %s", soundctl_output_default_name(&info, 0));
    for (int i = 1; i < output_count; ++i) {
        printf(", %s", soundctl_output_default_name(&info, i));
    }
    printf("\n");
    if (input_count > 0) {
        soundctl_print_target("input", MK_AUDIO_MIXER_INPUT_LEVEL, MK_AUDIO_MIXER_INPUT_MUTE);
        soundctl_print_default_target(&info, "input", MK_AUDIO_MIXER_INPUT_DEFAULT);
        printf("input endpoints: %s", soundctl_input_default_name(&info, 0));
        if (input_count > 1) {
            printf(", %s", soundctl_input_default_name(&info, 1));
        }
        printf("\n");
    }
    printf("features: %sirq %scapture %s %scodecready-quirk %smultichannel %scapture-dma %scorb-rirb %scodec-probe %swidget-probe %spath-programmed %susb-attach-ready %susb-attached-ready %scontrol-owner-audiosvc %skernel-backend-executor %sui-progress-decoupled\n",
           (feature_flags & 0x2u) != 0u ? "" : "no-",
           (feature_flags & 0x8u) != 0u ? "" : "no-",
           (feature_flags & 0x80u) != 0u ? "mmio" : "io",
           (feature_flags & 0x100u) != 0u ? "" : "no-",
           (feature_flags & 0x200u) != 0u ? "" : "no-",
           (feature_flags & 0x400u) != 0u ? "" : "no-",
           (feature_flags & 0x800u) != 0u ? "" : "no-",
           (feature_flags & 0x1000u) != 0u ? "" : "no-",
           (feature_flags & 0x2000u) != 0u ? "" : "no-",
           (feature_flags & 0x4000u) != 0u ? "" : "no-",
           (feature_flags & MK_AUDIO_FEATURE_USB_ATTACH_READY) != 0u ? "" : "no-",
           (feature_flags & MK_AUDIO_FEATURE_USB_ATTACHED_READY) != 0u ? "" : "no-",
           (feature_flags & MK_AUDIO_FEATURE_CONTROL_OWNER_AUDIOSVC) != 0u ? "" : "no-",
           (feature_flags & MK_AUDIO_FEATURE_KERNEL_BACKEND_EXECUTOR) != 0u ? "" : "no-",
           (feature_flags & MK_AUDIO_FEATURE_UI_PROGRESS_DECOUPLED) != 0u ? "" : "no-");
    printf("controls:\n");
    while (vibe_app_audio_get_control_info(index, &control_info) == 0) {
        printf("  %s/%s -> id=%u %s pair=%u\n",
               control_info.group,
               control_info.name,
               (unsigned int)control_info.control_id,
               control_info.kind == MK_AUDIO_CONTROL_TOGGLE ? "toggle" :
               (control_info.kind == MK_AUDIO_CONTROL_ENUM ? "enum" : "level"),
               (unsigned int)control_info.pair_id);
        ++index;
    }
    return 0;
}

static int soundctl_command_status(void) {
    struct mk_audio_info info;
    struct audio_status status;
    int input_count;
    unsigned feature_flags;
    char hardware_diag[160];

    soundctl_debug("soundctl: status begin\n");
    if (vibe_app_audio_get_info(&info) != 0 || vibe_app_audio_get_status(&status) != 0) {
        soundctl_debug("soundctl: status backend-fail\n");
        printf("soundctl: audio service unavailable\n");
        return 1;
    }
    soundctl_debug("soundctl: status info-ok\n");

    printf("device: %s %s (%s)\n", info.device.name, info.device.version, info.device.config);
    if (soundctl_device_hint(&info.device)[0] != '\0') {
        printf("hint: %s\n", soundctl_device_hint(&info.device));
    }
    soundctl_format_hardware_diag(&info, hardware_diag, (int)sizeof(hardware_diag));
    if (hardware_diag[0] != '\0') {
        printf("hardware: %s\n", hardware_diag);
    }
    printf("backend: %s\n", soundctl_backend_name(soundctl_status_backend(&status)));
    printf("mode: 0x%x active=%d pause=%d pending=%d xruns=%d\n",
           status.mode,
           status.active,
           status.pause,
           status._spare[1],
           status._spare[4]);
    printf("params: %u Hz %u-bit %s %u ch round=%u nblks=%u\n",
           info.parameters.rate,
           info.parameters.bits,
           info.parameters.sig ? "signed" : "unsigned",
           info.parameters.pchan,
           info.parameters.round,
           info.parameters.nblks);
    printf("io: written=%u consumed=%u\n",
           (unsigned int)status._spare[2],
           (unsigned int)status._spare[3]);
    input_count = soundctl_input_count(&info);
    feature_flags = soundctl_feature_flags(&info);
    soundctl_print_target("output", MK_AUDIO_MIXER_OUTPUT_LEVEL, MK_AUDIO_MIXER_OUTPUT_MUTE);
    soundctl_print_default_target(&info, "output", MK_AUDIO_MIXER_OUTPUT_DEFAULT);
    if (input_count > 0) {
        soundctl_print_target("input", MK_AUDIO_MIXER_INPUT_LEVEL, MK_AUDIO_MIXER_INPUT_MUTE);
        soundctl_print_default_target(&info, "input", MK_AUDIO_MIXER_INPUT_DEFAULT);
    }
    soundctl_debug("soundctl: status mixer-ok\n");
    if (soundctl_status_backend(&status) == 2) {
        soundctl_debug("soundctl: backend=compat-azalia\n");
    } else if (soundctl_status_backend(&status) == 4) {
        soundctl_debug("soundctl: backend=compat-uaudio\n");
    } else if (soundctl_status_backend(&status) == 1) {
        soundctl_debug("soundctl: backend=compat-ac97\n");
    } else if (soundctl_status_backend(&status) == 3) {
        soundctl_debug("soundctl: backend=pcspkr\n");
    } else {
        soundctl_debug("soundctl: backend=softmix\n");
    }
    soundctl_debug((feature_flags & 0x80u) != 0u ? "soundctl: transport=mmio\n" : "soundctl: transport=io\n");
    soundctl_debug((feature_flags & 0x100u) != 0u ? "soundctl: codecready-quirk=1\n" : "soundctl: codecready-quirk=0\n");
    soundctl_debug((feature_flags & 0x200u) != 0u ? "soundctl: multichannel=1\n" : "soundctl: multichannel=0\n");
    soundctl_debug((feature_flags & 0x400u) != 0u ? "soundctl: capture-dma=1\n" : "soundctl: capture-dma=0\n");
    soundctl_debug((feature_flags & 0x800u) != 0u ? "soundctl: corb-rirb=1\n" : "soundctl: corb-rirb=0\n");
    soundctl_debug((feature_flags & 0x1000u) != 0u ? "soundctl: codec-probe=1\n" : "soundctl: codec-probe=0\n");
    soundctl_debug((feature_flags & 0x2000u) != 0u ? "soundctl: widget-probe=1\n" : "soundctl: widget-probe=0\n");
    soundctl_debug((feature_flags & 0x4000u) != 0u ? "soundctl: path-programmed=1\n" : "soundctl: path-programmed=0\n");
    soundctl_debug((feature_flags & MK_AUDIO_FEATURE_CONTROL_OWNER_AUDIOSVC) != 0u ?
                       "soundctl: control-owner=audiosvc\n" :
                       "soundctl: control-owner=kernel\n");
    soundctl_debug((feature_flags & MK_AUDIO_FEATURE_KERNEL_BACKEND_EXECUTOR) != 0u ?
                       "soundctl: backend-executor=kernel\n" :
                       "soundctl: backend-executor=userland\n");
    soundctl_debug((feature_flags & MK_AUDIO_FEATURE_UI_PROGRESS_DECOUPLED) != 0u ?
                       "soundctl: ui-progress=decoupled\n" :
                       "soundctl: ui-progress=coupled\n");
    soundctl_debug("soundctl: status ok\n");
    soundctl_emit_hardware_debug(&info);
    return 0;
}

static int soundctl_command_tone(const char *duration_text, const char *hz_text) {
    struct audio_swpar params;
    uint8_t buffer[256 * 4];
    unsigned total_frames = 0u;
    unsigned frame = 0u;
    unsigned duration_ms = 200u;
    unsigned hz = 440u;
    unsigned period_frames;

    if (duration_text != 0 && soundctl_parse_uint(duration_text, &duration_ms) != 0) {
        printf("soundctl: duracao invalida\n");
        return 1;
    }
    if (hz_text != 0 && soundctl_parse_uint(hz_text, &hz) != 0) {
        printf("soundctl: frequencia invalida\n");
        return 1;
    }
    if (duration_ms == 0u) {
        duration_ms = 1u;
    }
    if (hz == 0u) {
        hz = 440u;
    }

    memset(&params, 0xff, sizeof(params));
    params.sig = 1u;
    params.le = 1u;
    params.bits = 16u;
    params.bps = 2u;
    params.rate = 44100u;
    params.pchan = 2u;
    params.rchan = 2u;
    params.nblks = 4u;
    params.round = 512u;

    total_frames = (params.rate * duration_ms) / 1000u;
    if (total_frames == 0u) {
        total_frames = 1u;
    }
    period_frames = params.rate / hz;
    if (period_frames < 2u) {
        period_frames = 2u;
    }

    if (vibe_app_audio_set_params(&params) != 0 || vibe_app_audio_start() != 0) {
        printf("soundctl: backend de audio indisponivel\n");
        return 1;
    }

    while (frame < total_frames) {
        unsigned chunk_frames = total_frames - frame;
        unsigned sample_index;
        unsigned bytes_to_write;
        int written;

        if (chunk_frames > 256u) {
            chunk_frames = 256u;
        }
        for (sample_index = 0u; sample_index < chunk_frames; ++sample_index) {
            unsigned phase = (frame + sample_index) % period_frames;
            int sample = phase < (period_frames / 2u) ? 12000 : -12000;
            unsigned offset = sample_index * 4u;

            buffer[offset + 0] = (uint8_t)(sample & 0xff);
            buffer[offset + 1] = (uint8_t)((sample >> 8) & 0xff);
            buffer[offset + 2] = (uint8_t)(sample & 0xff);
            buffer[offset + 3] = (uint8_t)((sample >> 8) & 0xff);
        }

        bytes_to_write = chunk_frames * 4u;
        written = vibe_app_audio_write(buffer, bytes_to_write);
        if (written <= 0) {
            (void)vibe_app_audio_stop();
            printf("soundctl: write falhou\n");
            return 1;
        }
        frame += (unsigned)written / 4u;
        vibe_app_yield();
    }

    (void)vibe_app_audio_stop();
    printf("tone: %u ms @ %u Hz\n", duration_ms, hz);
    return 0;
}

static int soundctl_load_wav_info(const unsigned char *data,
                                  int size,
                                  struct audio_swpar *params_out,
                                  unsigned *data_offset_out,
                                  unsigned *data_size_out) {
    unsigned offset = 12u;
    int found_fmt = 0;
    int found_data = 0;

    if (data == 0 || size < 12 || params_out == 0 || data_offset_out == 0 || data_size_out == 0) {
        return -1;
    }
    if (!soundctl_chunk_id_eq(data, "RIFF") || !soundctl_chunk_id_eq(data + 8, "WAVE")) {
        return -1;
    }

    memset(params_out, 0xff, sizeof(*params_out));
    params_out->sig = 1u;
    params_out->le = 1u;
    params_out->bits = 16u;
    params_out->bps = 2u;
    params_out->pchan = 2u;
    params_out->rchan = 2u;
    params_out->nblks = 4u;
    params_out->round = 512u;

    while ((offset + 8u) <= (unsigned)size) {
        unsigned chunk_size = soundctl_read_u32_le(data + offset + 4u);
        unsigned chunk_data_offset = offset + 8u;
        unsigned next_offset = chunk_data_offset + chunk_size + (chunk_size & 1u);

        if (chunk_data_offset > (unsigned)size || next_offset > (unsigned)size + 1u) {
            return -1;
        }

        if (soundctl_chunk_id_eq(data + offset, "fmt ")) {
            if (chunk_size < 16u || (chunk_data_offset + 16u) > (unsigned)size) {
                return -1;
            }
            if (soundctl_read_u16_le(data + chunk_data_offset) != 1u) {
                return -1;
            }
            params_out->pchan = soundctl_read_u16_le(data + chunk_data_offset + 2u);
            params_out->rchan = params_out->pchan;
            params_out->rate = soundctl_read_u32_le(data + chunk_data_offset + 4u);
            params_out->bps = (unsigned int)(soundctl_read_u16_le(data + chunk_data_offset + 14u) / 8u);
            params_out->bits = params_out->bps * 8u;
            if ((params_out->pchan != 1u && params_out->pchan != 2u) ||
                params_out->bits != 16u ||
                params_out->bps != 2u ||
                (params_out->rate != 11025u &&
                 params_out->rate != 22050u &&
                 params_out->rate != 44100u &&
                 params_out->rate != 48000u)) {
                return -1;
            }
            found_fmt = 1;
        } else if (soundctl_chunk_id_eq(data + offset, "data")) {
            *data_offset_out = chunk_data_offset;
            *data_size_out = chunk_size;
            found_data = 1;
        }

        if (found_fmt && found_data) {
            return 0;
        }
        offset = next_offset;
    }

    return -1;
}

static int soundctl_command_play(const char *path) {
    const char *data = 0;
    int size = 0;
    struct audio_swpar params;
    unsigned data_offset = 0u;
    unsigned data_size = 0u;
    unsigned streamed = 0u;
    unsigned stream_chunk = SOUNDCTL_WAV_STREAM_CHUNK;
    int backend_kind = -1;
    const char *target = path;

    if (target == 0 || *target == '\0') {
        target = "/assets/vibe_os_desktop.wav";
    }
    soundctl_debug("soundctl: play begin\n");

    if (vibe_app_read_file(target, &data, &size) != 0 || data == 0 || size <= 0) {
        soundctl_debug("soundctl: play read-fail\n");
        printf("soundctl: nao conseguiu ler %s\n", target);
        return 1;
    }
    if (soundctl_load_wav_info((const unsigned char *)data, size, &params, &data_offset, &data_size) != 0) {
        soundctl_debug("soundctl: play wav-fail\n");
        printf("soundctl: wav nao suportado em %s\n", target);
        return 1;
    }
    if ((int)(data_offset + data_size) > size) {
        soundctl_debug("soundctl: play trunc-fail\n");
        printf("soundctl: wav truncado em %s\n", target);
        return 1;
    }
    backend_kind = soundctl_current_backend();
    if (backend_kind == 2) {
        stream_chunk = SOUNDCTL_WAV_AZALIA_CHUNK;
    } else if (backend_kind == 4) {
        stream_chunk = SOUNDCTL_WAV_UAUDIO_CHUNK;
    }

    if (vibe_app_audio_set_params(&params) != 0 || vibe_app_audio_start() != 0) {
        soundctl_debug("soundctl: play backend-fail\n");
        printf("soundctl: backend de audio indisponivel\n");
        return 1;
    }

    while (streamed < data_size) {
        unsigned chunk_size = data_size - streamed;
        int written;

        if (chunk_size > stream_chunk) {
            chunk_size = stream_chunk;
        }
        soundctl_debug("soundctl: play loop\n");
        written = vibe_app_audio_write(data + data_offset + streamed, chunk_size);
        if (written <= 0) {
            (void)vibe_app_audio_stop();
            soundctl_debug("soundctl: play write-fail\n");
            printf("soundctl: write falhou em %s\n", target);
            return 1;
        }
        soundctl_debug("soundctl: play write-ok\n");
        streamed += (unsigned)written;
        soundctl_debug("soundctl: play advanced\n");
        if (backend_kind == 2) {
            if (soundctl_wait_for_playback_idle(soundctl_estimated_playback_ticks(&params, (unsigned)written)) != 0) {
                (void)vibe_app_audio_stop();
                soundctl_debug("soundctl: play wait-fail\n");
                printf("soundctl: timeout aguardando backend hda\n");
                return 1;
            }
        } else {
            vibe_app_yield();
        }
    }

    soundctl_debug("soundctl: play stop-begin\n");
    (void)vibe_app_audio_stop();
    soundctl_debug("soundctl: play ok\n");
    printf("play: %s\n", target);
    return 0;
}

static void soundctl_fill_wav_header(unsigned char *header,
                                     unsigned data_size,
                                     unsigned rate,
                                     unsigned channels,
                                     unsigned bits_per_sample) {
    unsigned block_align = channels * (bits_per_sample / 8u);
    unsigned byte_rate = rate * block_align;

    memcpy(header + 0u, "RIFF", 4u);
    soundctl_write_u32_le(header + 4u, 36u + data_size);
    memcpy(header + 8u, "WAVE", 4u);
    memcpy(header + 12u, "fmt ", 4u);
    soundctl_write_u32_le(header + 16u, 16u);
    soundctl_write_u16_le(header + 20u, 1u);
    soundctl_write_u16_le(header + 22u, channels);
    soundctl_write_u32_le(header + 24u, rate);
    soundctl_write_u32_le(header + 28u, byte_rate);
    soundctl_write_u16_le(header + 32u, block_align);
    soundctl_write_u16_le(header + 34u, bits_per_sample);
    memcpy(header + 36u, "data", 4u);
    soundctl_write_u32_le(header + 40u, data_size);
}

static int soundctl_command_record(const char *duration_text, const char *path) {
    struct audio_swpar params;
    unsigned duration_ms = 1000u;
    unsigned total_bytes;
    unsigned captured = 0u;
    unsigned char *wav_data = 0;
    const char *target = path;
    int capture_event_subscription = 0;

    if (duration_text != 0 && soundctl_parse_uint(duration_text, &duration_ms) != 0) {
        printf("soundctl: duracao invalida\n");
        return 1;
    }
    if (duration_ms == 0u) {
        duration_ms = 1u;
    }
    if (target == 0 || *target == '\0') {
        target = "/capture.wav";
    }
    soundctl_debug("soundctl: record begin\n");

    memset(&params, 0xff, sizeof(params));
    params.sig = 1u;
    params.le = 1u;
    params.bits = 16u;
    params.bps = 2u;
    params.rate = 44100u;
    params.pchan = 2u;
    params.rchan = 2u;
    params.nblks = 4u;
    params.round = 512u;

    total_bytes = ((params.rate * duration_ms) / 1000u) * params.rchan * params.bps;
    if (total_bytes == 0u) {
        total_bytes = params.round;
    }

    wav_data = (unsigned char *)malloc(44u + total_bytes);
    if (wav_data == 0) {
        soundctl_debug("soundctl: record nomem\n");
        printf("soundctl: sem memoria para gravacao\n");
        return 1;
    }
    memset(wav_data, 0, 44u + total_bytes);

    if (vibe_app_audio_set_params(&params) != 0 || vibe_app_audio_start() != 0) {
        free(wav_data);
        soundctl_debug("soundctl: record backend-fail\n");
        printf("soundctl: backend de audio indisponivel\n");
        return 1;
    }
    soundctl_emit_record_capture_debug();
    if (vibe_app_audio_event_subscribe() == 0) {
        capture_event_subscription = 1;
        soundctl_drain_capture_events(capture_event_subscription);
    }

    while (captured < total_bytes) {
        unsigned chunk_size = total_bytes - captured;
        int got;

        if (chunk_size > 252u) {
            chunk_size = 252u;
        }
        got = vibe_app_audio_read(wav_data + 44u + captured, chunk_size);
        if (got < 0) {
            (void)vibe_app_audio_stop();
            free(wav_data);
            soundctl_debug("soundctl: record read-fail\n");
            printf("soundctl: read falhou\n");
            return 1;
        }
        if (got == 0) {
            if (soundctl_wait_for_capture_ready(capture_event_subscription) != 0) {
                vibe_app_yield();
            }
            soundctl_drain_capture_events(capture_event_subscription);
            continue;
        }
        captured += (unsigned)got;
        soundctl_drain_capture_events(capture_event_subscription);
    }

    (void)vibe_app_audio_stop();
    soundctl_fill_wav_header(wav_data, captured, params.rate, params.rchan, params.bits);
    if (vibe_app_write_file(target, wav_data, (int)(44u + captured)) != 0) {
        free(wav_data);
        soundctl_debug("soundctl: record write-fail\n");
        printf("soundctl: nao conseguiu gravar %s\n", target);
        return 1;
    }
    if (vibe_app_sync() != 0) {
        free(wav_data);
        soundctl_debug("soundctl: record sync-fail\n");
        puts("soundctl: nao conseguiu sincronizar capture.wav");
        return 1;
    }

    free(wav_data);
    soundctl_debug("soundctl: record ok\n");
    soundctl_write_record_summary(target, duration_ms, captured);
    return 0;
}

static int soundctl_command_set(const char *target, const char *value_text) {
    int level_id = 0;
    int mute_id = 0;
    int value = 0;

    if (soundctl_target_ids(target, &level_id, &mute_id) != 0) {
        printf("soundctl: invalid target '%s'\n", target ? target : "");
        return 1;
    }

    if (soundctl_parse_percent(value_text, &value) != 0) {
        printf("soundctl: volume must be between 0 and 100\n");
        return 1;
    }

    if (soundctl_write_level_percent(level_id, value) != 0) {
        printf("soundctl: failed to update %s volume\n", target);
        return 1;
    }
    if (soundctl_write_mute(mute_id, value == 0) != 0) {
        printf("soundctl: failed to update %s mute state\n", target);
        return 1;
    }

    soundctl_print_target(target, level_id, mute_id);
    return 0;
}

static int soundctl_command_mute(const char *target, int muted) {
    int level_id = 0;
    int mute_id = 0;

    if (soundctl_target_ids(target, &level_id, &mute_id) != 0) {
        printf("soundctl: invalid target '%s'\n", target ? target : "");
        return 1;
    }

    if (soundctl_write_mute(mute_id, muted) != 0) {
        printf("soundctl: failed to update %s mute state\n", target);
        return 1;
    }

    soundctl_print_target(target, level_id, mute_id);
    return 0;
}

static int soundctl_command_default(const char *target, const char *value_text) {
    struct mk_audio_info info;
    int control_id = soundctl_default_control_id(target);
    int value;

    if (control_id < 0) {
        printf("soundctl: invalid default target '%s'\n", target ? target : "");
        return 1;
    }

    if (vibe_app_audio_get_info(&info) != 0) {
        printf("soundctl: audio service unavailable\n");
        return 1;
    }

    value = soundctl_default_choice_value(&info, control_id, value_text);
    if (value < 0) {
        printf("soundctl: invalid default value '%s'\n", value_text ? value_text : "");
        return 1;
    }

    if (soundctl_write_enum(control_id, value) != 0) {
        printf("soundctl: failed to update default %s\n", target);
        return 1;
    }

    soundctl_print_default_target(&info, target, control_id);
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    soundctl_debug("soundctl: main begin\n");
    if (argc < 2) {
        soundctl_debug("soundctl: main usage\n");
        soundctl_usage();
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        return soundctl_command_list();
    }
    if (strcmp(argv[1], "status") == 0) {
        return soundctl_command_status();
    }
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            soundctl_usage();
            return 1;
        }
        return soundctl_command_set(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "mute") == 0) {
        if (argc < 3) {
            soundctl_usage();
            return 1;
        }
        return soundctl_command_mute(argv[2], 1);
    }
    if (strcmp(argv[1], "unmute") == 0) {
        if (argc < 3) {
            soundctl_usage();
            return 1;
        }
        return soundctl_command_mute(argv[2], 0);
    }
    if (strcmp(argv[1], "default") == 0) {
        if (argc < 4) {
            soundctl_usage();
            return 1;
        }
        return soundctl_command_default(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "tone") == 0) {
        return soundctl_command_tone(argc >= 3 ? argv[2] : 0,
                                     argc >= 4 ? argv[3] : 0);
    }
    if (strcmp(argv[1], "play") == 0) {
        return soundctl_command_play(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "record") == 0) {
        return soundctl_command_record(argc >= 3 ? argv[2] : 0,
                                       argc >= 4 ? argv[3] : 0);
    }

    soundctl_usage();
    return 1;
}
