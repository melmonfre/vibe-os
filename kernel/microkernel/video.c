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

static uint32_t mk_video_current_pid(void) {
    process_t *current = scheduler_current();

    return current != 0 ? (uint32_t)current->pid : 0u;
}

static int mk_video_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_VIDEO);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
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
    case MK_MSG_VIDEO_FLIP:
        if (request->payload_size != 0u) {
            return -1;
        }
        kernel_video_flip();
        return mk_video_reply_result(reply, 0);
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
    uint32_t transfer_id;
    uint32_t text_length;

    if (text == 0) {
        return -1;
    }
    text_length = (uint32_t)strlen(text);
    if (mk_transfer_create(mk_video_current_pid(), text_length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, text, text_length + 1u) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.x = x;
    payload.y = y;
    payload.color = color;
    payload.text_length = text_length;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_TEXT, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return mk_video_decode_result(&reply);
}

int mk_video_service_flip(void) {
    struct mk_message request;
    struct mk_message reply;

    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_FLIP, 0, 0u) != 0) {
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
    if (mk_transfer_create(mk_video_current_pid(), MK_VIDEO_PALETTE_BYTES, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id,
                                type == MK_MSG_VIDEO_SET_PALETTE
                                    ? MK_TRANSFER_PERM_READ
                                    : MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (type == MK_MSG_VIDEO_SET_PALETTE &&
        mk_transfer_copy_from(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.transfer_id = transfer_id;
    payload.byte_count = MK_VIDEO_PALETTE_BYTES;
    if (mk_video_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    rc = mk_video_decode_result(&reply);
    if (type == MK_MSG_VIDEO_GET_PALETTE && rc == 0 &&
        mk_transfer_copy_to(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

int mk_video_service_set_palette(const uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_SET_PALETTE, (uint8_t *)(uintptr_t)rgb_triplets);
}

int mk_video_service_get_palette(uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_GET_PALETTE, rgb_triplets);
}

int mk_video_service_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_request payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    byte_count = (uint32_t)(src_w * src_h);
    if (mk_transfer_create(mk_video_current_pid(), byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
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
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return mk_video_decode_result(&reply);
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
