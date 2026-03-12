#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <userland/modules/include/utils.h>

#define FILEMANAGER_HIT_NONE   (-1)
#define FILEMANAGER_HIT_PARENT (-2)

struct filemanager_state {
    struct rect window;
    int cwd;
    int selected_node;
};

void filemanager_init_state(struct filemanager_state *fm);
void filemanager_draw_window(struct filemanager_state *fm, int active,
                             int min_hover, int max_hover, int close_hover);
struct rect filemanager_up_button_rect(const struct filemanager_state *fm);
struct rect filemanager_list_rect(const struct filemanager_state *fm);
int filemanager_hit_test_entry(const struct filemanager_state *fm, int x, int y);
int filemanager_open_node(struct filemanager_state *fm, int node);

#endif // FILEMANAGER_H
