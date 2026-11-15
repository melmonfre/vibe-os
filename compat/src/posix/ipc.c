#include "compat_fd_state.h"

#include <compat/posix/errno.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <include/userland_api.h>

#define SYSCALL_PIPE 127

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

/*
 * KNOWN_BUGS #34 root 8: SYSCALL_PIPE returns raw kernel ramfs fds (the
 * global table starts at 0), which collide with stdio and every compat fd
 * (links2's signal_pipe landed on fd 0: set_handlers(0,...) then read(0)
 * hit the console branch and starved every select() round). Map the kernel
 * pair onto fresh compat fds >= 3 (kind PIPE, host_fd = kernel fd) and
 * hand only those to callers. Kernel side untouched; no ABI change.
 */
int pipe(int pipefd[2]) {
    int kernel_fds[2];
    int read_fd;
    int write_fd;
    long ret;

    if (pipefd == 0) {
        errno = EFAULT;
        return -1;
    }

    ret = syscall6(SYSCALL_PIPE, (long)(uintptr_t)kernel_fds, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EMFILE;
        return -1;
    }

    read_fd = compat_fd_allocate(3);
    if (read_fd < 0 ||
        compat_fd_bind_pipe(read_fd, kernel_fds[0], O_RDONLY) != 0) {
        goto rollback;
    }
    write_fd = compat_fd_allocate(3);
    if (write_fd < 0 ||
        compat_fd_bind_pipe(write_fd, kernel_fds[1], O_WRONLY) != 0) {
        compat_fd_reset(read_fd);
        goto rollback;
    }

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;

rollback:
    (void)syscall6(34 /* SYSCALL_CLOSE */, kernel_fds[0], 0, 0, 0, 0, 0);
    (void)syscall6(34, kernel_fds[1], 0, 0, 0, 0, 0);
    errno = EMFILE;
    return -1;
}
