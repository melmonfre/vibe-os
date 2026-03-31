#include <userland/applications/include/audioplayer.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

static const struct rect DEFAULT_AUDIOPLAYER_WINDOW = {76, 42, 448, 218};

enum audioplayer_button_id {
    AUDIOPLAYER_BUTTON_BOOT = 0,
    AUDIOPLAYER_BUTTON_DESKTOP,
    AUDIOPLAYER_BUTTON_CAPTURE,
    AUDIOPLAYER_BUTTON_PLAY
};

static int audioplayer_name_has_extension(const char *name, const char *ext) {
    int name_len;
    int ext_len;

    if (!name || !ext) {
        return 0;
    }

    name_len = str_len(name);
    ext_len = str_len(ext);
    if (name_len < ext_len) {
        return 0;
    }
    return str_eq_ci(name + name_len - ext_len, ext);
}

static void audioplayer_set_path(struct audioplayer_state *player, const char *path) {
    str_copy_limited(player->path, path ? path : "", (int)sizeof(player->path));
}

static void audioplayer_set_status(struct audioplayer_state *player, const char *text) {
    str_copy_limited(player->status, text ? text : "", (int)sizeof(player->status));
}

static struct rect audioplayer_body_rect(const struct audioplayer_state *player) {
    struct rect r = {player->window.x + 4, player->window.y + 18, player->window.w - 8, player->window.h - 22};
    return r;
}

static struct rect audioplayer_toolbar_rect(const struct audioplayer_state *player) {
    struct rect r = {player->window.x + 10, player->window.y + 24, player->window.w - 20, 28};
    return r;
}

static struct rect audioplayer_path_rect(const struct audioplayer_state *player) {
    struct rect r = {player->window.x + 16, player->window.y + 68, player->window.w - 32, 18};
    return r;
}

static struct rect audioplayer_hint_rect(const struct audioplayer_state *player) {
    struct rect r = {player->window.x + 16, player->window.y + 94, player->window.w - 32, 44};
    return r;
}

static struct rect audioplayer_status_rect(const struct audioplayer_state *player) {
    struct rect r = {player->window.x + 12, player->window.y + player->window.h - 28, player->window.w - 24, 14};
    return r;
}

static struct rect audioplayer_button_rect(const struct audioplayer_state *player, int button) {
    struct rect toolbar = audioplayer_toolbar_rect(player);
    struct rect r = {toolbar.x, toolbar.y + 6, 58, 16};

    if (button == AUDIOPLAYER_BUTTON_BOOT) {
        r.x = toolbar.x + 152;
        r.w = 54;
    } else if (button == AUDIOPLAYER_BUTTON_DESKTOP) {
        r.x = toolbar.x + 212;
        r.w = 66;
    } else if (button == AUDIOPLAYER_BUTTON_CAPTURE) {
        r.x = toolbar.x + 284;
        r.w = 64;
    } else {
        r.x = toolbar.x + toolbar.w - 58;
        r.w = 50;
    }

    return r;
}

static int audioplayer_play_current(struct audioplayer_state *player) {
    if (!player || player->path[0] == '\0') {
        if (player) {
            audioplayer_set_status(player, "Digite um caminho .wav");
        }
        return -1;
    }

    if (sys_audio_play_asset(player->path) == 0) {
        audioplayer_set_status(player, "Playback iniciado");
        return 0;
    }

    audioplayer_set_status(player, "Falha ao iniciar playback");
    return -1;
}

void audioplayer_init_state(struct audioplayer_state *player) {
    if (!player) {
        return;
    }

    player->window = DEFAULT_AUDIOPLAYER_WINDOW;
    audioplayer_set_path(player, "/assets/vibe_os_desktop.wav");
    audioplayer_set_status(player, "Escolha um WAV e pressione PLAY");
    player->input_focus = 1;
}

int audioplayer_node_is_supported(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return 0;
    }
    return audioplayer_name_has_extension(g_fs_nodes[node].name, ".wav");
}

int audioplayer_open_node(struct audioplayer_state *player, int node) {
    char path[AUDIOPLAYER_PATH_MAX];

    if (!player || !audioplayer_node_is_supported(node)) {
        if (player) {
            audioplayer_set_status(player, "Arquivo nao e um WAV suportado");
        }
        return -1;
    }

    fs_build_path(node, path, (int)sizeof(path));
    audioplayer_set_path(player, path);
    audioplayer_set_status(player, "Arquivo carregado");
    player->input_focus = 1;
    return 0;
}

