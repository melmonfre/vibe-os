/*
 * netdb.c — the tree's only resolver entry points.
 *
 * gethostbyname() is REAL for numeric dotted-quad input (which is what
 * the P3 HTTP gates use) and an explicit ENOSYS diagnostic for symbolic
 * names: the OS resolver lives in the netmgrd host pool /runtime/resolv.
 * conf + the UDP path that the `host`/`dig` apps use (dns_client.h), and
 * a libc-level bridge to it is pending the fs-pool unification decision
 * (KNOWN_BUGS #29). Links (2)'s own dns.c numeric fast-path keeps working;
 * symbolic lookups through plain libc fail loudly, never silently.
 */
#include <stdint.h>
#include <stddef.h>
#include <compat/posix/errno.h>
#include <string.h>

#define NETDB_AF_INET 2

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};



static char *h_aliases_null[1] = { 0 };
static char *h_addr_ptrs[2] = { 0, 0 };
static unsigned char h_addr_bytes[4];
static char h_name_buf[32];
static struct hostent h_result;

static int parse_ipv4(const char *name, unsigned char out[4]) {
    int octet = 0, digits = 0, i;
    unsigned int val = 0;
    const char *p = name;

    for (;;) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10u + (unsigned int)(*p - '0');
            ++digits;
            if (val > 255u || digits > 3) {
                return 0;
            }
            ++p;
            continue;
        }
        if (*p == '.' && octet < 3) {
            if (digits == 0) {
                return 0;
            }
            out[octet++] = (unsigned char)val;
            val = 0;
            digits = 0;
            ++p;
            continue;
        }
        break;
    }
    if (octet != 3 || digits == 0 || *p != '\0') {
        return 0;
    }
    out[3] = (unsigned char)val;
    (void)i;
    return 1;
}

static void netdb_debug(const char *msg) {
    long ret;
    /* SYSCALL_WRITE_DEBUG = 11 (headers/include/userland_api.h) */
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(11L), "b"((long)(uintptr_t)msg)
                     : "memory", "cc");
}

struct hostent *gethostbyname(const char *name) {
    unsigned char bytes[4];

    if (name == 0 || name[0] == '\0') {
        errno = EINVAL;
        return 0;
    }
    if (parse_ipv4(name, bytes)) {
        int i;
        for (i = 0; i < 4; ++i) {
            h_addr_bytes[i] = bytes[i];
        }
        h_addr_ptrs[0] = (char *)h_addr_bytes;
        h_addr_ptrs[1] = 0;
        memset(h_name_buf, 0, sizeof(h_name_buf));
        for (i = 0; name[i] != '\0' && i < 31; ++i) {
            h_name_buf[i] = name[i];
        }
        h_result.h_name = h_name_buf;
        h_result.h_aliases = h_aliases_null;
        h_result.h_addrtype = NETDB_AF_INET;
        h_result.h_length = 4;
        h_result.h_addr_list = h_addr_ptrs;
        return &h_result;
    }
    netdb_debug("compat: gethostbyname: symbolic names unsupported (use IP; see dig/host apps) [ENOSYS]\n");
    errno = ENOSYS;
    return 0;
}
