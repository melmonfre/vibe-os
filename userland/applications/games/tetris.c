#include <userland/applications/include/games/tetris.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TETRIS_WINDOW = {40, 20, 400, 300};
static const int TETRIS_STEP_TICKS = 28;
static const uint8_t g_tetris_colors[7] = {11, 14, 10, 12, 9, 5, 6};
static const uint8_t g_tetris_shapes[7][4][4][4] = {
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}}
    },
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}}
    },
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}
    },
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

static void tetris_draw_block(int x, int y, int cell, uint8_t color, const struct desktop_theme *theme) {
    int inner = cell - 4;

    if (inner < 2) {
        inner = 2;
    }
    sys_rect(x, y, cell - 1, cell - 1, theme->menu_button_inactive);
    sys_rect(x + 1, y + 1, cell - 3, cell - 3, color);
    sys_rect(x + 1, y + 1, cell - 3, 1, theme->text);
    sys_rect(x + 1, y + 1, 1, cell - 3, theme->text);
    sys_rect(x + 2, y + 2, inner, inner, color);
}

static uint32_t tetris_next_random(struct tetris_state *tetris) {
    tetris->seed = (tetris->seed * 1664525u) + 1013904223u;
    return tetris->seed;
}

static int tetris_piece_fits(const struct tetris_state *tetris,
                             int piece_type,
                             int rotation,
                             int px,
                             int py) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!g_tetris_shapes[piece_type][rotation][y][x]) {
                continue;
            }
            if (px + x < 0 || px + x >= TETRIS_COLS ||
                py + y < 0 || py + y >= TETRIS_ROWS) {
                return 0;
            }
            if (tetris->board[py + y][px + x] != 0u) {
                return 0;
            }
        }
    }
    return 1;
}

static void tetris_spawn_piece(struct tetris_state *tetris) {
    tetris->piece_type = (int)(tetris_next_random(tetris) % 7u);
    tetris->rotation = 0;
    tetris->piece_x = 3;
    tetris->piece_y = 0;
    if (!tetris_piece_fits(tetris, tetris->piece_type, tetris->rotation,
                           tetris->piece_x, tetris->piece_y)) {
        tetris->game_over = 1;
    }
}

static void tetris_reset(struct tetris_state *tetris) {
    for (int y = 0; y < TETRIS_ROWS; ++y) {
        for (int x = 0; x < TETRIS_COLS; ++x) {
            tetris->board[y][x] = 0u;
        }
    }
    if (tetris->seed == 0u) {
        tetris->seed = 1u;
    }
    tetris->score = 0;
    tetris->game_over = 0;
    tetris->next_tick = 0u;
    tetris->tick_count = 0u;
    tetris_spawn_piece(tetris);
}

static void tetris_lock_piece(struct tetris_state *tetris) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (g_tetris_shapes[tetris->piece_type][tetris->rotation][y][x]) {
                int board_x = tetris->piece_x + x;
                int board_y = tetris->piece_y + y;
                if (board_y >= 0 && board_y < TETRIS_ROWS &&
                    board_x >= 0 && board_x < TETRIS_COLS) {
                    tetris->board[board_y][board_x] = (uint8_t)(tetris->piece_type + 1);
                }
            }
        }
    }
}

static void tetris_clear_lines(struct tetris_state *tetris) {
    int cleared = 0;

    for (int y = TETRIS_ROWS - 1; y >= 0; --y) {
        int full = 1;
        for (int x = 0; x < TETRIS_COLS; ++x) {
            if (tetris->board[y][x] == 0u) {
                full = 0;
                break;
            }
        }
        if (!full) {
            continue;
        }

        cleared += 1;
        for (int row = y; row > 0; --row) {
            for (int col = 0; col < TETRIS_COLS; ++col) {
                tetris->board[row][col] = tetris->board[row - 1][col];
            }
        }
        for (int col = 0; col < TETRIS_COLS; ++col) {
            tetris->board[0][col] = 0u;
        }
        y += 1;
    }

    tetris->score += cleared * 100;
}

static int tetris_try_move(struct tetris_state *tetris, int dx, int dy) {
    if (!tetris_piece_fits(tetris, tetris->piece_type, tetris->rotation,
                           tetris->piece_x + dx, tetris->piece_y + dy)) {
        return 0;
    }
    tetris->piece_x += dx;
    tetris->piece_y += dy;
    return 1;
}

static int tetris_try_rotate(struct tetris_state *tetris) {
    int next_rotation = (tetris->rotation + 1) % 4;

    if (!tetris_piece_fits(tetris, tetris->piece_type, next_rotation,
                           tetris->piece_x, tetris->piece_y)) {
        return 0;
    }
    tetris->rotation = next_rotation;
    return 1;
}

static void tetris_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = (unsigned)value;

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

void tetris_init_state(struct tetris_state *tetris) {
    tetris->window = DEFAULT_TETRIS_WINDOW;
    tetris->seed = 1u;
    tetris_reset(tetris);
}

