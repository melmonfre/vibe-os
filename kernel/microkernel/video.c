#include <include/userland_api.h>
#include <kernel/drivers/video/video.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/microkernel/video.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

#define MK_VIDEO_PALETTE_BYTES 768u
#define MK_VIDEO_UPLOAD_CACHE_SLOTS 4u
#define MK_VIDEO_PALETTE_CACHE_SLOTS 4u

struct mk_video_upload_cache {
    uint32_t owner_pid;
    uint32_t transfer_id;
    uint32_t capacity;
};

struct mk_video_palette_cache {
    uint32_t owner_pid;
    uint32_t transfer_id;
};

static struct mk_video_upload_cache g_video_upload_cache[MK_VIDEO_UPLOAD_CACHE_SLOTS];
static struct mk_video_palette_cache g_video_palette_cache[MK_VIDEO_PALETTE_CACHE_SLOTS];

static uint32_t mk_video_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_video_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_VIDEO);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static struct mk_video_upload_cache *mk_video_find_upload_cache(uint32_t owner_pid) {
    uint32_t i;

    for (i = 0; i < MK_VIDEO_UPLOAD_CACHE_SLOTS; ++i) {
        if (g_video_upload_cache[i].transfer_id != 0u &&
            g_video_upload_cache[i].owner_pid == owner_pid) {
            return &g_video_upload_cache[i];
        }
    }
    for (i = 0; i < MK_VIDEO_UPLOAD_CACHE_SLOTS; ++i) {
        if (g_video_upload_cache[i].transfer_id == 0u) {
            return &g_video_upload_cache[i];
        }
    }
    return 0;
}

static struct mk_video_palette_cache *mk_video_find_palette_cache(uint32_t owner_pid) {
    uint32_t i;

    for (i = 0; i < MK_VIDEO_PALETTE_CACHE_SLOTS; ++i) {
        if (g_video_palette_cache[i].transfer_id != 0u &&
            g_video_palette_cache[i].owner_pid == owner_pid) {
            return &g_video_palette_cache[i];
        }
    }
    for (i = 0; i < MK_VIDEO_PALETTE_CACHE_SLOTS; ++i) {
        if (g_video_palette_cache[i].transfer_id == 0u) {
            return &g_video_palette_cache[i];
        }
    }
    return 0;
}

static int mk_video_get_upload_transfer(uint32_t byte_count, uint32_t *transfer_id_out) {
    struct mk_video_upload_cache *cache;
    uint32_t owner_pid;

    if (transfer_id_out == 0 || byte_count == 0u) {
        return -1;
    }

    owner_pid = mk_video_current_pid();
    cache = mk_video_find_upload_cache(owner_pid);
    if (cache == 0) {
        return -1;
    }

    if (cache->transfer_id != 0u && cache->owner_pid == owner_pid && cache->capacity >= byte_count) {
        *transfer_id_out = cache->transfer_id;
        return 0;
    }

    if (cache->transfer_id != 0u && cache->owner_pid == owner_pid) {
        if (mk_transfer_destroy(cache->transfer_id) != 0) {
            return -1;
        }
        cache->transfer_id = 0u;
        cache->capacity = 0u;
    }

    if (cache->transfer_id == 0u) {
        if (mk_transfer_create(owner_pid, byte_count, &cache->transfer_id) != 0) {
            return -1;
        }
        cache->owner_pid = owner_pid;
        cache->capacity = byte_count;
    }

    *transfer_id_out = cache->transfer_id;
    return 0;
}

static int mk_video_get_palette_transfer(uint32_t *transfer_id_out) {
    struct mk_video_palette_cache *cache;
    uint32_t owner_pid;

    if (transfer_id_out == 0) {
        return -1;
    }

    owner_pid = mk_video_current_pid();
    cache = mk_video_find_palette_cache(owner_pid);
    if (cache == 0) {
        return -1;
    }

    if (cache->transfer_id != 0u) {
        if (cache->owner_pid == owner_pid) {
            *transfer_id_out = cache->transfer_id;
            return 0;
        }
        if (mk_transfer_destroy(cache->transfer_id) != 0) {
            return -1;
        }
        cache->transfer_id = 0u;
    }

    if (cache->transfer_id == 0u) {
        if (mk_transfer_create(owner_pid, MK_VIDEO_PALETTE_BYTES, &cache->transfer_id) != 0) {
            return -1;
        }
        cache->owner_pid = owner_pid;
    }

    *transfer_id_out = cache->transfer_id;
    return 0;
}

