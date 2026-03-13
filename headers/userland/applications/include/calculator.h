#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <userland/modules/include/utils.h>

#define CALCULATOR_BUTTON_COUNT 16

struct calculator_state {
    struct rect window;
    char display[24];
    int accumulator;
    char pending_op;
    int reset_input;
    int error;
};

void calculator_init_state(struct calculator_state *calc);
void calculator_press_key(struct calculator_state *calc, char key);
void calculator_backspace(struct calculator_state *calc);
void calculator_draw_window(struct calculator_state *calc, int active,
                            int min_hover, int max_hover, int close_hover);
struct rect calculator_button_rect(const struct calculator_state *calc, int index);
int calculator_hit_test(const struct calculator_state *calc, int x, int y);
char calculator_button_key(int index);

#endif // CALCULATOR_H
