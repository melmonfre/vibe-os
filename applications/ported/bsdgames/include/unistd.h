#ifndef VIBE_BSDGAME_UNISTD_H
#define VIBE_BSDGAME_UNISTD_H

#include <compat_defs.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
extern int optreset;

int getopt(int argc, char * const argv[], const char *optstring);
int access(const char *path, int mode);
int chdir(const char *path);
int close(int fd);
int dup(int fd);
int dup2(int fd, int newfd);
int execlp(const char *file, const char *arg0, ...);
int isatty(int fd);
int link(const char *oldpath, const char *newpath);
pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setegid(gid_t egid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
char *getlogin(void);
int execl(const char *path, const char *arg0, ...);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);
int unlink(const char *path);
int fsync(int fd);
int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
void _exit(int status);

#endif
