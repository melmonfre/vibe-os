#include "compat_signal_state.h"

#include <headers/signal.h>
#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <compat/libc/string.h>
#include <lang/include/vibe_app_runtime.h>

static struct sigaction g_signal_actions[NSIG];
static sigset_t g_signal_mask = 0u;
static sigset_t g_signal_pending = 0u;
static unsigned long long g_alarm_deadline_ms = 0ull;

static int compat_signal_is_valid(int signo) {
    return signo > 0 && signo < NSIG;
}

static int compat_signal_is_ignored_by_default(int signo) {
    return signo == SIGCHLD || signo == SIGURG || signo == SIGWINCH;
}

static void compat_signal_poll_alarm(void) {
    if (g_alarm_deadline_ms == 0ull) {
        return;
    }
    if (vibe_app_millis() < g_alarm_deadline_ms) {
        return;
    }
    g_alarm_deadline_ms = 0ull;
    g_signal_pending |= sigmask(SIGALRM);
}

static int compat_signal_dispatch_one(int signo) {
    struct sigaction action;
    sigset_t old_mask;

    if (!compat_signal_is_valid(signo)) {
        return 0;
    }

    action = g_signal_actions[signo];
    if (action.sa_handler == SIG_IGN) {
        return 1;
    }
    if (action.sa_handler == SIG_DFL) {
        if (compat_signal_is_ignored_by_default(signo)) {
            return 1;
        }
        _exit(128 + signo);
    }

    old_mask = g_signal_mask;
    g_signal_mask |= action.sa_mask;
    if ((action.sa_flags & SA_NODEFER) == 0) {
        g_signal_mask |= sigmask(signo);
    }
    if ((action.sa_flags & SA_RESETHAND) != 0) {
        memset(&g_signal_actions[signo], 0, sizeof(g_signal_actions[signo]));
    }

    if ((action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction != 0) {
        siginfo_t info;

        memset(&info, 0, sizeof(info));
        info.si_signo = signo;
        action.sa_sigaction(signo, &info, 0);
    } else if (action.sa_handler != 0) {
        action.sa_handler(signo);
    }

    g_signal_mask = old_mask;
    return 1;
}

static int compat_signal_dispatch_unblocked(void) {
    int signo;

    compat_signal_poll_alarm();
    for (signo = 1; signo < NSIG; ++signo) {
        sigset_t bit = sigmask(signo);

        if ((g_signal_pending & bit) == 0u) {
            continue;
        }
        if ((g_signal_mask & bit) != 0u) {
            continue;
        }
        g_signal_pending &= ~bit;
        return compat_signal_dispatch_one(signo);
    }
    return 0;
}

int compat_signal_dispatch_for_wait(void) {
    return compat_signal_dispatch_unblocked();
}

sighandler_t signal(int signo, sighandler_t handler) {
    struct sigaction action;
    struct sigaction old_action;

    if (!compat_signal_is_valid(signo)) {
        errno = EINVAL;
        return SIG_ERR;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    action.sa_flags = SA_RESTART;
    if (sigaction(signo, &action, &old_action) != 0) {
        return SIG_ERR;
    }
    return old_action.sa_handler;
}

void (*bsd_signal(int signo, void (*handler)(int)))(int) {
    return signal(signo, handler);
}

int raise(int signo) {
    if (!compat_signal_is_valid(signo)) {
        errno = EINVAL;
        return -1;
    }

    g_signal_pending |= sigmask(signo);
    (void)compat_signal_dispatch_unblocked();
    return 0;
}

int kill(pid_t pid, int signo) {
    pid_t self = getpid();

    if (signo < 0 || signo >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    if (pid != self && pid != 0) {
        errno = ESRCH;
        return -1;
    }
    if (signo == 0) {
        return 0;
    }
    return raise(signo);
}

int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact) {
    if (!compat_signal_is_valid(signo)) {
        errno = EINVAL;
        return -1;
    }
    if (signo == SIGKILL || signo == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }

    if (oldact != 0) {
        *oldact = g_signal_actions[signo];
    }
    if (act != 0) {
        g_signal_actions[signo] = *act;
    }
    return 0;
}

int sigpending(sigset_t *set) {
    compat_signal_poll_alarm();
    if (set == 0) {
        errno = EINVAL;
        return -1;
    }
    *set = g_signal_pending;
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    if (oldset != 0) {
        *oldset = g_signal_mask;
    }
    if (set == 0) {
        return 0;
    }

    switch (how) {
    case SIG_BLOCK:
        g_signal_mask |= *set;
        break;
    case SIG_UNBLOCK:
        g_signal_mask &= ~(*set);
        break;
    case SIG_SETMASK:
        g_signal_mask = *set;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    (void)compat_signal_dispatch_unblocked();
    return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    return sigprocmask(how, set, oldset);
}

int sigsuspend(const sigset_t *mask) {
    sigset_t old_mask;

    if (mask == 0) {
        errno = EINVAL;
        return -1;
    }

    old_mask = g_signal_mask;
    g_signal_mask = *mask;
    for (;;) {
        if (compat_signal_dispatch_unblocked() != 0) {
            g_signal_mask = old_mask;
            errno = EINTR;
            return -1;
        }
        vibe_app_yield();
    }
}

int killpg(pid_t pgrp, int signo) {
    pid_t self = getpid();

    if (pgrp == 0 || pgrp == self) {
        return kill(self, signo);
    }
    errno = ESRCH;
    return -1;
}

int siginterrupt(int signo, int flag) {
    struct sigaction action;

    if (sigaction(signo, 0, &action) != 0) {
        return -1;
    }
    if (flag != 0) {
        action.sa_flags &= ~SA_RESTART;
    } else {
        action.sa_flags |= SA_RESTART;
    }
    return sigaction(signo, &action, 0);
}

int sigpause(int mask) {
    sigset_t set = (sigset_t)mask;

    return sigsuspend(&set);
}

int sigsetmask(int mask) {
    sigset_t old_mask = 0u;
    sigset_t new_mask = (sigset_t)mask;

    if (sigprocmask(SIG_SETMASK, &new_mask, &old_mask) != 0) {
        return -1;
    }
    return (int)old_mask;
}

int sigwait(const sigset_t *set, int *sig) {
    int signo;

    if (set == 0 || sig == 0) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        compat_signal_poll_alarm();
        for (signo = 1; signo < NSIG; ++signo) {
            sigset_t bit = sigmask(signo);

            if (((*set) & bit) == 0u) {
                continue;
            }
            if ((g_signal_pending & bit) == 0u) {
                continue;
            }
            g_signal_pending &= ~bit;
            *sig = signo;
            return 0;
        }

        (void)compat_signal_dispatch_unblocked();
        vibe_app_yield();
    }
}

unsigned int alarm(unsigned int seconds) {
    unsigned long long now = vibe_app_millis();
    unsigned int remaining = 0;

    compat_signal_poll_alarm();
    if (g_alarm_deadline_ms != 0ull && g_alarm_deadline_ms > now) {
        unsigned long long delta = g_alarm_deadline_ms - now;

        remaining = (unsigned int)((delta + 999ull) / 1000ull);
    }

    if (seconds == 0u) {
        g_alarm_deadline_ms = 0ull;
    } else {
        g_alarm_deadline_ms = now + ((unsigned long long)seconds * 1000ull);
    }
    return remaining;
}

int pause(void) {
    for (;;) {
        if (compat_signal_dispatch_unblocked() != 0) {
            errno = EINTR;
            return -1;
        }
        vibe_app_yield();
    }
}
