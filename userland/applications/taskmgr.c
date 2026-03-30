#include <userland/applications/include/taskmgr.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TASKMGR_WINDOW = {30, 30, 580, 360};
static const int TASKMGR_ROW_HEIGHT = 18;
static const int TASKMGR_REFRESH_TICKS = 25;
static const uint32_t TASKMGR_VIDEO_REFRESH_TICKS = 250u;
static const uint32_t TASKMGR_SERVICE_REFRESH_TICKS = 250u;
static const uint32_t TASKMGR_NETMGRD_REFRESH_TICKS = 250u;
static const char *TASKMGR_NETMGRD_STATUS_PATH = "/runtime/netmgrd-status.txt";

static const char *taskmgr_video_backend_label(uint32_t backend_kind) {
    switch (backend_kind) {
    case VIDEO_BACKEND_LEGACY_LFB: return "legacy_lfb";
    case VIDEO_BACKEND_FAST_LFB: return "fast_lfb";
    default: return "none";
    }
}

static const char *taskmgr_video_native_label(uint32_t native_backend_kind) {
    switch (native_backend_kind) {
    case VIDEO_NATIVE_BACKEND_I915: return "Intel i915";
    case VIDEO_NATIVE_BACKEND_RADEON: return "AMD Radeon";
    case VIDEO_NATIVE_BACKEND_NOUVEAU: return "NVIDIA nouveau";
    case VIDEO_NATIVE_BACKEND_BGA: return "Bochs/BGA";
    case VIDEO_NATIVE_BACKEND_UNKNOWN: return "Nativo desconhecido";
    case VIDEO_NATIVE_BACKEND_NONE:
    default: return "Fallback BIOS/VBE";
    }
}

static const char *taskmgr_video_display_label(const struct video_bench_info *bench) {
    if (bench == 0) {
        return "indisponivel";
    }
    if (bench->native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
        return taskmgr_video_native_label(bench->native_backend_kind);
    }
    if (bench->detected_native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
        return taskmgr_video_native_label(bench->detected_native_backend_kind);
    }
    return "Fallback BIOS/VBE";
}

static const char *taskmgr_video_present_label(uint32_t present_copy_kind) {
    switch (present_copy_kind) {
    case VIDEO_PRESENT_COPY_MOVNTDQ: return "MOVNTDQ";
    case VIDEO_PRESENT_COPY_REP_MOVSD: return "rep movsd";
    case VIDEO_PRESENT_COPY_BYTE_LOOP:
    default: return "byte loop";
    }
}

static const char *taskmgr_audio_display_label(const struct mk_audio_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if (info->device.name[0] != '\0') {
        return info->device.name;
    }
    return "desconhecido";
}

static int taskmgr_audio_usb_attach_ready(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (info->parameters._spare[5] & 0x20000u) != 0u;
}

static int taskmgr_audio_usb_attached_ready(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (info->parameters._spare[5] & 0x40000u) != 0u;
}

static int taskmgr_audio_output_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (int)info->parameters._spare[1];
}

static int taskmgr_audio_input_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (int)info->parameters._spare[2];
}

static unsigned taskmgr_audio_feature_flags(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[5];
}

static unsigned taskmgr_audio_irq_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[0];
}

static void taskmgr_append_hex_fixed(char *buf, unsigned value, unsigned digits, int max_len) {
    static const char hex[] = "0123456789abcdef";
    char text[9];

    if (digits < 1u) {
        digits = 1u;
    }
    if (digits > 8u) {
        digits = 8u;
    }
    for (unsigned i = 0u; i < digits; ++i) {
        unsigned shift = (digits - 1u - i) * 4u;
        text[i] = hex[(value >> shift) & 0xfu];
    }
    text[digits] = '\0';
    str_append(buf, text, max_len);
}

static const char *taskmgr_audio_config_hint(const audio_device_t *device) {
    const char *config;

    if (device == 0) {
        return "";
    }
    config = device->config;
    if (strcmp(config, "hda-no-output-stream") == 0) {
        return "sem output stream HDA";
    }
    if (strcmp(config, "hda-reset-failed") == 0) {
        return "reset HDA falhou";
    }
    if (strcmp(config, "hda-bar-unavailable") == 0) {
        return "BAR MMIO HDA ausente";
    }
    if (strcmp(config, "no-usable-hw-backend") == 0) {
        return "hw ok, backend nao subiu";
    }
    if (strcmp(config, "no-pci-audio") == 0) {
        return "sem controlador PCI";
    }
    if (strcmp(config, "bar-unavailable") == 0) {
        return "BAR AC97 ausente";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attached") == 0) {
        return "USB Audio Class com attach real";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attach-ready") == 0) {
        return "USB Audio Class anexavel";
    }
    return "";
}

static const char *taskmgr_network_display_label(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_QUERY_ONLY) != 0u) {
        return "query-only";
    }
    if ((info->flags & MK_NETWORK_CAPS_LOOPBACK_READY) != 0u) {
        return "loopback";
    }
    return "ativo";
}

static const char *taskmgr_network_link_label(const struct mk_network_status *status) {
    if (status == 0) {
        return "indisponivel";
    }

    switch (status->link_state) {
    case MK_NETWORK_LINK_CONNECTED:
        if (status->active_kind == MK_NETWORK_IF_WIFI) {
            return "Wi-Fi conectado";
        }
        if (status->active_kind == MK_NETWORK_IF_ETHERNET) {
            return "Ethernet conectado";
        }
        return "Conectado";
    case MK_NETWORK_LINK_CONNECTING:
        return "Conectando";
    default:
        return "Desconectado";
    }
}

static const char *taskmgr_priority_label(uint32_t tier) {
    switch (tier) {
    case 0: return "desktop";
    case 1: return "input";
    case 2: return "video";
    case 3: return "storage";
    case 4: return "audio";
    case 5: return "network";
    case 6: return "apps";
    default: return "background";
    }
}

static const char *taskmgr_service_name(uint32_t service_type) {
    switch (service_type) {
    case 1u: return "init";
    case 2u: return "storage";
    case 3u: return "filesystem";
    case 4u: return "video";
    case 5u: return "input";
    case 6u: return "console";
    case 7u: return "network";
    case 8u: return "audio";
    default: return "unknown";
    }
}

static const char *taskmgr_service_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_SERVICE_EVENT_ONLINE: return "online";
    case MK_SERVICE_EVENT_OFFLINE: return "offline";
    case MK_SERVICE_EVENT_DEGRADED: return "degraded";
    case MK_SERVICE_EVENT_RECOVERED: return "recovered";
    case MK_SERVICE_EVENT_RESTARTED: return "restarted";
    default: return "unknown";
    }
}

