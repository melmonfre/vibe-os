#include "../include/compat/posix/unistd.h"
#include <compat/posix/fcntl.h>
#include <compat/posix/stat.h>
#include <compat/posix/errno.h>
#include <lang/include/vibe_app_runtime.h>

#define COMPAT_MAX_FDS 16
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct compat_fd_entry {
    int used;
    int host_fd;
};

static struct compat_fd_entry g_compat_fds[COMPAT_MAX_FDS];

static int compat_alloc_fd(void) {
    int i;

    for (i = 3; i < COMPAT_MAX_FDS; ++i) {
        if (!g_compat_fds[i].used) {
            return i;
        }
    }
    return -1;
}

int open(const char *path, int oflag, ...) {
    int fd;
    int host_fd;

    if (path == 0) {
        errno = EINVAL;
        return -1;
    }

    fd = compat_alloc_fd();
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    host_fd = vibe_app_open(path, oflag);
    if (host_fd < 0) {
        errno = ENOENT;
        return -1;
    }

    g_compat_fds[fd].used = 1;
    g_compat_fds[fd].host_fd = host_fd;
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    int rc;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd == STDIN_FILENO) {
        if (count == 0u) {
            return 0;
        }
        for (;;) {
            int c = vibe_app_poll_key();
            if (c != 0) {
                ((char *)buf)[0] = (char)c;
                return 1;
            }
            vibe_app_yield();
        }
    }

    if (fd < 0 || fd >= COMPAT_MAX_FDS || !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    rc = vibe_app_read(g_compat_fds[fd].host_fd, buf, (int)count);
    if (rc < 0) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)rc;
}

ssize_t write(int fd, const void *buf, size_t count) {
    size_t i;

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

    if (fd < 0 || fd >= COMPAT_MAX_FDS || !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }

    {
        int rc = vibe_app_write(g_compat_fds[fd].host_fd, buf, (int)count);
        if (rc < 0) {
            errno = EBADF;
            return -1;
        }
        return (ssize_t)rc;
    }
}

int close(int fd) {
    int i;

    if (fd < 0 || fd >= COMPAT_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd <= STDERR_FILENO) {
        return 0;
    }
    if (!g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }

    for (i = 3; i < COMPAT_MAX_FDS; ++i) {
        if (i != fd &&
            g_compat_fds[i].used &&
            g_compat_fds[i].host_fd == g_compat_fds[fd].host_fd) {
            g_compat_fds[fd].used = 0;
            g_compat_fds[fd].host_fd = -1;
            return 0;
        }
    }

    (void)vibe_app_close(g_compat_fds[fd].host_fd);
    g_compat_fds[fd].used = 0;
    g_compat_fds[fd].host_fd = -1;
    return 0;
}

int dup(int fd) {
    int newfd;

    if (fd < 0 || fd >= COMPAT_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd > STDERR_FILENO && !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }

    newfd = compat_alloc_fd();
    if (newfd < 0) {
        errno = EMFILE;
        return -1;
    }

    g_compat_fds[newfd] = g_compat_fds[fd];
    g_compat_fds[newfd].used = 1;
    return newfd;
}

int dup2(int fd, int newfd) {
    if (fd < 0 || fd >= COMPAT_MAX_FDS || newfd < 0 || newfd >= COMPAT_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd > STDERR_FILENO && !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    if (fd == newfd) {
        return newfd;
    }

    (void)close(newfd);
    g_compat_fds[newfd] = g_compat_fds[fd];
    g_compat_fds[newfd].used = 1;
    return newfd;
}

off_t lseek(int fd, off_t offset, int whence) {
    int rc;

    if (fd < 0 || fd >= COMPAT_MAX_FDS || !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    rc = vibe_app_lseek(g_compat_fds[fd].host_fd, (int)offset, whence);
    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }
    return (off_t)rc;
}

int isatty(int fd) {
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO) {
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
    if (fd < 0 || fd >= COMPAT_MAX_FDS || !g_compat_fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    if (vibe_app_fstat(g_compat_fds[fd].host_fd, &vibe_stat) != 0) {
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
    (void)fd;
    (void)cmd;
    errno = EINVAL;
    return -1;
}

pid_t getpid(void) {
    return 1;
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

unsigned int sleep(unsigned int seconds) {
    unsigned int i;

    for (i = 0; i < (seconds * 100u); ++i) {
        vibe_app_yield();
    }
    return 0;
}

int usleep(unsigned int usec) {
    unsigned int loops = (usec / 1000u) + 1u;
    unsigned int i;

    for (i = 0; i < loops; ++i) {
        vibe_app_yield();
    }
    return 0;
}

void _exit(int status) {
    (void)status;
    for (;;) {
        vibe_app_yield();
    }
}
