#ifndef _NET_IF_H_
#define _NET_IF_H_

#define IFNAMSIZ 16

struct sockaddr;

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr *ifru_addr;
        struct sockaddr *ifru_dstaddr;
        struct sockaddr *ifru_broadaddr;
        struct sockaddr *ifru_netmask;
        short ifru_flags;
        int ifru_metric;
        void *ifru_data;
    } ifr_ifru;
};

#define ifr_addr       ifr_ifru.ifru_addr
#define ifr_dstaddr    ifr_ifru.ifru_dstaddr
#define ifr_broadaddr  ifr_ifru.ifru_broadaddr
#define ifr_netmask    ifr_ifru.ifru_netmask
#define ifr_flags      ifr_ifru.ifru_flags
#define ifr_metric     ifr_ifru.ifru_metric
#define ifr_data       ifr_ifru.ifru_data

struct ifconf {
    int ifc_len;
    union {
        void *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

#endif
