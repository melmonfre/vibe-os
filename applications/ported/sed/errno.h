#ifndef VIBE_SED_ERRNO_H
#define VIBE_SED_ERRNO_H

extern int errno;

#ifndef EBADF
#define EBADF 9
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ELOOP
#define ELOOP 40
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef ENOENT
#define ENOENT 2
#endif
#ifndef EIO
#define EIO 5
#endif

#endif
