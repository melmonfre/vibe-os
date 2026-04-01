#ifndef COMMON_H
#define COMMON_H

/* mailbox helpers – will be replaced by actual unix socket later */
#include <stdint.h>

extern int recv_incoming(void *req);
extern void send_reply(void *rep);
#endif