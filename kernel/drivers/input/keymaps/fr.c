#include <headers/kernel/keymap.h>

extern keymap_t keymap_us;

keymap_t keymap_fr = {
    .name = "fr",
    .map = keymap_us.map,
    .shift_map = keymap_us.shift_map,
};
