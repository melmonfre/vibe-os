#ifndef VIBE_NET_IF_H
#define VIBE_NET_IF_H

#include <sys/types.h>

#define IF_NAMESIZE 16
#define IFNAMSIZ IF_NAMESIZE
#define IFDESCRSIZE 64

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_DEBUG 0x4
#define IFF_LOOPBACK 0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING 0x40
#define IFF_NOARP 0x80
#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200
#define IFF_OACTIVE 0x400
#define IFF_SIMPLEX 0x800
#define IFF_LINK0 0x1000
#define IFF_LINK1 0x2000
#define IFF_LINK2 0x4000
#define IFF_MULTICAST 0x8000

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int ifindex, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#endif
