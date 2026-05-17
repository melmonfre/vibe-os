#include <userland/modules/include/terminal.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/busybox.h>

static const struct rect DEFAULT_TERMINAL_WINDOW = {10, 24, 432, 320};

static struct terminal_state *g_term_capture_ctx = 0;
static char g_output_line_buffer[TERM_COLS + 1];
static int g_output_line_pos = 0;

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

static void terminal_debug_cmd(const char *prefix, const char *cmd) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
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

static void terminal_capture_flush_line(void) {
    if (g_term_capture_ctx == 0) {
        g_output_line_pos = 0;
        return;
    }
    g_output_line_buffer[g_output_line_pos] = '\0';
    terminal_push_line(g_term_capture_ctx, g_output_line_buffer);
    g_output_line_pos = 0;
}

static void terminal_output_callback(const char *buf, int len) {
    for (int i = 0; i < len; ++i) {
        char c = buf[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            terminal_capture_flush_line();
            continue;
        }
        if (g_output_line_pos >= TERM_COLS) {
            terminal_capture_flush_line();
        }
        g_output_line_buffer[g_output_line_pos++] = c;
    }
}

static void terminal_build_cwd(char *buf, int max_len) {
    fs_build_path(g_fs_cwd, buf, max_len);
}

static void terminal_build_prompt(char *buf, int max_len) {
    char cwd[96];

    terminal_build_cwd(cwd, (int)sizeof(cwd));
    buf[0] = '\0';
    str_append(buf, "[", max_len);
    str_append(buf, cwd, max_len);
    str_append(buf, "] $ ", max_len);
}

static void terminal_history_add(struct terminal_state *t, const char *line) {
    int last_index;

    if (!line || line[0] == '\0') {
        return;
    }
    if (t->history_count > 0) {
        last_index = t->history_next - 1;
        if (last_index < 0) {
            last_index += TERM_HISTORY_MAX;
        }
        if (str_eq(t->history[last_index], line)) {
            return;
        }
    }
    str_copy_limited(t->history[t->history_next], line, INPUT_MAX + 1);
    t->history_next = (t->history_next + 1) % TERM_HISTORY_MAX;
    if (t->history_count < TERM_HISTORY_MAX) {
        t->history_count += 1;
    }
}

static int terminal_history_index_from_view(const struct terminal_state *t, int view) {
    int idx = t->history_next - 1 - view;

    while (idx < 0) {
        idx += TERM_HISTORY_MAX;
    }
    return idx % TERM_HISTORY_MAX;
}

static void terminal_set_input(struct terminal_state *t, const char *text) {
    t->input_len = 0;
    while (text[t->input_len] != '\0' && t->input_len < INPUT_MAX) {
        t->input[t->input_len] = text[t->input_len];
        t->input_len += 1;
    }
    t->input[t->input_len] = '\0';
    t->input_cursor = t->input_len;
}

void terminal_init_state(struct terminal_state *t) {
    t->window = DEFAULT_TERMINAL_WINDOW;
    t->line_count = 0;
    t->input_len = 0;
    t->input[0] = '\0';
    t->input_cursor = 0;
    t->history_count = 0;
    t->history_next = 0;
    t->history_view = -1;
    t->draft[0] = '\0';
    t->draft_saved = 0;
}

void terminal_push_line(struct terminal_state *t, const char *text) {
    int n = str_len(text);

    if (t->line_count == TERM_SCROLLBACK) {
        for (int row = 1; row < TERM_SCROLLBACK; ++row) {
            str_copy_limited(t->lines[row - 1], t->lines[row], TERM_COLS + 1);
        }
        t->line_count = TERM_SCROLLBACK - 1;
    }

    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    for (int i = 0; i < n; ++i) {
        t->lines[t->line_count][i] = text[i];
    }
    t->lines[t->line_count][n] = '\0';
    t->line_count += 1;
}

void terminal_clear_lines(struct terminal_state *t) {
    t->line_count = 0;
}

void terminal_add_input_char(struct terminal_state *t, char c) {
    if (t->input_len >= INPUT_MAX) {
        return;
    }
    for (int i = t->input_len; i > t->input_cursor; --i) {
        t->input[i] = t->input[i - 1];
    }
    t->input[t->input_cursor] = c;
    t->input_len += 1;
    t->input_cursor += 1;
    t->input[t->input_len] = '\0';
    t->history_view = -1;
    t->draft_saved = 0;
}

