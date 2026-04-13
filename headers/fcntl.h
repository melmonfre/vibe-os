#ifndef VIBE_FCNTL_H
#define VIBE_FCNTL_H

#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_ACCMODE 0x0003

/*
 * Keep the historical VibeOS values for the flags already consumed by the
 * current userland ABI. Additional flags are assigned in unused slots so we
 * can grow source compatibility without breaking existing binaries.
 */
#define O_NONBLOCK 0x0004
#define O_APPEND   0x0008
#define O_SHLOCK   0x0010
#define O_EXLOCK   0x0020
#define O_CREAT    0x0040
#define O_ASYNC    0x0080
#define O_SYNC     0x0100
#define O_TRUNC    0x0200
#define O_NOFOLLOW 0x0400
#define O_EXCL     0x0800
#define O_NOCTTY   0x1000
#define O_CLOEXEC  0x2000
#define O_DIRECTORY 0x4000

#define O_DSYNC O_SYNC
#define O_RSYNC O_SYNC
#define O_FSYNC O_SYNC
#define O_NDELAY O_NONBLOCK

#define F_DUPFD   0
#define F_GETFD   1
#define F_SETFD   2
#define F_GETFL   3
#define F_SETFL   4
#define F_GETOWN  5
#define F_SETOWN  6
#define F_GETLK   7
#define F_SETLK   8
#define F_SETLKW  9
#define F_DUPFD_CLOEXEC 10
#define F_ISATTY  11

#define FD_CLOEXEC 1

#define F_RDLCK 1
#define F_UNLCK 2
#define F_WRLCK 3

#define LOCK_SH 0x01
#define LOCK_EX 0x02
#define LOCK_NB 0x04
#define LOCK_UN 0x08

#define AT_FDCWD -100
#define AT_EACCESS 0x01
#define AT_SYMLINK_NOFOLLOW 0x02
#define AT_SYMLINK_FOLLOW 0x04
#define AT_REMOVEDIR 0x08

struct flock {
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
    short l_type;
    short l_whence;
};

int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int openat(int dirfd, const char *path, int flags, ...);

static inline int fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);

    switch (cmd) {
    case F_GETFD:
        return 0;
    case F_SETFD:
        (void)arg;
        return 0;
    case F_GETFL:
        return O_RDONLY;
    case F_SETFL:
        (void)arg;
        return 0;
    default:
        errno = ENOSYS;
        return -1;
    }
}

static inline int flock(int fd, int operation) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    (void)operation;
    errno = ENOSYS;
    return -1;
}

#endif
