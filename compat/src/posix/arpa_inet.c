#include <compat/posix/errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define INADDR_NONE ((in_addr_t)0xffffffffu)
#define INET_ADDRSTRLEN 16
#define AF_INET 2

static int compat_inet_pton4(const char *src, unsigned char *dst) {
    int saw_digit = 0;
    int octets = 0;
    unsigned int val = 0;

    if (src == 0 || dst == 0) return -1;

    while (*src != '\0') {
        if (*src >= '0' && *src <= '9') {
            val = val * 10u + (unsigned int)(*src - '0');
            if (val > 255u) return -1;
            saw_digit = 1;
        } else if (*src == '.') {
            if (!saw_digit || octets >= 3) return -1;
            dst[octets++] = (unsigned char)val;
            val = 0;
            saw_digit = 0;
        } else {
            return -1;
        }
        ++src;
    }
    if (!saw_digit || octets != 3) return -1;
    dst[octets] = (unsigned char)val;
    return 1;
}

int inet_aton(const char *cp, struct in_addr *inp) {
    unsigned char bytes[4];

    if (cp == 0) return 0;
    if (compat_inet_pton4(cp, bytes) != 1) return 0;
    if (inp != 0) {
        inp->s_addr = ((unsigned long)bytes[0] << 24) |
                      ((unsigned long)bytes[1] << 16) |
                      ((unsigned long)bytes[2] << 8) |
                      (unsigned long)bytes[3];
    }
    return 1;
}

in_addr_t inet_addr(const char *cp) {
    struct in_addr addr;

    if (inet_aton(cp, &addr) == 0) return INADDR_NONE;
    return addr.s_addr;
}

char *inet_ntoa(struct in_addr in) {
    static char buf[INET_ADDRSTRLEN];
    unsigned long a = (unsigned long)in.s_addr;
    unsigned char b0 = (unsigned char)(a >> 24);
    unsigned char b1 = (unsigned char)(a >> 16);
    unsigned char b2 = (unsigned char)(a >> 8);
    unsigned char b3 = (unsigned char)(a);

    sprintf(buf, "%u.%u.%u.%u",
            (unsigned int)b0, (unsigned int)b1,
            (unsigned int)b2, (unsigned int)b3);
    return buf;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (src == 0 || dst == 0) {
        errno = EINVAL;
        return -1;
    }
    return compat_inet_pton4(src, (unsigned char *)dst);
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    const unsigned char *bytes;

    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return 0;
    }
    if (src == 0 || dst == 0) {
        errno = EINVAL;
        return 0;
    }
    bytes = (const unsigned char *)src;
    if (size < (socklen_t)INET_ADDRSTRLEN) {
        errno = ENOSPC;
        return 0;
    }
    sprintf(dst, "%u.%u.%u.%u",
            (unsigned int)bytes[0], (unsigned int)bytes[1],
            (unsigned int)bytes[2], (unsigned int)bytes[3]);
    return dst;
}

/*
 * Byte-order primitives: declared in the POSIX headers since the first
 * import but never defined anywhere in the tree — the only users so far
 * were the DNS client apps that open-coded the swaps. links2's connect.c
 * hits them directly. Host is little-endian i386.
 *
 * headers/netinet/in.h (kernel-tree side) provides these as macros;
 * userspace headers that declare plain functions need the real symbols,
 * so drop the macro names before defining them.
 */
#undef htons
#undef ntohs
#undef htonl
#undef ntohl

unsigned short htons(unsigned short x) {
    return (unsigned short)((x >> 8) | (x << 8));
}

unsigned short ntohs(unsigned short x) {
    return htons(x);
}

unsigned int htonl(unsigned int x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

unsigned int ntohl(unsigned int x) {
    return htonl(x);
}
