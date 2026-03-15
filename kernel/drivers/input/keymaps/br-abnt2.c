#include <headers/kernel/keymap.h>

extern keymap_t keymap_pt_br;

keymap_t keymap_br_abnt2 = {
    .name = "br-abnt2",
    .map = keymap_pt_br.map,
    .shift_map = keymap_pt_br.shift_map,
};
