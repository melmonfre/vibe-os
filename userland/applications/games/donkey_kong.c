#include <userland/applications/include/games/donkey_kong.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};
static const int DK_BOARD_W = 280;
static const int DK_BOARD_H = 224;
static const int DK_PLAYER_W = 16;
static const int DK_PLAYER_H = 14;
static const int DK_BARREL_W = 12;
static const int DK_BARREL_H = 10;
static const int DK_FRAME_TICKS = 16;
static const int DK_GRAVITY_PER_FRAME = 1;
static const int DK_JUMP_VY = -8;
static const int DK_PLAYER_SPEED = 4;
static const int DK_CLIMB_SPEED = 4;
static const int DK_BARREL_SPEED = 4;
static const int DK_BARREL_CLIMB_SPEED = 2;
static const int DK_BARREL_SPAWN_TICKS = 105;
static const int DK_INVULN_TICKS_AFTER_HIT = 100;
static const int DK_SURVIVAL_SCORE_TICKS = 35;
static const int g_platform_x1[DK_PLATFORM_COUNT] = {8, 18, 10, 22, 12, 22};
static const int g_platform_x2[DK_PLATFORM_COUNT] = {272, 260, 270, 258, 264, 238};
static const int g_platform_y1[DK_PLATFORM_COUNT] = {212, 176, 140, 104, 68, 34};
static const int g_platform_y2[DK_PLATFORM_COUNT] = {204, 184, 132, 112, 60, 42};
static const int g_ladder_x[DK_LADDER_COUNT] = {42, 92, 144, 194, 242};

static void donkey_kong_draw_player_sprite(int x, int y, const struct desktop_theme *t) {
    sys_rect(x + 5, y, 6, 2, t->menu_button);
    sys_rect(x + 4, y + 2, 8, 2, t->text);
    sys_rect(x + 3, y + 4, 10, 5, t->window);
    sys_rect(x + 2, y + 9, 4, 5, t->menu_button);
    sys_rect(x + 10, y + 9, 4, 5, t->menu_button);
    sys_rect(x + 5, y + 5, 2, 1, t->window_bg);
}

static void donkey_kong_draw_barrel_sprite(int x, int y, const struct desktop_theme *t) {
    sys_rect(x + 1, y + 3, 10, 5, t->menu_button);
    sys_rect(x + 2, y + 1, 8, 2, t->menu_button_inactive);
    sys_rect(x + 2, y + 8, 8, 1, t->menu_button_inactive);
    sys_rect(x + 3, y + 4, 1, 1, t->text);
    sys_rect(x + 8, y + 4, 1, 1, t->text);
    sys_rect(x + 4, y + 6, 4, 1, t->window_bg);
}

static void donkey_kong_draw_kong_sprite(int x, int y, const struct desktop_theme *t) {
    sys_rect(x + 8, y, 18, 7, t->menu_button);
    sys_rect(x + 4, y + 7, 28, 15, t->menu_button_inactive);
    sys_rect(x + 2, y + 10, 6, 4, t->text);
    sys_rect(x + 28, y + 10, 6, 4, t->text);
    sys_rect(x, y + 22, 10, 9, t->menu_button);
    sys_rect(x + 26, y + 22, 10, 9, t->menu_button);
    sys_rect(x + 12, y + 20, 12, 5, t->window);
}

static void donkey_kong_draw_goal_sprite(int x, int y, const struct desktop_theme *t) {
    sys_rect(x + 2, y, 11, 2, t->text);
    sys_rect(x + 1, y + 2, 13, 6, t->window);
    sys_rect(x + 4, y + 8, 6, 6, t->menu_button);
}

static void donkey_kong_draw_girder_span(int x0, int y0, int x1, int y1, const struct rect *board, const struct desktop_theme *t) {
    int dx = x1 - x0;
    if (dx <= 0) {
        return;
    }
    for (int x = x0; x <= x1; ++x) {
        int y = y0 + (((y1 - y0) * (x - x0)) / dx);
        sys_rect(board->x + x, board->y + y, 1, 5, t->window);
        sys_rect(board->x + x, board->y + y + 5, 1, 1, t->menu_button_inactive);
        if (((x - x0) % 16) < 6) {
            sys_rect(board->x + x, board->y + y + 2, 4, 1, t->menu_button);
        }
    }
}

