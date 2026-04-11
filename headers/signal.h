#ifndef VIBE_SIGNAL_H
#define VIBE_SIGNAL_H

#include <sys/types.h>

#define _NSIG 33
#define NSIG _NSIG

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGEMT 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGBUS 10
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGURG 16
#define SIGSTOP 17
#define SIGTSTP 18
#define SIGCONT 19
#define SIGCHLD 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGIO 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGINFO 29
#define SIGUSR1 30
#define SIGUSR2 31
#define SIGTHR 32

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
typedef void (*sig_t)(int);

#ifndef _SIGSET_T_DEFINED_
#define _SIGSET_T_DEFINED_
typedef unsigned int sigset_t;
#endif

typedef struct {
    int si_signo;
    int si_code;
    int si_errno;
} siginfo_t;

struct sigaction {
    union {
        void (*__sa_handler)(int);
        void (*__sa_sigaction)(int, siginfo_t *, void *);
    } __sigaction_u;
    sigset_t sa_mask;
    int sa_flags;
};

#define sa_handler __sigaction_u.__sa_handler
#define sa_sigaction __sigaction_u.__sa_sigaction

typedef struct sigaltstack {
    void *ss_sp;
    size_t ss_size;
    int ss_flags;
} stack_t;

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define BADSIG SIG_ERR

#define SA_ONSTACK 0x0001
#define SA_RESTART 0x0002
#define SA_RESETHAND 0x0004
#define SA_NOCLDSTOP 0x0008
#define SA_NODEFER 0x0010
#define SA_NOCLDWAIT 0x0020
#define SA_SIGINFO 0x0040

#define SS_ONSTACK 0x0001
#define SS_DISABLE 0x0004
#define MINSIGSTKSZ 8192
#define SIGSTKSZ 16384

#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

#define sigmask(m) (1U << ((m) - 1))

static inline int sigemptyset(sigset_t *set) {
    if (set == 0) {
        return -1;
    }
    *set = 0u;
    return 0;
}

static inline int sigfillset(sigset_t *set) {
    if (set == 0) {
        return -1;
    }
    *set = ~0u;
    return 0;
}

static inline int sigaddset(sigset_t *set, int signo) {
    if (set == 0 || signo <= 0 || signo >= _NSIG) {
        return -1;
    }
    *set |= sigmask(signo);
    return 0;
}

static inline int sigdelset(sigset_t *set, int signo) {
    if (set == 0 || signo <= 0 || signo >= _NSIG) {
        return -1;
    }
    *set &= ~sigmask(signo);
    return 0;
}

static inline int sigismember(const sigset_t *set, int signo) {
    if (set == 0 || signo <= 0 || signo >= _NSIG) {
        return 0;
    }
    return ((*set & sigmask(signo)) != 0u) ? 1 : 0;
}

sighandler_t signal(int signo, sighandler_t handler);
int raise(int signo);
int kill(pid_t pid, int signo);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

#endif
