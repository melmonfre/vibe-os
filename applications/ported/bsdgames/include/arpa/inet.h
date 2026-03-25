#ifndef VIBE_BSDGAME_ARPA_INET_H
#define VIBE_BSDGAME_ARPA_INET_H

#include <compat_defs.h>
#include <stdint.h>

static inline uint16_t bsdgame_bswap16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

static inline uint32_t bsdgame_bswap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

#define htons(x) bsdgame_bswap16((uint16_t)(x))
#define ntohs(x) bsdgame_bswap16((uint16_t)(x))
#define htonl(x) bsdgame_bswap32((uint32_t)(x))
#define ntohl(x) bsdgame_bswap32((uint32_t)(x))

#endif
