#ifndef ICON_THEME_H
#define ICON_THEME_H

#include <userland/modules/include/utils.h>

enum icon_theme_context {
    ICON_THEME_CONTEXT_APPS = 0,
    ICON_THEME_CONTEXT_PLACES,
    ICON_THEME_CONTEXT_ACTIONS,
    ICON_THEME_CONTEXT_STATUS,
    ICON_THEME_CONTEXT_NOTIFICATIONS,
    ICON_THEME_CONTEXT_PANEL
};

void icon_theme_init(void);
void icon_theme_reset_cache(void);
void icon_theme_set_current(const char *theme_name);
const char *icon_theme_current(void);
int icon_theme_draw(const char *name,
                    enum icon_theme_context context,
                    int preferred_size,
                    int dst_x,
                    int dst_y,
                    int dst_w,
                    int dst_h);
int icon_theme_draw_inset(const char *name,
                          enum icon_theme_context context,
                          int preferred_size,
                          const struct rect *outer,
                          int padding_x,
                          int padding_y,
                          int width,
                          int height);

#endif
