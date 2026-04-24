#include <userland/applications/include/games/minesweeper.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_MINESWEEPER_WINDOW = {44, 20, 430, 388};

static uint32_t minesweeper_next_random(struct minesweeper_state *game) {
    game->seed = (game->seed * 1664525u) + 1013904223u;
    return game->seed;
}

static void minesweeper_append_int(char *buf, int value, int max_len) {
    char tmp[16];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = value < 0 ? (unsigned)(-value) : (unsigned)value;

    if (len >= max_len - 1) {
        return;
    }
    if (value < 0 && len < max_len - 1) {
        buf[len++] = '-';
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

static int minesweeper_in_bounds(int x, int y) {
    return x >= 0 && x < MINESWEEPER_COLS && y >= 0 && y < MINESWEEPER_ROWS;
}

static void minesweeper_compute_adjacent(struct minesweeper_state *game) {
    for (int y = 0; y < MINESWEEPER_ROWS; ++y) {
        for (int x = 0; x < MINESWEEPER_COLS; ++x) {
            int count = 0;

            if (game->mines[y][x]) {
                game->adjacent[y][x] = 0u;
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0) || !minesweeper_in_bounds(x + dx, y + dy)) {
                        continue;
                    }
                    if (game->mines[y + dy][x + dx]) {
                        count += 1;
                    }
                }
            }
            game->adjacent[y][x] = (uint8_t)count;
        }
    }
}

static void minesweeper_place_mines(struct minesweeper_state *game) {
    int placed = 0;

    while (placed < MINESWEEPER_MINES) {
        int x = (int)(minesweeper_next_random(game) % MINESWEEPER_COLS);
        int y = (int)(minesweeper_next_random(game) % MINESWEEPER_ROWS);

        if (game->mines[y][x]) {
            continue;
        }
        game->mines[y][x] = 1u;
        placed += 1;
    }
    game->mines_placed = 1;
    minesweeper_compute_adjacent(game);
}

static void minesweeper_reset(struct minesweeper_state *game) {
    for (int y = 0; y < MINESWEEPER_ROWS; ++y) {
        for (int x = 0; x < MINESWEEPER_COLS; ++x) {
            game->mines[y][x] = 0u;
            game->adjacent[y][x] = 0u;
            game->revealed[y][x] = 0u;
            game->flagged[y][x] = 0u;
        }
    }
    if (game->seed == 0u) {
        game->seed = 1u;
    }
    game->cursor_x = 0;
    game->cursor_y = 0;
    game->mines_placed = 0;
    game->revealed_safe = 0;
    game->exploded = 0;
    game->won = 0;
}

static void minesweeper_board_rect(const struct minesweeper_state *game,
                                   struct rect *board,
                                   int *cell_size) {
    int available_w;
    int available_h;
    int cell;

    if (game == 0 || board == 0 || cell_size == 0) {
        return;
    }

    available_w = game->window.w - 32;
    available_h = game->window.h - 126;
    cell = available_w / MINESWEEPER_COLS;

    if ((available_h / MINESWEEPER_ROWS) < cell) {
        cell = available_h / MINESWEEPER_ROWS;
    }
    if (cell < 18) {
        cell = 18;
    }

    board->w = MINESWEEPER_COLS * cell;
    board->h = MINESWEEPER_ROWS * cell;
    board->x = game->window.x + ((game->window.w - board->w) / 2);
    board->y = game->window.y + 68;
    *cell_size = cell;
}

static int minesweeper_hit_test_cell(const struct minesweeper_state *game,
                                     int px,
                                     int py,
                                     int *cell_x,
                                     int *cell_y) {
    struct rect board;
    int cell;
    int x;
    int y;

    if (game == 0 || cell_x == 0 || cell_y == 0) {
        return 0;
    }

    minesweeper_board_rect(game, &board, &cell);
    if (!point_in_rect(&board, px, py)) {
        return 0;
    }

    x = (px - board.x) / cell;
    y = (py - board.y) / cell;
    if (!minesweeper_in_bounds(x, y)) {
        return 0;
    }

    *cell_x = x;
    *cell_y = y;
    return 1;
}

static void minesweeper_reveal_all_mines(struct minesweeper_state *game) {
    for (int y = 0; y < MINESWEEPER_ROWS; ++y) {
        for (int x = 0; x < MINESWEEPER_COLS; ++x) {
            if (game->mines[y][x]) {
                game->revealed[y][x] = 1u;
            }
        }
    }
}

