#ifndef VIBE_SYS_UIO_H
#define VIBE_SYS_UIO_H

#include <sys/types.h>

struct iovec {
    void *iov_base;
    size_t iov_len;
};

#endif
