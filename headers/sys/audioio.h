#ifndef VIBE_SYS_AUDIOIO_H
#define VIBE_SYS_AUDIOIO_H

#include <stdint.h>
#ifndef __VIBE_KERNEL__
#include <string.h>
#endif

#define AUMODE_PLAY   0x01
#define AUMODE_RECORD 0x02

#define AUDIO_INITPAR(p) \
    (void)memset((void *)(p), 0xff, sizeof(struct audio_swpar))

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

#define MAX_AUDIO_DEV_LEN 16

typedef struct audio_device {
    char name[MAX_AUDIO_DEV_LEN];
    char version[MAX_AUDIO_DEV_LEN];
    char config[MAX_AUDIO_DEV_LEN];
} audio_device_t;

struct audio_pos {
    unsigned int play_pos;
    unsigned int play_xrun;
    unsigned int rec_pos;
    unsigned int rec_xrun;
};

#define AUDIO_MIN_GAIN 0
#define AUDIO_MAX_GAIN 255

typedef struct mixer_level {
    int num_channels;
    uint8_t level[8];
} mixer_level_t;

#define AUDIO_MIXER_LEVEL_MONO  0
#define AUDIO_MIXER_LEVEL_LEFT  0
#define AUDIO_MIXER_LEVEL_RIGHT 1

typedef struct audio_mixer_name {
    char name[MAX_AUDIO_DEV_LEN];
    int msg_id;
} audio_mixer_name_t;

typedef struct mixer_devinfo {
    int index;
    audio_mixer_name_t label;
    int type;
#define AUDIO_MIXER_CLASS 0
#define AUDIO_MIXER_ENUM  1
#define AUDIO_MIXER_SET   2
#define AUDIO_MIXER_VALUE 3
    int mixer_class;
    int next;
    int prev;
#define AUDIO_MIXER_LAST -1
    union {
        struct {
            int num_mem;
            struct {
                audio_mixer_name_t label;
                int ord;
            } member[32];
        } e;
        struct {
            int num_mem;
            struct {
                audio_mixer_name_t label;
                int mask;
            } member[32];
        } s;
        struct {
            audio_mixer_name_t units;
            int num_channels;
            int delta;
        } v;
    } un;
} mixer_devinfo_t;

typedef struct mixer_ctrl {
    int dev;
    int type;
    union {
        int ord;
        int mask;
        mixer_level_t value;
    } un;
} mixer_ctrl_t;

#define AudioNmicrophone "mic"
#define AudioNline       "line"
#define AudioNcd         "cd"
#define AudioNdac        "dac"
#define AudioNaux        "aux"
#define AudioNrecord     "record"
#define AudioNvolume     "volume"
#define AudioNmonitor    "monitor"
#define AudioNspeaker    "spkr"
#define AudioNheadphone  "hp"
#define AudioNoutput     "output"
#define AudioNinput      "input"
#define AudioNmaster     "master"
#define AudioNmute       "mute"
#define AudioNmode       "mode"
#define AudioNsource     "source"
#define AudioNvideo      "video"

#endif
