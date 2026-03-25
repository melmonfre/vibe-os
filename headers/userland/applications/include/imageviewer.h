#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <stdint.h>

#include <userland/modules/include/utils.h>

struct imageviewer_state {
    struct rect window;
    int image_node;
    int rendered_node;
    int image_w;
    int image_h;
    int render_limit_w;
    int render_limit_h;
    uint8_t *pixels;
    char message[64];
};

void imageviewer_init_state(struct imageviewer_state *viewer);
void imageviewer_shutdown_state(struct imageviewer_state *viewer);
int imageviewer_open_node(struct imageviewer_state *viewer, int node);
void imageviewer_draw_window(struct imageviewer_state *viewer, int active,
                             int min_hover, int max_hover, int close_hover);
int imageviewer_handle_click(struct imageviewer_state *viewer, int x, int y);

#endif // IMAGEVIEWER_H
