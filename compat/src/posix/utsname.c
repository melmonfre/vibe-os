#include <sys/utsname.h>
#include <compat/libc/string.h>

int uname(struct utsname *buf) {
    if (buf == 0) {
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "VibeOS");
    strcpy(buf->nodename, "vibe-machine");
    strcpy(buf->release, "compat");
    strcpy(buf->version, "vibekernel");
    strcpy(buf->machine, "i686");
    return 0;
}
