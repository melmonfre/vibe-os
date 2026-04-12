#include <userland/modules/include/terminal.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/busybox.h>

/* default geometry used when a new terminal is spawned */
static const struct rect DEFAULT_TERMINAL_WINDOW = {10, 24, 400, 300};

/* Global state for output capture during command execution */
static struct terminal_state *g_term_capture_ctx = 0;
static char g_output_line_buffer[256];
static int g_output_line_pos = 0;

static void terminal_debug_cmd(const char *prefix, const char *cmd) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void terminal_append_int(char *buf, int max_len, int value) {
    char digits[16];
    unsigned int magnitude;
    int length = 0;

    if (max_len <= 0) {
        return;
    }
    if (value < 0) {
        str_append(buf, "-", max_len);
        magnitude = (unsigned int)(-value);
    } else {
        magnitude = (unsigned int)value;
    }
    if (magnitude == 0u) {
        str_append(buf, "0", max_len);
        return;
    }
    while (magnitude != 0u && length < (int)sizeof(digits)) {
        digits[length++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (length > 0) {
        char text[2];
        text[0] = digits[--length];
        text[1] = '\0';
        str_append(buf, text, max_len);
    }
}

static void terminal_debug_cmd_status(const char *cmd, int rc) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, "terminal: command exit ", (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, " rc=", (int)sizeof(msg));
    terminal_append_int(msg, (int)sizeof(msg), rc);
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

/* Callback that receives console output and adds to terminal */
static void terminal_output_callback(const char *buf, int len) {
    int i;
    
    if (g_term_capture_ctx == 0) return;
    
    for (i = 0; i < len; ++i) {
        char c = buf[i];
        
        if (c == '\n') {
            g_output_line_buffer[g_output_line_pos] = '\0';
            if (g_output_line_pos > 0) {
                terminal_push_line(g_term_capture_ctx, g_output_line_buffer);
            }
            g_output_line_pos = 0;
        } else if (c != '\r' && g_output_line_pos < 255) {
            g_output_line_buffer[g_output_line_pos++] = c;
        }
    }
}

void terminal_init_state(struct terminal_state *t) {
    t->window = DEFAULT_TERMINAL_WINDOW;
    t->line_count = 0;
    t->input_len = 0;
    t->input[0] = '\0';
}

void terminal_push_line(struct terminal_state *t, const char *text) {
    int i;
    int n = str_len(text);

    if (t->line_count == TERM_ROWS) {
        for (i = 1; i < TERM_ROWS; ++i) {
            int j = 0;
            while (t->lines[i][j] != '\0') {
                t->lines[i - 1][j] = t->lines[i][j];
                ++j;
            }
            t->lines[i - 1][j] = '\0';
        }
        t->line_count = TERM_ROWS - 1;
    }

    /* Store full line without truncation - wrapping happens at render time */
    if (n > TERM_COLS - 1) {
        n = TERM_COLS - 1;
    }
    for (i = 0; i < n; ++i) {
        t->lines[t->line_count][i] = text[i];
    }
    t->lines[t->line_count][n] = '\0';
    ++t->line_count;
}

void terminal_clear_lines(struct terminal_state *t) {
    t->line_count = 0;
}

void terminal_add_input_char(struct terminal_state *t, char c) {
    if (t->input_len < INPUT_MAX) {
        t->input[t->input_len++] = c;
        t->input[t->input_len] = '\0';
    }
}

void terminal_backspace(struct terminal_state *t) {
    if (t->input_len > 0) {
        --t->input_len;
        t->input[t->input_len] = '\0';
    }
}

const char *terminal_get_input(struct terminal_state *t) {
    return t->input;
}

void terminal_reset_input(struct terminal_state *t) {
    t->input_len = 0;
    t->input[0] = '\0';
}

int terminal_execute_command(struct terminal_state *t) {
    char line[INPUT_MAX + 1];
    char *argv[32];
    char *cursor;
    int argc;
    int i;
    int rc;
    int use_external_handoff = 0;

    for (i = 0; i < t->input_len && i < INPUT_MAX; ++i) {
        line[i] = t->input[i];
    }
    line[i] = '\0';

    {
        char cwd[52];
        char prompt_line[256];
        fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
        prompt_line[0] = '\0';
        str_append(prompt_line, "[", (int)sizeof(prompt_line));
        str_append(prompt_line, cwd, (int)sizeof(prompt_line));
        str_append(prompt_line, "] ", (int)sizeof(prompt_line));
        str_append(prompt_line, line, (int)sizeof(prompt_line));
        terminal_push_line(t, prompt_line);
    }

    cursor = line;
    
    /* Tokenize command line */
    argc = 0;
    while (argc < 31) {
        char *token = next_token(&cursor);
        if (!token) break;
        argv[argc++] = token;
    }
    argv[argc] = 0;

    if (argc == 0) {
        terminal_reset_input(t);
        return 0;
    }

    use_external_handoff = busybox_command_uses_external_app(argc, argv);

    terminal_debug_cmd("shell: command ", argv[0]);
    terminal_debug_cmd("terminal: command start ", argv[0]);

    if (!use_external_handoff) {
        /* Set up output capture for builtins that really run inline here. */
        g_term_capture_ctx = t;
        g_output_line_pos = 0;
        console_set_output_handler(terminal_output_callback);
    } else {
        /*
         * External modular apps are not attached to the embedded terminal
         * buffer. Hand them the real text console instead of blocking the
         * desktop while pretending output will appear in-window.
         */
        sys_leave_graphics();
    }

    rc = busybox_main(argc, argv);

    if (!use_external_handoff && g_output_line_pos > 0) {
        g_output_line_buffer[g_output_line_pos] = '\0';
        terminal_push_line(t, g_output_line_buffer);
        g_output_line_pos = 0;
    }

    if (!use_external_handoff) {
        console_set_output_handler(0);
        g_term_capture_ctx = 0;
    }

    terminal_debug_cmd_status(argv[0], rc);
    terminal_debug_cmd("terminal: command done ", argv[0]);

    terminal_reset_input(t);
    return 0;
}

int terminal_run_command(struct terminal_state *t, const char *command, int clear_before) {
    int i = 0;

    if (clear_before) {
        terminal_clear_lines(t);
    }

    terminal_reset_input(t);
    while (command[i] != '\0' && i < INPUT_MAX) {
        t->input[i] = command[i];
        ++i;
    }
    t->input[i] = '\0';
    t->input_len = i;
    return terminal_execute_command(t);
}

void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {t->window.x + 4, t->window.y + 18, t->window.w - 8, t->window.h - 22};
    struct rect hero = {t->window.x + 10, t->window.y + 24, t->window.w - 20, 26};
    struct rect log = {t->window.x + 10, t->window.y + 58, t->window.w - 20, t->window.h - 100};
    struct rect input = {t->window.x + 10, t->window.y + t->window.h - 34, t->window.w - 20, 18};
    int text_x = log.x + 4;
    int text_y = log.y + 4;
    int max_display_lines = (log.h - 8) / 8;
    int max_display_cols = (log.w - 8) / 8;
    int i, j;
    
    if (max_display_lines < 1) max_display_lines = 1;
    if (max_display_cols < 20) max_display_cols = 20;

    draw_window_frame(&t->window, "TERMINAL", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&hero, ui_color_panel());
    ui_draw_inset(&log, theme->window_bg);
    ui_draw_inset(&input, theme->window_bg);

    sys_text(hero.x + 6, hero.y + 5, theme->text, "Shell integrada");
    sys_text(hero.x + hero.w - 116, hero.y + 5, ui_color_muted(), "busybox + apps");

    /* Render lines - simple wrapping */
    for (i = 0; i < t->line_count && i < max_display_lines; ++i) {
        const char *line = t->lines[i];
        int line_len = str_len(line);
        
        if (line_len <= max_display_cols) {
            sys_text(text_x, text_y + (i * 8), theme->text, line);
        } else {
            /* For wrapped lines, only show first portion in this simple version */
            char wrapped[129];
            int show_len = line_len > 128 ? 128 : line_len;
            if (show_len > max_display_cols) show_len = max_display_cols;
            
            for (j = 0; j < show_len; ++j) {
                wrapped[j] = line[j];
            }
            wrapped[show_len] = '\0';
            sys_text(text_x, text_y + (i * 8), theme->text, wrapped);
        }
    }

    /* Draw input line */
    {
        char input_line[130];
        int max_input_display = (input.w - 8) / 8;
        int n = 0;
        
        if (max_input_display < 20) max_input_display = 20;
        
        input_line[n++] = '>';
        input_line[n++] = ' ';
        for (i = 0; i < t->input_len && n < max_input_display - 1; ++i) {
            input_line[n++] = t->input[i];
        }
        input_line[n] = '\0';
        sys_text(input.x + 4, input.y + 5, theme->text, input_line);
    }
}
