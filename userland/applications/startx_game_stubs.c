#include <stdint.h>

static const char *g_startx_doom_error = "DOOM indisponivel no startx.app modular";
static int g_startx_craft_started = 0;

int doom_port_run_full(void) {
    return -1;
}

const char *doom_port_last_error(void) {
    return g_startx_doom_error;
}

int doom_port_iwad_available(void) {
    return 0;
}

int craft_upstream_start(int width, int height) {
    (void)width;
    (void)height;
    g_startx_craft_started = 0;
    return -1;
}

int craft_upstream_frame(void) {
    return g_startx_craft_started ? 0 : -1;
}

void craft_upstream_stop(void) {
    g_startx_craft_started = 0;
}

void craft_upstream_resize(int width, int height) {
    (void)width;
    (void)height;
}

void craft_upstream_queue_key(int key) {
    (void)key;
}

void craft_upstream_set_mouse(int x, int y, int dx, int dy,
                              uint8_t buttons, int focused, int inside) {
    (void)x;
    (void)y;
    (void)dx;
    (void)dy;
    (void)buttons;
    (void)focused;
    (void)inside;
}

void craft_upstream_blit(int x, int y) {
    (void)x;
    (void)y;
}

void craft_upstream_request_close(void) {
    g_startx_craft_started = 0;
}