static void donkey_kong_draw_backdrop(const struct rect *board, const struct desktop_theme *t) {
    for (int col = 0; col < 5; ++col) {
        int bx = board->x + 16 + (col * 50);
        int bw = 20 + ((col % 2) * 4);
        int bh = 26 + ((col % 3) * 10);
        int by = board->y + 14 + (12 - (col % 3) * 2);
        sys_rect(bx, by + 10, bw, bh, t->menu_button_inactive);
        sys_rect(bx + 4, by + 4, bw - 8, 6, t->window);
        sys_rect(bx + 6, by + 18, bw - 12, 2, t->window_bg);
    }
    sys_rect(board->x + 12, board->y + 18, board->w - 24, 1, t->taskbar);
}

static uint32_t donkey_kong_next_random(struct donkey_kong_state *s) {
    s->seed = (s->seed * 1664525u) + 1013904223u;
    return s->seed;
}

static void donkey_kong_append_int(char *buf, int value, int max_len) {
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

static int donkey_kong_platform_y(int platform, int x) {
    int x1 = g_platform_x1[platform];
    int x2 = g_platform_x2[platform];
    int y1 = g_platform_y1[platform];
    int y2 = g_platform_y2[platform];
    int dx = x2 - x1;

    if (x <= x1 || dx <= 0) {
        return y1;
    }
    if (x >= x2) {
        return y2;
    }
    return y1 + (((y2 - y1) * (x - x1)) / dx);
}

static int donkey_kong_support_platform(int x_center, int feet_y) {
    for (int i = DK_PLATFORM_COUNT - 1; i >= 0; --i) {
        int py;
        if (x_center < g_platform_x1[i] || x_center > g_platform_x2[i]) {
            continue;
        }
        py = donkey_kong_platform_y(i, x_center);
        if (feet_y >= py - 1 && feet_y <= py + 3) {
            return i;
        }
    }
    return -1;
}

static int donkey_kong_ladder_under_player(const struct donkey_kong_state *s) {
    int center_x = s->player_x + (DK_PLAYER_W / 2);
    int feet_y = s->player_y + DK_PLAYER_H;

    for (int i = 0; i < DK_LADDER_COUNT; ++i) {
        int top = donkey_kong_platform_y(4, g_ladder_x[i]);
        int bottom = donkey_kong_platform_y(0, g_ladder_x[i]);
        if (feet_y < top - 2 || feet_y > bottom + 2) {
            continue;
        }
        if (center_x >= g_ladder_x[i] - 4 && center_x <= g_ladder_x[i] + 4) {
            return i;
        }
    }
    return -1;
}

static int donkey_kong_barrel_ladder_available(int barrel_platform, int ladder_idx) {
    int high = barrel_platform;
    int low = barrel_platform - 1;
    int ladder_top_platform = -1;

    for (int i = DK_PLATFORM_COUNT - 1; i > 0; --i) {
        int top_here = donkey_kong_platform_y(i, g_ladder_x[ladder_idx]);
        int bottom_here = donkey_kong_platform_y(i - 1, g_ladder_x[ladder_idx]);
        if (top_here < bottom_here) {
            ladder_top_platform = i;
            break;
        }
    }

    if (ladder_top_platform < 0) {
        return 0;
    }
    return (high == ladder_top_platform || high == ladder_top_platform - 1 || low >= 0);
}

static void donkey_kong_respawn_player(struct donkey_kong_state *s) {
    s->player_x = 10;
    s->player_y = donkey_kong_platform_y(0, s->player_x + (DK_PLAYER_W / 2)) - DK_PLAYER_H;
    s->player_vy = 0;
    s->jumping = 0;
    s->on_ladder = 0;
    s->input_move = 0;
    s->input_climb = 0;
}

static void reset(struct donkey_kong_state *s) {
    s->score = 0;
    s->avoided_barrels = 0;
    s->game_over = 0;
    s->win = 0;
    s->lives = 3;
    s->tick_count = 0u;
    s->next_tick = 0u;
    s->physics_accum = 0u;
    s->score_accum = 0u;
    s->spawn_accum = 0u;
    s->invuln_ticks = 0u;
    if (s->seed == 0u) {
        s->seed = 1u;
    }

    donkey_kong_respawn_player(s);
    for (int i = 0; i < DK_MAX_BARRELS; ++i) {
        s->barrel_active[i] = 0;
        s->barrel_x[i] = 0;
        s->barrel_y[i] = 0;
        s->barrel_dir[i] = -1;
        s->barrel_platform[i] = DK_PLATFORM_COUNT - 1;
        s->barrel_on_ladder[i] = 0;
        s->barrel_ladder_target[i] = 0;
    }
}

void donkey_kong_init_state(struct donkey_kong_state *s) {
    s->window = DEFAULT_WINDOW;
    s->seed = 1u;
    reset(s);
}

int donkey_kong_handle_key(struct donkey_kong_state *s, int key) {
    if (s->game_over || s->win) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n') {
            reset(s);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        s->input_move = -1;
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        s->input_move = 1;
        return 1;
    }
    if (key == KEY_ARROW_UP) {
        s->input_climb = -1;
        return 1;
    }
    if (key == KEY_ARROW_DOWN) {
        s->input_climb = 1;
        return 1;
    }
    if (key == ' ') {
        if (!s->jumping && !s->on_ladder) {
            s->jumping = 1;
            s->player_vy = DK_JUMP_VY;
            return 1;
        }
    }
    if (key == 'r' || key == 'R') {
        reset(s);
        return 1;
    }
    return 0;
}

