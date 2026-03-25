#ifndef VIBE_SYS_SOCKET_H
#define VIBE_SYS_SOCKET_H

#include <sys/types.h>
#include <sys/uio.h>

typedef unsigned int socklen_t;
typedef uint8_t sa_family_t;

/*
 * BSD socket constants mirrored into the VibeOS headers so extracted
 * services can preserve a familiar control-plane ABI.
 */
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_RDM       4
#define SOCK_SEQPACKET 5

#define SOCK_NONBLOCK  0x4000
#define SOCK_CLOEXEC   0x8000

#define SOL_SOCKET     0xffff

#define SO_DEBUG       0x0001
#define SO_ACCEPTCONN  0x0002
#define SO_REUSEADDR   0x0004
#define SO_KEEPALIVE   0x0008
#define SO_DONTROUTE   0x0010
#define SO_BROADCAST   0x0020
#define SO_LINGER      0x0080
#define SO_OOBINLINE   0x0100
#define SO_REUSEPORT   0x0200
#define SO_SNDBUF      0x1001
#define SO_RCVBUF      0x1002
#define SO_SNDTIMEO    0x1005
#define SO_RCVTIMEO    0x1006
#define SO_ERROR       0x1007
#define SO_TYPE        0x1008
#define SO_DOMAIN      0x1024
#define SO_PROTOCOL    0x1025

#define AF_UNSPEC      0
#define AF_UNIX        1
#define AF_LOCAL       AF_UNIX
#define AF_INET        2
#define AF_INET6       24
#define AF_LINK        18
#define AF_MAX         37

#define MSG_OOB        0x0001
#define MSG_PEEK       0x0002
#define MSG_DONTROUTE  0x0004
#define MSG_WAITALL    0x0040
#define MSG_NOSIGNAL   0x0800

struct linger {
    int l_onoff;
    int l_linger;
};

struct sockaddr {
    uint8_t sa_len;
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    uint8_t ss_len;
    sa_family_t ss_family;
    char __ss_padding[126];
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    socklen_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#endif
