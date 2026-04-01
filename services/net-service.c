/*
 * Vibe-OS network micro-service
 * Stripped from compat/sys/dev/pci/if_rl_pci.c (RTL8139)
 * Provides pure user-space driver via mailbox
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "net-msg.h"
#include "common.h"

static volatile uint8_t *iobase;

/* --- minimal RTL8139 emulation stubs ------------------------------------ */
static inline uint32_t inl(uint16_t port) {
    return *((volatile uint32_t*)(iobase + port));
}
static inline void outl(uint16_t port, uint32_t val) {
    *((volatile uint32_t*)(iobase + port)) = val;
}

static void rtl_reset(void) {
    outl(0x37, 0x10); /* soft reset */
    while ((inl(0x37) & 0x10) != 0)
        ;
}

static int rtl_init(void) {
    rtl_reset();
    return 0;
}

/* --- mailbox ------------------------------------------------------------- */

static int net_rpc(net_msg_t *msg, net_reply_t *reply) {
    switch (msg->cmd) {
        case NET_CMD_OPEN:
            rtl_init();
            reply->result = 0;
            break;
        case NET_CMD_SEND:
            /* pretend to actually DMA a packet */
            reply->result = msg->len;
            break;
        case NET_CMD_RECV:
            /* drop everything – for now always returns nothing */
            reply->result = 0;
            reply->len    = 0;
            break;
        default:
            reply->result = -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem for net");
        return 1;
    }
    iobase = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0x9000 /* traditional I/O port memory mirror */);
    if (iobase == MAP_FAILED) {
        perror("mmap net BAR");
        return 1;
    }

    printf("[net-service] ready on mailbox\n");
    while (1) {
        net_msg_t req = {0};
        net_reply_t rep = {0};
        if (recv_incoming(&req)) continue;
        net_rpc(&req, &rep);
        send_reply(&rep);
    }
    return 0;
}