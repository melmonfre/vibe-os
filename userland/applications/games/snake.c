#include <userland/applications/include/games/snake.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_SNAKE_WINDOW = {40, 20, 400, 300};
static const int SNAKE_STEP_TICKS = 12;

static void snake_draw_cell(int x, int y, int cell, uint8_t base, uint8_t hi, uint8_t lo) {
    int inner = cell - 3;

    if (inner < 2) {
        inner = 2;
    }
    sys_rect(x, y, cell - 1, cell - 1, lo);
    sys_rect(x + 1, y + 1, cell - 3, cell - 3, base);
    sys_rect(x + 1, y + 1, cell - 3, 1, hi);
    sys_rect(x + 1, y + 1, 1, cell - 3, hi);
    sys_rect(x + 2, y + 2, inner, inner, base);
}

static uint32_t snake_next_random(struct snake_state *snake) {
    snake->seed = (snake->seed * 1103515245u) + 12345u;
    return snake->seed;
}

static void snake_place_food(struct snake_state *snake) {
    for (;;) {
        int occupied = 0;
        int food_x;
        int food_y;

        food_x = (int)(snake_next_random(snake) % SNAKE_GRID_W);
        food_y = (int)(snake_next_random(snake) % SNAKE_GRID_H);
        for (int i = 0; i < snake->length; ++i) {
            if (snake->body_x[i] == food_x && snake->body_y[i] == food_y) {
                occupied = 1;
                break;
            }
        }
        if (!occupied) {
            snake->food_x = food_x;
            snake->food_y = food_y;
            return;
        }
    }
}

static void snake_reset(struct snake_state *snake) {
    snake->length = 4;
    snake->body_x[0] = 9;
    snake->body_y[0] = 7;
    snake->body_x[1] = 8;
    snake->body_y[1] = 7;
    snake->body_x[2] = 7;
    snake->body_y[2] = 7;
    snake->body_x[3] = 6;
    snake->body_y[3] = 7;
    snake->dir_x = 1;
    snake->dir_y = 0;
    snake->next_dir_x = 1;
    snake->next_dir_y = 0;
    snake->next_tick = 0u;
    snake->tick_count = 0u;
    snake->score = 0;
    snake->game_over = 0;
    if (snake->seed == 0u) {
        snake->seed = 1u;
    }
    snake_place_food(snake);
}

static int snake_hits_self(const struct snake_state *snake, int x, int y) {
    for (int i = 0; i < snake->length; ++i) {
        if (snake->body_x[i] == x && snake->body_y[i] == y) {
            return 1;
        }
    }
    return 0;
}