int audioplayer_handle_click(struct audioplayer_state *player, int x, int y) {
    struct rect input;
    struct rect boot;
    struct rect desktop;
    struct rect capture;
    struct rect play;

    if (!player) {
        return 0;
    }

    input = audioplayer_path_rect(player);
    boot = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_BOOT);
    desktop = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_DESKTOP);
    capture = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_CAPTURE);
    play = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_PLAY);

    if (point_in_rect(&input, x, y)) {
        player->input_focus = 1;
        return 1;
    }
    player->input_focus = 0;

    if (point_in_rect(&boot, x, y)) {
        audioplayer_set_path(player, "/assets/vibe_os_boot.wav");
        audioplayer_set_status(player, "Preset de boot carregado");
        return 1;
    }
    if (point_in_rect(&desktop, x, y)) {
        audioplayer_set_path(player, "/assets/vibe_os_desktop.wav");
        audioplayer_set_status(player, "Preset do desktop carregado");
        return 1;
    }
    if (point_in_rect(&capture, x, y)) {
        audioplayer_set_path(player, "/capture.wav");
        audioplayer_set_status(player, "Preset de captura carregado");
        return 1;
    }
    if (point_in_rect(&play, x, y)) {
        (void)audioplayer_play_current(player);
        return 1;
    }

    return 0;
}

int audioplayer_handle_key(struct audioplayer_state *player, int key) {
    int len;

    if (!player) {
        return 0;
    }

    if (key == '\t') {
        player->input_focus = !player->input_focus;
        return 1;
    }

    if (!player->input_focus) {
        if (key == '\n') {
            (void)audioplayer_play_current(player);
            return 1;
        }
        return 0;
    }

    if (key == '\b' || key == 127) {
        len = str_len(player->path);
        if (len > 0) {
            player->path[len - 1] = '\0';
        }
        return 1;
    }
    if (key == '\n') {
        (void)audioplayer_play_current(player);
        return 1;
    }
    if (key >= 32 && key <= 126) {
        len = str_len(player->path);
        if (len < (int)sizeof(player->path) - 1) {
            player->path[len] = (char)key;
            player->path[len + 1] = '\0';
            return 1;
        }
    }

    return 0;
}

void audioplayer_draw_window(struct audioplayer_state *player, int active,
                             int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = audioplayer_body_rect(player);
    struct rect toolbar = audioplayer_toolbar_rect(player);
    struct rect path = audioplayer_path_rect(player);
    struct rect hint = audioplayer_hint_rect(player);
    struct rect status = audioplayer_status_rect(player);
    struct rect boot = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_BOOT);
    struct rect desktop = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_DESKTOP);
    struct rect capture = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_CAPTURE);
    struct rect play = audioplayer_button_rect(player, AUDIOPLAYER_BUTTON_PLAY);

    draw_window_frame(&player->window, "AUDIO PLAYER", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&toolbar, ui_color_panel());
    ui_draw_inset(&path, ui_color_window_bg());
    ui_draw_surface(&hint, ui_color_panel());

    ui_draw_button(&boot, "BOOT", UI_BUTTON_NORMAL, 0);
    ui_draw_button(&desktop, "DESKTOP", UI_BUTTON_NORMAL, 0);
    ui_draw_button(&capture, "CAPTURE", UI_BUTTON_NORMAL, 0);
    ui_draw_button(&play, "PLAY", UI_BUTTON_ACTIVE, 0);

    sys_text(toolbar.x + 8, toolbar.y + 10, theme->text, "Teste WAV rapido no desktop");
    sys_text(path.x + 6, path.y + 5, theme->text, player->path[0] != '\0' ? player->path : "(vazio)");
    sys_text(hint.x + 8, hint.y + 8, ui_color_muted(), "Edite o caminho, pressione Enter ou clique em PLAY.");
    sys_text(hint.x + 8, hint.y + 20, ui_color_muted(), "Presets uteis: boot, desktop e /capture.wav.");
    if (player->input_focus) {
        sys_text(path.x + path.w - 56, path.y + 5, ui_color_muted(), "[edit]");
    }

    ui_draw_status(&status, player->status);
}
