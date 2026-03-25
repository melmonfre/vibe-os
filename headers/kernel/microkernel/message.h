#ifndef KERNEL_MICROKERNEL_MESSAGE_H
#define KERNEL_MICROKERNEL_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#define MK_MESSAGE_ABI_VERSION 1u
#define MK_MESSAGE_PAYLOAD_MAX 256u

enum mk_message_type {
    MK_MSG_NONE = 0,
    MK_MSG_HELLO = 1,
    MK_MSG_SERVICE_REGISTER = 2,
    MK_MSG_SERVICE_LOOKUP = 3,
    MK_MSG_BLOCK_READ = 16,
    MK_MSG_BLOCK_WRITE = 17,
    MK_MSG_BLOCK_INFO = 18,
    MK_MSG_FS_OPEN = 32,
    MK_MSG_FS_READ = 33,
    MK_MSG_FS_WRITE = 34,
    MK_MSG_FS_CLOSE = 35,
    MK_MSG_FS_LSEEK = 36,
    MK_MSG_FS_STAT = 37,
    MK_MSG_FS_FSTAT = 38,
    MK_MSG_CONSOLE_WRITE_DEBUG = 40,
    MK_MSG_CONSOLE_TEXT_CLEAR = 41,
    MK_MSG_CONSOLE_TEXT_PUTC = 42,
    MK_MSG_CONSOLE_CURSOR_MOVE = 43,
    MK_MSG_CONSOLE_TEXT_WRITE = 44,
    MK_MSG_VIDEO_MODE_SET = 48,
    MK_MSG_VIDEO_CLEAR = 49,
    MK_MSG_VIDEO_RECT = 50,
    MK_MSG_VIDEO_TEXT = 51,
    MK_MSG_VIDEO_FLIP = 52,
    MK_MSG_VIDEO_LEAVE = 53,
    MK_MSG_VIDEO_BLIT8 = 54,
    MK_MSG_VIDEO_SET_PALETTE = 55,
    MK_MSG_VIDEO_GET_PALETTE = 56,
    MK_MSG_VIDEO_GET_INFO = 57,
    MK_MSG_VIDEO_GET_CAPS = 58,
    MK_MSG_INPUT_EVENT = 64,
    MK_MSG_INPUT_MOUSE_POLL = 65,
    MK_MSG_INPUT_KEY_READ = 66,
    MK_MSG_INPUT_SET_LAYOUT = 67,
    MK_MSG_INPUT_GET_LAYOUT = 68,
    MK_MSG_INPUT_GET_AVAILABLE_LAYOUTS = 69,
    MK_MSG_NET_SOCKET = 80,
    MK_MSG_NET_BIND = 81,
    MK_MSG_NET_CONNECT = 82,
    MK_MSG_NET_SEND = 83,
    MK_MSG_NET_RECV = 84,
    MK_MSG_NET_SETSOCKOPT = 85,
    MK_MSG_NET_GETSOCKOPT = 86,
    MK_MSG_NET_GETINFO = 87,
    MK_MSG_AUDIO_GETINFO = 96,
    MK_MSG_AUDIO_GET_STATUS = 97,
    MK_MSG_AUDIO_GET_PARAMS = 98,
    MK_MSG_AUDIO_SET_PARAMS = 99,
    MK_MSG_AUDIO_START = 100,
    MK_MSG_AUDIO_STOP = 101,
    MK_MSG_AUDIO_WRITE = 102,
    MK_MSG_AUDIO_READ = 103,
    MK_MSG_AUDIO_MIXER_READ = 104,
    MK_MSG_AUDIO_MIXER_WRITE = 105
};

struct mk_message {
    uint32_t abi_version;
    uint32_t type;
    uint32_t source_pid;
    uint32_t target_pid;
    uint32_t payload_size;
    uint8_t payload[MK_MESSAGE_PAYLOAD_MAX];
};

void mk_message_init(struct mk_message *message, uint32_t type);
int mk_message_set_payload(struct mk_message *message, const void *payload, size_t payload_size);

#endif
