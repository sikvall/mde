/**
 * @file ui_internal.h
 * @brief Internal declarations and helpers shared across UI and dialog modules.
 */

#ifndef MDE_UI_INTERNAL_H
#define MDE_UI_INTERNAL_H

#define _XOPEN_SOURCE_EXTENDED 1
#include <ncursesw/curses.h>
#include "ui.h"
#include "theme.h"
#include "utf8.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draw a full-width horizontal box divider with left/right T-junctions.
 */
void draw_box_hrule(WINDOW *win, int y, int width);
void calc_file_dialog_geom(int rows, int cols,
                           int *out_h, int *out_w, int *out_y, int *out_x);
size_t read_utf8_input(WINDOW *win, int first_ch, char *out_seq, size_t cap);

/**
 * @brief Update terminal cursor shape (block for insert, underline for overwrite).
 */
void update_cursor_shape(bool insert_mode);

#ifdef __cplusplus
}
#endif

#endif // MDE_UI_INTERNAL_H