static void snake_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = (unsigned)value;

    if (len >= max_len - 1) {
        return;
    }

    if (uvalue == 0u) {
        tmp[pos++] = '0';
    } else {
        while (uvalue > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (uvalue % 10u));
            uvalue /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

void snake_init_state(struct snake_state *snake) {
    snake->window = DEFAULT_SNAKE_WINDOW;
    snake->seed = 1u;
    snake_reset(snake);
}

int snake_handle_key(struct snake_state *snake, int key) {
    if (snake->game_over) {
        if (key == ' ' || key == '\n' || key == 'r' || key == 'R') {
            snake_reset(snake);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_UP && snake->dir_y == 0) {
        snake->next_dir_x = 0;
        snake->next_dir_y = -1;
        return 1;
    }
    if (key == KEY_ARROW_DOWN && snake->dir_y == 0) {
        snake->next_dir_x = 0;
        snake->next_dir_y = 1;
        return 1;
    }
    if (key == KEY_ARROW_LEFT && snake->dir_x == 0) {
        snake->next_dir_x = -1;
        snake->next_dir_y = 0;
        return 1;
    }
    if (key == KEY_ARROW_RIGHT && snake->dir_x == 0) {
        snake->next_dir_x = 1;
        snake->next_dir_y = 0;
        return 1;
    }
    return 0;
}

int snake_step(struct snake_state *snake, uint32_t ticks) {
    int new_x;
    int new_y;
    int grow = 0;
    (void)ticks;

    if (snake->game_over) {
        return 0;
    }

    snake->tick_count += 1u;

    if (snake->next_tick == 0u) {
        snake->next_tick = snake->tick_count + SNAKE_STEP_TICKS;
        return 1;
    }
    if (snake->tick_count < snake->next_tick) {
        return 0;
    }
    snake->next_tick = snake->tick_count + SNAKE_STEP_TICKS;

    snake->dir_x = snake->next_dir_x;
    snake->dir_y = snake->next_dir_y;
    new_x = snake->body_x[0] + snake->dir_x;
    new_y = snake->body_y[0] + snake->dir_y;

    if (new_x < 0 || new_x >= SNAKE_GRID_W ||
        new_y < 0 || new_y >= SNAKE_GRID_H ||
        snake_hits_self(snake, new_x, new_y)) {
        snake->game_over = 1;
        return 1;
    }

    if (new_x == snake->food_x && new_y == snake->food_y) {
        grow = 1;
        if (snake->length < SNAKE_MAX_SEGMENTS) {
            snake->length += 1;
        }
        snake->score += 10;
    }

    for (int i = snake->length - 1; i > 0; --i) {
        snake->body_x[i] = snake->body_x[i - 1];
        snake->body_y[i] = snake->body_y[i - 1];
    }
    snake->body_x[0] = new_x;
    snake->body_y[0] = new_y;

    if (grow) {
        snake_place_food(snake);
    }
    return 1;
}

void snake_draw_window(struct snake_state *snake, int active,
                       int min_hover, int max_hover, int close_hover) {
    int cell = (snake->window.h - 72) / SNAKE_GRID_H;
    int max_cell_w = (snake->window.w - 24) / SNAKE_GRID_W;
    int hud_y;
    struct rect board;
    const struct desktop_theme *theme = ui_theme_get();
    char score[24];

    if (max_cell_w < cell) {
        cell = max_cell_w;
    }
    if (cell < 8) {
        cell = 8;
    }
    board.w = SNAKE_GRID_W * cell;
    board.h = SNAKE_GRID_H * cell;
    board.x = snake->window.x + ((snake->window.w - board.w) / 2);
    board.y = snake->window.y + 28;
    hud_y = board.y + board.h + 10;

    draw_window_frame(&snake->window, "SNAKE", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){snake->window.x + 4, snake->window.y + 18,
                                   snake->window.w - 8, snake->window.h - 22},
                    ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, theme->background);

    for (int y = 0; y < SNAKE_GRID_H; ++y) {
        for (int x = 0; x < SNAKE_GRID_W; ++x) {
            uint8_t tile = ((x + y) & 1) ? theme->background : ui_color_canvas();
            sys_rect(board.x + (x * cell), board.y + (y * cell), cell - 1, cell - 1, tile);
        }
    }

    {
        int fx = board.x + (snake->food_x * cell);
        int fy = board.y + (snake->food_y * cell);
        int bite = cell / 3;

        snake_draw_cell(fx, fy, cell, theme->menu_button_inactive, theme->text, theme->window);
        sys_rect(fx + (cell / 2), fy + 2, 2, bite, theme->window);
        sys_rect(fx + (cell / 2) + 2, fy + 1, bite, 2, theme->text);
    }
    for (int i = snake->length - 1; i >= 0; --i) {
        int sx = board.x + (snake->body_x[i] * cell);
        int sy = board.y + (snake->body_y[i] * cell);
        int eye_y = sy + (cell / 3);
        uint8_t base = (i == 0) ? theme->menu_button : theme->window;
        uint8_t hi = (i == 0) ? theme->text : theme->menu_button;
        uint8_t lo = theme->menu_button_inactive;

        snake_draw_cell(sx, sy, cell, base, hi, lo);
        if (i == 0) {
            int eye_x1 = sx + (cell / 3);
            int eye_x2 = sx + cell - (cell / 3) - 2;
            sys_rect(eye_x1, eye_y, 2, 2, theme->text);
            sys_rect(eye_x2, eye_y, 2, 2, theme->text);
        }
    }

    str_copy_limited(score, "Score ", (int)sizeof(score));
    snake_append_int(score, snake->score, (int)sizeof(score));
    sys_text(board.x, hud_y, theme->text, score);
    if (snake->game_over) {
        sys_text(board.x + 90, hud_y, theme->text, "R reinicia");
    } else {
        sys_text(board.x + 90, hud_y, theme->text, "Setas movem");
    }
    sys_text(board.x + 210, hud_y, theme->text, "Coma e cresca");
}
