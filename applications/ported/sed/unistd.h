#ifndef VIBE_SED_UNISTD_H
#define VIBE_SED_UNISTD_H

#include_next <unistd.h>
#include <sys/types.h>

ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int unlink(const char *path);

#endif
