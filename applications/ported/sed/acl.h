#ifndef VIBE_SED_ACL_H
#define VIBE_SED_ACL_H

#include <sys/types.h>

static inline int
xcopy_acl(const char *src_name, int src_fd, const char *dst_name, int dst_fd, mode_t mode)
{
  (void) src_name;
  (void) src_fd;
  (void) dst_name;
  (void) dst_fd;
  (void) mode;
  return 0;
}

#endif
