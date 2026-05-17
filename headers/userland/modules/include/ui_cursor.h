#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include <include/userland_api.h>
#include <userland/modules/include/utils.h>

/* Optimized cursor with background preservation */

/* Initialize cursor system */
void cursor_init(void);

/* Draw cursor at (x, y) */
void cursor_draw(int x, int y);

/* Move cursor to (x, y) with preserved background */
void cursor_move(int x, int y);

/* Get the cursor bounding box for composition/dirty tracking. */
void cursor_get_bounds(int x, int y, struct rect *out);

#endif