static const char *taskmgr_audio_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_AUDIO_EVENT_QUEUED: return "queued";
    case MK_AUDIO_EVENT_IDLE: return "idle";
    case MK_AUDIO_EVENT_UNDERRUN: return "underrun";
    case MK_AUDIO_EVENT_OVERFLOW: return "overflow";
    default: return "unknown";
    }
}

static const char *taskmgr_video_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_VIDEO_EVENT_PRESENT: return "present";
    case MK_VIDEO_EVENT_PRESENT_SUBMITTED: return "submit";
    case MK_VIDEO_EVENT_MODE_SET: return "mode";
    case MK_VIDEO_EVENT_MODE_SET_BEGIN: return "mode-begin";
    case MK_VIDEO_EVENT_MODE_SET_DONE: return "mode-done";
    case MK_VIDEO_EVENT_LEAVE: return "leave";
    case MK_VIDEO_EVENT_OVERFLOW: return "overflow";
    case MK_VIDEO_EVENT_BACKEND_FAILED: return "backend-fail";
    case MK_VIDEO_EVENT_BACKEND_RECOVERED: return "backend-ok";
    default: return "unknown";
    }
}

static const char *taskmgr_network_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_NETWORK_EVENT_STATUS: return "status";
    case MK_NETWORK_EVENT_SOCKET_RECV: return "recv";
    case MK_NETWORK_EVENT_SOCKET_ACCEPT: return "accept";
    case MK_NETWORK_EVENT_SOCKET_SEND: return "send";
    case MK_NETWORK_EVENT_SOCKET_CLOSED: return "closed";
    case MK_NETWORK_EVENT_BACKEND_RX: return "backend-rx";
    case MK_NETWORK_EVENT_BACKEND_TX: return "backend-tx";
    case MK_NETWORK_EVENT_OVERFLOW: return "overflow";
    default: return "unknown";
    }
}

static const char *taskmgr_service_health_label(const struct task_snapshot_entry *entry) {
    if (entry == 0 || entry->service_type == 0u) {
        return "n/a";
    }
    if ((entry->flags & TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED) != 0u) {
        return "degradado";
    }
    if ((entry->flags & TASK_SNAPSHOT_FLAG_SERVICE_ONLINE) != 0u) {
        return "online";
    }
    return "offline";
}

static int taskmgr_parse_int(const char *text) {
    int value = 0;

    if (text == 0) {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        value = (value * 10) + (*text - '0');
        ++text;
    }
    return value;
}

static void taskmgr_set_netmgrd_text(char *dst, int dst_size, const char *value) {
    if (dst == 0 || dst_size <= 0) {
        return;
    }
    if (value == 0 || value[0] == '\0' || (value[0] == '-' && value[1] == '\0')) {
        dst[0] = '\0';
        return;
    }
    str_copy_limited(dst, value, dst_size);
}

static void append_uint(char *buf, unsigned v, int max_len) {
    char tmp[12];
    int pos = 0;

    if (max_len <= 1) {
        return;
    }
    if (v == 0u) {
        tmp[pos++] = '0';
    } else {
        while (v > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        for (int i = 0; i < pos / 2; ++i) {
            char c = tmp[i];
            tmp[i] = tmp[pos - 1 - i];
            tmp[pos - 1 - i] = c;
        }
    }
    tmp[pos] = '\0';
    str_append(buf, tmp, max_len);
}

static void append_int(char *buf, int v, int max_len) {
    if (v < 0) {
        str_append(buf, "-", max_len);
        append_uint(buf, (unsigned)(-v), max_len);
    } else {
        append_uint(buf, (unsigned)v, max_len);
    }
}

static void append_hex_fixed(char *buf, unsigned value, int digits, int max_len) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[9];

    if (digits <= 0) {
        return;
    }
    if (digits > 8) {
        digits = 8;
    }
    for (int i = 0; i < digits; ++i) {
        int shift = (digits - 1 - i) * 4;
        tmp[i] = hex[(value >> shift) & 0xFu];
    }
    tmp[digits] = '\0';
    str_append(buf, tmp, max_len);
}

static void taskmgr_append_video_pci_label(char *buf,
                                           int max_len,
                                           const struct video_bench_info *bench) {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t revision;

    if (buf == 0 || bench == 0) {
        return;
    }

    vendor_id = bench->gpu_vendor_id;
    device_id = bench->gpu_device_id;
    revision = bench->gpu_revision;
    if (vendor_id == 0u && device_id == 0u) {
        vendor_id = bench->detected_gpu_vendor_id;
        device_id = bench->detected_gpu_device_id;
        revision = bench->detected_gpu_revision;
    }
    if (vendor_id == 0u && device_id == 0u) {
        str_append(buf, "PCI n/d", max_len);
        return;
    }

    str_append(buf, "PCI 0x", max_len);
    append_hex_fixed(buf, vendor_id, 4, max_len);
    str_append(buf, ":", max_len);
    append_hex_fixed(buf, device_id, 4, max_len);
    str_append(buf, " rev 0x", max_len);
    append_hex_fixed(buf, revision, 2, max_len);
}

static const char *taskmgr_window_label(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Gerenciador";
    case APP_CALCULATOR: return "Calculadora";
    case APP_SKETCHPAD: return "Sketchpad";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_PACMAN: return "Pacman";
    case APP_SPACE_INVADERS: return "Invaders";
    case APP_PONG: return "Pong";
    case APP_DONKEY_KONG: return "Donkey Kong";
    case APP_BRICK_RACE: return "Brick Race";
    case APP_FLAP_BIRB: return "Flap Birb";
    case APP_DOOM: return "DOOM";
    case APP_CRAFT: return "Craft";
    case APP_IMAGEVIEWER: return "Imagens";
    case APP_AUDIO_PLAYER: return "Audio Player";
    case APP_PERSONALIZE: return "Personalizar";
    case APP_TRASH: return "Lixeira";
    default: return "Aplicacao";
    }
}

static const char *taskmgr_kind_label(uint32_t kind) {
    switch (kind) {
    case 0u: return "Usuario";
    case 1u: return "Servico";
    case 2u: return "Kernel";
    default: return "Outro";
    }
}

static const char *taskmgr_state_label(uint32_t state) {
    switch (state) {
    case 0u: return "Pronto";
    case 1u: return "Executando";
    case 2u: return "Bloqueado";
    case 3u: return "Finalizado";
    default: return "Desconhecido";
    }
}

