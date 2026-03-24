#ifndef VIBE_BSDGAME_ERRNO_H
#define VIBE_BSDGAME_ERRNO_H

#include <compat/posix/errno.h>

#ifndef ERANGE
#define ERANGE 34
#endif

#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#endif
