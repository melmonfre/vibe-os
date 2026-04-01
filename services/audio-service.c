/*
 * Vibe-OS audio micro-service
 * Extracted from compat/sys/dev/pci/auvia.c and friends – stripped to user land.
 *
 * Builds a mailbox: audio_msg_t => pcm / mixer
 * Registration at "/dev/audio-service" (real busybox line later).
 *
 * TODO:
 *   - Replace INXS() & co with tiny ioport wrappers
 *   - Keep only single AC97 codec path for simplicity
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "audio-msg.h"
#include "common.h"

/* ---------- minimal driver glue ---------------------------------------- */
static volatile uint8_t *iobase;
static uint16_t ac97_base, pcm_base;

static uint16_t inw(uint16_t port) {
    return *((volatile uint16_t*)(iobase + port));
}
static void outw(uint16_t port, uint16_t val) {
    *((volatile uint16_t*)(iobase + port)) = val;
}

static void codec_write(uint8_t reg, uint16_t val) {
    outw(ac97_base + 0x00, (reg << 8) | ((reg+1) << 16));
    outw(ac97_base + 0x02, val);
}
static uint16_t codec_read(uint8_t reg) {
    outw(ac97_base + 0x00, (reg << 8) | ((reg+1) << 16));
    return inw(ac97_base + 0x02);
}

static void pcm_setup(void) {
    codec_write(0x02, 0x0001);   /* PCM out volume = un-muted, unity gain */
}

/* ---------- mailbox ----------------------------------------------------- */

static int audio_rpc(audio_msg_t *msg, audio_reply_t *reply) {
    switch (msg->cmd) {
        case AUDIO_CMD_OPEN:
            pcm_setup();
            reply->result = 0;
            break;
        case AUDIO_CMD_PLAY:
            /* write msg->pcm_buf_phys @ msg->pcm_len to DMA buffers – extremely fake */
            (void)msg;
            reply->result = 0;
            break;
        case AUDIO_CMD_SET_VOL:
            codec_write(0x02, (msg->left_vol << 8) | msg->right_vol);
            reply->result = 0;
            break;
        default:
            reply->result = -1;
    }
    return 0;
}

/* ---------- entrance ---------------------------------------------------- */

int main(int argc, char **argv) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem for audio");
        return 1;
    }
    iobase = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0xa0000 /* inventive */);
    if (iobase == MAP_FAILED) {
        perror("mmap audio bars");
        return 1;
    }

    ac97_base = 0x1000; /* fixed for PoC */
    pcm_base  = 0x2000;

    printf("[audio-service] ready on mailbox\n");
    while (1) {
        audio_msg_t req = {0};
        audio_reply_t rep = {0};
        if (recv_incoming(&req)) continue;
        audio_rpc(&req, &rep);
        send_reply(&rep);
    }
    return 0;
}