static void minesweeper_flood_reveal(struct minesweeper_state *game, int start_x, int start_y) {
    int queue_x[MINESWEEPER_ROWS * MINESWEEPER_COLS];
    int queue_y[MINESWEEPER_ROWS * MINESWEEPER_COLS];
    int read = 0;
    int write = 0;

    queue_x[write] = start_x;
    queue_y[write] = start_y;
    write += 1;

    while (read < write) {
        int x = queue_x[read];
        int y = queue_y[read];
        read += 1;

        if (!minesweeper_in_bounds(x, y) ||
            game->revealed[y][x] ||
            game->flagged[y][x] ||
            game->mines[y][x]) {
            continue;
        }

        game->revealed[y][x] = 1u;
        game->revealed_safe += 1;
        if (game->adjacent[y][x] != 0u) {
            continue;
        }

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || !minesweeper_in_bounds(x + dx, y + dy)) {
                    continue;
                }
                queue_x[write] = x + dx;
                queue_y[write] = y + dy;
                write += 1;
            }
        }
    }
}

static void minesweeper_update_win_state(struct minesweeper_state *game) {
    if (!game->exploded &&
        game->revealed_safe >= (MINESWEEPER_ROWS * MINESWEEPER_COLS) - MINESWEEPER_MINES) {
        game->won = 1;
        minesweeper_reveal_all_mines(game);
    }
}

static int minesweeper_reveal_cursor(struct minesweeper_state *game) {
    int x = game->cursor_x;
    int y = game->cursor_y;

    if (game->flagged[y][x] || game->revealed[y][x]) {
        return 0;
    }
    if (!game->mines_placed) {
        minesweeper_place_mines(game);
    }
    if (game->mines[y][x]) {
        game->exploded = 1;
        minesweeper_reveal_all_mines(game);
        return 1;
    }

    minesweeper_flood_reveal(game, x, y);
    minesweeper_update_win_state(game);
    return 1;
}

static uint8_t minesweeper_number_color(int value, const struct desktop_theme *theme) {
    switch (value) {
    case 1: return 11;
    case 2: return 10;
    case 3: return 12;
    case 4: return 9;
    case 5: return 13;
    case 6: return 14;
    case 7: return theme->text;
    case 8: return 6;
    default: return theme->text;
    }
}

void minesweeper_init_state(struct minesweeper_state *game) {
    game->window = DEFAULT_MINESWEEPER_WINDOW;
    game->seed = 1u;
    minesweeper_reset(game);
}

int minesweeper_step(struct minesweeper_state *game, uint32_t ticks) {
    (void)game;
    (void)ticks;
    return 0;
}

int minesweeper_handle_key(struct minesweeper_state *game, int key) {
    if (key == 'r' || key == 'R' || key == 'n' || key == 'N') {
        minesweeper_reset(game);
        return 1;
    }

    if (key == KEY_ARROW_LEFT && game->cursor_x > 0) {
        game->cursor_x -= 1;
        return 1;
    }
    if (key == KEY_ARROW_RIGHT && game->cursor_x + 1 < MINESWEEPER_COLS) {
        game->cursor_x += 1;
        return 1;
    }
    if (key == KEY_ARROW_UP && game->cursor_y > 0) {
        game->cursor_y -= 1;
        return 1;
    }
    if (key == KEY_ARROW_DOWN && game->cursor_y + 1 < MINESWEEPER_ROWS) {
        game->cursor_y += 1;
        return 1;
    }

    if (game->exploded || game->won) {
        return 0;
    }

    if (key == 'f' || key == 'F') {
        if (!game->revealed[game->cursor_y][game->cursor_x]) {
            game->flagged[game->cursor_y][game->cursor_x] =
                (uint8_t)!game->flagged[game->cursor_y][game->cursor_x];
            return 1;
        }
        return 0;
    }

    if (key == ' ' || key == '\n') {
        return minesweeper_reveal_cursor(game);
    }

    return 0;
}

int minesweeper_handle_click(struct minesweeper_state *game, int x, int y, int button) {
    int cell_x;
    int cell_y;

    if (game == 0 || !minesweeper_hit_test_cell(game, x, y, &cell_x, &cell_y)) {
        return 0;
    }

    game->cursor_x = cell_x;
    game->cursor_y = cell_y;

    if (button == MINESWEEPER_CLICK_FLAG) {
        if (game->exploded || game->won || game->revealed[cell_y][cell_x]) {
            return 1;
        }
        game->flagged[cell_y][cell_x] = (uint8_t)!game->flagged[cell_y][cell_x];
        return 1;
    }

    if (game->exploded || game->won) {
        return 1;
    }

    return minesweeper_reveal_cursor(game);
}

