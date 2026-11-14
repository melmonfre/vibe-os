#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <include/userland_api.h>

#define SYSCALL_FORK    120
#define SYSCALL_EXECVE  121
#define SYSCALL_EXIT    122
#define SYSCALL_WAITPID 123
#define SYSCALL_KILL    128

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

/* libcompat must stay self-contained: the ported apps link libcompat.a
 * WITHOUT userland/modules/syscalls.o, so the syscall entries the execve
 * router needs are wrapped here with the same raw int-0x80 convention this
 * file already uses for fork/execve/exit. */
static void compat_write_debug(const char *msg) {
    (void)syscall6(SYSCALL_WRITE_DEBUG, (long)(uintptr_t)msg, 0, 0, 0, 0, 0);
}

static void compat_yield(void) {
    (void)syscall6(SYSCALL_YIELD, 0, 0, 0, 0, 0, 0);
}

static int compat_launch_app(char *const *argv, int argc) {
    return (int)syscall6(SYSCALL_LAUNCH_APP,
                         (long)(uintptr_t)argv,
                         (long)argc,
                         0, 0, 0, 0);
}

static int compat_event_subscribe(uint32_t event_mask, uint32_t class_mask) {
    return (int)syscall6(SYSCALL_TASK_EVENT_SUBSCRIBE,
                         (long)event_mask,
                         (long)class_mask,
                         0, 0, 0, 0);
}

static int compat_event_receive(struct mk_task_event *event, uint32_t timeout_ticks) {
    return (int)syscall6(SYSCALL_TASK_EVENT_RECV,
                         (long)(uintptr_t)event,
                         (long)timeout_ticks,
                         0, 0, 0, 0);
}

static int compat_task_snapshot(struct task_snapshot_summary *summary,
                                struct task_snapshot_entry *entries,
                                uint32_t max_entries) {
    return (int)syscall6(SYSCALL_TASK_SNAPSHOT,
                         (long)(uintptr_t)summary,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         0, 0, 0);
}

/* Child entry for fork. Called by kernel with eax=0.
 * The stack contains a copy of the parent's user stack with the
 * return address at the correct position. We just "ret" to pop it
 * and return to terminal_init_ksh with eax=0 (fork returns 0 for child). */
__attribute__((naked)) void fork_child_entry(void) {
    __asm__ volatile(
        "xor %eax, %eax\n"       /* eax = 0 (fork returns 0 for child) */
        "pop %ecx\n"             /* mirror the fork stub's epilogue exactly: */
        "pop %ebx\n"             /* five saved registers first, so the    */
        "pop %esi\n"             /* caller's callee-saved context is fully */
        "pop %edi\n"             /* restored (the parent will do the same */
        "pop %ebp\n"             /* after its own int returns) ...        */
        "ret\n"                  /* ...then pop the return address        */
    );
}

pid_t fork(void) {
    /*
     * KNOWN_BUGS #26: naked and layout-fixed. The previous version used C
     * asm operands on a non-naked function -- the compiler's own prologue
     * shifted the stack, so the kernel's `[esp+20]` return-address read hit
     * a DATA pointer (retslot=142628c0: resumed the child at a non-code
     * word which then read address 0, `err=5 cr2=0`). Here exactly five
     * pushes sit between `call fork`'s return address and the trap ESP;
     * ECX carries that ESP to the kernel (the int-0x80 ABI maps ecx -> `b`)
     * and ESI carries the child entry (-> `d`). The child mirror pops the
     * same five slots and `ret`s to the pop'd return address -- identical
     * epilogue to the parent's -- so terminal_init_ksh resumes with intact
     * callee-saved context and eax=0.
     */
    __asm__ volatile(
        "push %%ebp\n"
        "push %%edi\n"
        "push %%esi\n"
        "push %%ebx\n"
        "push %%ecx\n"
        "mov  %%esp, %%ecx\n"
        "movl $fork_child_entry, %%esi\n"
        "movl $120, %%eax\n"
        "int  $0x80\n"
        "pop  %%ecx\n"
        "pop  %%ebx\n"
        "pop  %%esi\n"
        "pop  %%edi\n"
        "pop  %%ebp\n"
        "ret\n"
        :
        :
        : "memory");
    __builtin_unreachable();
}

