#ifndef VIBE_NETINET_IN_H
#define VIBE_NETINET_IN_H

#include <sys/socket.h>
#include <sys/types.h>

#ifndef _IN_TYPES_DEFINED_
#define _IN_TYPES_DEFINED_
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
#endif

static inline uint16_t __vibe_bswap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint32_t __vibe_bswap32(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

#ifndef htons
#define htons(x) __vibe_bswap16((uint16_t)(x))
#define ntohs(x) __vibe_bswap16((uint16_t)(x))
#define htonl(x) __vibe_bswap32((uint32_t)(x))
#define ntohl(x) __vibe_bswap32((uint32_t)(x))
#endif

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_ICMPV6 58
#define IPPROTO_RAW 255

#define IPPORT_RESERVED 1024
#define IPPORT_USERRESERVED 49151
#define IPPORT_HIFIRSTAUTO 49152
#define IPPORT_HILASTAUTO 65535

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    union {
        uint8_t __u6_addr8[16];
        uint16_t __u6_addr16[8];
        uint32_t __u6_addr32[4];
    } __u6_addr;
};

#define s6_addr __u6_addr.__u6_addr8
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32

#define INADDR_ANY ((in_addr_t)0x00000000u)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001u)
#define INADDR_BROADCAST ((in_addr_t)0xffffffffu)
#define INADDR_NONE ((in_addr_t)0xffffffffu)

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#define IN6ADDR_ANY_INIT {{{ 0 }}}
#define IN6ADDR_LOOPBACK_INIT {{{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }}}

struct sockaddr_in {
    uint8_t sin_len;
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    int8_t sin_zero[8];
};

struct sockaddr_in6 {
    uint8_t sin6_len;
    sa_family_t sin6_family;
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};

#define IP_OPTIONS 1
#define IP_HDRINCL 2
#define IP_TOS 3
#define IP_TTL 4
#define IP_MULTICAST_IF 9
#define IP_MULTICAST_TTL 10
#define IP_MULTICAST_LOOP 11
#define IP_ADD_MEMBERSHIP 12
#define IP_DROP_MEMBERSHIP 13
#define IP_PORTRANGE 19

#define IP_DEFAULT_MULTICAST_TTL 1
#define IP_DEFAULT_MULTICAST_LOOP 1

#define IP_PORTRANGE_DEFAULT 0
#define IP_PORTRANGE_HIGH 1
#define IP_PORTRANGE_LOW 2

#endif
