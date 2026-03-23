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

static void build_prompt(char *out, int max_len) {
    char cwd[80];

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    out[0] = '\0';
    str_append(out, "user@vibe-os:", max_len);
    str_append(out, cwd, max_len);
    str_append(out, " % ", max_len);
}

static void prompt_print(const char *prompt) {
    console_write(prompt);
}

static int history_index_from_view(int history_view) {
    int idx = g_shell_history_next - 1 - history_view;

    while (idx < 0) {
        idx += SHELL_HISTORY_MAX;
    }
    return idx % SHELL_HISTORY_MAX;
}

static void load_line_state(char *buf, int maxlen, int *len, int *cursor, const char *src) {
    *len = 0;
    while (src[*len] != '\0' && *len < (maxlen - 1)) {
        buf[*len] = src[*len];
        *len += 1;
    }
    buf[*len] = '\0';
    *cursor = *len;
}

static void render_line(const char *prompt,
                        const char *buf,
                        int len,
                        int cursor,
                        int *previous_len) {
    int clear_tail = *previous_len - len;
    int back = 0;

    if (clear_tail < 0) {
        clear_tail = 0;
    }

    console_putc('\r');
    console_write(prompt);
    for (int i = 0; i < len; ++i) {
        console_putc(buf[i]);
    }
    for (int i = 0; i < clear_tail; ++i) {
        console_putc(' ');
    }

    back = clear_tail + (len - cursor);
    if (back > 0) {
        console_move_cursor(-back);
    }

    *previous_len = len;
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

static int read_line(char *buf, int maxlen, const char *prompt) {
    int len = 0;
    int cursor = 0;
    int previous_len = 0;
    int max_input = maxlen - 1;
    int history_view = -1;
    char draft[LINE_MAX];
    int draft_saved = 0;

    if (str_len(prompt) < 79 && max_input > 79 - str_len(prompt)) {
        max_input = 79 - str_len(prompt);
    }
    if (max_input < 1) {
        max_input = 1;
    }

    draft[0] = '\0';

    for (;;) {
        int c = sys_poll_key();

        if (c == 0) {
            fs_tick();
            sys_yield();
            continue;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == 3) {
            buf[0] = '\0';
            console_write("^C\n");
            return -1;
        }

        if (c == '\n') {
            buf[len] = '\0';
            console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && cursor > 0) {
            for (int i = cursor - 1; i < len; ++i) {
                buf[i] = buf[i + 1];
            }
            cursor -= 1;
            len -= 1;
            render_line(prompt, buf, len, cursor, &previous_len);
            continue;
        }

        if (c == KEY_DELETE && cursor < len) {
            for (int i = cursor; i < len; ++i) {
                buf[i] = buf[i + 1];
            }
            len -= 1;
            render_line(prompt, buf, len, cursor, &previous_len);
            continue;
        }

        if (c == KEY_ARROW_LEFT) {
            if (cursor > 0) {
                cursor -= 1;
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_RIGHT) {
            if (cursor < len) {
                cursor += 1;
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_UP) {
            if (g_shell_history_count > 0 && history_view + 1 < g_shell_history_count) {
                if (!draft_saved) {
                    str_copy_limited(draft, buf, sizeof(draft));
                    draft_saved = 1;
                }
                history_view += 1;
                load_line_state(buf, maxlen, &len, &cursor,
                                g_shell_history[history_index_from_view(history_view)]);
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_DOWN) {
            if (history_view > 0) {
                history_view -= 1;
                load_line_state(buf, maxlen, &len, &cursor,
                                g_shell_history[history_index_from_view(history_view)]);
                render_line(prompt, buf, len, cursor, &previous_len);
            } else if (history_view == 0) {
                history_view = -1;
                if (draft_saved) {
                    load_line_state(buf, maxlen, &len, &cursor, draft);
                } else {
                    len = 0;
                    cursor = 0;
                    buf[0] = '\0';
                }
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c >= 32 && c < 127 && len < max_input) {
            for (int i = len; i > cursor; --i) {
                buf[i] = buf[i - 1];
            }
            buf[cursor] = (char)c;
            cursor += 1;
            len += 1;
            buf[len] = '\0';
            history_view = -1;
            render_line(prompt, buf, len, cursor, &previous_len);
        }
    }
}

void shell_start_ready(void) {
    char line[LINE_MAX];
    char *argv[SHELL_MAX_ARGS + 1];
    char prompt[96];

    for (;;) {
        int argc;
        int line_len;

        build_prompt(prompt, (int)sizeof(prompt));
        prompt_print(prompt);

        line_len = read_line(line, LINE_MAX, prompt);
        if (line_len < 0) {
            continue;
        }
        if (line_len == 0) {
            continue;
        }

        shell_history_add(line);
        argc = tokenize_line(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            fs_tick();
            continue;
        }

        if (busybox_main(argc, argv) != 0) {
            fs_flush();
            break;
        }
    }

    fs_flush();
    console_write("bye\n");
}

void shell_start(void) {
    console_init();
    fs_init();
    shell_start_ready();
}
