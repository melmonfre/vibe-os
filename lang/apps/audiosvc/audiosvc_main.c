#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>

#define AUDIOSVC_STATUS_EXPORT_PATH "/runtime/audiosvc-status.txt"
#define MK_AUDIO_STATUS_BACKEND_MASK 0x000000ffu
#define MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED 0x00000100u
#define MK_AUDIO_STATUS_FLAG_IRQ_SEEN 0x00000200u
#define MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ 0x00000400u
#define MK_AUDIO_STATUS_FLAG_STARVATION 0x00000800u
#define MK_AUDIO_STATUS_FLAG_UNDERRUN 0x00001000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_DATA 0x00002000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN 0x00004000u

static void audiosvc_debug(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

static const char *audiosvc_backend_name(int backend_kind) {
    switch (backend_kind) {
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

static int audiosvc_status_backend(const struct audio_status *status) {
    if (status == 0) {
        return 0;
    }
    return status->_spare[0] & (int)MK_AUDIO_STATUS_BACKEND_MASK;
}

static unsigned audiosvc_status_flags(const struct audio_status *status) {
    if (status == 0) {
        return 0u;
    }
    return (unsigned)status->_spare[0] & ~MK_AUDIO_STATUS_BACKEND_MASK;
}

static int audiosvc_output_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 1;
    }
    if ((int)info->parameters._spare[1] > 0) {
        return (int)info->parameters._spare[1];
    }
    return 1;
}

static int audiosvc_input_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    if ((int)info->parameters._spare[2] >= 0) {
        return (int)info->parameters._spare[2];
    }
    return 0;
}

static unsigned audiosvc_output_mask(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0x1u;
    }
    return info->parameters._spare[3] != 0u ? info->parameters._spare[3] : 0x1u;
}

static unsigned audiosvc_input_mask(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[4];
}

