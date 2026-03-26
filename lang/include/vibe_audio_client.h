#ifndef VIBE_LANG_VIBE_AUDIO_CLIENT_H
#define VIBE_LANG_VIBE_AUDIO_CLIENT_H

#include <stdint.h>

#define AUMODE_PLAY   0x01
#define AUMODE_RECORD 0x02

#define MAX_AUDIO_DEV_LEN 16

#define AUDIO_MIN_GAIN 0
#define AUDIO_MAX_GAIN 255

#define AUDIO_MIXER_CLASS 0
#define AUDIO_MIXER_ENUM  1
#define AUDIO_MIXER_SET   2
#define AUDIO_MIXER_VALUE 3

#define AUDIO_MIXER_LEVEL_MONO  0
#define AUDIO_MIXER_LEVEL_LEFT  0
#define AUDIO_MIXER_LEVEL_RIGHT 1

struct audio_swpar {
    unsigned int sig;
    unsigned int le;
    unsigned int bits;
    unsigned int bps;
    unsigned int msb;
    unsigned int rate;
    unsigned int pchan;
    unsigned int rchan;
    unsigned int nblks;
    unsigned int round;
    unsigned int _spare[6];
};

struct audio_status {
    int mode;
    int pause;
    int active;
    int _spare[5];
};

typedef struct audio_device {
    char name[MAX_AUDIO_DEV_LEN];
    char version[MAX_AUDIO_DEV_LEN];
    char config[MAX_AUDIO_DEV_LEN];
} audio_device_t;

typedef struct mixer_level {
    int num_channels;
    uint8_t level[8];
} mixer_level_t;

typedef struct mixer_ctrl {
    int dev;
    int type;
    union {
        int ord;
        int mask;
        mixer_level_t value;
    } un;
} mixer_ctrl_t;

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

#endif
