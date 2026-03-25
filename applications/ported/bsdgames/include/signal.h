#ifndef VIBE_BSDGAME_SIGNAL_H
#define VIBE_BSDGAME_SIGNAL_H

typedef void (*sig_t)(int);
typedef unsigned long long sigset_t;
typedef int sig_atomic_t;
struct sigaction {
    sig_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
};

#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)
#define SIG_ERR ((sig_t)-1)

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGCHLD 17
#define SIGALRM 14
#define SIGTERM 15
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTOU 21
#define SIGWINCH 28

#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

sig_t signal(int sig, sig_t handler);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
unsigned int alarm(unsigned int seconds);
int kill(pid_t pid, int sig);

#endif