static void taskmgr_refresh(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_refresh_ticks != 0u && (ticks - tm->last_refresh_ticks) < (uint32_t)TASKMGR_REFRESH_TICKS) {
        return;
    }

    tm->task_count = sys_task_snapshot(&tm->summary, tm->tasks, TASK_SNAPSHOT_MAX);
    if (tm->task_count < 0) {
        tm->task_count = 0;
    }
    if (tm->selected_pid != 0u) {
        int found = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            tm->selected_pid = 0u;
        }
    }
    tm->last_refresh_ticks = ticks;
}

static void taskmgr_note_service_event(struct taskmgr_state *tm, const struct mk_service_event *event) {
    uint32_t slot;

    if (tm == 0 || event == 0 || event->event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    slot = (tm->service_event_head + tm->service_event_count) % TASKMGR_SERVICE_EVENT_HISTORY;
    if (tm->service_event_count == TASKMGR_SERVICE_EVENT_HISTORY) {
        slot = tm->service_event_head;
        tm->service_event_head = (tm->service_event_head + 1u) % TASKMGR_SERVICE_EVENT_HISTORY;
    } else {
        tm->service_event_count += 1u;
    }
    tm->service_events[slot].event = *event;

    if (event->service_type == MK_SERVICE_AUDIO) {
        tm->last_audio_refresh_ticks = 0u;
    } else if (event->service_type == MK_SERVICE_NETWORK) {
        tm->last_network_refresh_ticks = 0u;
        tm->last_netmgrd_refresh_ticks = 0u;
    } else if (event->service_type == MK_SERVICE_VIDEO) {
        tm->last_video_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_service_events(struct taskmgr_state *tm) {
    uint32_t service_type;

    if (tm == 0) {
        return;
    }

    for (service_type = 2u; service_type <= 8u; ++service_type) {
        struct mk_service_event event;
        uint32_t mask = 1u << service_type;

        if ((tm->service_event_subscriptions & mask) == 0u) {
            if (sys_service_subscribe(service_type) == 0) {
                tm->service_event_subscriptions |= mask;
            }
        }
        if ((tm->service_event_subscriptions & mask) == 0u) {
            continue;
        }

        while (sys_service_event_receive(service_type, &event, 0u) == 0) {
            taskmgr_note_service_event(tm, &event);
        }
    }
}

static void taskmgr_refresh_audio_events(struct taskmgr_state *tm) {
    struct mk_audio_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->audio_event_subscription) {
        if (sys_audio_event_subscribe() == 0) {
            tm->audio_event_subscription = 1;
        }
    }
    if (!tm->audio_event_subscription) {
        return;
    }

    while (sys_audio_event_receive(&event, 0u) == 0) {
        tm->audio_event = event;
        tm->audio_event_valid = 1;
        tm->last_audio_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_video_events(struct taskmgr_state *tm) {
    struct mk_video_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->video_event_subscription) {
        if (sys_video_event_subscribe() == 0) {
            tm->video_event_subscription = 1;
        }
    }
    if (!tm->video_event_subscription) {
        return;
    }

    while (sys_video_event_receive(&event, 0u) == 0) {
        tm->video_event = event;
        tm->video_event_valid = 1;
        tm->last_video_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_network_events(struct taskmgr_state *tm) {
    struct mk_network_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->network_event_subscription) {
        if (sys_network_event_subscribe() == 0) {
            tm->network_event_subscription = 1;
        }
    }
    if (!tm->network_event_subscription) {
        return;
    }

    while (sys_network_event_receive(&event, 0u) == 0) {
        tm->network_event = event;
        tm->network_event_valid = 1;
        tm->last_network_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_video_bench(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_video_refresh_ticks != 0u &&
        (ticks - tm->last_video_refresh_ticks) < TASKMGR_VIDEO_REFRESH_TICKS) {
        return;
    }

    tm->video_bench_valid = (sys_gfx_bench(&tm->video_bench) == 0);
    tm->last_video_refresh_ticks = ticks;
}

static void taskmgr_refresh_audio_info(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_audio_refresh_ticks != 0u &&
        (ticks - tm->last_audio_refresh_ticks) < TASKMGR_SERVICE_REFRESH_TICKS) {
        return;
    }

    tm->audio_info_valid = (sys_audio_get_info(&tm->audio_info) == 0);
    tm->audio_status_valid = (sys_audio_get_status(&tm->audio_status) == 0);
    tm->last_audio_refresh_ticks = ticks;
}

static void taskmgr_refresh_network_info(struct taskmgr_state *tm, uint32_t ticks) {
    char text[512];
    int size;
    char *line;

    if (tm == 0) {
        return;
    }
    if (tm->last_network_refresh_ticks != 0u &&
        (ticks - tm->last_network_refresh_ticks) < TASKMGR_SERVICE_REFRESH_TICKS) {
        return;
    }

    tm->network_info_valid = (sys_network_get_info(&tm->network_info) == 0);
    tm->network_status_valid = (sys_network_get_status(&tm->network_status) == 0);
    tm->last_network_refresh_ticks = ticks;

    if (tm->last_netmgrd_refresh_ticks != 0u &&
        (ticks - tm->last_netmgrd_refresh_ticks) < TASKMGR_NETMGRD_REFRESH_TICKS) {
        return;
    }

    memset(&tm->netmgrd_status, 0, sizeof(tm->netmgrd_status));
    size = fs_read_file_bytes(TASKMGR_NETMGRD_STATUS_PATH, 0, text, (int)sizeof(text) - 1);
    if (size <= 0) {
        tm->last_netmgrd_refresh_ticks = ticks;
        return;
    }

    text[size] = '\0';
    line = text;
    while (*line != '\0') {
        char *next = line;
        char *sep;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        sep = line;
        while (*sep != '\0' && *sep != '=') {
            ++sep;
        }
        if (*sep == '=') {
            *sep = '\0';
            sep += 1;
            if (strcmp(line, "state") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.state,
                                         (int)sizeof(tm->netmgrd_status.state),
                                         sep);
            } else if (strcmp(line, "active_if") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.active_if,
                                         (int)sizeof(tm->netmgrd_status.active_if),
                                         sep);
            } else if (strcmp(line, "active_kind") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.active_kind,
                                         (int)sizeof(tm->netmgrd_status.active_kind),
                                         sep);
            } else if (strcmp(line, "backend") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.backend,
                                         (int)sizeof(tm->netmgrd_status.backend),
                                         sep);
            } else if (strcmp(line, "dns_mode") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.dns_mode,
                                         (int)sizeof(tm->netmgrd_status.dns_mode),
                                         sep);
            } else if (strcmp(line, "lease_state") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.lease_state,
                                         (int)sizeof(tm->netmgrd_status.lease_state),
                                         sep);
            } else if (strcmp(line, "lease_source") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.lease_source,
                                         (int)sizeof(tm->netmgrd_status.lease_source),
                                         sep);
            } else if (strcmp(line, "manager") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.manager,
                                         (int)sizeof(tm->netmgrd_status.manager),
                                         sep);
            } else if (strcmp(line, "ssid") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.ssid,
                                         (int)sizeof(tm->netmgrd_status.ssid),
                                         sep);
            } else if (strcmp(line, "ip") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.ip,
                                         (int)sizeof(tm->netmgrd_status.ip),
                                         sep);
            } else if (strcmp(line, "gateway") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.gateway,
                                         (int)sizeof(tm->netmgrd_status.gateway),
                                         sep);
            } else if (strcmp(line, "dns") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.dns,
                                         (int)sizeof(tm->netmgrd_status.dns),
                                         sep);
            } else if (strcmp(line, "autoconnect") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.autoconnect,
                                         (int)sizeof(tm->netmgrd_status.autoconnect),
                                         sep);
            } else if (strcmp(line, "saved_profiles") == 0) {
                tm->netmgrd_status.saved_profiles = taskmgr_parse_int(sep);
            }
        }

        line = next;
    }

    tm->netmgrd_status.valid = 1;
    tm->last_netmgrd_refresh_ticks = ticks;
}

