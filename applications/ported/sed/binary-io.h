#ifndef VIBE_SED_BINARY_IO_H
#define VIBE_SED_BINARY_IO_H

#ifndef O_BINARY
#define O_BINARY 0
#endif

static inline int
set_binary_mode(int fd, int mode)
{
  (void) fd;
  (void) mode;
  return 0;
}

#endif
