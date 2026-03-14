#include "../include/compat/posix/unistd.h"
#include "../include/sys/types.h"

int errno;

int isatty(int fd) {
    // For VibeOS apps: assume only stdin(0), stdout(1), stderr(2) are TTYs
    if (fd >= 0 && fd <= 2) {
        return 1;
    }
    errno = 25; // ENOTTY
    return 0;
}

pid_t getpid(void) {
    // Stub: VibeOS apps don't need true PID currently
    return 1;
}
