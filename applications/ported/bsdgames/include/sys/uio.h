#ifndef VIBE_BSDGAME_SYS_UIO_H
#define VIBE_BSDGAME_SYS_UIO_H

#include <compat_defs.h>

struct iovec {
    void *iov_base;
    size_t iov_len;
};

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#endif