static void donkey_kong_spawn_barrel(struct donkey_kong_state *s) {
    for (int i = 0; i < DK_MAX_BARRELS; ++i) {
        if (s->barrel_active[i]) {
            continue;
        }
        s->barrel_active[i] = 1;
        s->barrel_platform[i] = DK_PLATFORM_COUNT - 1;
        s->barrel_dir[i] = 1;
        s->barrel_on_ladder[i] = 0;
        s->barrel_x[i] = g_platform_x1[DK_PLATFORM_COUNT - 1] + 4;
        s->barrel_y[i] = donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, s->barrel_x[i]) - DK_BARREL_H;
        return;
    }
}

static void donkey_kong_step_player(struct donkey_kong_state *s) {
    int center_x = s->player_x + (DK_PLAYER_W / 2);
    int feet_y = s->player_y + DK_PLAYER_H;
    int ladder = donkey_kong_ladder_under_player(s);

    if (s->input_climb != 0 && ladder >= 0) {
        s->on_ladder = 1;
        s->jumping = 0;
        s->player_vy = 0;
        s->player_x = g_ladder_x[ladder] - (DK_PLAYER_W / 2);
    }

    if (s->on_ladder) {
        if (s->input_climb < 0) {
            s->player_y -= DK_CLIMB_SPEED;
        } else if (s->input_climb > 0) {
            s->player_y += DK_CLIMB_SPEED;
        }
        if (ladder < 0) {
            s->on_ladder = 0;
        } else {
            int top = donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, g_ladder_x[ladder]) - DK_PLAYER_H;
            int bottom = donkey_kong_platform_y(0, g_ladder_x[ladder]) - DK_PLAYER_H;
            if (s->player_y < top) {
                s->player_y = top;
                s->on_ladder = 0;
            }
            if (s->player_y > bottom) {
                s->player_y = bottom;
                s->on_ladder = 0;
            }
        }
    } else {
        if (s->input_move < 0) {
            s->player_x -= DK_PLAYER_SPEED;
        } else if (s->input_move > 0) {
            s->player_x += DK_PLAYER_SPEED;
        }
        if (s->player_x < 0) {
            s->player_x = 0;
        }
        if (s->player_x > DK_BOARD_W - DK_PLAYER_W) {
            s->player_x = DK_BOARD_W - DK_PLAYER_W;
        }

        s->player_vy += DK_GRAVITY_PER_FRAME;
        if (s->player_vy > 6) {
            s->player_vy = 6;
        }
        s->player_y += s->player_vy;
        center_x = s->player_x + (DK_PLAYER_W / 2);
        feet_y = s->player_y + DK_PLAYER_H;
        int support = donkey_kong_support_platform(center_x, feet_y);
        if (support >= 0 && s->player_vy >= 0) {
            int py = donkey_kong_platform_y(support, center_x);
            s->player_y = py - DK_PLAYER_H;
            s->player_vy = 0;
            s->jumping = 0;
        } else {
            s->jumping = 1;
        }
    }

    if (s->player_y < 0) {
        s->player_y = 0;
    }
    if (s->player_y > DK_BOARD_H - DK_PLAYER_H) {
        s->player_y = DK_BOARD_H - DK_PLAYER_H;
    }

    s->input_move = 0;
    s->input_climb = 0;
}

static void donkey_kong_step_barrels(struct donkey_kong_state *s) {
    for (int i = 0; i < DK_MAX_BARRELS; ++i) {
        int barrel_center_x;
        int barrel_center_y;

        if (!s->barrel_active[i]) {
            continue;
        }

        if (s->barrel_on_ladder[i]) {
            s->barrel_y[i] += DK_BARREL_CLIMB_SPEED;
            if (s->barrel_y[i] >= s->barrel_ladder_target[i]) {
                s->barrel_on_ladder[i] = 0;
                s->barrel_platform[i] -= 1;
                if (s->barrel_platform[i] < 0) {
                    s->barrel_active[i] = 0;
                    continue;
                }
                s->barrel_y[i] = donkey_kong_platform_y(s->barrel_platform[i], s->barrel_x[i]) - DK_BARREL_H;
                s->barrel_dir[i] = -s->barrel_dir[i];
            }
        } else {
            int p = s->barrel_platform[i];
            int nx = s->barrel_x[i] + (s->barrel_dir[i] * DK_BARREL_SPEED);
            int edge_hit = 0;

            if (nx <= g_platform_x1[p]) {
                nx = g_platform_x1[p];
                edge_hit = 1;
            } else if (nx >= g_platform_x2[p] - DK_BARREL_W) {
                nx = g_platform_x2[p] - DK_BARREL_W;
                edge_hit = 1;
            }

            s->barrel_x[i] = nx;
            s->barrel_y[i] = donkey_kong_platform_y(p, s->barrel_x[i] + (DK_BARREL_W / 2)) - DK_BARREL_H;

            if (!edge_hit && p > 0) {
                int center = s->barrel_x[i] + (DK_BARREL_W / 2);
                for (int l = 0; l < DK_LADDER_COUNT; ++l) {
                    if (!donkey_kong_barrel_ladder_available(p, l)) {
                        continue;
                    }
                    if (center >= g_ladder_x[l] - 3 && center <= g_ladder_x[l] + 3) {
                        if ((donkey_kong_next_random(s) % 100u) < 28u) {
                            s->barrel_on_ladder[i] = 1;
                            s->barrel_ladder_target[i] = donkey_kong_platform_y(p - 1, g_ladder_x[l]) - DK_BARREL_H;
                            s->barrel_x[i] = g_ladder_x[l] - (DK_BARREL_W / 2);
                        }
                        break;
                    }
                }
            }

            if (edge_hit) {
                if (p == 0) {
                    s->barrel_active[i] = 0;
                    s->score += 15;
                    s->avoided_barrels += 1;
                    continue;
                }
                s->barrel_platform[i] -= 1;
                s->barrel_dir[i] = -s->barrel_dir[i];
                s->barrel_y[i] = donkey_kong_platform_y(s->barrel_platform[i],
                                                        s->barrel_x[i] + (DK_BARREL_W / 2)) - DK_BARREL_H;
            }
        }

        barrel_center_x = s->barrel_x[i] + (DK_BARREL_W / 2);
        barrel_center_y = s->barrel_y[i] + (DK_BARREL_H / 2);
        if (barrel_center_x >= s->player_x &&
            barrel_center_x <= s->player_x + DK_PLAYER_W &&
            barrel_center_y >= s->player_y &&
            barrel_center_y <= s->player_y + DK_PLAYER_H) {
            if (s->invuln_ticks == 0u) {
                s->lives -= 1;
                s->invuln_ticks = DK_INVULN_TICKS_AFTER_HIT;
                if (s->lives <= 0) {
                    s->game_over = 1;
                    return;
                }
                donkey_kong_respawn_player(s);
            }
        }
    }
}

static int donkey_kong_check_win(struct donkey_kong_state *s) {
    int rescue_x = 222;
    int rescue_y = donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, rescue_x) - DK_PLAYER_H - 2;
    int px = s->player_x + (DK_PLAYER_W / 2);

    if (px >= rescue_x - 8 && px <= rescue_x + 8 && s->player_y <= rescue_y + 3) {
        s->win = 1;
        s->score += 250;
        return 1;
    }
    return 0;
}

int donkey_kong_step(struct donkey_kong_state *s, uint32_t ticks) {
    uint32_t delta;
    int changed = 0;

    if (s->game_over || s->win) {
        s->tick_count = ticks;
        return 0;
    }

    if (s->tick_count == 0u) {
        s->tick_count = ticks;
        return 1;
    }
    delta = ticks - s->tick_count;
    s->tick_count = ticks;
    if (delta > 120u) {
        delta = 120u;
    }

    s->physics_accum += delta;
    s->spawn_accum += delta;
    s->score_accum += delta;
    if (s->invuln_ticks > delta) {
        s->invuln_ticks -= delta;
    } else {
        s->invuln_ticks = 0u;
    }

    while (s->score_accum >= (uint32_t)DK_SURVIVAL_SCORE_TICKS) {
        s->score += 1;
        s->score_accum -= (uint32_t)DK_SURVIVAL_SCORE_TICKS;
        changed = 1;
    }
    while (s->spawn_accum >= (uint32_t)DK_BARREL_SPAWN_TICKS) {
        donkey_kong_spawn_barrel(s);
        s->spawn_accum -= (uint32_t)DK_BARREL_SPAWN_TICKS;
        changed = 1;
    }

    while (s->physics_accum >= (uint32_t)DK_FRAME_TICKS) {
        donkey_kong_step_player(s);
        donkey_kong_step_barrels(s);
        if (s->game_over) {
            return 1;
        }
        if (donkey_kong_check_win(s)) {
            return 1;
        }
        s->physics_accum -= (uint32_t)DK_FRAME_TICKS;
        changed = 1;
    }

    return changed;
}

void donkey_kong_draw_window(struct donkey_kong_state *s, int active,
                             int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect body = {s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22};
    struct rect topbar = {s->window.x + 8, s->window.y + 22, s->window.w - 16, 18};
    struct rect board = {s->window.x + 8, s->window.y + 44, DK_BOARD_W, DK_BOARD_H};
    struct rect hud = {board.x + board.w + 8, board.y, s->window.w - (board.w + 24), board.h};
    char score_text[24];
    char lives_text[24];
    struct rect stat1 = {hud.x + 6, hud.y + 12, hud.w - 12, 28};
    struct rect stat2 = {hud.x + 6, hud.y + 46, hud.w - 12, 28};
    struct rect stat3 = {hud.x + 6, hud.y + 80, hud.w - 12, 28};
    struct rect help = {hud.x + 6, hud.y + 118, hud.w - 12, 56};
    struct rect status = {hud.x + 6, hud.y + 182, hud.w - 12, 30};

    draw_window_frame(&s->window, "MONKEY DONG", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_canvas());
    ui_draw_surface(&topbar, ui_color_panel());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud, ui_color_canvas());
    ui_draw_inset(&stat1, ui_color_canvas());
    ui_draw_inset(&stat2, ui_color_canvas());
    ui_draw_inset(&stat3, ui_color_canvas());
    ui_draw_inset(&help, ui_color_canvas());
    ui_draw_inset(&status, ui_color_canvas());

    sys_text(topbar.x + 6, topbar.y + 5, ui_color_muted(), "Arcade climb");

    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, t->background);
    donkey_kong_draw_backdrop(&board, t);

    for (int p = 0; p < DK_PLATFORM_COUNT; ++p) {
        int y_left = donkey_kong_platform_y(p, g_platform_x1[p]);
        int y_right = donkey_kong_platform_y(p, g_platform_x2[p]);
        donkey_kong_draw_girder_span(g_platform_x1[p], y_left, g_platform_x2[p], y_right, &board, t);
    }
    for (int l = 0; l < DK_LADDER_COUNT; ++l) {
        int top = donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, g_ladder_x[l]) + 2;
        int bottom = donkey_kong_platform_y(0, g_ladder_x[l]) - 2;
        for (int y = top; y <= bottom; y += 4) {
            sys_rect(board.x + g_ladder_x[l], board.y + y, 1, 3, t->menu_button_inactive);
            sys_rect(board.x + g_ladder_x[l] + 5, board.y + y, 1, 3, t->menu_button_inactive);
            sys_rect(board.x + g_ladder_x[l] + 1, board.y + y + 1, 4, 1, t->text);
        }
    }

    for (int i = 0; i < DK_MAX_BARRELS; ++i) {
        if (!s->barrel_active[i]) {
            continue;
        }
        donkey_kong_draw_barrel_sprite(board.x + s->barrel_x[i], board.y + s->barrel_y[i], t);
    }

    if (!(s->invuln_ticks > 0u && ((s->tick_count / 8u) % 2u) == 0u)) {
        donkey_kong_draw_player_sprite(board.x + s->player_x, board.y + s->player_y, t);
    }
    donkey_kong_draw_kong_sprite(board.x + 12,
                                 board.y + donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, 24) - 34,
                                 t);
    donkey_kong_draw_goal_sprite(board.x + 218,
                                 board.y + donkey_kong_platform_y(DK_PLATFORM_COUNT - 1, 222) - 16,
                                 t);

    str_copy_limited(score_text, "Score ", (int)sizeof(score_text));
    donkey_kong_append_int(score_text, s->score, (int)sizeof(score_text));
    str_copy_limited(lives_text, "Vidas ", (int)sizeof(lives_text));
    donkey_kong_append_int(lives_text, s->lives, (int)sizeof(lives_text));

    sys_text(stat1.x + 6, stat1.y + 6, t->text, score_text);
    sys_text(stat2.x + 6, stat2.y + 6, t->text, lives_text);
    sys_text(stat3.x + 6, stat3.y + 6, t->text, "Evitados");
    str_copy_limited(score_text, "", (int)sizeof(score_text));
    donkey_kong_append_int(score_text, s->avoided_barrels, (int)sizeof(score_text));
    sys_text(stat3.x + 6, stat3.y + 16, t->text, score_text);
    sys_text(help.x + 6, help.y + 6, t->text, "Setas movem");
    sys_text(help.x + 6, help.y + 18, t->text, "Space pula");
    sys_text(help.x + 6, help.y + 30, t->text, "Suba ate o topo");
    if (s->game_over) {
        sys_text(status.x + 6, status.y + 8, t->text, "Game over");
        sys_text(status.x + 6, status.y + 18, t->text, "R reinicia");
    } else if (s->win) {
        sys_text(status.x + 6, status.y + 8, t->text, "Pauline!");
        sys_text(status.x + 6, status.y + 18, t->text, "R reinicia");
    } else {
        sys_text(status.x + 6, status.y + 8, t->text, "Desvie dos barris");
        sys_text(status.x + 6, status.y + 18, t->text, "e alcance o topo");
    }
}