void terminal_backspace(struct terminal_state *t) {
    if (t->input_cursor <= 0 || t->input_len <= 0) {
        return;
    }
    for (int i = t->input_cursor - 1; i < t->input_len; ++i) {
        t->input[i] = t->input[i + 1];
    }
    t->input_cursor -= 1;
    t->input_len -= 1;
    t->history_view = -1;
    t->draft_saved = 0;
}

void terminal_delete_char(struct terminal_state *t) {
    if (t->input_cursor >= t->input_len) {
        return;
    }
    for (int i = t->input_cursor; i < t->input_len; ++i) {
        t->input[i] = t->input[i + 1];
    }
    t->input_len -= 1;
    t->history_view = -1;
    t->draft_saved = 0;
}

void terminal_move_cursor_left(struct terminal_state *t) {
    if (t->input_cursor > 0) {
        t->input_cursor -= 1;
    }
}

void terminal_move_cursor_right(struct terminal_state *t) {
    if (t->input_cursor < t->input_len) {
        t->input_cursor += 1;
    }
}

void terminal_history_prev(struct terminal_state *t) {
    if (t->history_count <= 0 || t->history_view + 1 >= t->history_count) {
        return;
    }
    if (!t->draft_saved) {
        str_copy_limited(t->draft, t->input, (int)sizeof(t->draft));
        t->draft_saved = 1;
    }
    t->history_view += 1;
    terminal_set_input(t, t->history[terminal_history_index_from_view(t, t->history_view)]);
}

void terminal_history_next(struct terminal_state *t) {
    if (t->history_view > 0) {
        t->history_view -= 1;
        terminal_set_input(t, t->history[terminal_history_index_from_view(t, t->history_view)]);
        return;
    }
    if (t->history_view == 0) {
        t->history_view = -1;
        if (t->draft_saved) {
            terminal_set_input(t, t->draft);
        } else {
            terminal_reset_input(t);
        }
        t->draft_saved = 0;
    }
}

const char *terminal_get_input(struct terminal_state *t) {
    return t->input;
}

void terminal_reset_input(struct terminal_state *t) {
    t->input_len = 0;
    t->input_cursor = 0;
    t->input[0] = '\0';
    t->history_view = -1;
    t->draft_saved = 0;
    t->draft[0] = '\0';
}

int terminal_execute_command(struct terminal_state *t) {
    char line[INPUT_MAX + 1];
    char prompt[120];
    char prompt_line[TERM_COLS + 1];
    char *argv[32];
    char *cursor;
    int argc;
    int rc;

    for (int i = 0; i < t->input_len && i < INPUT_MAX; ++i) {
        line[i] = t->input[i];
    }
    line[t->input_len] = '\0';

    terminal_build_prompt(prompt, (int)sizeof(prompt));
    prompt_line[0] = '\0';
    str_append(prompt_line, prompt, (int)sizeof(prompt_line));
    str_append(prompt_line, line, (int)sizeof(prompt_line));
    terminal_push_line(t, prompt_line);

    cursor = line;
    argc = 0;
    while (argc < 31) {
        char *token = next_token(&cursor);

        if (!token) {
            break;
        }
        argv[argc++] = token;
    }
    argv[argc] = 0;

    if (argc == 0) {
        terminal_reset_input(t);
        return 0;
    }

    terminal_history_add(t, line);

    if (str_eq(argv[0], "clear")) {
        terminal_clear_lines(t);
        terminal_reset_input(t);
        return 0;
    }

    terminal_debug_cmd("terminal: command start ", argv[0]);
    g_term_capture_ctx = t;
    g_output_line_pos = 0;
    console_set_output_handler(terminal_output_callback);
    rc = busybox_main(argc, argv);
    if (g_output_line_pos > 0) {
        terminal_capture_flush_line();
    }
    console_set_output_handler(0);
    g_term_capture_ctx = 0;
    terminal_debug_cmd_status(argv[0], rc);

    terminal_reset_input(t);
    if ((str_eq(argv[0], "exit") || str_eq(argv[0], "shutdown")) && rc != 0) {
        return 1;
    }
    return 0;
}

int terminal_run_command(struct terminal_state *t, const char *command, int clear_before) {
    if (clear_before) {
        terminal_clear_lines(t);
    }
    terminal_set_input(t, command);
    return terminal_execute_command(t);
}

