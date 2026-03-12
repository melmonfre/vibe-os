#include <userland/modules/include/shell.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <stdint.h>

#define LINE_MAX 128
#define SHELL_MAX_ARGS 16
#define SHELL_HISTORY_MAX 16

static char g_shell_history[SHELL_HISTORY_MAX][LINE_MAX];
static int g_shell_history_count = 0;
static int g_shell_history_next = 0;

static void prompt_print(void) {
    char cwd[80];

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    console_write("user@vibe-os:");
    console_write(cwd);
    console_write(" % ");
}

static void echo_backspace(void) {
    console_putc('\b');
    console_putc(' ');
    console_putc('\b');
}

void shell_history_add(const char *line) {
    if (line == 0 || line[0] == '\0') {
        return;
    }

    str_copy_limited(g_shell_history[g_shell_history_next], line, LINE_MAX);
    g_shell_history_next = (g_shell_history_next + 1) % SHELL_HISTORY_MAX;
    if (g_shell_history_count < SHELL_HISTORY_MAX) {
        ++g_shell_history_count;
    }
}

void shell_history_print(void) {
    if (g_shell_history_count == 0) {
        console_write("(empty)\n");
        return;
    }

    for (int i = 0; i < g_shell_history_count; ++i) {
        int idx = g_shell_history_next - g_shell_history_count + i;
        if (idx < 0) {
            idx += SHELL_HISTORY_MAX;
        }
        console_write(g_shell_history[idx]);
        console_putc('\n');
    }
}

static int tokenize_line(char *line, char **argv, int max_args) {
    char *cursor = line;
    int argc = 0;

    while (argc < max_args) {
        char *token = next_token(&cursor);
        if (token == 0) {
            break;
        }
        argv[argc++] = token;
    }
    argv[argc] = 0;
    return argc;
}

static int read_line(char *buf, int maxlen) {
    int len = 0;

    for (;;) {
        int c = sys_poll_key();

        if (c == 0) {
            sys_yield();
            continue;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == '\n') {
            buf[len] = '\0';
            console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            echo_backspace();
            continue;
        }

        if (c >= 32 && c < 127 && len < maxlen - 1) {
            buf[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

void shell_start(void) {
    char line[LINE_MAX];
    char *argv[SHELL_MAX_ARGS + 1];

    console_init();
    fs_init();

    for (;;) {
        int argc;

        prompt_print();

        if (read_line(line, LINE_MAX) == 0) {
            continue;
        }

        shell_history_add(line);
        argc = tokenize_line(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        if (busybox_main(argc, argv) != 0) {
            break;
        }
    }

    console_write("bye\n");
}
