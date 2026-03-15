#include <headers/kernel/keymap.h>

extern keymap_t keymap_us;

keymap_t keymap_us_intl = {
    .name = "us-intl",
    .map = keymap_us.map,
    .shift_map = keymap_us.shift_map,
};