void taskmgr_init_state(struct taskmgr_state *tm) {
    tm->window = DEFAULT_TASKMGR_WINDOW;
    tm->selected_tab = TASKMGR_TAB_PROCESSES;
    tm->last_refresh_ticks = 0u;
    tm->last_video_refresh_ticks = 0u;
    tm->last_audio_refresh_ticks = 0u;
    tm->last_network_refresh_ticks = 0u;
    tm->last_netmgrd_refresh_ticks = 0u;
    tm->selected_pid = 0u;
    tm->task_count = 0;
    tm->video_bench_valid = 0;
    tm->audio_info_valid = 0;
    tm->audio_status_valid = 0;
    tm->network_info_valid = 0;
    tm->network_status_valid = 0;
    tm->audio_event_subscription = 0;
    tm->video_event_subscription = 0;
    tm->network_event_subscription = 0;
    tm->audio_event_valid = 0;
    tm->video_event_valid = 0;
    tm->network_event_valid = 0;
    tm->service_event_subscriptions = 0u;
    tm->service_event_head = 0u;
    tm->service_event_count = 0u;
    memset(&tm->netmgrd_status, 0, sizeof(tm->netmgrd_status));
    memset(&tm->audio_event, 0, sizeof(tm->audio_event));
    memset(&tm->video_event, 0, sizeof(tm->video_event));
    memset(&tm->network_event, 0, sizeof(tm->network_event));
    memset(tm->service_events, 0, sizeof(tm->service_events));
}

static struct rect taskmgr_sidebar_rect(const struct taskmgr_state *tm) {
    struct rect r = {tm->window.x + 10, tm->window.y + 60, 116, tm->window.h - 72};
    return r;
}

static struct rect taskmgr_content_rect(const struct taskmgr_state *tm) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + side.w + 10, tm->window.y + 60,
                     tm->window.w - (side.w + 30), tm->window.h - 72};
    return r;
}

static struct rect taskmgr_sidebar_button_rect(const struct taskmgr_state *tm, int index) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + 6, side.y + 8 + (index * 22), side.w - 12, 18};
    return r;
}

static struct rect taskmgr_apps_row_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 42 + (visible_index * TASKMGR_ROW_HEIGHT),
                     content.w - 16, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_apps_close_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect row = taskmgr_apps_row_rect(tm, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static struct rect taskmgr_details_row_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 58 + (visible_index * TASKMGR_ROW_HEIGHT),
                     content.w - 16, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_details_terminate_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect row = taskmgr_details_row_rect(tm, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static void taskmgr_draw_sidebar(const struct taskmgr_state *tm) {
    static const char *labels[3] = {"Processos", "Desempenho", "Detalhes"};
    struct rect side = taskmgr_sidebar_rect(tm);

    ui_draw_surface(&side, ui_color_panel());
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);
        enum ui_button_style style = (tm->selected_tab == i) ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL;

        ui_draw_button(&button, labels[i], style, 0);
    }
}

static void taskmgr_draw_header(const struct rect *content,
                                const struct desktop_theme *theme,
                                const char *title,
                                const char *subtitle) {
    struct rect hero = {content->x, content->y, content->w, 34};

    ui_draw_surface(&hero, ui_color_panel());
    sys_text(hero.x + 8, hero.y + 7, theme->text, title);
    sys_text(hero.x + 8, hero.y + 19, ui_color_muted(), subtitle);
}

static void taskmgr_draw_processes_tab(struct taskmgr_state *tm,
                                       struct window *wins,
                                       int win_count,
                                       uint32_t ticks) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    char subtitle[96] = "";
    int visible_index = 0;

    str_copy_limited(subtitle, "Aplicativos abertos no desktop", (int)sizeof(subtitle));
    taskmgr_draw_header(&content, theme, "Processos", subtitle);

    for (int i = 0; i < win_count; ++i) {
        struct rect row;
        struct rect close_button;
        char line[128] = "";
        unsigned uptime;

        if (!wins[i].active) {
            continue;
        }

        row = taskmgr_apps_row_rect(tm, visible_index);
        close_button = taskmgr_apps_close_rect(tm, visible_index);
        ui_draw_inset(&row, ui_color_window_bg());
        ui_draw_button(&close_button, "Encerrar", UI_BUTTON_DANGER, 0);

        str_append(line, taskmgr_window_label(wins[i].type), (int)sizeof(line));
        str_append(line, "  janela ", (int)sizeof(line));
        append_uint(line, (unsigned)i, (int)sizeof(line));
        str_append(line, "  atividade ", (int)sizeof(line));
        uptime = (ticks - wins[i].start_ticks) / 100u;
        append_uint(line, uptime, (int)sizeof(line));
        str_append(line, "s", (int)sizeof(line));
        sys_text(row.x + 6, row.y + 4, theme->text, line);
        visible_index += 1;
    }

    if (visible_index == 0) {
        sys_text(content.x + 12, content.y + 54, ui_color_muted(), "Nenhum app grafico ativo.");
    }
}

static void taskmgr_draw_performance_card(const struct rect *card,
                                          const char *title,
                                          const char *value,
                                          const char *detail) {
    const struct desktop_theme *theme = ui_theme_get();

    ui_draw_inset(card, ui_color_window_bg());
    sys_text(card->x + 8, card->y + 6, theme->text, title);
    sys_text(card->x + 8, card->y + 19, theme->text, value);
    sys_text(card->x + 8, card->y + 31, ui_color_muted(), detail);
}

