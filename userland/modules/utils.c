#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <kernel/microkernel/audio.h>
#include <sys/audioio.h>

#define AUDIO_WAV_HEADER_SIZE 44
#define AUDIO_WAV_CHUNK_HEADER_SIZE 8
#define AUDIO_WAV_STREAM_CHUNK 960
#define AUDIO_WAV_AZALIA_CHUNK 16384
#define AUDIO_STATUS_BACKEND_MASK 0x000000ffu
#define AUDIO_BACKEND_COMPAT_AZALIA 2
#define AUDIO_STATUS_FLAG_IRQ_REGISTERED 0x00000100u
#define AUDIO_STATUS_FLAG_IRQ_SEEN 0x00000200u
#define AUDIO_FEATURE_PATH_PROGRAMMED 0x4000u

static char g_audio_last_playback_error[48] = "ok";
static char g_audio_last_playback_detail[160] = "";

static void audio_set_last_playback_error(const char *text) {
    str_copy_limited(g_audio_last_playback_error,
                     text ? text : "unknown",
                     (int)sizeof(g_audio_last_playback_error));
}

const char *audio_last_playback_error(void) {
    return g_audio_last_playback_error;
}

static const char *audio_backend_name_from_kind(int backend_kind) {
    switch (backend_kind) {
    case 2:
        return "compat-azalia";
    case 1:
        return "compat-ac97";
    default:
        return "softmix";
    }
}

static void audio_append_hex(char *dst, int max_len, unsigned value, int digits) {
    static const char hex[] = "0123456789abcdef";
    char text[9];

    if (digits < 1) {
        digits = 1;
    }
    if (digits > 8) {
        digits = 8;
    }
    for (int i = 0; i < digits; ++i) {
        int shift = (digits - 1 - i) * 4;
        text[i] = hex[(value >> shift) & 0xf];
    }
    text[digits] = '\0';
    str_append(dst, text, max_len);
}

static void audio_update_last_playback_detail(void) {
    struct mk_audio_info info;
    struct audio_status status;
    int backend_kind = -1;
    unsigned feature_flags = 0u;
    unsigned status_flags = 0u;
    char line[160];

    g_audio_last_playback_detail[0] = '\0';
    if (sys_audio_get_info(&info) != 0 || sys_audio_get_status(&status) != 0) {
        return;
    }

    backend_kind = status._spare[0] & (int)AUDIO_STATUS_BACKEND_MASK;
    status_flags = (unsigned)status._spare[0] & ~AUDIO_STATUS_BACKEND_MASK;
    feature_flags = info.parameters._spare[5];
    line[0] = '\0';
    str_append(line, audio_backend_name_from_kind(backend_kind), (int)sizeof(line));
    if (info.device.config[0] != '\0') {
        str_append(line, " cfg=", (int)sizeof(line));
        str_append(line, info.device.config, (int)sizeof(line));
    }
    str_append(line, " act=", (int)sizeof(line));
    str_append(line, status.active ? "1" : "0", (int)sizeof(line));
    str_append(line, " pend=", (int)sizeof(line));
    str_append(line, status._spare[1] != 0 ? "1" : "0", (int)sizeof(line));
    str_append(line, " path=", (int)sizeof(line));
    str_append(line, (feature_flags & AUDIO_FEATURE_PATH_PROGRAMMED) != 0u ? "1" : "0", (int)sizeof(line));
    str_append(line, " irq=", (int)sizeof(line));
    str_append(line, (status_flags & AUDIO_STATUS_FLAG_IRQ_REGISTERED) != 0u ? "1" : "0", (int)sizeof(line));
    str_append(line, "/", (int)sizeof(line));
    str_append(line, (status_flags & AUDIO_STATUS_FLAG_IRQ_SEEN) != 0u ? "1" : "0", (int)sizeof(line));
    if (info.controller_pci_id != 0u) {
        str_append(line, " pci=", (int)sizeof(line));
        audio_append_hex(line, (int)sizeof(line), (unsigned)((info.controller_pci_id >> 16) & 0xffffu), 4);
        str_append(line, ":", (int)sizeof(line));
        audio_append_hex(line, (int)sizeof(line), (unsigned)(info.controller_pci_id & 0xffffu), 4);
    }
    if (info.codec_vendor_id != 0u) {
        str_append(line, " codec=", (int)sizeof(line));
        audio_append_hex(line, (int)sizeof(line), (unsigned)info.codec_vendor_id, 8);
    }
    if (info.output_route != 0u) {
        str_append(line, " route=", (int)sizeof(line));
        audio_append_hex(line, (int)sizeof(line), (unsigned)((info.output_route >> 8) & 0xffu), 2);
        str_append(line, "/", (int)sizeof(line));
        audio_append_hex(line, (int)sizeof(line), (unsigned)(info.output_route & 0xffu), 2);
    }
    str_copy_limited(g_audio_last_playback_detail, line, (int)sizeof(g_audio_last_playback_detail));
}

