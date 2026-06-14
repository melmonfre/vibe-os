#include <userland/modules/include/shell.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <stdint.h>
#include <stddef.h>

extern int ksh_main(int argc, char *argv[]);

#define BSD_ENV_MAX 32

static char *g_bsd_environ[BSD_ENV_MAX];
static int g_bsd_environ_count;
extern char **environ;

static void *bsd_alloc(size_t sz)
{
    extern void *vibe_app_malloc(unsigned long);
    return vibe_app_malloc((unsigned long)sz);
}

static int bsd_setenv(const char *name, const char *value, int overwrite)
{
    char *entry;
    size_t nl, vl;
    int i;

    if (name == 0 || name[0] == '\0' || value == 0)
        return -1;
    nl = 0;
    while (name[nl] != '\0') nl++;
    vl = 0;
    while (value[vl] != '\0') vl++;
    entry = (char *)bsd_alloc(nl + vl + 2);
    if (entry == 0) return -1;
    for (i = 0; i < (int)nl; i++) entry[i] = name[i];
    entry[nl] = '=';
    for (i = 0; i < (int)vl; i++) entry[nl + 1 + i] = value[i];
    entry[nl + 1 + vl] = '\0';
    for (i = 0; i < g_bsd_environ_count; i++) {
        size_t enl = 0;
        if (g_bsd_environ[i] == 0) continue;
        while (g_bsd_environ[i][enl] != '\0' && g_bsd_environ[i][enl] != '=')
            enl++;
        if (enl == nl) {
            int match = 1;
            size_t j;
            for (j = 0; j < nl; j++) {
                if (g_bsd_environ[i][j] != name[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                if (overwrite) {
                    g_bsd_environ[i] = entry;
                }
                return 0;
            }
        }
    }
    if (g_bsd_environ_count < BSD_ENV_MAX - 1) {
        g_bsd_environ[g_bsd_environ_count++] = entry;
        g_bsd_environ[g_bsd_environ_count] = 0;
        environ = g_bsd_environ;
    }
    return 0;
}

static void bsd_shell_setup_environ(void)
{
    g_bsd_environ_count = 0;
    environ = g_bsd_environ;
    bsd_setenv("PATH", "/bin:/usr/bin:/compat/bin", 1);
    bsd_setenv("TERM", "vt100", 1);
    bsd_setenv("USER", "root", 1);
    bsd_setenv("HOME", "/", 1);
    bsd_setenv("SHELL", "/bin/ksh", 1);
    bsd_setenv("PWD", "/", 1);
    bsd_setenv("IFS", " \t\n", 1);
    bsd_setenv("PS1", "$ ", 1);
    bsd_setenv("PS2", "> ", 1);
}

int shell_bsd_main(int argc, char **argv)
{
    char *ksh_argv[8];
    char ksh_name[16];
    int ksh_argc;

    (void)argc;
    (void)argv;

    console_init();
    fs_init();

    bsd_shell_setup_environ();

    ksh_name[0] = '\0';
    str_append(ksh_name, "ksh", (int)sizeof(ksh_name));
    ksh_argv[0] = ksh_name;
    ksh_argv[1] = "-i";
    ksh_argv[2] = 0;
    ksh_argc = 2;

    sys_write_debug("shell_bsd: starting pdksh\n");

    return ksh_main(ksh_argc, ksh_argv);
}
