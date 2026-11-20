/**
 * @file editor.h
 * @brief High-level editor state, cursor navigation, viewport math, and Markdown helpers.
 *
 * Coordinates text buffer interactions with view modes (Split, In-Place, Preview),
 * viewport scroll offsets, smart Markdown list auto-continuation, checkbox
 * toggling, format wrapping, and file I/O.
 */

#ifndef MDE_EDITOR_H
#define MDE_EDITOR_H

#include "buffer.h"
#include "renderer.h"
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Operating view modes.
 */
typedef enum {
    VIEW_SPLIT = 0,   /**< Dual-pane: Editor left, Preview right */
    VIEW_INPLACE,     /**< Fullscreen editor with rich in-place syntax highlighting */
    VIEW_PREVIEW      /**< Fullscreen rendered markdown reader */
} ViewMode;

/**
 * @brief Master editor instance state.
 */
typedef struct {
    Buffer buffer;               /**< Underlying document buffer */
    RenderedDoc rendered_doc;    /**< Cached rendered preview document */
    bool rendered_dirty;         /**< Dirty flag indicating preview needs refresh */

    ViewMode view_mode;          /**< Current active view mode */
    bool insert_mode;            /**< Insert mode active (true) vs Overwrite mode (false) */

    // Cursor position in buffer
    size_t cursor_row;           /**< 0-indexed line index */
    size_t cursor_col;           /**< Byte offset within current line */
    int target_visual_col;       /**< Preferred visual screen column for up/down navigation */

    // Viewport scrolling
    size_t scroll_row;           /**< First visible line in editor pane */
    size_t scroll_col;           /**< Horizontal scroll offset in visual columns */
    size_t preview_scroll_row;   /**< First visible line in preview pane */

    // Window dimensions
    int screen_rows;             /**< Total terminal screen rows */
    int screen_cols;             /**< Total terminal screen columns */
    int editor_width;            /**< Width of editor pane in columns */
    int preview_width;           /**< Width of preview pane in columns */

    // Search state
    char search_query[128];      /**< Current search query string */
    bool search_active;          /**< Flag indicating search matches are active */
    size_t search_match_row;     /**< Row of currently highlighted match */
    size_t search_match_col;     /**< Column byte offset of match */
    size_t search_match_len;     /**< Length of match in bytes */

    // Status message
    char status_msg[128];        /**< Ephemeral notification message */
    time_t status_time;          /**< Timestamp when message was posted */

    // Keystroke & cursor blink state
    uint64_t last_key_time;      /**< Monotonic timestamp in ms of the last keypress */

    bool quit_requested;         /**< Termination flag */
} Editor;

/**
 * @brief Initialize an editor instance with optional file to open.
 * @param ed Pointer to Editor structure.
 * @param filename File path to open, or NULL for an untitled buffer.
 */
void editor_init(Editor *ed, const char *filename);

/**
 * @brief Free all resources associated with an editor instance.
 * @param ed Pointer to Editor structure.
 */
void editor_free(Editor *ed);

/**
 * @brief Set a formatted ephemeral notification message in the status bar.
 * @param ed Pointer to Editor structure.
 * @param fmt Printf-style format string.
 */
void editor_set_status(Editor *ed, const char *fmt, ...);

/**
 * @brief Cycle active view mode: Split -> In-Place -> Preview -> Split.
 * @param ed Pointer to Editor structure.
 */
void editor_cycle_view_mode(Editor *ed);

// Cursor movement operations
void editor_move_left(Editor *ed);
void editor_move_right(Editor *ed);
void editor_move_up(Editor *ed);
void editor_move_down(Editor *ed);
void editor_move_home(Editor *ed);
void editor_move_end(Editor *ed);
void editor_move_page_up(Editor *ed);
void editor_move_page_down(Editor *ed);

// Typing & Editing mutations
void editor_insert_char(Editor *ed, const char *utf8_seq, size_t len);
void editor_backspace(Editor *ed);
void editor_delete(Editor *ed);
void editor_smart_enter(Editor *ed);
void editor_indent(Editor *ed);
void editor_unindent(Editor *ed);

// Markdown helpers
void editor_toggle_insert_mode(Editor *ed);
void editor_toggle_checkbox(Editor *ed);
void editor_wrap_format(Editor *ed, const char *marker);

// History operations
void editor_undo(Editor *ed);
void editor_redo(Editor *ed);

// File I/O
bool editor_save(Editor *ed, const char *opt_path);
bool editor_open(Editor *ed, const char *path);

// Cursor blink & timing
uint64_t editor_get_time_ms(void);
void editor_touch_key(Editor *ed);
bool editor_cursor_is_visible(const Editor *ed);

// Search operations
bool editor_search_find_next(Editor *ed, const char *query, bool forward);

// Viewport layout synchronization
void editor_update_scroll(Editor *ed, int view_rows);
void editor_refresh_rendered(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // MDE_EDITOR_H
