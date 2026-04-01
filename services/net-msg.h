#ifndef NET_MSG_H
#define NET_MSG_H

enum net_cmd {
    NET_CMD_OPEN     = 0,
    NET_CMD_CLOSE    = 1,
    NET_CMD_SEND     = 2,
    NET_CMD_RECV     = 3,
};

typedef struct {
    enum net_cmd cmd;
    uint32_t buf_phys;
    uint32_t len;
    uint8_t dst_mac[6];
} net_msg_t;

typedef struct {
    int32_t result;
    uint32_t len;
} net_reply_t;

#endif