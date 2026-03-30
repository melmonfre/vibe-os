#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/process.h>

int ipc_send(process_t *dest, const void *data, size_t len);
int ipc_receive(process_t *self, void *buf, size_t bufsize);
int ipc_receive_wait(process_t *self, void *buf, size_t bufsize);
int ipc_receive_wait_timeout(process_t *self,
                             void *buf,
                             size_t bufsize,
                             uint32_t timeout_ticks);

#endif /* KERNEL_IPC_H */