/*
 * KNOWN_BUGS #24: the pdksh terminal (desktop) executed commands by calling
 * this execve() from the forked child. Direct SYSCALL_EXECVE loads the ELF
 * and enters it with ONE stack slot (envp, or argv when envp==0) — it cannot
 * deliver the app ABI entry `vibe_app_entry(ctx, argc, argv)` — so every
 * AppFS app run from the terminal died silently WITHOUT any witness (the
 * audiosvc pid=16 launched/terminated with zero output, 11/11 measurement).
 * The proven external-command contract in this OS is busybox's: subscribe to
 * TERMINATED for the launch class, `sys_launch_app_argv`, then wait for the
 * app's exit event. Mirror exactly that here: the ksh child launches the app
 * through the host, waits, and _exit()s — ksh job control still reaps its
 * child, which now carries the command's completion. Serial witnesses
 * `ksh: exec <cmd>` / `ksh: exit <cmd>` give the harness a real typed-command
 * dispatch/ack channel (the busybox-era `shell: command`/`terminal: command
 * done` markers never existed in the ksh path). Honest limit (same as the
 * busybox external path): mk_task_event carries no exit status, so $? of a
 * typed app command reflects "exited", not the app rc — wiring an rc channel
 * is separate work. Non-app images keep the old direct-execve fallback.
 */
static int compat_str_eq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b ? 1 : 0;
}

static const char *compat_basename(const char *s) {
    const char *last = s;
    const char *p;

    for (p = s; *p != '\0'; ++p) {
        if (*p == '/') {
            last = p + 1;
        }
    }
    return last;
}

static void compat_exec_debug(const char *tag, const char *cmd) {
    char msg[96];
    int len = 0;
    const char *p;

    for (p = tag; *p != '\0' && len < 80; ++p) {
        msg[len++] = *p;
    }
    for (p = cmd; *p != '\0' && len < 92; ++p) {
        msg[len++] = *p;
    }
    msg[len++] = '\n';
    msg[len] = '\0';
    compat_write_debug(msg);
}

static int compat_task_pid_alive(uint32_t pid) {
    struct task_snapshot_summary summary;
    struct task_snapshot_entry entries[TASK_SNAPSHOT_MAX];
    uint32_t count;

    if (pid == 0u) {
        return 0;
    }
    if (compat_task_snapshot(&summary, entries, TASK_SNAPSHOT_MAX) != 0) {
        return 1;
    }
    count = summary.total_tasks;
    if (count > TASK_SNAPSHOT_MAX) {
        count = TASK_SNAPSHOT_MAX;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (entries[i].pid == pid && entries[i].state != 3u) {
            return 1;
        }
    }
    return 0;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    struct mk_task_event event;
    char *launch_argv[32];
    const char *cmd;
    uint32_t class_mask;
    int argc = 0;
    int pid;

    (void)path; (void)envp;

    if (argv != 0 && argv[0] != 0) {
        cmd = compat_basename(argv[0]);
        class_mask = MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
        if (compat_str_eq(cmd, "startx")) {
            class_mask = MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP);
        } else if (compat_str_eq(cmd, "audiosvc") &&
                   argv[1] != 0 &&
                   compat_str_eq(argv[1], "play-asset")) {
            class_mask = MK_TASK_CLASS_MASK(MK_TASK_CLASS_AUDIO_IO);
        }

        compat_exec_debug("ksh: exec ", cmd);

        if (compat_event_subscribe(MK_TASK_EVENT_MASK_TERMINATED,
                                   class_mask) == 0) {
            while (argc < 31 && argv[argc] != 0) {
                launch_argv[argc] = argv[argc];
                ++argc;
            }
            if (argc == 0) {
                errno = ENOENT;
                return -1;
            }
            launch_argv[argc] = 0;

            pid = compat_launch_app(launch_argv, argc);
            if (pid > 0) {
                for (;;) {
                    if (compat_event_receive(&event, 4u) == 0) {
                        if (event.pid == (uint32_t)pid &&
                            event.event_type == (uint32_t)MK_TASK_EVENT_TERMINATED) {
                            break;
                        }
                        continue;
                    }
                    if (!compat_task_pid_alive((uint32_t)pid)) {
                        break;
                    }
                    compat_yield();
                }
                compat_exec_debug("ksh: exit ", cmd);
                _exit(0);
            }
        }
    }

    {
        long ret = syscall6(SYSCALL_EXECVE,
                            (long)(uintptr_t)path,
                            (long)(uintptr_t)argv,
                            (long)(uintptr_t)envp,
                            0, 0, 0);

        if (ret < 0) {
            errno = ENOENT;
            return -1;
        }
        return (int)ret;
    }
}

