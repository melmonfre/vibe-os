#ifndef KERNEL_MICROKERNEL_AUDIO_H
#define KERNEL_MICROKERNEL_AUDIO_H

#include <include/userland_api.h>
#include <stdint.h>
#include <sys/audioio.h>

struct mk_message;
struct process;

#define MK_AUDIO_INLINE_WRITE_MAX 240u
#define MK_AUDIO_INLINE_READ_MAX 252u
#define MK_AUDIO_ASSET_PATH_MAX 192u

enum mk_audio_capability_flags {
    MK_AUDIO_CAPS_QUERY_ONLY = 1u << 0,
    MK_AUDIO_CAPS_PLAYBACK = 1u << 1,
    MK_AUDIO_CAPS_CAPTURE = 1u << 2,
    MK_AUDIO_CAPS_MIXER = 1u << 3,
    MK_AUDIO_CAPS_BSD_AUDIOIO_ABI = 1u << 4
};

struct mk_audio_result {
    int32_t value;
};

struct mk_audio_transfer_request {
    uint32_t size;
    uint32_t transfer_id;
};

struct mk_audio_read_reply {
    uint32_t size;
    uint8_t data[MK_AUDIO_INLINE_READ_MAX];
};

struct mk_audio_write_request {
    uint32_t size;
    uint8_t data[MK_AUDIO_INLINE_WRITE_MAX];
};

struct mk_audio_play_asset_request {
    char path[MK_AUDIO_ASSET_PATH_MAX];
};

enum mk_audio_mixer_control_id {
    MK_AUDIO_MIXER_OUTPUT_LEVEL = 0,
    MK_AUDIO_MIXER_OUTPUT_MUTE = 1,
    MK_AUDIO_MIXER_INPUT_LEVEL = 2,
    MK_AUDIO_MIXER_INPUT_MUTE = 3,
    MK_AUDIO_MIXER_OUTPUT_DEFAULT = 4,
    MK_AUDIO_MIXER_INPUT_DEFAULT = 5
};

enum mk_audio_control_kind {
    MK_AUDIO_CONTROL_LEVEL = 1,
    MK_AUDIO_CONTROL_TOGGLE = 2,
    MK_AUDIO_CONTROL_ENUM = 3
};

struct mk_audio_control_info {
    uint32_t control_id;
    uint32_t kind;
    uint32_t pair_id;
    uint32_t reserved;
    char group[MAX_AUDIO_DEV_LEN];
    char name[MAX_AUDIO_DEV_LEN];
};

struct mk_audio_info {
    uint32_t flags;
    audio_device_t device;
    struct audio_status status;
    struct audio_swpar parameters;
    uint32_t controller_pci_id;
    uint32_t controller_location;
    uint32_t codec_vendor_id;
    uint32_t output_route;
};

void mk_audio_service_init(void);
int mk_audio_service_ready(void);
int mk_audio_service_get_info(struct mk_audio_info *info);
int mk_audio_service_get_status(struct audio_status *status);
int mk_audio_service_set_params(const struct audio_swpar *params);
int mk_audio_service_start(void);
int mk_audio_service_stop(void);
int mk_audio_service_write(const void *data, uint32_t size);
int mk_audio_service_write_direct(const void *data, uint32_t size);
int mk_audio_service_write_async(const void *data, uint32_t size);
int mk_audio_service_play_asset(const char *path);
int mk_audio_service_subscribe(struct process *subscriber);
int mk_audio_service_event_receive(struct process *subscriber,
                                   struct mk_audio_event *event,
                                   uint32_t timeout_ticks);
void mk_audio_service_pump_async(void);
int mk_audio_service_read(void *data, uint32_t size);
int mk_audio_service_get_control_info(uint32_t index, struct mk_audio_control_info *info);
int mk_audio_service_mixer_read(mixer_ctrl_t *control);
int mk_audio_service_mixer_write(const mixer_ctrl_t *control);
int mk_audio_service_last_request(struct mk_message *message);

#endif
