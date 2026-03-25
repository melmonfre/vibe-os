#ifndef VIBE_BSDGAME_FCNTL_H
#define VIBE_BSDGAME_FCNTL_H

#include <compat/posix/fcntl.h>
#include <sys/file.h>

#ifndef O_RDONLY
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0x0040
#define O_EXCL     0x0080
#define O_NOCTTY   0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800
#define O_ACCMODE  0x0003
#endif

#endif