__attribute__((noreturn))
void _exit(int status) {
    syscall6(SYSCALL_EXIT, status, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

pid_t waitpid(pid_t pid, int *status, int options) {
    long ret = syscall6(SYSCALL_WAITPID,
                        (long)pid,
                        (long)(uintptr_t)status,
                        (long)options,
                        0, 0, 0);

    if (ret < 0) {
        errno = ECHILD;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

int kill(pid_t pid, int sig) {
    long ret = syscall6(SYSCALL_KILL, (long)pid, (long)sig, 0, 0, 0, 0);

    if (ret < 0) {
        errno = ESRCH;
        return -1;
    }
    return (int)ret;
}

pid_t getpid(void) {
    long ret = syscall6(SYSCALL_GETPID, 0, 0, 0, 0, 0, 0);

    if (ret <= 0) {
        return 1;
    }
    return (pid_t)ret;
}

#include <sys/resource.h>
#include <sys/stat.h>

char **environ = 0;

int unlink(const char *path) {
    (void)path;
    return -1;
}

mode_t umask(mode_t mask) {
    (void)mask;
    return 022;
}

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (usage) {
        usage->ru_utime.tv_sec = 0;
        usage->ru_utime.tv_usec = 0;
        usage->ru_stime.tv_sec = 0;
        usage->ru_stime.tv_usec = 0;
        usage->ru_maxrss = 0;
        usage->ru_ixrss = 0;
        usage->ru_idrss = 0;
        usage->ru_isrss = 0;
        usage->ru_minflt = 0;
        usage->ru_majflt = 0;
        usage->ru_nswap = 0;
        usage->ru_inblock = 0;
        usage->ru_oublock = 0;
        usage->ru_msgsnd = 0;
        usage->ru_msgrcv = 0;
        usage->ru_nsignals = 0;
        usage->ru_nvcsw = 0;
        usage->ru_nivcsw = 0;
    }
    return 0;
}

/*
 * execl()/execv(): the POSIX variadic and path-only wrappers were declared
 * in compat/include/compat/posix/unistd.h but never implemented anywhere in
 * the tree (link-time gap that killed the links2 port). They collect into
 * the same argv shape execve already consumes; execve dispatches by the
 * AppFS registry name (argv[0] basename), so execlp/execvpe inherit that
 * resolution with no separate lookup path.
 */
int execv(const char *path, char *const argv[]) {
    return execve(path, argv, environ);
}

int execl(const char *path, const char *arg, ...) {
    char *argv[32];
    const char *a;
    va_list ap;
    int argc = 0;

    if (path == 0 || arg == 0) {
        errno = EINVAL;
        return -1;
    }
    argv[argc++] = (char *)arg;
    va_start(ap, arg);
    while (argc < 31 && (a = va_arg(ap, const char *)) != 0) {
        argv[argc++] = (char *)a;
    }
    va_end(ap);
    if (a != 0) {
        /* more args than the registry path transports */
        errno = E2BIG;
        return -1;
    }
    argv[argc] = 0;
    return execve(path, argv, environ);
}

/* POSIX wait(): the options-0 form of waitpid(). links2's select.c reaper
 * calls it; the symbol was declared but absent, exactly like execl(). */
pid_t wait(int *status) {
    return waitpid((pid_t)-1, status, 0);
}

/*
 * execvp(): same registry dispatch as execve (argv[0] basename resolves
 * the AppFS name), with the ambient environ.
 */
int execvp(const char *file, char *const argv[]) {
    return execve(file, argv, environ);
}

/*
 * system(): fork + execv of the whitespace-tokenized command + waitpid.
 * This OS has no /bin/sh to spawn for quoted-command semantics, so the
 * documented simplification is whitespace tokenization only (no quote,
 * redirect or glob processing); non-zero exit is reported via the
 * wait-status like POSIX. links2's call sites (external viewers, stty
 * resets on error paths) stay reachable instead of failing the link.
 */
int system(const char *command) {
    char *argv[32];
    const char *p;
    int argc = 0;
    int status = 0;
    pid_t pid;

    if (command == 0) {
        return 0; /* shell-existence probe: no sh here; 0 == false */
    }
    p = command;
    while (*p != '\0' && argc < 31) {
        while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = (char *)p;
        while (*p != '\0' && *p != ' ' && !(*p >= '\t' && *p <= '\r')) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
    }
    if (argc == 0) {
        return 0; /* empty command: success per POSIX */
    }
    argv[argc] = 0;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return status;
}
