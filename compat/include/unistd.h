#ifndef _COMPAT_UNISTD_H
#define _COMPAT_UNISTD_H

#include <sys/types.h>

int isatty(int fd);
pid_t getpid(void);

#endif /* _COMPAT_UNISTD_H */