static void minesweeper_draw_cell(const struct minesweeper_state *game,
                                  int x,
                                  int y,
                                  int px,
                                  int py,
                                  int cell,
                                  const struct desktop_theme *theme) {
    int cursor = (x == game->cursor_x && y == game->cursor_y);
    uint8_t base = cursor ? theme->menu : theme->window;

    if (game->revealed[y][x]) {
        base = game->mines[y][x] ? 12 : theme->background;
        sys_rect(px, py, cell - 2, cell - 2, theme->menu_button_inactive);
        sys_rect(px + 2, py + 2, cell - 6, cell - 6, base);
        if (game->mines[y][x]) {
            sys_text(px + (cell / 2) - 3, py + (cell / 2) - 4, theme->text, "*");
        } else if (game->adjacent[y][x] > 0u) {
            char text[2];

            text[0] = (char)('0' + game->adjacent[y][x]);
            text[1] = '\0';
            sys_text(px + (cell / 2) - 3, py + (cell / 2) - 4,
                     minesweeper_number_color(game->adjacent[y][x], theme), text);
        }
        return;
    }

    sys_rect(px, py, cell - 2, cell - 2, theme->menu_button_inactive);
    sys_rect(px + 2, py + 2, cell - 6, cell - 6, base);
    if (game->flagged[y][x]) {
        sys_text(px + (cell / 2) - 3, py + (cell / 2) - 4, 10, "F");
    } else if (cursor) {
        sys_rect(px + 5, py + 5, cell - 12, 1, theme->text);
        sys_rect(px + 5, py + 5, 1, cell - 12, theme->text);
    }
}

void minesweeper_draw_window(struct minesweeper_state *game, int active,
                             int min_hover, int max_hover, int close_hover) {
    struct rect panel;
    struct rect board;
    struct rect hud;
    struct rect footer;
    const struct desktop_theme *theme = ui_theme_get();
    int cell = 0;
    int flags_left = MINESWEEPER_MINES;
    char status[48];
    char mines[32];

    for (int y = 0; y < MINESWEEPER_ROWS; ++y) {
        for (int x = 0; x < MINESWEEPER_COLS; ++x) {
            if (game->flagged[y][x]) {
                flags_left -= 1;
            }
        }
    }

    minesweeper_board_rect(game, &board, &cell);
    panel = (struct rect){game->window.x + 8, game->window.y + 20,
                          game->window.w - 16, game->window.h - 28};
    hud = (struct rect){game->window.x + 14, game->window.y + 28,
                        game->window.w - 28, 30};
    footer = (struct rect){game->window.x + 14, board.y + board.h + 10,
                           game->window.w - 28, 42};

    draw_window_frame(&game->window, "Campo Minado", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&panel, ui_color_window_bg());
    ui_draw_surface(&hud, theme->menu);
    ui_draw_inset(&board, ui_color_window_bg());
    ui_draw_surface(&footer, theme->window);

    mines[0] = '\0';
    str_append(mines, "Minas ", (int)sizeof(mines));
    minesweeper_append_int(mines, flags_left, (int)sizeof(mines));
    sys_text(hud.x + 10, hud.y + 8, theme->text, mines);
    sys_text(hud.x + 110, hud.y + 8, theme->text, "Setas/mouse movem  Clique abre  Botao dir/F marca  R reinicia");

    for (int y = 0; y < MINESWEEPER_ROWS; ++y) {
        for (int x = 0; x < MINESWEEPER_COLS; ++x) {
            minesweeper_draw_cell(game,
                                  x, y,
                                  board.x + (x * cell),
                                  board.y + (y * cell),
                                  cell,
                                  theme);
        }
    }

    status[0] = '\0';
    if (game->exploded) {
        str_append(status, "Kaboom. Aperte R para outra rodada.", (int)sizeof(status));
    } else if (game->won) {
        str_append(status, "Tabuleiro limpo. Boa.", (int)sizeof(status));
    } else if (!game->mines_placed) {
        str_append(status, "Primeiro movimento monta o campo.", (int)sizeof(status));
    } else {
        str_append(status, "Abra tudo que nao for mina.", (int)sizeof(status));
    }
    sys_text(footer.x + 10, footer.y + 12, theme->text, status);
}
