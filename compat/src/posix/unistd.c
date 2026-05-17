#include <compat/posix/unistd.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/stat.h>
#include <compat/posix/errno.h>
#include <stdint.h>
#include <lang/include/vibe_app_runtime.h>
#include <include/userland_api.h>

#include "compat_fd_state.h"
#include "compat_tty_input.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

int open(const char *path, int oflag, ...) {
    int fd;
    int host_fd;
    struct flock lock;

    if (path == 0) {
        errno = EINVAL;
        return -1;
    }

    compat_fd_bootstrap();

    fd = compat_fd_allocate(3);
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    host_fd = vibe_app_open(path, oflag);
    if (host_fd < 0) {
        errno = ENOENT;
        return -1;
    }

    (void)compat_fd_bind(fd, host_fd, oflag, 0, 0);

    if ((oflag & (O_SHLOCK | O_EXLOCK)) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = (oflag & O_EXLOCK) ? F_WRLCK : F_RDLCK;
        lock.l_whence = SEEK_SET;
        if (compat_fd_set_lock(fd, &lock, 0) != 0) {
            (void)close(fd);
            return -1;
        }
    }

    return fd;
}

int access(const char *path, int mode) {
    struct stat st;

    if (path == 0) {
        errno = EINVAL;
        return -1;
    }

    if ((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st) != 0) {
        return -1;
    }

    return 0;
}

int creat(const char *path, mode_t mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int openat(int dirfd, const char *path, int oflag, ...) {
    if (dirfd != AT_FDCWD) {
        errno = ENOSYS;
        return -1;
    }
    return open(path, oflag);
}

ssize_t read(int fd, void *buf, size_t count) {
    int rc;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd == STDIN_FILENO) {
        return compat_tty_read(fd, buf, count);
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        rc = compat_syscall5(SYSCALL_NETWORK_RECV,
                             entry->host_fd,
                             (int)(uintptr_t)buf,
                             (int)count,
                             0,
                             0);
        if (rc < 0) {
            errno = entry->socket_error != 0 ? entry->socket_error : EIO;
            return -1;
        }
        return (ssize_t)rc;
    }
    rc = vibe_app_read(entry->host_fd, buf, (int)count);
    if (rc < 0) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)rc;
}

ssize_t write(int fd, const void *buf, size_t count) {
    size_t i;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char *text = (const char *)buf;
        for (i = 0; i < count; ++i) {
            vibe_app_console_putc(text[i]);
        }
        return (ssize_t)count;
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        int rc = compat_syscall5(SYSCALL_NETWORK_SEND,
                                 entry->host_fd,
                                 (int)(uintptr_t)buf,
                                 (int)count,
                                 0,
                                 0);
        if (rc < 0) {
            errno = entry->socket_error != 0 ? entry->socket_error : EPIPE;
            return -1;
        }
        return (ssize_t)rc;
    }

    {
        int rc = vibe_app_write(entry->host_fd, buf, (int)count);
        if (rc < 0) {
            errno = EBADF;
            return -1;
        }
        return (ssize_t)rc;
    }
}

int close(int fd) {
    const struct compat_fd_entry *entry;
    int host_fd;

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    if (fd <= STDERR_FILENO) {
        return 0;
    }

    host_fd = entry->host_fd;
    if (host_fd >= 0 && !compat_fd_refs_host(host_fd, entry->kind, fd)) {
        if (entry->kind == COMPAT_FD_KIND_SOCKET) {
            (void)compat_syscall5(SYSCALL_NETWORK_CLOSE, host_fd, 0, 0, 0, 0);
        } else {
            (void)vibe_app_close(host_fd);
        }
    }
    compat_fd_reset(fd);
    return 0;
}

int dup(int fd) {
    int newfd;

    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }

    newfd = compat_fd_allocate(0);
    if (newfd < 0) {
        errno = EMFILE;
        return -1;
    }

    if (compat_fd_duplicate(fd, newfd, 0) != 0) {
        errno = EBADF;
        return -1;
    }
    return newfd;
}

int dup2(int fd, int newfd) {
    if (!compat_fd_is_valid(fd) || newfd < 0 || newfd >= COMPAT_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd == newfd) {
        return newfd;
    }

    (void)close(newfd);
    if (compat_fd_duplicate(fd, newfd, 0) != 0) {
        errno = EBADF;
        return -1;
    }
    return newfd;
}

off_t lseek(int fd, off_t offset, int whence) {
    int rc;
    const struct compat_fd_entry *entry;

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        errno = ESPIPE;
        return -1;
    }
    rc = vibe_app_lseek(entry->host_fd, (int)offset, whence);
    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }
    return (off_t)rc;
}

int isatty(int fd) {
    if (compat_fd_is_tty(fd)) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}

