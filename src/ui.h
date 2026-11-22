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

#ifndef CTRL_KEY
#define CTRL_KEY(k) ((k) & 0x1f)
#endif

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
 * @brief Display an interactive text input prompt in the message bar (requires non-empty input).
 *
 * @param ed Pointer to the Editor instance.
 * @param prompt Prompt label (e.g., "Find", "Save file as").
 * @param out_buf Buffer to store user input.
 * @param buf_size Maximum capacity of out_buf.
 * @return true if confirmed with Enter (non-empty), false if cancelled with Escape/Ctrl+C.
 */
bool ui_prompt(Editor *ed, const char *prompt, char *out_buf, size_t buf_size);

/**
 * @brief Display an interactive text input prompt allowing an empty response on Enter.
 *
 * @param ed Pointer to the Editor instance.
 * @param prompt Prompt label (e.g., "Replace with").
 * @param out_buf Buffer to store user input.
 * @param buf_size Maximum capacity of out_buf.
 * @return true if confirmed with Enter, false if cancelled with Escape/Ctrl+C.
 */
bool ui_prompt_allow_empty(Editor *ed, const char *prompt, char *out_buf, size_t buf_size);

/**
 * @brief Run Nano-compatible interactive Search & Replace (`Ctrl+\` / `Alt+R`).
 *
 * Prompts for search and replacement strings, highlights matches sequentially,
 * and lets the user press Y (replace), N (skip), A (replace all), or Esc/^C (cancel).
 *
 * @param ed Pointer to the Editor instance.
 * @param search_buf Shared search query buffer updated on confirmation.
 * @param search_cap Capacity of search_buf.
 */
void ui_search_and_replace(Editor *ed, char *search_buf, size_t search_cap);

/**
 * @brief Display a scrollable Document Outline modal listing all Markdown headings (H1-H6).
 *
 * Pressing Enter jumps the cursor directly to the selected heading.
 *
 * @param ed Pointer to the Editor instance.
 * @return true if the user jumped to a heading, false if cancelled or no headings exist.
 */
bool ui_show_outline(Editor *ed);

/**
 * @brief Document and selection metrics container.
 */
typedef struct {
    size_t total_lines;           /**< Total number of lines in buffer */
    size_t total_words;           /**< Total words in buffer */
    size_t total_chars;           /**< Total bytes/characters including newlines */
    size_t total_chars_no_spaces; /**< Characters excluding whitespace */
    bool has_selection;           /**< True if an active selection exists */
    size_t sel_lines;             /**< Number of lines spanned by selection */
    size_t sel_words;             /**< Words inside selection */
    size_t sel_chars;             /**< Total characters in selection */
    size_t sel_chars_no_spaces;   /**< Non-whitespace characters in selection */
    int read_time_min;            /**< Estimated reading minutes (200 wpm) */
    int read_time_sec;            /**< Estimated reading seconds remainder */
} DocumentStats;

/**
 * @brief Compute document and selection metrics.
 */
void compute_document_stats(const Editor *ed, DocumentStats *stats);

/**
 * @brief Display an interactive modal popover showing document and selection statistics.
 *
 * @param ed Pointer to the Editor instance.
 * @return true if displayed successfully.
 */
bool ui_show_document_stats(Editor *ed);

/**
 * @brief Prompt to save or discard unsaved changes before an action (quit or switch file).
 *
 * @param ed Pointer to the Editor instance.
 * @param action_desc Short verb phrase (e.g., "exit", "switch file").
 * @return 1 for Save & Continue, 0 for Discard & Continue, -1 for Cancel.
 */
int ui_confirm_unsaved(Editor *ed, const char *action_desc);

/**
 * @brief Confirm quitting when unsaved changes exist in the buffer.
 *
 * @param ed Pointer to the Editor instance.
 * @return 1 for Save & Quit, 0 for Discard & Quit, -1 for Cancel.
 */
int ui_confirm_quit(Editor *ed);

/**
 * @brief Check whether a filesystem path already exists and whether it is a directory.
 *
 * @param path Path to inspect.
 * @param out_is_dir Optional output set to true if the existing path is a directory.
 * @return true if the path exists on disk, false otherwise.
 */
bool ui_path_exists(const char *path, bool *out_is_dir);

/**
 * @brief Prompt the user to confirm overwriting an existing file before saving.
 *
 * @param ed Pointer to the Editor instance.
 * @param path Destination path that already exists.
 * @return true if the user explicitly confirms overwrite (y/Y), false otherwise.
 */
bool ui_confirm_overwrite(Editor *ed, const char *path);

/**
 * @brief File dialog operation mode.
 */
typedef enum {
    FILE_DIALOG_OPEN,    /**< Select an existing file to open */
    FILE_DIALOG_SAVE_AS, /**< Choose directory and filename to save buffer */
    FILE_DIALOG_LINK     /**< Choose or type a file path to insert as a Markdown hyperlink */
} FileDialogMode;

/**
 * @brief Display an interactive modal file browser dialogue for navigating files and directories.
 *
 * @param ed Pointer to Editor.
 * @param mode FILE_DIALOG_OPEN or FILE_DIALOG_SAVE_AS.
 * @param out_path Destination buffer for selected absolute/relative file path.
 * @param max_len Size of out_path buffer.
 * @return true if a file was selected/confirmed, false if cancelled (Esc/q).
 */
bool ui_file_dialog(Editor *ed, FileDialogMode mode, char *out_path, size_t max_len);

/**
 * @brief Determine number of shortcut hint rows to render at the bottom of the screen.
 *
 * @param screen_rows Screen height in rows.
 * @return 2 on standard displays (e.g. 24 rows), 3 on taller displays (>= 28 rows), 1 on minimal screens.
 */
int ui_get_shortcut_hint_rows(int screen_rows);

/**
 * @brief Display a centered popup dialog with two overlay tabs:
 *        Tab 1: Editor cheatsheet (keys, shortcuts, navigation).
 *        Tab 2: Markdown syntax guide (all Markdown constructs and tips).
 * @param ed Pointer to the Editor instance.
 */
void ui_show_help(Editor *ed);

/**
 * @brief Display an interactive Export Menu modal for exporting Markdown to
 *        HTML, XML, EPUB, or LaTeX (using pandoc when appropriate).
 *
 * @param ed Pointer to the Editor instance.
 * @return true if a document was exported, false if cancelled.
 */
bool ui_export_menu(Editor *ed);

/**
 * @brief Display an interactive modal dialog for viewing and editing document & EPUB metadata.
 *
 * @param ed Pointer to Editor instance.
 * @return true if metadata was updated, false if cancelled.
 */
bool ui_dialog_metadata(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // MDE_UI_H