const char *audio_last_playback_detail(void) {
    return g_audio_last_playback_detail;
}

static uint16_t audio_read_u16_le(const uint8_t *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t audio_read_u32_le(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static int audio_chunk_id_eq(const uint8_t *src, const char *id) {
    return src[0] == (uint8_t)id[0] &&
           src[1] == (uint8_t)id[1] &&
           src[2] == (uint8_t)id[2] &&
           src[3] == (uint8_t)id[3];
}

static uint32_t audio_estimated_playback_ticks(const struct audio_swpar *params, uint32_t data_size) {
    uint32_t bytes_per_second;
    uint32_t duration_ms;
    uint32_t ticks;

    if (params == 0 || params->rate == 0u || params->pchan == 0u || params->bps == 0u) {
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

static void audio_debug_line(const char *prefix, const char *tag, const char *suffix) {
    char line[128];

    line[0] = '\0';
    str_append(line, prefix, (int)sizeof(line));
    str_append(line, tag ? tag : "audio", (int)sizeof(line));
    str_append(line, suffix, (int)sizeof(line));
    sys_write_debug(line);
}

static int audio_backend_kind(void) {
    struct audio_status status;

    if (sys_audio_get_status(&status) != 0) {
        return -1;
    }
    return status._spare[0] & (int)AUDIO_STATUS_BACKEND_MASK;
}

static int audio_wait_for_playback_idle(uint32_t expected_ticks) {
    struct audio_status status;
    uint32_t start = sys_ticks();
    uint32_t timeout = expected_ticks + 40u;

    if (timeout < 40u) {
        timeout = 40u;
    }

    for (;;) {
        if (sys_audio_get_status(&status) == 0) {
            if (status.active == 0 && status._spare[1] == 0) {
                return 0;
            }
        }
        if ((uint32_t)(sys_ticks() - start) >= timeout) {
            return -1;
        }
        sys_sleep();
    }
}

static int audio_playback_is_idle(void) {
    struct audio_status status;

    if (sys_audio_get_status(&status) != 0) {
        return 0;
    }
    return status.active == 0 && status._spare[1] == 0;
}

static uint32_t audio_idle_timeout_ticks(uint32_t expected_ticks) {
    uint32_t timeout = expected_ticks + 40u;

    if (timeout < 40u) {
        timeout = 40u;
    }
    return timeout;
}

static int audio_load_wav_info(int node,
                               uint32_t file_size,
                               struct audio_swpar *params_out,
                               uint32_t *data_offset_out,
                               uint32_t *data_size_out) {
    uint8_t header[12];
    uint8_t chunk_header[AUDIO_WAV_CHUNK_HEADER_SIZE];
    uint8_t fmt_payload[16];
    uint32_t offset;
    int found_fmt = 0;
    int found_data = 0;

    if (params_out == 0 || data_offset_out == 0 || data_size_out == 0) {
        return -1;
    }
    if (file_size < sizeof(header) || fs_read_node_bytes(node, 0, header, (int)sizeof(header)) != (int)sizeof(header)) {
        return -1;
    }
    if (!audio_chunk_id_eq(header, "RIFF") || !audio_chunk_id_eq(header + 8, "WAVE")) {
        return -1;
    }

    AUDIO_INITPAR(params_out);
    params_out->sig = 1u;
    params_out->le = 1u;
    params_out->bits = 16u;
    params_out->bps = 2u;
    params_out->pchan = 2u;
    params_out->rchan = 2u;
    params_out->nblks = 4u;
    params_out->round = 512u;

    offset = 12u;
    while ((offset + AUDIO_WAV_CHUNK_HEADER_SIZE) <= file_size) {
        uint32_t chunk_size;
        uint32_t chunk_data_offset;
        uint32_t next_offset;

        if (fs_read_node_bytes(node, (int)offset, chunk_header, (int)sizeof(chunk_header)) != (int)sizeof(chunk_header)) {
            return -1;
        }
        chunk_size = audio_read_u32_le(chunk_header + 4);
        chunk_data_offset = offset + AUDIO_WAV_CHUNK_HEADER_SIZE;
        next_offset = chunk_data_offset + chunk_size + (chunk_size & 1u);
        if (chunk_data_offset > file_size || next_offset > file_size + 1u) {
            return -1;
        }

        if (audio_chunk_id_eq(chunk_header, "fmt ")) {
            if (chunk_size < sizeof(fmt_payload) ||
                fs_read_node_bytes(node, (int)chunk_data_offset, fmt_payload, (int)sizeof(fmt_payload)) != (int)sizeof(fmt_payload)) {
                return -1;
            }
            if (audio_read_u16_le(fmt_payload + 0) != 1u) {
                return -1;
            }
            params_out->pchan = audio_read_u16_le(fmt_payload + 2);
            params_out->rchan = params_out->pchan;
            params_out->rate = audio_read_u32_le(fmt_payload + 4);
            params_out->bps = (unsigned int)(audio_read_u16_le(fmt_payload + 14) / 8u);
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
        } else if (audio_chunk_id_eq(chunk_header, "data")) {
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

int audio_play_wav_best_effort(const char *path, const char *tag) {
    int node;
    int backend_kind;
    int waited_for_hda_chunks = 0;
    uint32_t file_size;
    struct audio_swpar params;
    uint32_t data_offset = 0u;
    uint32_t data_size = 0u;
    uint8_t buffer[AUDIO_WAV_AZALIA_CHUNK];
    uint32_t streamed;
    uint32_t playback_ticks;

    if (path == 0 || path[0] == '\0') {
        audio_set_last_playback_error("empty-path");
        g_audio_last_playback_detail[0] = '\0';
        return -1;
    }

    audio_set_last_playback_error("ok");
    audio_debug_line("audio: begin ", tag, "\n");

    node = fs_resolve(path);
    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        audio_set_last_playback_error("missing-file");
        audio_update_last_playback_detail();
        audio_debug_line("audio: missing wav ", tag, "\n");
        return -1;
    }

    file_size = (uint32_t)g_fs_nodes[node].size;
    if (audio_load_wav_info(node, file_size, &params, &data_offset, &data_size) != 0) {
        audio_set_last_playback_error("unsupported-wav");
        audio_update_last_playback_detail();
        audio_debug_line("audio: unsupported wav ", tag, "\n");
        return -1;
    }
    playback_ticks = audio_estimated_playback_ticks(&params, data_size);
    backend_kind = audio_backend_kind();
    if (backend_kind == AUDIO_BACKEND_COMPAT_AZALIA &&
        tag != 0 &&
        str_eq(tag, "desktop")) {
        audio_set_last_playback_error("deferred");
        audio_debug_line("audio: defer wav ", tag, "\n");
        return 0;
    }
    if (sys_audio_set_params(&params) != 0) {
        audio_set_last_playback_error("set-params-failed");
        audio_update_last_playback_detail();
        audio_debug_line("audio: set_params failed ", tag, "\n");
        return -1;
    }
    if (sys_audio_start() != 0) {
        audio_set_last_playback_error("start-failed");
        audio_update_last_playback_detail();
        audio_debug_line("audio: start failed ", tag, "\n");
        return -1;
    }
    audio_debug_line("audio: started ", tag, "\n");

    streamed = 0u;
    while (streamed < data_size) {
        uint32_t chunk_size = data_size - streamed;
        int written;

        if (backend_kind == AUDIO_BACKEND_COMPAT_AZALIA) {
            if (chunk_size > AUDIO_WAV_AZALIA_CHUNK) {
                chunk_size = AUDIO_WAV_AZALIA_CHUNK;
            }
        } else if (chunk_size > (uint32_t)sizeof(buffer)) {
            chunk_size = (uint32_t)sizeof(buffer);
        }
        if (fs_read_node_bytes(node, (int)(data_offset + streamed), buffer, (int)chunk_size) != (int)chunk_size) {
            audio_set_last_playback_error("read-failed");
            audio_update_last_playback_detail();
            audio_debug_line("audio: read failed ", tag, "\n");
            (void)sys_audio_stop();
            return -1;
        }
        written = sys_audio_write(buffer, chunk_size);
        if (written <= 0) {
            audio_set_last_playback_error("write-failed");
            audio_update_last_playback_detail();
            audio_debug_line("audio: write failed ", tag, "\n");
            (void)sys_audio_stop();
            return -1;
        }
        audio_debug_line("audio: wrote ", tag, "\n");
        streamed += (uint32_t)written;
        if (backend_kind == AUDIO_BACKEND_COMPAT_AZALIA) {
            uint32_t chunk_ticks = audio_estimated_playback_ticks(&params, (uint32_t)written);

            if (audio_wait_for_playback_idle(chunk_ticks) != 0) {
                audio_set_last_playback_error("wait-timeout");
                audio_update_last_playback_detail();
                audio_debug_line("audio: wait timeout ", tag, "\n");
                break;
            }
            waited_for_hda_chunks = 1;
            audio_debug_line("audio: queued ", tag, "\n");
        }
        if (backend_kind != AUDIO_BACKEND_COMPAT_AZALIA) {
            sys_yield();
        }
    }

    if (playback_ticks > 0u &&
        !(backend_kind == AUDIO_BACKEND_COMPAT_AZALIA && waited_for_hda_chunks)) {
        uint32_t started = sys_ticks();
        while ((uint32_t)(sys_ticks() - started) < playback_ticks) {
            sys_sleep();
        }
    }
    audio_debug_line("audio: stopping ", tag, "\n");
    (void)sys_audio_stop();
    audio_debug_line("audio: done ", tag, "\n");
    audio_set_last_playback_error("ok");
    audio_update_last_playback_detail();
    return 0;
}

int audio_play_wav_async_start(struct audio_async_playback *playback, const char *path, const char *tag) {
    int node;
    uint32_t file_size;
    struct audio_swpar *params;

    if (playback == 0) {
        audio_set_last_playback_error("invalid-state");
        g_audio_last_playback_detail[0] = '\0';
        return -1;
    }
    memset(playback, 0, sizeof(*playback));
    params = (struct audio_swpar *)playback->params_storage;

    if (path == 0 || path[0] == '\0') {
        audio_set_last_playback_error("empty-path");
        g_audio_last_playback_detail[0] = '\0';
        return -1;
    }

    audio_set_last_playback_error("ok");
    audio_debug_line("audio: begin ", tag, "\n");

    node = fs_resolve(path);
    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        audio_set_last_playback_error("missing-file");
        audio_update_last_playback_detail();
        audio_debug_line("audio: missing wav ", tag, "\n");
        return -1;
    }

    file_size = (uint32_t)g_fs_nodes[node].size;
    if (audio_load_wav_info(node,
                            file_size,
                            params,
                            &playback->data_offset,
                            &playback->data_size) != 0) {
        audio_set_last_playback_error("unsupported-wav");
        audio_update_last_playback_detail();
        audio_debug_line("audio: unsupported wav ", tag, "\n");
        return -1;
    }

    playback->backend_kind = audio_backend_kind();
    playback->node = node;
    playback->last_chunk_ticks = 0u;
    str_copy_limited(playback->tag, tag ? tag : "audio", (int)sizeof(playback->tag));

    if (sys_audio_set_params(params) != 0) {
        audio_set_last_playback_error("set-params-failed");
        audio_update_last_playback_detail();
        audio_debug_line("audio: set_params failed ", tag, "\n");
        return -1;
    }
    if (sys_audio_start() != 0) {
        audio_set_last_playback_error("start-failed");
        audio_update_last_playback_detail();
        audio_debug_line("audio: start failed ", tag, "\n");
        return -1;
    }

    playback->active = 1;
    audio_debug_line("audio: started ", playback->tag, "\n");
    return 0;
}

int audio_play_wav_async_poll(struct audio_async_playback *playback) {
    uint8_t buffer[AUDIO_WAV_AZALIA_CHUNK];
    struct audio_swpar *params;

    if (playback == 0 || playback->active == 0) {
        return 0;
    }
    params = (struct audio_swpar *)playback->params_storage;

    if (playback->waiting_for_idle) {
        if (audio_playback_is_idle()) {
            playback->waiting_for_idle = 0;
            if (!playback->finalizing) {
                audio_debug_line("audio: queued ", playback->tag, "\n");
            }
        } else if ((uint32_t)(sys_ticks() - playback->idle_started) >= playback->idle_timeout) {
            audio_set_last_playback_error("wait-timeout");
            audio_update_last_playback_detail();
            audio_debug_line("audio: wait timeout ", playback->tag, "\n");
            (void)sys_audio_stop();
            playback->active = 0;
            return -1;
        } else {
            return 1;
        }
    }

    if (playback->finalizing || playback->streamed >= playback->data_size) {
        audio_debug_line("audio: stopping ", playback->tag, "\n");
        (void)sys_audio_stop();
        audio_debug_line("audio: done ", playback->tag, "\n");
        audio_set_last_playback_error("ok");
        audio_update_last_playback_detail();
        playback->active = 0;
        return 0;
    }

    {
        uint32_t chunk_size = playback->data_size - playback->streamed;
        int written;

        if (chunk_size > (uint32_t)sizeof(buffer)) {
            chunk_size = (uint32_t)sizeof(buffer);
        }
        if (fs_read_node_bytes(playback->node,
                               (int)(playback->data_offset + playback->streamed),
                               buffer,
                               (int)chunk_size) != (int)chunk_size) {
            audio_set_last_playback_error("read-failed");
            audio_update_last_playback_detail();
            audio_debug_line("audio: read failed ", playback->tag, "\n");
            (void)sys_audio_stop();
            playback->active = 0;
            return -1;
        }

        written = sys_audio_write(buffer, chunk_size);
        if (written <= 0) {
            audio_set_last_playback_error("write-failed");
            audio_update_last_playback_detail();
            audio_debug_line("audio: write failed ", playback->tag, "\n");
            (void)sys_audio_stop();
            playback->active = 0;
            return -1;
        }

        playback->streamed += (uint32_t)written;
        playback->last_chunk_ticks = audio_estimated_playback_ticks(params, (uint32_t)written);
        audio_debug_line("audio: wrote ", playback->tag, "\n");

        if (playback->backend_kind == AUDIO_BACKEND_COMPAT_AZALIA ||
            playback->streamed >= playback->data_size) {
            playback->waiting_for_idle = 1;
            playback->finalizing = playback->streamed >= playback->data_size;
            playback->idle_started = sys_ticks();
            playback->idle_timeout = audio_idle_timeout_ticks(playback->last_chunk_ticks);
        }
    }

    return 1;
}

int str_len(const char *s) {
    int n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

__attribute__((noinline, optimize("O0")))
int str_eq(const char *a, const char *b) {
    for (;;) {
        char ca = *a;
        char cb = *b;

        if (ca != cb) {
            return 0;
        }
        if (ca == '\0') {
            return 1;
        }

        ++a;
        ++b;
    }
}

int to_upper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}

int str_eq_ci(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (to_upper(*a) != to_upper(*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

void str_copy_limited(char *dst, const char *src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < (max_len - 1)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void str_append(char *dst, const char *src, int max_len) {
    int len = str_len(dst);
    int i = 0;

    while (src[i] != '\0' && (len + i) < (max_len - 1)) {
        dst[len + i] = src[i];
        ++i;
    }
    dst[len + i] = '\0';
}

char *skip_spaces(char *s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

char *next_token(char **cursor) {
    char *start = skip_spaces(*cursor);
    char *p;

    if (*start == '\0') {
        *cursor = start;
        return 0;
    }

    p = start;
    while (*p != '\0' && *p != ' ') {
        ++p;
    }

    if (*p != '\0') {
        *p = '\0';
        ++p;
    }

    *cursor = p;
    return start;
}

int point_in_rect(const struct rect *r, int x, int y) {
    return x >= r->x && x < (r->x + r->w) && y >= r->y && y < (r->y + r->h);
}

struct rect window_close_button(const struct rect *w) {
    struct rect close = {w->x + w->w - 14, w->y + 2, 10, 10};
    return close;
}

struct rect window_max_button(const struct rect *w) {
    struct rect max = {w->x + w->w - 26, w->y + 2, 10, 10};
    return max;
}

struct rect window_min_button(const struct rect *w) {
    struct rect min = {w->x + w->w - 38, w->y + 2, 10, 10};
    return min;
}

struct rect window_resize_grip(const struct rect *w) {
    struct rect grip = {w->x + w->w - 12, w->y + w->h - 12, 12, 12};
    return grip;
}
