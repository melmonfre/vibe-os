#ifndef AUDIO_MSG_H
#define AUDIO_MSG_H

/* Message protocol for user-space audio service (mailbox) */

enum audio_cmd {
    AUDIO_CMD_OPEN  = 0,
    AUDIO_CMD_CLOSE = 1,
    AUDIO_CMD_PLAY  = 2,
    AUDIO_CMD_RECORD = 3,
    AUDIO_CMD_SET_VOL = 4,
    AUDIO_CMD_GET_VOL = 5,
};

typedef struct {
    enum audio_cmd cmd;
    uint32_t pcm_buf_phys; /* user-supplied physical address */
    uint32_t pcm_len;
    uint32_t left_vol :8;
    uint32_t right_vol:8;
} audio_msg_t;

typedef struct {
    int32_t result; /* 0 = OK, neg error */
} audio_reply_t;

#endif