static void taskmgr_draw_performance_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    struct rect cards[8];
    char value[64];
    char detail[96];

    taskmgr_refresh_video_bench(tm, tm->last_refresh_ticks);
    taskmgr_refresh_audio_info(tm, tm->last_refresh_ticks);
    taskmgr_refresh_network_info(tm, tm->last_refresh_ticks);
    taskmgr_draw_header(&content, theme, "Desempenho", "Visao geral do kernel e do escalonador");

    for (int i = 0; i < 8; ++i) {
        int col = i % 2;
        int row = i / 2;

        cards[i].x = content.x + 8 + (col * (((content.w - 20) / 2) + 4));
        cards[i].y = content.y + 44 + (row * 54);
        cards[i].w = (content.w - 20) / 2;
        cards[i].h = 48;
    }

    value[0] = '\0';
    append_uint(value, tm->summary.cpu_count, (int)sizeof(value));
    str_copy_limited(detail, "CPUs visiveis", (int)sizeof(detail));
    str_append(detail, "  ativos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.started_cpu_count, (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[0], "CPU", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.total_tasks, (int)sizeof(value));
    str_copy_limited(detail, "prontos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.ready_tasks, (int)sizeof(detail));
    str_append(detail, "  exec ", (int)sizeof(detail));
    append_uint(detail, tm->summary.running_tasks, (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[1], "Tarefas", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.physmem_free_kb / 1024u, (int)sizeof(value));
    str_append(value, " MiB", (int)sizeof(value));
    str_copy_limited(detail, "livre de ", (int)sizeof(detail));
    append_uint(detail, tm->summary.physmem_total_kb / 1024u, (int)sizeof(detail));
    str_append(detail, " MiB fisicos", (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[2], "Memoria Fisica Livre", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.kernel_heap_used / 1024u, (int)sizeof(value));
    str_append(value, " KiB", (int)sizeof(value));
    str_copy_limited(detail, "heap livre ", (int)sizeof(detail));
    append_uint(detail, tm->summary.kernel_heap_free / 1024u, (int)sizeof(detail));
    str_append(detail, " KiB", (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[3], "Heap do Kernel", value, detail);

    if (tm->video_bench_valid) {
        str_copy_limited(value,
                         taskmgr_video_display_label(&tm->video_bench),
                         (int)sizeof(value));
        detail[0] = '\0';
        str_append(detail, taskmgr_video_backend_label(tm->video_bench.backend_kind), (int)sizeof(detail));
        if (tm->video_bench.native_backend_kind == VIDEO_NATIVE_BACKEND_NONE &&
            tm->video_bench.detected_native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
            str_append(detail, "  detectado", (int)sizeof(detail));
        } else if (tm->video_bench.native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
            str_append(detail, "  ativo", (int)sizeof(detail));
        }
        str_append(detail, "  ", (int)sizeof(detail));
        taskmgr_append_video_pci_label(detail, (int)sizeof(detail), &tm->video_bench);
        str_append(detail, "  ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_width, (int)sizeof(detail));
        str_append(detail, "x", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_height, (int)sizeof(detail));
        taskmgr_draw_performance_card(&cards[4], "Driver de Video", value, detail);

        str_copy_limited(value,
                         taskmgr_video_present_label(tm->video_bench.present_copy_kind),
                         (int)sizeof(value));
        str_copy_limited(detail, "pitch ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_pitch, (int)sizeof(detail));
        str_append(detail, "  WC ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.wc_enabled, (int)sizeof(detail));
        str_append(detail, "  PAT ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.cpu_has_pat, (int)sizeof(detail));
        if (tm->video_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_video_event_name(tm->video_event.event_type), (int)sizeof(detail));
            str_append(detail, " #", (int)sizeof(detail));
            append_uint(detail, tm->video_event.sequence, (int)sizeof(detail));
            if (tm->video_event.completed_sequence > 0u) {
                str_append(detail, " done ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.completed_sequence, (int)sizeof(detail));
            }
            if (tm->video_event.pending_depth > 0u) {
                str_append(detail, " q ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.pending_depth, (int)sizeof(detail));
            }
            if (tm->video_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.dropped_events, (int)sizeof(detail));
            }
        }
        taskmgr_draw_performance_card(&cards[5], "Present / Scanout", value, detail);
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_gfx_bench falhou", (int)sizeof(detail));
        taskmgr_draw_performance_card(&cards[4], "Driver de Video", value, detail);
        taskmgr_draw_performance_card(&cards[5], "Present / Scanout", value, detail);
    }

    if (tm->audio_info_valid) {
        unsigned audio_features = taskmgr_audio_feature_flags(&tm->audio_info);
        str_copy_limited(value, taskmgr_audio_display_label(&tm->audio_info), (int)sizeof(value));
        if (tm->audio_info.device.version[0] != '\0') {
            str_append(value, " ", (int)sizeof(value));
            str_append(value, tm->audio_info.device.version, (int)sizeof(value));
        }
        detail[0] = '\0';
        if (tm->audio_info.device.config[0] != '\0') {
            str_append(detail, tm->audio_info.device.config, (int)sizeof(detail));
            if (taskmgr_audio_config_hint(&tm->audio_info.device)[0] != '\0') {
                str_append(detail, " ", (int)sizeof(detail));
                str_append(detail, taskmgr_audio_config_hint(&tm->audio_info.device), (int)sizeof(detail));
            }
        } else {
            str_append(detail, "config n/d", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_MIXER) != 0u) {
            str_append(detail, "  mixer", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u) {
            str_append(detail, "  play", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
            str_append(detail, "  rec", (int)sizeof(detail));
        }
        if (taskmgr_audio_output_count(&tm->audio_info) > 0) {
            str_append(detail, "  out ", (int)sizeof(detail));
            append_uint(detail, (unsigned)taskmgr_audio_output_count(&tm->audio_info), (int)sizeof(detail));
        }
        if (taskmgr_audio_input_count(&tm->audio_info) > 0) {
            str_append(detail, "  in ", (int)sizeof(detail));
            append_uint(detail, (unsigned)taskmgr_audio_input_count(&tm->audio_info), (int)sizeof(detail));
        }
        if (tm->audio_info.controller_pci_id != 0u) {
            str_append(detail, "  pci ", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, (tm->audio_info.controller_pci_id >> 16) & 0xffffu, 4u, (int)sizeof(detail));
            str_append(detail, ":", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, tm->audio_info.controller_pci_id & 0xffffu, 4u, (int)sizeof(detail));
        }
        if (tm->audio_info.codec_vendor_id != 0u) {
            str_append(detail, "  codec ", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, tm->audio_info.codec_vendor_id, 8u, (int)sizeof(detail));
        }
        if (tm->audio_info.output_route != 0u) {
            str_append(detail, "  route ", (int)sizeof(detail));
            if (taskmgr_audio_usb_attach_ready(&tm->audio_info)) {
                str_append(detail, "as", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 24) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/alt", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 16) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/ep", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 8) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/cfg", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, tm->audio_info.output_route & 0xffu, 2u, (int)sizeof(detail));
            } else {
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 8) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, tm->audio_info.output_route & 0xffu, 2u, (int)sizeof(detail));
            }
        }
        str_append(detail, (audio_features & 0x2u) != 0u ? "  irq" : "  no-irq", (int)sizeof(detail));
        str_append(detail, (audio_features & 0x80u) != 0u ? "  mmio" : "  io", (int)sizeof(detail));
        if ((audio_features & 0x10u) != 0u) {
            str_append(detail, " seen", (int)sizeof(detail));
        }
        if ((audio_features & 0x4u) != 0u) {
            str_append(detail, " bad-irq", (int)sizeof(detail));
        }
        if ((audio_features & 0x20u) != 0u) {
            str_append(detail, " starve", (int)sizeof(detail));
        }
        if ((audio_features & 0x40u) != 0u) {
            str_append(detail, " underrun", (int)sizeof(detail));
        }
        if ((audio_features & 0x100u) != 0u) {
            str_append(detail, " codec-quirk", (int)sizeof(detail));
        }
        if ((audio_features & 0x200u) != 0u) {
            str_append(detail, " multich", (int)sizeof(detail));
        }
        if ((audio_features & 0x400u) != 0u) {
            str_append(detail, " cap-dma", (int)sizeof(detail));
        }
        if ((audio_features & 0x800u) != 0u) {
            str_append(detail, " corb-rirb", (int)sizeof(detail));
        }
        if ((audio_features & 0x1000u) != 0u) {
            str_append(detail, " codec-ok", (int)sizeof(detail));
        }
        if ((audio_features & 0x2000u) != 0u) {
            str_append(detail, " widgets", (int)sizeof(detail));
        }
        if ((audio_features & 0x4000u) != 0u) {
            str_append(detail, " path-ok", (int)sizeof(detail));
        }
        if (taskmgr_audio_usb_attach_ready(&tm->audio_info)) {
            str_append(detail, " usb-attach", (int)sizeof(detail));
        }
        if (taskmgr_audio_usb_attached_ready(&tm->audio_info)) {
            str_append(detail, " usb-attached", (int)sizeof(detail));
        }
        if (tm->audio_status_valid) {
            str_append(detail, "  ", (int)sizeof(detail));
            str_append(detail, tm->audio_status.active ? "ativo" : "idle", (int)sizeof(detail));
            if ((tm->audio_status._spare[0] & 0x00002000) != 0) {
                str_append(detail, " cap-data", (int)sizeof(detail));
            }
            if ((tm->audio_status._spare[0] & 0x00004000) != 0) {
                str_append(detail, " cap-xrun", (int)sizeof(detail));
            }
        }
        if (tm->audio_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_audio_event_name(tm->audio_event.event_type), (int)sizeof(detail));
            str_append(detail, " q ", (int)sizeof(detail));
            append_uint(detail, tm->audio_event.queued_bytes, (int)sizeof(detail));
            str_append(detail, " u ", (int)sizeof(detail));
            append_uint(detail, tm->audio_event.underruns, (int)sizeof(detail));
            if (tm->audio_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->audio_event.dropped_events, (int)sizeof(detail));
            }
        }
        taskmgr_draw_performance_card(&cards[6], "Driver de Audio", value, detail);
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_audio_get_info falhou", (int)sizeof(detail));
        taskmgr_draw_performance_card(&cards[6], "Driver de Audio", value, detail);
    }

    if (tm->network_info_valid) {
        if (tm->network_status_valid) {
            str_copy_limited(value, taskmgr_network_link_label(&tm->network_status), (int)sizeof(value));
        } else {
            str_copy_limited(value, taskmgr_network_display_label(&tm->network_info), (int)sizeof(value));
        }
        detail[0] = '\0';
        if (tm->network_status_valid) {
            if (tm->network_status.active_if[0] != '\0') {
                str_append(detail, tm->network_status.active_if, (int)sizeof(detail));
            } else {
                str_append(detail, "-", (int)sizeof(detail));
            }
            if (tm->network_status.current_ssid[0] != '\0') {
                str_append(detail, "  ", (int)sizeof(detail));
                str_append(detail, tm->network_status.current_ssid, (int)sizeof(detail));
            }
            if (tm->network_status.ip_address[0] != '\0') {
                str_append(detail, "  ", (int)sizeof(detail));
                str_append(detail, tm->network_status.ip_address, (int)sizeof(detail));
            }
            if (tm->network_status.dns_server[0] != '\0') {
                str_append(detail, "  dns ", (int)sizeof(detail));
                str_append(detail, tm->network_status.dns_server, (int)sizeof(detail));
            }
        }
        if (tm->netmgrd_status.valid) {
            if (tm->netmgrd_status.saved_profiles > 0) {
                str_append(detail, detail[0] != '\0' ? "  perfis " : "perfis ", (int)sizeof(detail));
                append_uint(detail, (unsigned)tm->netmgrd_status.saved_profiles, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.autoconnect[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  auto " : "auto ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.autoconnect, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.backend[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  backend " : "backend ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.backend, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.dns_mode[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  dns-mode " : "dns-mode ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.dns_mode, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.lease_state[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  lease " : "lease ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.lease_state, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.lease_source[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  src " : "src ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.lease_source, (int)sizeof(detail));
            }
        }
        if ((tm->network_info.flags & MK_NETWORK_CAPS_BSD_SOCKET_ABI) != 0u) {
            str_append(detail, detail[0] != '\0' ? "  socket-bsd" : "socket-bsd", (int)sizeof(detail));
        }
        if ((tm->network_info.flags & MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING) != 0u) {
            str_append(detail, "  drivers pendentes", (int)sizeof(detail));
        }
        if ((tm->network_info.supported_families & MK_NETWORK_FAMILY_INET) != 0u) {
            str_append(detail, "  inet", (int)sizeof(detail));
        }
        if ((tm->network_info.supported_families & MK_NETWORK_FAMILY_INET6) != 0u) {
            str_append(detail, "  inet6", (int)sizeof(detail));
        }
        if (tm->network_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_network_event_name(tm->network_event.event_type), (int)sizeof(detail));
            if (tm->network_event.handle > 0) {
                str_append(detail, " h ", (int)sizeof(detail));
                append_int(detail, tm->network_event.handle, (int)sizeof(detail));
            }
            if (tm->network_event.peer_handle > 0) {
                str_append(detail, " p ", (int)sizeof(detail));
                append_int(detail, tm->network_event.peer_handle, (int)sizeof(detail));
            }
            if (tm->network_event.byte_count > 0u) {
                str_append(detail, " b ", (int)sizeof(detail));
                append_uint(detail, tm->network_event.byte_count, (int)sizeof(detail));
            }
            if (tm->network_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->network_event.dropped_events, (int)sizeof(detail));
            }
        }
        taskmgr_draw_performance_card(&cards[7], "Driver de Rede", value, detail);
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_network_get_info falhou", (int)sizeof(detail));
        taskmgr_draw_performance_card(&cards[7], "Driver de Rede", value, detail);
    }

    value[0] = '\0';
    str_copy_limited(value, "Tempo ativo ", (int)sizeof(value));
    append_uint(value, tm->summary.uptime_ticks / 100u, (int)sizeof(value));
    str_append(value, "s", (int)sizeof(value));
    sys_text(content.x + 12, content.y + 268, theme->text, value);

    detail[0] = '\0';
    str_copy_limited(detail, "PID atual ", (int)sizeof(detail));
    append_uint(detail, tm->summary.current_pid, (int)sizeof(detail));
    str_append(detail, "  bloqueados ", (int)sizeof(detail));
    append_uint(detail, tm->summary.blocked_tasks, (int)sizeof(detail));
    if (tm->audio_status_valid) {
        str_append(detail, "  audio pend ", (int)sizeof(detail));
        append_uint(detail, (unsigned)tm->audio_status._spare[1], (int)sizeof(detail));
        str_append(detail, " xr ", (int)sizeof(detail));
        append_uint(detail, (unsigned)tm->audio_status._spare[4], (int)sizeof(detail));
        if (tm->audio_info_valid) {
            str_append(detail, " irq ", (int)sizeof(detail));
            append_uint(detail, taskmgr_audio_irq_count(&tm->audio_info), (int)sizeof(detail));
        }
    }
    if (tm->video_bench_valid) {
        str_append(detail, "  frame ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.frame_ticks, (int)sizeof(detail));
        str_append(detail, "t", (int)sizeof(detail));
    }
    if (tm->video_event_valid) {
        str_append(detail, "  vid ", (int)sizeof(detail));
        str_append(detail, taskmgr_video_event_name(tm->video_event.event_type), (int)sizeof(detail));
        str_append(detail, " #", (int)sizeof(detail));
        append_uint(detail, tm->video_event.sequence, (int)sizeof(detail));
        if (tm->video_event.completed_sequence > 0u) {
            str_append(detail, " done ", (int)sizeof(detail));
            append_uint(detail, tm->video_event.completed_sequence, (int)sizeof(detail));
        }
        if (tm->video_event.pending_depth > 0u) {
            str_append(detail, " q ", (int)sizeof(detail));
            append_uint(detail, tm->video_event.pending_depth, (int)sizeof(detail));
        }
        if (tm->video_event.dropped_events > 0u) {
            str_append(detail, " drop ", (int)sizeof(detail));
            append_uint(detail, tm->video_event.dropped_events, (int)sizeof(detail));
        }
    }
    if (tm->network_event_valid) {
        str_append(detail, "  net ", (int)sizeof(detail));
        str_append(detail, taskmgr_network_event_name(tm->network_event.event_type), (int)sizeof(detail));
        str_append(detail, " #", (int)sizeof(detail));
        append_uint(detail, tm->network_event.sequence, (int)sizeof(detail));
        if (tm->network_event.byte_count > 0u) {
            str_append(detail, " b ", (int)sizeof(detail));
            append_uint(detail, tm->network_event.byte_count, (int)sizeof(detail));
        }
        if (tm->network_event.dropped_events > 0u) {
            str_append(detail, " drop ", (int)sizeof(detail));
            append_uint(detail, tm->network_event.dropped_events, (int)sizeof(detail));
        }
    }
    if (tm->netmgrd_status.valid) {
        if (tm->netmgrd_status.state[0] != '\0') {
            str_append(detail, "  netmgrd ", (int)sizeof(detail));
            str_append(detail, tm->netmgrd_status.state, (int)sizeof(detail));
        }
        if (tm->netmgrd_status.manager[0] != '\0') {
            str_append(detail, " / ", (int)sizeof(detail));
            str_append(detail, tm->netmgrd_status.manager, (int)sizeof(detail));
        }
        if (tm->netmgrd_status.lease_state[0] != '\0') {
            str_append(detail, "  lease ", (int)sizeof(detail));
            str_append(detail, tm->netmgrd_status.lease_state, (int)sizeof(detail));
        }
        if (tm->netmgrd_status.lease_source[0] != '\0') {
            str_append(detail, "  src ", (int)sizeof(detail));
            str_append(detail, tm->netmgrd_status.lease_source, (int)sizeof(detail));
        }
        if (tm->netmgrd_status.autoconnect[0] != '\0') {
            str_append(detail, " -> ", (int)sizeof(detail));
            str_append(detail, tm->netmgrd_status.autoconnect, (int)sizeof(detail));
        }
    }
    sys_text(content.x + 12, content.y + 280, ui_color_muted(), detail);

    if (tm->service_event_count != 0u) {
        uint32_t lines = tm->service_event_count < 3u ? tm->service_event_count : 3u;

        sys_text(content.x + 12, content.y + 294, theme->text, "Eventos recentes de servico");
        for (uint32_t i = 0; i < lines; ++i) {
            uint32_t index = (tm->service_event_head + tm->service_event_count - 1u - i) %
                             TASKMGR_SERVICE_EVENT_HISTORY;
            const struct mk_service_event *event = &tm->service_events[index].event;
            char line[128] = "";

            str_append(line, taskmgr_service_name(event->service_type), (int)sizeof(line));
            str_append(line, " ", (int)sizeof(line));
            str_append(line, taskmgr_service_event_name(event->event_type), (int)sizeof(line));
            str_append(line, " pid ", (int)sizeof(line));
            append_uint(line, event->pid, (int)sizeof(line));
            str_append(line, " rst ", (int)sizeof(line));
            append_uint(line, event->restart_count, (int)sizeof(line));
            str_append(line, " t", (int)sizeof(line));
            append_uint(line, event->tick, (int)sizeof(line));
            sys_text(content.x + 12, content.y + 306 + ((int)i * 12), ui_color_muted(), line);
        }
    }
}

static void taskmgr_draw_details_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);

    taskmgr_draw_header(&content, theme, "Detalhes", "PIDs reais, prioridade, reinicios e estado");
    sys_text(content.x + 12, content.y + 44, ui_color_muted(), "Nome         PID  Estado       CPU  Prio      Rest  Runtime");

    for (int i = 0; i < tm->task_count; ++i) {
        struct rect row = taskmgr_details_row_rect(tm, i);
        struct rect kill = taskmgr_details_terminate_rect(tm, i);
        char line[160] = "";
        uint8_t text_color = theme->text;

        ui_draw_inset(&row, ui_color_window_bg());
        if (tm->selected_pid == tm->tasks[i].pid) {
            ui_draw_surface(&row, ui_color_panel());
        }
        if (tm->tasks[i].pid == tm->summary.current_pid) {
            text_color = ui_color_muted();
        }

        if (tm->tasks[i].name[0] != '\0') {
            str_copy_limited(line, tm->tasks[i].name, (int)sizeof(line));
        } else {
            str_copy_limited(line, taskmgr_kind_label(tm->tasks[i].kind), (int)sizeof(line));
        }
        while (str_len(line) < 12) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_uint(line, tm->tasks[i].pid, (int)sizeof(line));
        while (str_len(line) < 17) {
            str_append(line, " ", (int)sizeof(line));
        }
        str_append(line, taskmgr_state_label(tm->tasks[i].state), (int)sizeof(line));
        while (str_len(line) < 30) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_int(line, tm->tasks[i].current_cpu, (int)sizeof(line));
        while (str_len(line) < 35) {
            str_append(line, " ", (int)sizeof(line));
        }
        str_append(line, taskmgr_priority_label(tm->tasks[i].priority_tier), (int)sizeof(line));
        while (str_len(line) < 46) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_uint(line, tm->tasks[i].service_restart_count, (int)sizeof(line));
        while (str_len(line) < 52) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_uint(line, tm->tasks[i].runtime_ticks / 100u, (int)sizeof(line));
        str_append(line, "s", (int)sizeof(line));
        sys_text(row.x + 6, row.y + 4, text_color, line);

        if (tm->tasks[i].pid != tm->summary.current_pid &&
            (tm->tasks[i].flags & (1u << 1)) == 0u) {
            ui_draw_button(&kill, "PID", UI_BUTTON_DANGER, 0);
        } else {
            ui_draw_button(&kill, "Atual", UI_BUTTON_NORMAL, 0);
        }
    }

    if (tm->selected_pid != 0u) {
        char detail[128] = "";
        const struct task_snapshot_entry *selected = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                selected = &tm->tasks[i];
                break;
            }
        }

        if (selected != 0) {
            str_copy_limited(detail, "Selecionado PID ", (int)sizeof(detail));
            append_uint(detail, selected->pid, (int)sizeof(detail));
            str_append(detail, "  tipo ", (int)sizeof(detail));
            str_append(detail, taskmgr_kind_label(selected->kind), (int)sizeof(detail));
            str_append(detail, "  prio ", (int)sizeof(detail));
            str_append(detail, taskmgr_priority_label(selected->priority_tier), (int)sizeof(detail));
            str_append(detail, "  saude ", (int)sizeof(detail));
            str_append(detail, taskmgr_service_health_label(selected), (int)sizeof(detail));
            str_append(detail, "  pref ", (int)sizeof(detail));
            append_int(detail, selected->preferred_cpu, (int)sizeof(detail));
            sys_text(content.x + 12, content.y + content.h - 14, ui_color_muted(), detail);
        }
    }
}

void taskmgr_draw_window(struct taskmgr_state *tm,
                         struct window *wins,
                         int win_count,
                         uint32_t ticks,
                         int active,
                         int min_hover,
                         int max_hover,
                         int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {tm->window.x + 4, tm->window.y + 18, tm->window.w - 8, tm->window.h - 22};
    struct rect title = {tm->window.x + 10, tm->window.y + 24, tm->window.w - 20, 30};

    taskmgr_refresh(tm, ticks);
    taskmgr_refresh_service_events(tm);
    taskmgr_refresh_audio_events(tm);
    taskmgr_refresh_video_events(tm);
    taskmgr_refresh_network_events(tm);
    draw_window_frame(&tm->window, "GERENCIADOR DE TAREFAS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&title, ui_color_panel());
    sys_text(title.x + 8, title.y + 6, theme->text, "Monitoramento e controle do sistema");
    sys_text(title.x + title.w - 120, title.y + 6, ui_color_muted(), "VibeOS Desktop");

    taskmgr_draw_sidebar(tm);
    switch (tm->selected_tab) {
    case TASKMGR_TAB_PROCESSES:
        taskmgr_draw_processes_tab(tm, wins, win_count, ticks);
        break;
    case TASKMGR_TAB_PERFORMANCE:
        taskmgr_draw_performance_tab(tm);
        break;
    case TASKMGR_TAB_DETAILS:
    default:
        taskmgr_draw_details_tab(tm);
        break;
    }
}

struct taskmgr_action taskmgr_handle_click(struct taskmgr_state *tm,
                                           const struct window *wins,
                                           int win_count,
                                           int x,
                                           int y,
                                           uint32_t ticks) {
    struct taskmgr_action action = {TASKMGR_ACTION_NONE, -1};

    taskmgr_refresh(tm, ticks);
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);

        if (point_in_rect(&button, x, y)) {
            tm->selected_tab = i;
            return action;
        }
    }

    if (tm->selected_tab == TASKMGR_TAB_PROCESSES) {
        int visible_index = 0;

        for (int i = 0; i < win_count; ++i) {
            struct rect row;
            struct rect close_button;

            if (!wins[i].active) {
                continue;
            }
            row = taskmgr_apps_row_rect(tm, visible_index);
            close_button = taskmgr_apps_close_rect(tm, visible_index);
            if (point_in_rect(&close_button, x, y)) {
                action.type = TASKMGR_ACTION_CLOSE_WINDOW;
                action.value = i;
                return action;
            }
            if (point_in_rect(&row, x, y)) {
                return action;
            }
            visible_index += 1;
        }
        return action;
    }

    if (tm->selected_tab == TASKMGR_TAB_DETAILS) {
        for (int i = 0; i < tm->task_count; ++i) {
            struct rect row = taskmgr_details_row_rect(tm, i);
            struct rect kill = taskmgr_details_terminate_rect(tm, i);

            if (point_in_rect(&row, x, y)) {
                tm->selected_pid = tm->tasks[i].pid;
            }
            if (tm->tasks[i].pid != tm->summary.current_pid &&
                (tm->tasks[i].flags & (1u << 1)) == 0u &&
                point_in_rect(&kill, x, y)) {
                action.type = TASKMGR_ACTION_TERMINATE_PID;
                action.value = (int)tm->tasks[i].pid;
                return action;
            }
        }
    }

    return action;
}
