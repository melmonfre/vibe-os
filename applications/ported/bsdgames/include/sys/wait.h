#ifndef VIBE_BSDGAME_SYS_WAIT_H
#define VIBE_BSDGAME_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1

#define WIFEXITED(status) 1
#define WEXITSTATUS(status) ((status) & 0xff)
#define WIFSIGNALED(status) 0
#define WTERMSIG(status) 0
#define WIFSTOPPED(status) 0
#define WSTOPSIG(status) 0

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif
