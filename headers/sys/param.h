#ifndef VIBE_SYS_PARAM_H
#define VIBE_SYS_PARAM_H

#include <sys/types.h>

#define MACHINE "i386"
#define MACHINE_ARCH "i386"

#ifndef NBBY
#define NBBY 8
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef PAGE_MASK
#define PAGE_MASK (PAGE_SIZE - 1)
#endif

#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif

#ifndef rounddown
#define rounddown(x, y) ((x) - ((x) % (y)))
#endif

#ifndef powerof2
#define powerof2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif
