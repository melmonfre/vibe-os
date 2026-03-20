#include <userland/applications/include/games/brick_race.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};
static const uint32_t BR_STEP_TICKS = 3u;
static const int BR_TRACK_W = 284;
static const int BR_TRACK_H = 214;
static const int BR_HORIZON_Y = 18;
static const int BR_PLAYER_Y = 182;
static const int BR_X_LIMIT = 70;
static const int BR_BASE_SPEED = 8;
static const int BR_MAX_SPEED = 26;
static const int BR_BOOST_BONUS = 7;
static const int BR_ENERGY_MAX = 100;
static const int BR_INVULN_TICKS = 24;
static const int BR_GOAL_PROGRESS = 32000;
static const int BR_TIME_TOTAL = 4700;

static int br_abs(int v) { return (v < 0) ? -v : v; }

static int br_clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static uint32_t br_rand(struct brick_race_state *s) {
    s->seed = s->seed * 1664525u + 1013904223u;
    return s->seed;
}

static int br_track_half_w(int y) {
    return 26 + ((y - BR_HORIZON_Y) * 104) / (BR_TRACK_H - BR_HORIZON_Y);
}

static int br_curve_target(int progress) {
    int phase = (progress / 210) % 112;
    if (phase < 28) {
        return phase - 14;
    }
    if (phase < 56) {
        return 42 - phase;
    }
    if (phase < 84) {
        return -(phase - 70);
    }
    return phase - 98;
}

static void br_draw_rival_sprite(int x, int y, int w, int h, int kind, const struct desktop_theme *t) {
    int body = (kind == 2) ? t->window : t->menu_button_inactive;
    int canopy = (kind == 1) ? t->text : t->menu_button;

    if (w < 8) {
        w = 8;
    }
    if (h < 6) {
        h = 6;
    }

    sys_rect(x - (w / 2), y - h, w, h, body);
    sys_rect(x - (w / 3), y - h - (h / 3), (w * 2) / 3, h / 3, canopy);
    sys_rect(x - (w / 4), y - h + 1, w / 2, 1, t->window_bg);
    sys_rect(x - (w / 2), y - 2, 2, 2, t->text);
    sys_rect(x + (w / 2) - 2, y - 2, 2, 2, t->text);
}

static void br_draw_player_sprite(int x, int y, const struct desktop_theme *t) {
    sys_rect(x - 12, y - 7, 24, 9, t->menu_button);
    sys_rect(x - 6, y - 12, 12, 4, t->text);
    sys_rect(x - 15, y - 2, 5, 4, t->window);
    sys_rect(x + 10, y - 2, 5, 4, t->window);
    sys_rect(x - 4, y - 14, 8, 2, t->menu_button_inactive);
    sys_rect(x - 2, y - 6, 4, 2, t->window_bg);
}

static void br_append_int(char *dst, int value, int max_len) {
    char tmp[16];
    int len = 0;
    int i;

    if (value < 0) {
        str_append(dst, "-", max_len);
        value = -value;
    }
    if (value == 0) {
        str_append(dst, "0", max_len);
        return;
    }
    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (i = len - 1; i >= 0; --i) {
        char one[2];
        one[0] = tmp[i];
        one[1] = '\0';
        str_append(dst, one, max_len);
    }
}

static void br_reset(struct brick_race_state *s) {
    s->lane = 1;
    s->player_x = 0;
    s->speed = BR_BASE_SPEED;
    s->target_speed = BR_BASE_SPEED + 2;
    s->boost_ticks = 0;
    s->energy = BR_ENERGY_MAX;
    s->invuln_ticks = 0;
    s->road_shift = 0;
    s->score = 0;
    s->game_over = 0;
    s->win = 0;
    s->progress = 0;
    s->max_progress = BR_GOAL_PROGRESS;
    s->race_time_left = BR_TIME_TOTAL;
    s->seed = 0x41C64E6Du;
    s->tick_count = 0u;
    s->next_tick = 0u;
    s->last_ticks = 0u;
    s->next_spawn_tick = 18u;
    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        s->obs_active[i] = 0;
        s->obs_lane[i] = 0;
        s->obs_y[i] = 0;
        s->obs_kind[i] = 0;
        s->obs_phase[i] = 0;
    }
}

static void br_spawn_one(struct brick_race_state *s, int lane, int kind, int phase) {
    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        if (!s->obs_active[i]) {
            s->obs_active[i] = 1;
            s->obs_lane[i] = lane;
            s->obs_y[i] = 8;
            s->obs_kind[i] = kind;
            s->obs_phase[i] = phase;
            return;
        }
    }
}

static int br_object_offset(const struct brick_race_state *s, int i) {
    int lane_offset = s->obs_lane[i] * 24;
    int depth = s->obs_y[i];
    int move = 0;

    if (s->obs_kind[i] == 1) {
        int t = ((int)(s->tick_count / 2u) + s->obs_phase[i]) % 32;
        move = (t < 16) ? (t - 8) : (24 - t);
    } else if (s->obs_kind[i] == 2) {
        int t = ((int)s->tick_count + s->obs_phase[i]) % 48;
        move = (t < 24) ? (t - 12) : (36 - t);
        move /= 2;
    }

    return lane_offset + ((move * (40 + depth)) / 140);
}

void brick_race_init_state(struct brick_race_state *s) {
    s->window = DEFAULT_WINDOW;
    br_reset(s);
}

int brick_race_handle_key(struct brick_race_state *s, int key) {
    if (s->game_over || s->win) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n' || key == '\r') {
            br_reset(s);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        s->player_x -= 12;
        s->lane = br_clamp(((s->player_x + 42) / 28), 0, 2);
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        s->player_x += 12;
        s->lane = br_clamp(((s->player_x + 42) / 28), 0, 2);
        return 1;
    }
    if (key == KEY_ARROW_UP || key == 'b' || key == 'B') {
        s->boost_ticks = 36;
        return 1;
    }
    if (key == 'r' || key == 'R') {
        br_reset(s);
        return 1;
    }
    return 0;
}

int brick_race_step(struct brick_race_state *s, uint32_t ticks) {
    int changed = 0;

    if (s->last_ticks == 0u) {
        s->last_ticks = ticks;
        s->next_tick = ticks + BR_STEP_TICKS;
        return 1;
    }
    if (ticks <= s->last_ticks) {
        return 0;
    }
    s->last_ticks = ticks;

    while (ticks >= s->next_tick) {
        s->tick_count += 1u;
        s->next_tick += BR_STEP_TICKS;
        changed = 1;

        if (s->game_over || s->win) {
            continue;
        }

        if (s->race_time_left > 0) {
            s->race_time_left -= 1;
        }
        if (s->boost_ticks > 0) {
            s->boost_ticks -= 1;
            if ((s->tick_count & 3u) == 0u && s->energy > 0) {
                s->energy -= 1;
            }
        }
        if (s->invuln_ticks > 0) {
            s->invuln_ticks -= 1;
        } else if (s->energy < BR_ENERGY_MAX && (s->tick_count % 12u) == 0u) {
            s->energy += 1;
        }

        s->target_speed = BR_BASE_SPEED + 2 + (s->progress / 2600);
        if (s->target_speed > BR_MAX_SPEED - 4) {
            s->target_speed = BR_MAX_SPEED - 4;
        }
        if (s->boost_ticks > 0) {
            s->target_speed += BR_BOOST_BONUS;
        }
        if (s->energy < 28 && s->target_speed > (BR_BASE_SPEED + 5)) {
            s->target_speed = BR_BASE_SPEED + 5;
        }

        s->target_speed = br_clamp(s->target_speed, BR_BASE_SPEED, BR_MAX_SPEED);
        if (s->speed < s->target_speed) {
            s->speed += 1;
        } else if (s->speed > s->target_speed) {
            s->speed -= 1;
        }

        s->player_x = br_clamp(s->player_x, -BR_X_LIMIT, BR_X_LIMIT);
        s->player_x -= br_curve_target(s->progress) / 16;
        s->player_x = br_clamp(s->player_x, -BR_X_LIMIT, BR_X_LIMIT);

        s->progress += s->speed;
        s->score = s->progress / 5;

        s->road_shift += (br_curve_target(s->progress) - s->road_shift) / 5;
        s->road_shift = br_clamp(s->road_shift, -26, 26);

        if (s->tick_count >= s->next_spawn_tick) {
            int gap = 24 - (s->speed / 2);
            int lane = (int)(br_rand(s) % 3u) - 1;
            int kind = (int)(br_rand(s) % 3u);
            int phase = (int)(br_rand(s) % 64u);
            br_spawn_one(s, lane, kind, phase);

            if ((br_rand(s) & 7u) == 0u) {
                int lane2 = (int)(br_rand(s) % 3u) - 1;
                int kind2 = (int)(br_rand(s) % 2u);
                br_spawn_one(s, lane2, kind2, (int)(br_rand(s) % 64u));
            }

            if (gap < 8) {
                gap = 8;
            }
            s->next_spawn_tick = s->tick_count + (uint32_t)gap;
        }

        for (int i = 0; i < BRICK_RACE_OBS; ++i) {
            int obj_offset;
            int delta;

            if (!s->obs_active[i]) {
                continue;
            }

            s->obs_y[i] += s->speed + 2 + ((s->obs_kind[i] == 2) ? 1 : 0);
            if (s->obs_y[i] > 1050) {
                s->obs_active[i] = 0;
                continue;
            }

            if (s->obs_y[i] < 850 || s->obs_y[i] > 1012 || s->invuln_ticks > 0) {
                continue;
            }
            obj_offset = br_object_offset(s, i);
            delta = br_abs(obj_offset - s->player_x);
            if (delta < ((s->obs_kind[i] == 2) ? 18 : 20)) {
                s->energy -= (s->obs_kind[i] == 2) ? 22 : 16;
                s->speed -= 4;
                if (s->speed < BR_BASE_SPEED) {
                    s->speed = BR_BASE_SPEED;
                }
                s->invuln_ticks = BR_INVULN_TICKS;
                s->obs_active[i] = 0;
            }
        }

        if (s->energy <= 0 || s->race_time_left <= 0) {
            s->game_over = 1;
        }
        if (s->progress >= s->max_progress) {
            s->win = 1;
        }
    }
    return changed;
}

void brick_race_draw_window(struct brick_race_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect body = {s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22};
    struct rect topbar = {s->window.x + 8, s->window.y + 22, s->window.w - 16, 18};
    struct rect board = {s->window.x + 10, s->window.y + 44, BR_TRACK_W, BR_TRACK_H};
    struct rect hud = {board.x + board.w + 6, board.y, s->window.w - (board.w + 20), board.h};
    struct rect stat1 = {hud.x + 4, hud.y + 10, hud.w - 8, 24};
    struct rect stat2 = {hud.x + 4, hud.y + 38, hud.w - 8, 24};
    struct rect stat3 = {hud.x + 4, hud.y + 66, hud.w - 8, 24};
    struct rect bars = {hud.x + 4, hud.y + 96, hud.w - 8, 40};
    struct rect status = {hud.x + 4, hud.y + 144, hud.w - 8, 42};
    int center_x = board.x + (board.w / 2);
    int player_screen_x;
    char line[32];

    draw_window_frame(&s->window, "BRICK RACE", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_canvas());
    ui_draw_surface(&topbar, ui_color_panel());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud, ui_color_canvas());
    ui_draw_inset(&stat1, ui_color_canvas());
    ui_draw_inset(&stat2, ui_color_canvas());
    ui_draw_inset(&stat3, ui_color_canvas());
    ui_draw_inset(&bars, ui_color_canvas());
    ui_draw_inset(&status, ui_color_canvas());
    sys_text(topbar.x + 6, topbar.y + 5, ui_color_muted(), "F-zero inspired sprint");

    sys_rect(board.x + 1, board.y + 1, board.w - 2, BR_HORIZON_Y + 24, t->background);
    sys_rect(board.x + 1, board.y + BR_HORIZON_Y + 24, board.w - 2, board.h - (BR_HORIZON_Y + 25), ui_color_canvas());
    for (int i = 0; i < 6; ++i) {
        int bx = board.x + 14 + i * 44;
        int bh = 18 + ((i * 9 + (s->progress / 90)) % 20);
        sys_rect(bx, board.y + BR_HORIZON_Y + 12 - bh, 22, bh, t->menu_button_inactive);
        sys_rect(bx + 6, board.y + BR_HORIZON_Y + 6 - bh, 8, 5, t->window);
    }

    for (int y = BR_HORIZON_Y; y < BR_TRACK_H; y += 2) {
        int half_w = br_track_half_w(y);
        int road_center = center_x + (s->road_shift * y) / BR_TRACK_H;
        int left = road_center - half_w;
        int right = road_center + half_w;
        int stripe = ((y + (s->progress / 9)) / 10) & 1;

        sys_rect(left - 4, board.y + y, 4, 2, t->window);
        sys_rect(left, board.y + y, (half_w * 2), 2, ui_color_canvas());
        sys_rect(right, board.y + y, 4, 2, t->window);
        if (stripe) {
            sys_rect(left, board.y + y, (half_w * 2), 2, t->menu_button_inactive);
        }
        if ((y % 14) < 8) {
            sys_rect(road_center - 2, board.y + y, 4, 2, t->text);
        }
    }

    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        int depth;
        int y;
        int half_w;
        int road_center;
        int x;
        int w;
        int h;

        if (!s->obs_active[i]) {
            continue;
        }

        depth = s->obs_y[i];
        y = BR_HORIZON_Y + ((depth * (BR_TRACK_H - BR_HORIZON_Y - 2)) / 1024);
        half_w = br_track_half_w(y);
        road_center = center_x + (s->road_shift * y) / BR_TRACK_H;
        x = road_center + (br_object_offset(s, i) * half_w) / 56;
        w = 7 + (depth / 86);
        h = 5 + (depth / 108);
        if (s->obs_kind[i] == 2) {
            h += 2;
        }
        br_draw_rival_sprite(x, board.y + y, w, h, s->obs_kind[i], t);
    }

    player_screen_x = center_x + s->player_x;
    if ((s->invuln_ticks == 0) || ((s->invuln_ticks & 2) == 0)) {
        br_draw_player_sprite(player_screen_x, board.y + BR_PLAYER_Y, t);
    }

    line[0] = '\0';
    str_copy_limited(line, "Vel ", (int)sizeof(line));
    br_append_int(line, s->speed, (int)sizeof(line));
    sys_text(stat1.x + 5, stat1.y + 7, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Energia ", (int)sizeof(line));
    br_append_int(line, s->energy, (int)sizeof(line));
    str_append(line, "%", (int)sizeof(line));
    sys_text(stat2.x + 5, stat2.y + 7, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Progresso ", (int)sizeof(line));
    if (s->max_progress > 0) {
        br_append_int(line, (s->progress * 100) / s->max_progress, (int)sizeof(line));
    } else {
        br_append_int(line, 0, (int)sizeof(line));
    }
    str_append(line, "%", (int)sizeof(line));
    sys_text(stat3.x + 5, stat3.y + 7, t->text, line);

    sys_text(bars.x + 5, bars.y + 5, ui_color_muted(), "Energia");
    sys_rect(bars.x + 5, bars.y + 16, bars.w - 10, 6, t->menu_button_inactive);
    sys_rect(bars.x + 5, bars.y + 16, ((bars.w - 10) * s->energy) / BR_ENERGY_MAX, 6, t->menu_button);
    sys_text(bars.x + 5, bars.y + 26, ui_color_muted(), "Progresso");
    sys_rect(bars.x + 5, bars.y + 36, bars.w - 10, 6, t->menu_button_inactive);
    sys_rect(bars.x + 5, bars.y + 36, ((bars.w - 10) * s->progress) / s->max_progress, 6, t->window);

    line[0] = '\0';
    str_copy_limited(line, "Tempo ", (int)sizeof(line));
    br_append_int(line, s->race_time_left / 33, (int)sizeof(line));
    sys_text(status.x + 5, status.y + 6, t->text, line);
    line[0] = '\0';
    str_copy_limited(line, "Score ", (int)sizeof(line));
    br_append_int(line, s->score, (int)sizeof(line));
    sys_text(status.x + 5, status.y + 18, t->text, line);

    if (s->win) {
        sys_text(status.x + 5, status.y + 30, t->text, "Chegada!");
    } else if (s->game_over) {
        sys_text(status.x + 5, status.y + 30, t->text, "Explodiu");
    } else {
        sys_text(status.x + 5, status.y + 30, t->text, "<- -> move / UP boost");
    }
}
