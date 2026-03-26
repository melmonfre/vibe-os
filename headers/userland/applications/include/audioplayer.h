#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <userland/modules/include/utils.h>

#define AUDIOPLAYER_PATH_MAX 96

struct audioplayer_state {
    struct rect window;
    char path[AUDIOPLAYER_PATH_MAX];
    char status[160];
    int input_focus;
};

void audioplayer_init_state(struct audioplayer_state *player);
int audioplayer_node_is_supported(int node);
int audioplayer_open_node(struct audioplayer_state *player, int node);
void audioplayer_draw_window(struct audioplayer_state *player, int active,
                             int min_hover, int max_hover, int close_hover);
int audioplayer_handle_click(struct audioplayer_state *player, int x, int y);
int audioplayer_handle_key(struct audioplayer_state *player, int key);

#endif // AUDIOPLAYER_H
