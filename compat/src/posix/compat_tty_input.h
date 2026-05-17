#ifndef COMPAT_POSIX_TTY_INPUT_H
#define COMPAT_POSIX_TTY_INPUT_H

#include <stddef.h>
#include <sys/types.h>

ssize_t compat_tty_read(int fd, void *buf, size_t count);

#endif