int tetris_handle_key(struct tetris_state *tetris, int key) {
    int changed = 0;

    if (tetris->game_over) {
        if (key == ' ' || key == '\n' || key == 'r' || key == 'R') {
            tetris_reset(tetris);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        changed = tetris_try_move(tetris, -1, 0);
    } else if (key == KEY_ARROW_RIGHT) {
        changed = tetris_try_move(tetris, 1, 0);
    } else if (key == KEY_ARROW_UP) {
        changed = tetris_try_rotate(tetris);
    } else if (key == KEY_ARROW_DOWN) {
        if (!tetris_try_move(tetris, 0, 1)) {
            tetris_lock_piece(tetris);
            tetris_clear_lines(tetris);
            tetris_spawn_piece(tetris);
        }
        changed = 1;
    } else if (key == ' ') {
        while (tetris_try_move(tetris, 0, 1)) {
        }
        tetris_lock_piece(tetris);
        tetris_clear_lines(tetris);
        tetris_spawn_piece(tetris);
        changed = 1;
    } else if (key == 'r' || key == 'R') {
        tetris_reset(tetris);
        changed = 1;
    }

    return changed;
}

int tetris_step(struct tetris_state *tetris, uint32_t ticks) {
    (void)ticks;
    if (tetris->game_over) {
        return 0;
    }

    tetris->tick_count += 1u;

    if (tetris->next_tick == 0u) {
        tetris->next_tick = tetris->tick_count + TETRIS_STEP_TICKS;
        return 1;
    }
    if (tetris->tick_count < tetris->next_tick) {
        return 0;
    }
    tetris->next_tick = tetris->tick_count + TETRIS_STEP_TICKS;

    if (!tetris_try_move(tetris, 0, 1)) {
        tetris_lock_piece(tetris);
        tetris_clear_lines(tetris);
        tetris_spawn_piece(tetris);
    }
    return 1;
}

void tetris_draw_window(struct tetris_state *tetris, int active,
                        int min_hover, int max_hover, int close_hover) {
    int cell_w = (tetris->window.w - 120) / TETRIS_COLS;
    int cell_h = (tetris->window.h - 70) / TETRIS_ROWS;
    int cell = cell_w < cell_h ? cell_w : cell_h;
    struct rect board;
    struct rect body;
    struct rect topbar;
    struct rect hud_panel;
    struct rect stat1;
    struct rect help;
    struct rect preview;
    int hud_x;
    const struct desktop_theme *theme = ui_theme_get();
    char score[24];
    int preview_cell;

    if (cell > 12) {
        cell = 12;
    }
    if (cell < 8) {
        cell = 8;
    }
    board.w = TETRIS_COLS * cell;
    board.h = TETRIS_ROWS * cell;
    body = (struct rect){tetris->window.x + 4, tetris->window.y + 18, tetris->window.w - 8, tetris->window.h - 22};
    topbar = (struct rect){tetris->window.x + 8, tetris->window.y + 22, tetris->window.w - 16, 18};
    board.x = tetris->window.x + 14;
    board.y = tetris->window.y + 46 + ((tetris->window.h - 58 - board.h) / 2);
    hud_x = board.x + board.w + 12;
    hud_panel = (struct rect){hud_x - 4, board.y, tetris->window.x + tetris->window.w - hud_x - 10, board.h};
    stat1 = (struct rect){hud_panel.x + 6, hud_panel.y + 10, hud_panel.w - 12, 26};
    help = (struct rect){hud_panel.x + 6, hud_panel.y + 44, hud_panel.w - 12, 54};
    preview = (struct rect){hud_panel.x + 6, hud_panel.y + 106, hud_panel.w - 12, 64};
    preview_cell = (preview.w - 20) / 4;
    if (preview_cell > 10) {
        preview_cell = 10;
    }
    if (preview_cell < 6) {
        preview_cell = 6;
    }

    draw_window_frame(&tetris->window, "TETRAX", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_canvas());
    ui_draw_surface(&topbar, ui_color_panel());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud_panel, ui_color_canvas());
    ui_draw_inset(&stat1, ui_color_canvas());
    ui_draw_inset(&help, ui_color_canvas());
    ui_draw_inset(&preview, ui_color_canvas());
    sys_text(topbar.x + 6, topbar.y + 5, ui_color_muted(), "Stack clean");
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, theme->background);

    for (int y = 0; y < TETRIS_ROWS; ++y) {
        for (int x = 0; x < TETRIS_COLS; ++x) {
            uint8_t color = ((x + y) & 1) ? theme->background : ui_color_canvas();
            sys_rect(board.x + (x * cell), board.y + (y * cell), cell - 1, cell - 1, color);
            if (tetris->board[y][x] != 0u) {
                color = g_tetris_colors[tetris->board[y][x] - 1u];
                tetris_draw_block(board.x + (x * cell), board.y + (y * cell), cell, color, theme);
            }
        }
    }

    if (!tetris->game_over) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                if (!g_tetris_shapes[tetris->piece_type][tetris->rotation][y][x]) {
                    continue;
                }
                tetris_draw_block(board.x + ((tetris->piece_x + x) * cell),
                                  board.y + ((tetris->piece_y + y) * cell),
                                  cell,
                                  g_tetris_colors[tetris->piece_type],
                                  theme);
            }
        }
    }

    str_copy_limited(score, "Score ", (int)sizeof(score));
    tetris_append_int(score, tetris->score, (int)sizeof(score));
    sys_text(stat1.x + 6, stat1.y + 8, theme->text, score);
    sys_text(help.x + 6, help.y + 8, theme->text, "Setas move/gira");
    if (tetris->game_over) {
        sys_text(help.x + 6, help.y + 22, theme->text, "R reinicia");
    } else {
        sys_text(help.x + 6, help.y + 22, theme->text, "Space drop");
    }
    sys_text(help.x + 6, help.y + 36, theme->text, "Empilhe linhas");
    sys_text(preview.x + 6, preview.y + 8, ui_color_muted(), "Peca");

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!g_tetris_shapes[tetris->piece_type][tetris->rotation][y][x]) {
                continue;
            }
            tetris_draw_block(preview.x + 10 + (x * preview_cell),
                              preview.y + 24 + (y * preview_cell),
                              preview_cell,
                              g_tetris_colors[tetris->piece_type],
                              theme);
        }
    }
}
