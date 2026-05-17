#ifndef TERMINAL_H
#define TERMINAL_H

#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

#define TERM_COLS 512
#define TERM_ROWS 14
#define TERM_SCROLLBACK 256
#define TERM_HISTORY_MAX 16
#define INPUT_MAX 128

/* each terminal instance */
struct terminal_state {
    struct rect window;
    char lines[TERM_SCROLLBACK][TERM_COLS + 1];
    int line_count;
    char input[INPUT_MAX + 1];
    int input_len;
    int input_cursor;
    char history[TERM_HISTORY_MAX][INPUT_MAX + 1];
    int history_count;
    int history_next;
    int history_view;
    char draft[INPUT_MAX + 1];
    int draft_saved;
};

/* lifecycle */
void terminal_init_state(struct terminal_state *t);
void terminal_push_line(struct terminal_state *t, const char *text);
void terminal_clear_lines(struct terminal_state *t);

/* input */
void terminal_add_input_char(struct terminal_state *t, char c);
void terminal_backspace(struct terminal_state *t);
void terminal_delete_char(struct terminal_state *t);
void terminal_move_cursor_left(struct terminal_state *t);
void terminal_move_cursor_right(struct terminal_state *t);
void terminal_history_prev(struct terminal_state *t);
void terminal_history_next(struct terminal_state *t);
const char *terminal_get_input(struct terminal_state *t);
void terminal_reset_input(struct terminal_state *t);

/* execute/draw */
int terminal_execute_command(struct terminal_state *t);
int terminal_run_command(struct terminal_state *t, const char *command, int clear_before);
void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover);

#endif // TERMINAL_H
