/**
 * @file ui.h
 * @brief Terminal user interface, layout manager, and dialogs.
 *
 * Manages ncurses windows, responsive layout calculation for split and
 * in-place views, column ruler rendering, active line gutter markers,
 * status and command bars, and interactive modals (open, save, help).
 */

#ifndef MDE_UI_H
#define MDE_UI_H

#define _XOPEN_SOURCE_EXTENDED 1
#include <ncursesw/curses.h>
#include "editor.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ncurses screen, raw mode, colors, and mouse handling.
 */
void ui_init(void);

/**
 * @brief Restore terminal settings and exit ncurses cleanly.
 */
void ui_shutdown(void);

/**
 * @brief Render the complete screen (editor, ruler, divider, preview, status bar).
 * @param ed Pointer to the Editor instance.
 */
void ui_draw(Editor *ed);

/**
 * @brief Display an interactive text input prompt in the message bar.
 *
 * @param ed Pointer to the Editor instance.
 * @param prompt Prompt label (e.g., "Find", "Save file as").
 * @param out_buf Buffer to store user input.
 * @param buf_size Maximum capacity of out_buf.
 * @return true if confirmed with Enter, false if cancelled with Escape.
 */
bool ui_prompt(Editor *ed, const char *prompt, char *out_buf, size_t buf_size);

/**
 * @brief Confirm quitting when unsaved changes exist in the buffer.
 *
 * @param ed Pointer to the Editor instance.
 * @return 1 for Save & Quit, 0 for Discard & Quit, -1 for Cancel.
 */
int ui_confirm_quit(Editor *ed);

/**
 * @brief Display a centered popup dialog with two overlay tabs:
 *        Tab 1: Editor cheatsheet (keys, shortcuts, navigation).
 *        Tab 2: Markdown syntax guide (all Markdown constructs and tips).
 * @param ed Pointer to the Editor instance.
 */
void ui_show_help(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // MDE_UI_H