static int mk_video_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;
    process_t *current;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_VIDEO);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    current = scheduler_current();
    message->source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_video_reply_result(struct mk_message *reply, int value) {
    struct mk_video_result result;

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_video_decode_result(const struct mk_message *reply) {
    struct mk_video_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_video_validate_text_transfer(uint32_t transfer_id,
                                           uint32_t text_length,
                                           char **text_out) {
    char *text;

    if (text_out == 0 || text_length == 0u) {
        return -1;
    }

    text = (char *)mk_transfer_data_read(transfer_id);
    if (text == 0 || mk_transfer_size(transfer_id) < text_length + 1u) {
        return -1;
    }
    if (text[text_length] != '\0') {
        return -1;
    }

    *text_out = text;
    return 0;
}

static int mk_video_reply_mode(struct mk_message *reply,
                               int value,
                               const struct video_mode *mode) {
    struct mk_video_mode_reply payload;

    memset(&payload, 0, sizeof(payload));
    payload.value = value;
    if (mode != 0) {
        payload.mode = *mode;
    }
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_video_decode_mode(const struct mk_message *reply, struct video_mode *mode) {
    struct mk_video_mode_reply payload;

    if (reply == 0 || mode == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }
    memcpy(&payload, reply->payload, sizeof(payload));
    *mode = payload.mode;
    return payload.value;
}

static int mk_video_reply_caps(struct mk_message *reply,
                               int value,
                               const struct video_capabilities *caps) {
    struct mk_video_caps_reply payload;

    memset(&payload, 0, sizeof(payload));
    payload.value = value;
    if (caps != 0) {
        payload.caps = *caps;
    }
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_video_decode_caps(const struct mk_message *reply, struct video_capabilities *caps) {
    struct mk_video_caps_reply payload;

    if (reply == 0 || caps == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }
    memcpy(&payload, reply->payload, sizeof(payload));
    *caps = payload.caps;
    return payload.value;
}

static int mk_video_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_VIDEO_CLEAR: {
        const struct mk_video_color_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_color_request *)request->payload;
        kernel_video_clear((uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_RECT: {
        const struct mk_video_rect_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_rect_request *)request->payload;
        kernel_gfx_rect(payload->x, payload->y, payload->width, payload->height,
                        (uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_TEXT: {
        const struct mk_video_text_request *payload;
        char *text;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_request *)request->payload;
        if (mk_video_validate_text_transfer(payload->transfer_id, payload->text_length, &text) != 0) {
            return -1;
        }
        kernel_gfx_draw_text(payload->x, payload->y, text, (uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_TEXT_INLINE: {
        const struct mk_video_text_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_inline_request *)request->payload;
        if (payload->text_length == 0u ||
            payload->text_length >= MK_VIDEO_INLINE_TEXT_MAX ||
            payload->text[payload->text_length] != '\0') {
            return -1;
        }
        kernel_gfx_draw_text(payload->x,
                             payload->y,
                             payload->text,
                             (uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_FLIP:
        if (request->payload_size == 0u) {
            kernel_video_flip();
            return mk_video_reply_result(reply, 0);
        }
        if (request->payload_size == sizeof(struct mk_video_present_request)) {
            const struct mk_video_present_request *payload =
                (const struct mk_video_present_request *)request->payload;
            kernel_video_flip_mode(payload->mode);
            return mk_video_reply_result(reply, 0);
        }
        return -1;
    case MK_MSG_VIDEO_LEAVE:
        if (request->payload_size != 0u) {
            return -1;
        }
        kernel_video_leave_graphics();
        return mk_video_reply_result(reply, 0);
    case MK_MSG_VIDEO_BLIT8: {
        const struct mk_video_blit8_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0 || mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        kernel_gfx_blit8(src,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT: {
        const struct mk_video_blit8_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0 || mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        kernel_gfx_blit8_present(src,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->scale);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8(payload->pixels,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8_present(payload->pixels,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->scale);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT: {
        const struct mk_video_blit8_stretch_present_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0 || mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        kernel_gfx_blit8_stretch_present(src,
                                         payload->src_width,
                                         payload->src_height,
                                         payload->dst_x,
                                         payload->dst_y,
                                         payload->dst_width,
                                         payload->dst_height);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH: {
        const struct mk_video_blit8_stretch_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0 || mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        kernel_gfx_blit8_stretch(src,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_INLINE: {
        const struct mk_video_blit8_stretch_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8_stretch(payload->pixels,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE: {
        const struct mk_video_blit8_stretch_present_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8_stretch_present(payload->pixels,
                                         payload->src_width,
                                         payload->src_height,
                                         payload->dst_x,
                                         payload->dst_y,
                                         payload->dst_width,
                                         payload->dst_height);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_MODE_SET: {
        const struct mk_video_mode_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_mode_request *)request->payload;
        return mk_video_reply_result(reply, kernel_video_set_mode(payload->width, payload->height));
    }
    case MK_MSG_VIDEO_SET_PALETTE: {
        const struct mk_video_palette_request *payload;
        const uint8_t *palette;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        palette = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (palette == 0 || payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        return mk_video_reply_result(reply, kernel_video_set_palette(palette));
    }
    case MK_MSG_VIDEO_GET_PALETTE: {
        const struct mk_video_palette_request *payload;
        uint8_t *palette;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        palette = (uint8_t *)mk_transfer_data_write(payload->transfer_id);
        if (palette == 0 || payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        rc = kernel_video_get_palette(palette);
        return mk_video_reply_result(reply, rc);
    }
    case MK_MSG_VIDEO_GET_INFO: {
        struct video_mode *mode;

        if (request->payload_size != 0u) {
            return -1;
        }
        mode = kernel_video_get_mode();
        return mk_video_reply_mode(reply, 0, mode);
    }
    case MK_MSG_VIDEO_GET_CAPS: {
        struct video_capabilities caps;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&caps, 0, sizeof(caps));
        kernel_video_get_capabilities(&caps);
        return mk_video_reply_caps(reply, 0, &caps);
    }
    default:
        return -1;
    }
}

void mk_video_service_init(void) {
    (void)mk_service_launch_task(MK_SERVICE_VIDEO,
                                 "video",
                                 mk_video_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN |
                                 MK_LAUNCH_FLAG_CRITICAL);
}

int mk_video_service_clear(uint8_t color) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_color_request payload;

    payload.color = color;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_CLEAR, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_rect(int x, int y, int w, int h, uint8_t color) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_rect_request payload;

    payload.x = x;
    payload.y = y;
    payload.width = w;
    payload.height = h;
    payload.color = color;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_RECT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_text(int x, int y, uint8_t color, const char *text) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_text_request payload;
    struct mk_video_text_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t text_length;

    if (text == 0) {
        return -1;
    }
    text_length = (uint32_t)strlen(text);
    if (text_length != 0u && text_length < MK_VIDEO_INLINE_TEXT_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.x = x;
        inline_payload.y = y;
        inline_payload.color = color;
        inline_payload.text_length = text_length;
        memcpy(inline_payload.text, text, text_length + 1u);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_TEXT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(text_length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, text, text_length + 1u) != 0) {
        return -1;
    }

    payload.x = x;
    payload.y = y;
    payload.color = color;
    payload.text_length = text_length;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_TEXT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_flip(void) {
    return mk_video_service_flip_mode(VIDEO_PRESENT_AUTO);
}

int mk_video_service_flip_mode(uint32_t mode) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_present_request payload;

    payload.mode = mode;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_FLIP, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_leave_graphics(void) {
    struct mk_message request;
    struct mk_message reply;

    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_LEAVE, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_set_mode(uint32_t width, uint32_t height) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_mode_request payload;

    payload.width = width;
    payload.height = height;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_MODE_SET, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

static int mk_video_palette_request_common(uint32_t type, uint8_t *palette) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_palette_request payload;
    uint32_t transfer_id;
    int rc;

    if (palette == 0) {
        return -1;
    }
    if (mk_video_get_palette_transfer(&transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id,
                                type == MK_MSG_VIDEO_SET_PALETTE
                                    ? MK_TRANSFER_PERM_READ
                                    : MK_TRANSFER_PERM_WRITE) != 0) {
        return -1;
    }
    if (type == MK_MSG_VIDEO_SET_PALETTE &&
        mk_transfer_copy_from(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        return -1;
    }

    payload.transfer_id = transfer_id;
    payload.byte_count = MK_VIDEO_PALETTE_BYTES;
    if (mk_video_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }

    rc = mk_video_decode_result(&reply);
    if (type == MK_MSG_VIDEO_GET_PALETTE && rc == 0 &&
        mk_transfer_copy_to(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        return -1;
    }
    return rc;
}

int mk_video_service_set_palette(const uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_SET_PALETTE, (uint8_t *)(uintptr_t)rgb_triplets);
}

int mk_video_service_get_palette(uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_GET_PALETTE, rgb_triplets);
}

int mk_video_service_blit8_transfer(uint32_t transfer_id, uint32_t byte_count,
                                    int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_request payload;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.scale = scale;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_present_transfer(uint32_t transfer_id, uint32_t byte_count,
                                            int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_request payload;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.scale = scale;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_PRESENT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    byte_count = (uint32_t)(src_w * src_h);
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX && scale > 0) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.scale = scale;
        inline_payload.byte_count = byte_count;
        memcpy(inline_payload.pixels, src, byte_count);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_transfer(transfer_id, byte_count, src_w, src_h, dst_x, dst_y, scale);
}

int mk_video_service_blit8_present(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    byte_count = (uint32_t)(src_w * src_h);
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX && scale > 0) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.scale = scale;
        inline_payload.byte_count = byte_count;
        memcpy(inline_payload.pixels, src, byte_count);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_PRESENT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_present_transfer(transfer_id,
                                                   byte_count,
                                                   src_w,
                                                   src_h,
                                                   dst_x,
                                                   dst_y,
                                                   scale);
}

int mk_video_service_blit8_stretch_transfer(uint32_t transfer_id, uint32_t byte_count,
                                            int src_w, int src_h,
                                            int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_request payload;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.dst_width = dst_w;
    payload.dst_height = dst_h;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_STRETCH, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    byte_count = (uint32_t)(src_w * src_h);
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.dst_width = dst_w;
        inline_payload.dst_height = dst_h;
        inline_payload.byte_count = byte_count;
        memcpy(inline_payload.pixels, src, byte_count);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_STRETCH_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }
    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_stretch_transfer(transfer_id, byte_count,
                                                   src_w, src_h,
                                                   dst_x, dst_y, dst_w, dst_h);
}

int mk_video_service_blit8_stretch_present_transfer(uint32_t transfer_id, uint32_t byte_count,
                                                    int src_w, int src_h,
                                                    int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_present_request payload;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.dst_width = dst_w;
    payload.dst_height = dst_h;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT,
                                 &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                           int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_present_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    byte_count = (uint32_t)(src_w * src_h);
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.dst_width = dst_w;
        inline_payload.dst_height = dst_h;
        inline_payload.byte_count = byte_count;
        memcpy(inline_payload.pixels, src, byte_count);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }
    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_stretch_present_transfer(transfer_id, byte_count,
                                                           src_w, src_h,
                                                           dst_x, dst_y, dst_w, dst_h);
}

int mk_video_service_get_info(struct video_mode *mode) {
    struct mk_message request;
    struct mk_message reply;

    if (mode == 0) {
        return -1;
    }
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_GET_INFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_mode(&reply, mode);
}

int mk_video_service_get_caps(struct video_capabilities *caps) {
    struct mk_message request;
    struct mk_message reply;

    if (caps == 0) {
        return -1;
    }
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_GET_CAPS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_caps(&reply, caps);
}
