#ifndef KERNEL_MICROKERNEL_AUDIO_H
#define KERNEL_MICROKERNEL_AUDIO_H

#include <stdint.h>
#include <sys/audioio.h>

struct mk_message;

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

struct mk_audio_info {
    uint32_t flags;
    audio_device_t device;
    struct audio_status status;
    struct audio_swpar parameters;
};

void mk_audio_service_init(void);
int mk_audio_service_ready(void);
int mk_audio_service_get_info(struct mk_audio_info *info);
int mk_audio_service_last_request(struct mk_message *message);

#endif