static const char *audiosvc_output_name_from_bit(int bit) {
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

static const char *audiosvc_hda_output_name_from_bit(int bit) {
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

static unsigned audiosvc_feature_flags(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[5];
}

static void audiosvc_append_char(char *buf, int max_len, int *len, char ch) {
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

static void audiosvc_append_text(char *buf, int max_len, int *len, const char *text) {
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

static void audiosvc_append_hex(char *buf, int max_len, int *len, unsigned value, int digits) {
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        unsigned nibble = (value >> (unsigned)shift) & 0xfu;
        audiosvc_append_char(buf, max_len, len, (char)(nibble < 10u ? ('0' + nibble) : ('a' + (nibble - 10u))));
    }
}

static void audiosvc_format_hardware_diag(const struct mk_audio_info *info, char *buf, int max_len) {
    if (buf == 0 || max_len <= 0) {
        return;
    }
    buf[0] = '\0';
    if (info == 0 || info->controller_pci_id == 0u) {
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

static void audiosvc_emit_hardware_debug(const struct mk_audio_info *info) {
    char line[96];
    int len = 0;

    if (info == 0) {
        return;
    }
    line[0] = '\0';
    audiosvc_append_text(line, (int)sizeof(line), &len, "audiosvc: pci=");
    audiosvc_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->controller_pci_id >> 16) & 0xffffu), 4);
    audiosvc_append_char(line, (int)sizeof(line), &len, ':');
    audiosvc_append_hex(line, (int)sizeof(line), &len, (unsigned)(info->controller_pci_id & 0xffffu), 4);
    audiosvc_append_char(line, (int)sizeof(line), &len, '\n');
    audiosvc_debug(line);
    len = 0;
    line[0] = '\0';
    audiosvc_append_text(line, (int)sizeof(line), &len, "audiosvc: codec=");
    audiosvc_append_hex(line, (int)sizeof(line), &len, (unsigned)info->codec_vendor_id, 8);
    audiosvc_append_char(line, (int)sizeof(line), &len, '\n');
    audiosvc_debug(line);
    len = 0;
    line[0] = '\0';
    audiosvc_append_text(line, (int)sizeof(line), &len, "audiosvc: route=");
    audiosvc_append_hex(line, (int)sizeof(line), &len, (unsigned)((info->output_route >> 8) & 0xffu), 2);
    audiosvc_append_char(line, (int)sizeof(line), &len, '/');
    audiosvc_append_hex(line, (int)sizeof(line), &len, (unsigned)(info->output_route & 0xffu), 2);
    audiosvc_append_char(line, (int)sizeof(line), &len, '\n');
    audiosvc_debug(line);
}

static const char *audiosvc_device_hint(const audio_device_t *device) {
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
    return "";
}

static const char *audiosvc_output_name(const struct mk_audio_info *info, int index) {
    unsigned mask = audiosvc_output_mask(info);
    int backend = info != 0 ? audiosvc_status_backend(&info->status) : 0;
    int seen = 0;

    for (int bit = 0; bit < 4; ++bit) {
        if ((mask & (1u << bit)) == 0u) {
            continue;
        }
        if (seen == index) {
            return backend == 2 ? audiosvc_hda_output_name_from_bit(bit)
                                : audiosvc_output_name_from_bit(bit);
        }
        ++seen;
    }
    return backend == 2 ? audiosvc_hda_output_name_from_bit(0)
                        : audiosvc_output_name_from_bit(0);
}

static const char *audiosvc_input_name(const struct mk_audio_info *info, int index) {
    int input_count = audiosvc_input_count(info);
    int backend = info != 0 ? audiosvc_status_backend(&info->status) : 0;

    if (index == 1 && input_count > 1) {
        return backend == 2 ? "line-in" : "line";
    }
    return "mic";
}

static int audiosvc_read_level_percent(int control_id, int *percent_out) {
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

static int audiosvc_read_enum(int control_id, int *value_out) {
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

static int audiosvc_read_state(struct mk_audio_info *info,
                               struct audio_status *status,
                               int *output_volume,
                               int *input_volume,
                               int *output_muted,
                               int *input_muted,
                               int *selected_output,
                               int *selected_input) {
    if (info == 0 || status == 0 || output_volume == 0 || input_volume == 0 ||
        output_muted == 0 || input_muted == 0 || selected_output == 0 || selected_input == 0) {
        return -1;
    }

    if (vibe_app_audio_get_info(info) != 0 || vibe_app_audio_get_status(status) != 0) {
        return -1;
    }
    if (audiosvc_read_level_percent(MK_AUDIO_MIXER_OUTPUT_LEVEL, output_volume) != 0 ||
        audiosvc_read_level_percent(MK_AUDIO_MIXER_INPUT_LEVEL, input_volume) != 0 ||
        audiosvc_read_enum(MK_AUDIO_MIXER_OUTPUT_MUTE, output_muted) != 0 ||
        audiosvc_read_enum(MK_AUDIO_MIXER_INPUT_MUTE, input_muted) != 0 ||
        audiosvc_read_enum(MK_AUDIO_MIXER_OUTPUT_DEFAULT, selected_output) != 0 ||
        audiosvc_read_enum(MK_AUDIO_MIXER_INPUT_DEFAULT, selected_input) != 0) {
        return -1;
    }

    if (*selected_output < 0 || *selected_output > 1) {
        *selected_output = 0;
    }
    if (*selected_input < 0 || *selected_input > 1) {
        *selected_input = 0;
    }
    return 0;
}

static int audiosvc_write_state_file(const char *path) {
    struct mk_audio_info info;
    struct audio_status status;
    char text[768];
    int output_volume = 0;
    int input_volume = 0;
    int output_muted = 0;
    int input_muted = 0;
    int selected_output = 0;
    int selected_input = 0;
    int output_count;
    int input_count;
    unsigned output_mask;
    unsigned input_mask;
    unsigned feature_flags;
    unsigned status_flags;
    char hardware_diag[160];
    const char *target = path != 0 && *path != '\0' ? path : AUDIOSVC_STATUS_EXPORT_PATH;

    if (audiosvc_read_state(&info,
                            &status,
                            &output_volume,
                            &input_volume,
                            &output_muted,
                            &input_muted,
                            &selected_output,
                            &selected_input) != 0) {
        return -1;
    }
    output_count = audiosvc_output_count(&info);
    input_count = audiosvc_input_count(&info);
    output_mask = audiosvc_output_mask(&info);
    input_mask = audiosvc_input_mask(&info);
    feature_flags = audiosvc_feature_flags(&info);
    status_flags = audiosvc_status_flags(&status);
    audiosvc_format_hardware_diag(&info, hardware_diag, (int)sizeof(hardware_diag));
    if (selected_output >= output_count) {
        selected_output = 0;
    }
    if (selected_input >= input_count && input_count > 0) {
        selected_input = 0;
    }

    (void)vibe_app_create_dir("/runtime");
    snprintf(text,
             sizeof(text),
             "device=%s\nversion=%s\nconfig=%s\nbackend=%s\nactive=%d\npending=%d\nxruns=%d\n"
             "output_count=%d\noutput0=%s\noutput1=%s\noutput2=%s\noutput3=%s\n"
             "input_count=%d\ninput0=%s\ninput1=%s\n"
             "output_mask=%u\ninput_mask=%u\nfeature_flags=%u\nstatus_flags=%u\ntransport=%s\ncodecready_quirk=%d\nmultichannel=%d\ncapture_dma=%d\ncorb_rirb=%d\ncodec_probe=%d\nwidget_probe=%d\npath_programmed=%d\ncapture_data=%d\ncapture_xrun=%d\nirq_count=%u\n"
             "hardware=%s\npci_id=%08x\npci_location=%06x\ncodec_id=%08x\noutput_route=%04x\n"
             "default_output=%s\ndefault_input=%s\n"
             "output_volume=%d\ninput_volume=%d\noutput_muted=%d\ninput_muted=%d\n",
             info.device.name,
             info.device.version,
             info.device.config,
             audiosvc_backend_name(audiosvc_status_backend(&status)),
             status.active,
             status._spare[1],
             status._spare[4],
             output_count,
             audiosvc_output_name(&info, 0),
             output_count > 1 ? audiosvc_output_name(&info, 1) : "",
             output_count > 2 ? audiosvc_output_name(&info, 2) : "",
             output_count > 3 ? audiosvc_output_name(&info, 3) : "",
             input_count,
             input_count > 0 ? audiosvc_input_name(&info, 0) : "",
             input_count > 1 ? audiosvc_input_name(&info, 1) : "",
             output_mask,
             input_mask,
             feature_flags,
             status_flags,
             (feature_flags & 0x80u) != 0u ? "mmio" : "io",
             (feature_flags & 0x100u) != 0u ? 1 : 0,
             (feature_flags & 0x200u) != 0u ? 1 : 0,
             (feature_flags & 0x400u) != 0u ? 1 : 0,
             (feature_flags & 0x800u) != 0u ? 1 : 0,
             (feature_flags & 0x1000u) != 0u ? 1 : 0,
             (feature_flags & 0x2000u) != 0u ? 1 : 0,
             (feature_flags & 0x4000u) != 0u ? 1 : 0,
             (status_flags & MK_AUDIO_STATUS_FLAG_CAPTURE_DATA) != 0u ? 1 : 0,
             (status_flags & MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN) != 0u ? 1 : 0,
             info.parameters._spare[0],
             hardware_diag,
             info.controller_pci_id,
             info.controller_location,
             info.codec_vendor_id,
             info.output_route,
             audiosvc_output_name(&info, selected_output),
             input_count > 0 ? audiosvc_input_name(&info, selected_input) : "none",
             output_volume,
             input_volume,
             output_muted != 0 ? 1 : 0,
             input_muted != 0 ? 1 : 0);
    return vibe_app_write_file(target, text, (int)strlen(text));
}

static void audiosvc_usage(void) {
    printf("usage: audiosvc <command> [args]\n");
    printf("commands:\n");
    printf("  status\n");
    printf("  export-state [path]\n");
}

static int audiosvc_command_status(void) {
    struct mk_audio_info info;
    struct audio_status status;
    int output_volume = 0;
    int input_volume = 0;
    int output_muted = 0;
    int input_muted = 0;
    int selected_output = 0;
    int selected_input = 0;
    int output_count = 0;
    int input_count = 0;
    unsigned feature_flags = 0u;
    unsigned status_flags = 0u;
    char hardware_diag[160];

    if (audiosvc_read_state(&info,
                            &status,
                            &output_volume,
                            &input_volume,
                            &output_muted,
                            &input_muted,
                            &selected_output,
                            &selected_input) != 0) {
        printf("audiosvc: audio service unavailable\n");
        return 1;
    }
    output_count = audiosvc_output_count(&info);
    input_count = audiosvc_input_count(&info);
    feature_flags = audiosvc_feature_flags(&info);
    status_flags = audiosvc_status_flags(&status);
    audiosvc_format_hardware_diag(&info, hardware_diag, (int)sizeof(hardware_diag));
    if (selected_output >= output_count) {
        selected_output = 0;
    }
    if (selected_input >= input_count && input_count > 0) {
        selected_input = 0;
    }

    printf("device: %s %s (%s)\n", info.device.name, info.device.version, info.device.config);
    if (audiosvc_device_hint(&info.device)[0] != '\0') {
        printf("hint: %s\n", audiosvc_device_hint(&info.device));
    }
    if (hardware_diag[0] != '\0') {
        printf("hardware: %s\n", hardware_diag);
    }
    printf("backend: %s\n", audiosvc_backend_name(audiosvc_status_backend(&status)));
    printf("outputs: %s", audiosvc_output_name(&info, 0));
    for (int i = 1; i < output_count; ++i) {
        printf(", %s", audiosvc_output_name(&info, i));
    }
    printf("\n");
    if (input_count > 0) {
        printf("inputs: %s", audiosvc_input_name(&info, 0));
        if (input_count > 1) {
            printf(", %s", audiosvc_input_name(&info, 1));
        }
        printf("\n");
    } else {
        printf("inputs: none\n");
    }
    printf("default output: %s\n", audiosvc_output_name(&info, selected_output));
    printf("default input: %s\n", input_count > 0 ? audiosvc_input_name(&info, selected_input) : "none");
    printf("output volume: %d%% %s\n", output_volume, output_muted ? "(muted)" : "(active)");
    printf("input volume: %d%% %s\n", input_volume, input_muted ? "(muted)" : "(active)");
    printf("features: %sirq %scapture %s %scodecready-quirk %smultichannel %scapture-dma %scorb-rirb %scodec-probe %swidget-probe %spath-programmed irq-count=%u\n",
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
           info.parameters._spare[0]);
    printf("runtime: %sirq-seen %sno-valid-irq %sstarvation %sunderrun %scapture-data %scapture-xrun\n",
           (status_flags & MK_AUDIO_STATUS_FLAG_IRQ_SEEN) != 0u ? "" : "no-",
           (status_flags & MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ) != 0u ? "" : "no-",
           (status_flags & MK_AUDIO_STATUS_FLAG_STARVATION) != 0u ? "" : "no-",
           (status_flags & MK_AUDIO_STATUS_FLAG_UNDERRUN) != 0u ? "" : "no-",
           (status_flags & MK_AUDIO_STATUS_FLAG_CAPTURE_DATA) != 0u ? "" : "no-",
           (status_flags & MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN) != 0u ? "" : "no-");
    if (audiosvc_status_backend(&status) == 2) {
        audiosvc_debug("audiosvc: backend=compat-azalia\n");
    } else if (audiosvc_status_backend(&status) == 1) {
        audiosvc_debug("audiosvc: backend=compat-ac97\n");
    } else {
        audiosvc_debug("audiosvc: backend=softmix\n");
    }
    audiosvc_debug("audiosvc: outputs listed\n");
    if (input_count > 1) {
        audiosvc_debug("audiosvc: inputs=mic,line\n");
    } else if (input_count == 1) {
        audiosvc_debug("audiosvc: inputs=mic\n");
    } else {
        audiosvc_debug("audiosvc: inputs=none\n");
    }
    audiosvc_debug("audiosvc: default-output selected\n");
    if (selected_input == 1 && input_count > 1) {
        audiosvc_debug("audiosvc: default-input=line\n");
    } else if (input_count > 0) {
        audiosvc_debug("audiosvc: default-input=mic\n");
    } else {
        audiosvc_debug("audiosvc: default-input=none\n");
    }
    if ((feature_flags & 0x80u) != 0u) {
        audiosvc_debug("audiosvc: transport=mmio\n");
    } else {
        audiosvc_debug("audiosvc: transport=io\n");
    }
    if ((feature_flags & 0x100u) != 0u) {
        audiosvc_debug("audiosvc: codecready-quirk=1\n");
    } else {
        audiosvc_debug("audiosvc: codecready-quirk=0\n");
    }
    if ((feature_flags & 0x200u) != 0u) {
        audiosvc_debug("audiosvc: multichannel=1\n");
    } else {
        audiosvc_debug("audiosvc: multichannel=0\n");
    }
    if ((feature_flags & 0x400u) != 0u) {
        audiosvc_debug("audiosvc: capture-dma=1\n");
    } else {
        audiosvc_debug("audiosvc: capture-dma=0\n");
    }
    if ((feature_flags & 0x800u) != 0u) {
        audiosvc_debug("audiosvc: corb-rirb=1\n");
    } else {
        audiosvc_debug("audiosvc: corb-rirb=0\n");
    }
    if ((feature_flags & 0x1000u) != 0u) {
        audiosvc_debug("audiosvc: codec-probe=1\n");
    } else {
        audiosvc_debug("audiosvc: codec-probe=0\n");
    }
    if ((feature_flags & 0x2000u) != 0u) {
        audiosvc_debug("audiosvc: widget-probe=1\n");
    } else {
        audiosvc_debug("audiosvc: widget-probe=0\n");
    }
    if ((feature_flags & 0x4000u) != 0u) {
        audiosvc_debug("audiosvc: path-programmed=1\n");
    } else {
        audiosvc_debug("audiosvc: path-programmed=0\n");
    }
    audiosvc_debug("audiosvc: status ok\n");
    audiosvc_emit_hardware_debug(&info);
    return 0;
}

static int audiosvc_command_export_state(const char *path) {
    const char *target = path != 0 && *path != '\0' ? path : AUDIOSVC_STATUS_EXPORT_PATH;

    if (audiosvc_write_state_file(target) != 0) {
        audiosvc_debug("audiosvc: export failed\n");
        printf("audiosvc: failed to write %s\n", target);
        return 1;
    }
    audiosvc_debug("audiosvc: export ok\n");
    printf("audiosvc: state exported to %s\n", target);
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 2 || argv == 0 || argv[1] == 0) {
        audiosvc_usage();
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        return audiosvc_command_status();
    }
    if (strcmp(argv[1], "export-state") == 0) {
        return audiosvc_command_export_state(argc > 2 ? argv[2] : 0);
    }

    audiosvc_usage();
    return 1;
}