int stat(const char *path, struct stat *buf) {
    struct vibe_app_stat vibe_stat;

    if (path == 0 || buf == 0) {
        errno = EINVAL;
        return -1;
    }
    if (vibe_app_stat(path, &vibe_stat) != 0) {
        errno = ENOENT;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = vibe_stat.is_dir ? 0040755 : 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int fstat(int fd, struct stat *buf) {
    struct vibe_app_stat vibe_stat;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = 0020000;
        buf->st_nlink = 1;
        return 0;
    }
    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = 0140666;
        buf->st_nlink = 1;
        return 0;
    }
    if (vibe_app_fstat(entry->host_fd, &vibe_stat) != 0) {
        errno = EBADF;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int lstat(const char *path, struct stat *buf) {
    return stat(path, buf);
}

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg = 0;
    struct compat_fd_entry *entry;
    int newfd;

    entry = compat_fd_get(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);

    switch (cmd) {
    case F_DUPFD:
        newfd = compat_fd_allocate((int)arg);
        if (newfd < 0) {
            errno = EMFILE;
            return -1;
        }
        if (compat_fd_duplicate(fd, newfd, 0) != 0) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    case F_DUPFD_CLOEXEC:
        newfd = compat_fd_allocate((int)arg);
        if (newfd < 0) {
            errno = EMFILE;
            return -1;
        }
        if (compat_fd_duplicate(fd, newfd, 1) != 0) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    case F_GETFD:
        return entry->fd_flags;
    case F_SETFD:
        entry->fd_flags = (int)arg & FD_CLOEXEC;
        return 0;
    case F_GETFL:
        return entry->flags;
    case F_SETFL:
        entry->flags &= ~(O_APPEND | O_NONBLOCK | O_ASYNC | O_SYNC);
        entry->flags |= (int)arg & (O_APPEND | O_NONBLOCK | O_ASYNC | O_SYNC);
        return 0;
    case F_GETOWN:
        return getpid();
    case F_SETOWN:
        return 0;
    case F_ISATTY:
        return entry->is_tty ? 1 : 0;
    case F_GETLK:
        return compat_fd_get_lock(fd, (struct flock *)(uintptr_t)arg);
    case F_SETLK:
        return compat_fd_set_lock(fd, (const struct flock *)(uintptr_t)arg, 0);
    case F_SETLKW:
        return compat_fd_set_lock(fd, (const struct flock *)(uintptr_t)arg, 1);
    default:
        errno = EINVAL;
        return -1;
    }
}

int flock(int fd, int operation) {
    struct flock lock;

    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    if ((operation & LOCK_UN) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, 0);
    }
    if ((operation & LOCK_EX) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, (operation & LOCK_NB) == 0);
    }
    if ((operation & LOCK_SH) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, (operation & LOCK_NB) == 0);
    }

    errno = EINVAL;
    return -1;
}

pid_t getpid(void) {
    int pid = compat_syscall5(SYSCALL_GETPID, 0, 0, 0, 0, 0);

    if (pid <= 0) {
        return 1;
    }
    return (pid_t)pid;
}

pid_t getppid(void) {
    return 1;
}

char *getenv(const char *name) {
    return (char *)vibe_app_getenv(name);
}

char *getcwd(char *buf, size_t size) {
    if (buf == 0 || size == 0u) {
        errno = EINVAL;
        return 0;
    }
    if (vibe_app_getcwd(buf, (int)size) != 0) {
        errno = EINVAL;
        return 0;
    }
    return buf;
}

int rmdir(const char *path) {
    int rc;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    rc = vibe_app_remove_dir(path);
    if (rc == 0) {
        return 0;
    }
    if (rc == -2) {
        errno = ENOTEMPTY;
        return -1;
    }
    if (rc == -3) {
        errno = ENOTDIR;
        return -1;
    }

    errno = ENOENT;
    return -1;
}

int mkdir(const char *path, mode_t mode) {
    int rc;

    (void)mode;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    rc = vibe_app_create_dir(path);
    if (rc == 0) {
        return 0;
    }
    if (rc == -2) {
        errno = EEXIST;
        return -1;
    }

    errno = ENOENT;
    return -1;
}

unsigned int sleep(unsigned int seconds) {
    (void)vibe_app_sleep_ms(seconds * 1000u);
    return 0;
}

int usleep(unsigned int usec) {
    unsigned int ms = usec / 1000u;

    if (ms == 0u && usec != 0u) {
        ms = 1u;
    }
    (void)vibe_app_sleep_ms(ms);
    return 0;
}

void sync(void) {
    (void)vibe_app_sync();
}

void _exit(int status) {
    (void)status;
    for (;;) {
        vibe_app_yield();
    }
}