static void terminal_draw_recent_lines(const struct terminal_state *t,
                                       const struct rect *log,
                                       int max_display_lines,
                                       int max_display_cols,
                                       uint8_t color) {
    int remaining = max_display_lines;

    clip_push(log->x + 3, log->y + 3, log->w - 6, log->h - 6);
    for (int line_index = t->line_count - 1; line_index >= 0 && remaining > 0; --line_index) {
        const char *line = t->lines[line_index];
        int line_len = str_len(line);
        int segment_count = (line_len + max_display_cols - 1) / max_display_cols;

        if (line_len == 0) {
            segment_count = 1;
        }
        for (int segment = segment_count - 1; segment >= 0 && remaining > 0; --segment) {
            char chunk[TERM_COLS + 1];
            int start = segment * max_display_cols;
            int chunk_len = line_len - start;
            int draw_y;

            if (chunk_len < 0) {
                chunk_len = 0;
            }
            if (chunk_len > max_display_cols) {
                chunk_len = max_display_cols;
            }
            for (int i = 0; i < chunk_len; ++i) {
                chunk[i] = line[start + i];
            }
            chunk[chunk_len] = '\0';
            draw_y = log->y + 4 + ((remaining - 1) * 8);
            sys_text(log->x + 4, draw_y, color, chunk);
            remaining -= 1;
        }
    }
    clip_pop();
}

void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {t->window.x + 4, t->window.y + 18, t->window.w - 8, t->window.h - 22};
    struct rect hero = {t->window.x + 10, t->window.y + 24, t->window.w - 20, 30};
    struct rect log = {t->window.x + 10, t->window.y + 62, t->window.w - 20, t->window.h - 106};
    struct rect input = {t->window.x + 10, t->window.y + t->window.h - 36, t->window.w - 20, 20};
    struct rect hero_main = {hero.x + 6, hero.y, hero.w - 166, hero.h};
    struct rect hero_hint = {hero.x + hero.w - 154, hero.y, 148, hero.h};
    int max_display_lines = (log.h - 8) / 8;
    int max_display_cols = (log.w - 10) / 8;
    char cwd[96];
    char cwd_fit[96];
    char input_line[INPUT_MAX + 16];
    char hint_fit[64];
    const char *prompt = "$ ";
    int prompt_len = 2;
    int input_capacity;
    int input_start = 0;
    int visible_input_len;
    int cursor_in_view;

    if (max_display_lines < 1) {
        max_display_lines = 1;
    }
    if (max_display_cols < 8) {
        max_display_cols = 8;
    }

    terminal_build_cwd(cwd, (int)sizeof(cwd));
    draw_window_frame(&t->window, "TERMINAL", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&hero, ui_color_panel());
    ui_draw_inset(&log, theme->window_bg);
    ui_draw_inset(&input, theme->window_bg);

    ui_text_copy_fit(cwd_fit, (int)sizeof(cwd_fit), cwd, hero_main.w);
    ui_text_copy_fit(hint_fit,
                     (int)sizeof(hint_fit),
                     "builtin + apps sem sair do desktop",
                     hero_hint.w);
    ui_draw_text_clipped(&hero_main, hero.x + 6, hero.y + 5, theme->text, "Vibe Shell");
    ui_draw_text_clipped(&hero_main, hero.x + 6, hero.y + 17, ui_color_muted(), cwd_fit);
    ui_draw_text_clipped(&hero_hint, hero_hint.x, hero.y + 11, ui_color_muted(), hint_fit);

    terminal_draw_recent_lines(t, &log, max_display_lines, max_display_cols, theme->text);

    input_capacity = (input.w - 10) / 8;
    if (input_capacity < prompt_len + 1) {
        input_capacity = prompt_len + 1;
    }
    input_capacity -= prompt_len;
    if (t->input_cursor > input_capacity) {
        input_start = t->input_cursor - input_capacity;
    }
    if (t->input_len - input_start > input_capacity) {
        visible_input_len = input_capacity;
    } else {
        visible_input_len = t->input_len - input_start;
    }
    if (visible_input_len < 0) {
        visible_input_len = 0;
    }

    input_line[0] = '\0';
    str_append(input_line, prompt, (int)sizeof(input_line));
    for (int i = 0; i < visible_input_len && prompt_len + i < (int)sizeof(input_line) - 1; ++i) {
        input_line[prompt_len + i] = t->input[input_start + i];
        input_line[prompt_len + i + 1] = '\0';
    }
    ui_draw_text_clipped(&input, input.x + 4, input.y + 6, theme->text, input_line);

    cursor_in_view = t->input_cursor - input_start;
    if (cursor_in_view < 0) {
        cursor_in_view = 0;
    }
    if (cursor_in_view > input_capacity) {
        cursor_in_view = input_capacity;
    }
    sys_rect(input.x + 4 + ((prompt_len + cursor_in_view) * 8),
             input.y + input.h - 5,
             6,
             1,
             theme->text);
}
