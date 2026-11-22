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
 * @brief Automatic line break (hard wrap) column limits and default.
 */
#define MDE_WRAP_COL_OFF     0
#define MDE_WRAP_COL_MIN     20
#define MDE_WRAP_COL_MAX     255
#define MDE_WRAP_COL_DEFAULT 78
#define MDE_LINK_HISTORY_MAX 64

/**
 * @brief Saved location entry for hypertext link navigation history.
 */
typedef struct {
    char *file_path;             /**< Heap-allocated path of previously visited file */
    size_t cursor_row;           /**< Saved 0-indexed row */
    size_t cursor_col;           /**< Saved byte column offset */
} LinkHistoryEntry;

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
    bool insert_mode;            /**< Insert mode active (true, default) vs Overwrite mode (false) */
    int wrap_col;                /**< Auto line break column (20-255, or 0 = off; default 78) */

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

    // Selection / Mark state
    bool mark_active;            /**< Flag indicating if selection mark is active */
    size_t mark_row;             /**< Selection anchor line index */
    size_t mark_col;             /**< Selection anchor byte offset */

    // Clipboard
    char *clipboard;             /**< Heap-allocated text clipboard */
    size_t clipboard_len;        /**< Byte length of clipboard contents */

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

    // Hypertext link navigation history
    LinkHistoryEntry link_history[MDE_LINK_HISTORY_MAX];
    size_t link_history_count;

    bool quit_requested;         /**< Termination flag */
    bool backup_made;            /**< True if ~ backup was created this session */
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

/* =========================================================================
 * Cursor Movement & Position Reporting
 * ========================================================================= */

/** @brief Move cursor one UTF-8 character left (wrapping to previous line end). */
void editor_move_left(Editor *ed);

/** @brief Move cursor one UTF-8 character right (wrapping to next line start). */
void editor_move_right(Editor *ed);

/** @brief Move cursor backward by one word boundary (crossing lines when at col 0). */
void editor_move_word_left(Editor *ed);

/** @brief Move cursor forward by one word boundary (crossing lines when at EOL). */
void editor_move_word_right(Editor *ed);

/** @brief Move cursor up one line, preserving preferred visual column. */
void editor_move_up(Editor *ed);

/** @brief Move cursor down one line, preserving preferred visual column. */
void editor_move_down(Editor *ed);

/** @brief Jump cursor to the beginning (column 0) of the current line. */
void editor_move_home(Editor *ed);

/** @brief Jump cursor to the end of the current line. */
void editor_move_end(Editor *ed);

/** @brief Scroll and move cursor up by one viewport page. */
void editor_move_page_up(Editor *ed);

/** @brief Scroll and move cursor down by one viewport page. */
void editor_move_page_down(Editor *ed);

/** @brief Jump cursor to the first line, column 0 of the document. */
void editor_move_doc_start(Editor *ed);

/** @brief Jump cursor to the end of the last line of the document. */
void editor_move_doc_end(Editor *ed);

/**
 * @brief Move cursor to a 1-based line number and optional 1-based visual column.
 * @param ed Pointer to Editor.
 * @param line_1based 1-indexed target line (clamped to valid range).
 * @param col_1based 1-indexed target visual column (0 or 1 means line start).
 */
void editor_goto_line(Editor *ed, size_t line_1based, size_t col_1based);

/** @brief Post current line/column/percentage/word/char statistics to the status bar. */
void editor_report_position(Editor *ed);

/* =========================================================================
 * Typing & Editing Mutations
 * ========================================================================= */

/**
 * @brief Insert (or overwrite, in Overwrite mode) a UTF-8 character sequence.
 *
 * Replaces any active selection and applies automatic line breaking if the
 * resulting visual line width exceeds ed->wrap_col.
 *
 * @param ed Pointer to Editor.
 * @param utf8_seq UTF-8 byte sequence.
 * @param len Length in bytes.
 */
void editor_insert_char(Editor *ed, const char *utf8_seq, size_t len);

/** @brief Delete the active selection or the UTF-8 character preceding the cursor. */
void editor_backspace(Editor *ed);

/** @brief Delete the active selection or the UTF-8 character under the cursor. */
void editor_delete(Editor *ed);

/** @brief Insert a newline with smart Markdown list/task continuation and auto-indent. */
void editor_smart_enter(Editor *ed);

/** @brief Indent the current line by two leading spaces. */
void editor_indent(Editor *ed);

/** @brief Remove up to two leading spaces from the current line. */
void editor_unindent(Editor *ed);

/* =========================================================================
 * Selection & Mark Operations
 * ========================================================================= */

/** @brief Anchor the selection mark at the current cursor position. */
void editor_set_mark(Editor *ed);

/** @brief Clear any active selection mark. */
void editor_clear_mark(Editor *ed);

/** @brief Toggle the selection mark ON at the cursor, or OFF if already active. */
void editor_toggle_mark(Editor *ed);

/** @brief Return true if a non-empty text selection is currently active. */
bool editor_has_selection(const Editor *ed);

/**
 * @brief Retrieve normalized (start <= end) selection coordinates.
 * @return true if a non-empty selection is active, false otherwise.
 */
bool editor_get_selection(const Editor *ed, size_t *start_row, size_t *start_col,
                          size_t *end_row, size_t *end_col);

/**
 * @brief Allocate and return a copy of the currently selected text.
 * @param ed Pointer to Editor.
 * @param out_len Optional output receiving byte length of returned string.
 * @return Newly allocated null-terminated string (caller must free), or NULL.
 */
char *editor_get_selected_text(const Editor *ed, size_t *out_len);

/**
 * @brief Delete the active text selection as a single undo group.
 * @param ed Pointer to Editor.
 * @param group Undo group ID (0 allocates a new group automatically).
 * @return true if a selection was deleted.
 */
bool editor_delete_selection(Editor *ed, uint64_t group);

/* =========================================================================
 * Clipboard, Cut, Copy, Paste & Killing
 * ========================================================================= */

/** @brief Copy the active selection to the clipboard (synced via OSC 52). */
bool editor_copy(Editor *ed);

/** @brief Copy the entire current line (including newline if not last line) to the clipboard. */
bool editor_copy_line(Editor *ed);

/** @brief Cut the active selection to the clipboard as an atomic undo group. */
bool editor_cut(Editor *ed);

/** @brief Cut the entire current line to the clipboard as an atomic undo group. */
bool editor_cut_line(Editor *ed);

/** @brief Paste the internal clipboard contents at the current cursor position. */
bool editor_paste(Editor *ed);

/** @brief Insert arbitrary (possibly multi-line) UTF-8 text at the cursor as one undo group. */
bool editor_paste_text(Editor *ed, const char *text, size_t len);

/** @brief Kill text from cursor to end of line, or join with next line if at EOL. */
void editor_kill_to_end_of_line(Editor *ed);

/** @brief Kill the word immediately preceding the cursor into the clipboard. */
void editor_kill_prev_word(Editor *ed);

/* =========================================================================
 * Markdown Helpers, Line Wrapping & History
 * ========================================================================= */

/** @brief Toggle between Insert mode (block cursor) and Overwrite mode (underscore cursor). */
void editor_toggle_insert_mode(Editor *ed);

/** @brief Toggle a Markdown task checkbox (`- [ ]` <-> `- [x]`) or create one on the active line. */
void editor_toggle_checkbox(Editor *ed);

/** @brief Insert paired inline formatting markers (e.g. `**`) or wrap the active selection. */
void editor_wrap_format(Editor *ed, const char *marker);

/**
 * @brief Reflow (re-wrap) the contiguous Markdown paragraph around the cursor to wrap_col.
 *
 * Preserves leading indentation and Markdown blockquote (`> `) prefixes, stopping
 * at blank lines, headings, lists, horizontal rules, tables, and fenced code blocks.
 *
 * @param ed Pointer to Editor.
 * @return true if the paragraph was reflowed, false if cursor is not on a reflowable paragraph.
 */
bool editor_reflow_paragraph(Editor *ed);

/**
 * @brief Set automatic line break column width.
 * @param ed Pointer to Editor.
 * @param cols Column limit in [MDE_WRAP_COL_MIN, MDE_WRAP_COL_MAX], or MDE_WRAP_COL_OFF (0).
 * @return true if valid and applied, false otherwise.
 */
bool editor_set_wrap_col(Editor *ed, int cols);

/**
 * @brief Parse a wrap column specification ("20"-"255", "0", "off", "none").
 * @param str Input string.
 * @param out_cols Output parsed column width.
 * @return true on valid input, false otherwise.
 */
bool editor_parse_wrap_col(const char *str, int *out_cols);

/** @brief Undo the most recent atomic editing group and reposition cursor. */
void editor_undo(Editor *ed);

/** @brief Redo the most recently undone atomic editing group and reposition cursor. */
void editor_redo(Editor *ed);

/* =========================================================================
 * File I/O & Hypertext Linking
 * ========================================================================= */

/** @brief Return true if the current buffer has unsaved modifications. */
bool editor_has_unsaved_changes(const Editor *ed);

/** @brief Save the buffer to opt_path (or existing file_path when opt_path is NULL). */
bool editor_save(Editor *ed, const char *opt_path);

/** @brief Load a file from disk into the editor buffer and reset cursor/scroll. */
bool editor_open(Editor *ed, const char *path);

/**
 * @brief Compute a portable relative path from from_file's directory to to_file.
 * @param from_file Source file path (or NULL to use current working directory).
 * @param to_file Destination file path.
 * @param out_rel Destination buffer for the relative path.
 * @param out_cap Capacity of out_rel.
 * @return true on success.
 */
bool editor_make_relative_path(const char *from_file, const char *to_file,
                               char *out_rel, size_t out_cap);

/**
 * @brief Extract the Markdown link target URL and optional `#anchor` under the cursor
 *        (falling back to the first non-image link on the active line).
 */
bool editor_extract_link_at_cursor(const Editor *ed,
                                   char *out_url, size_t url_cap,
                                   char *out_anchor, size_t anchor_cap);

/**
 * @brief Resolve a local relative/absolute Markdown link target to a normalized absolute path.
 * @return true for local targets, false for external URLs or invalid inputs.
 */
bool editor_resolve_link_path(const Editor *ed, const char *link_target,
                              char *out_path, size_t out_cap);

/**
 * @brief Insert a relative Markdown hyperlink `[Label](relative/path)` at the cursor.
 *
 * Uses the active selection as label if present, otherwise derives the label from
 * the target file's first H1 heading or filename stem.
 */
bool editor_insert_file_link(Editor *ed, const char *target_path);

/**
 * @brief Check whether following the link at the cursor will load a different local file.
 *
 * Returns false for missing links, external URLs, pure `#anchor` jumps, or links
 * resolving to the currently open file.
 *
 * @param ed Pointer to Editor.
 * @param out_resolved Optional buffer receiving the resolved target file path.
 * @param cap Capacity of out_resolved.
 * @return true if following the link will load a different file into the editor.
 */
bool editor_link_will_switch_file(const Editor *ed, char *out_resolved, size_t cap);

/**
 * @brief Follow a local Markdown file link or `#heading` anchor under the cursor.
 *
 * Pushes the current location onto the navigation history stack and opens the target
 * file (or initializes a new buffer at that path if the file does not yet exist).
 */
bool editor_follow_link(Editor *ed);

/** @brief Pop the link navigation history stack and restore the previous file and cursor. */
bool editor_navigate_back(Editor *ed);

/* =========================================================================
 * Timing, Search & Viewport Synchronization
 * ========================================================================= */

/** @brief Return current monotonic clock timestamp in milliseconds. */
uint64_t editor_get_time_ms(void);

/** @brief Record the current timestamp as the most recent user keystroke. */
void editor_touch_key(Editor *ed);

/** @brief Return true when the cursor should be rendered (always true for steady cursor). */
bool editor_cursor_is_visible(const Editor *ed);

/**
 * @brief Perform case-insensitive search forward or backward with wrap-around.
 * @param ed Pointer to Editor.
 * @param query Substring to search for.
 * @param forward true to search forward, false to search backward.
 * @return true if a match was found and cursor moved to it.
 */
bool editor_search_find_next(Editor *ed, const char *query, bool forward);

/**
 * @brief Search forward (case-insensitive) starting strictly at (from_row, from_col).
 * @param ed Pointer to Editor.
 * @param query Substring to search for.
 * @param from_row Starting 0-indexed row.
 * @param from_col Starting byte offset within from_row.
 * @param wrap If true, wrap around to the start of the buffer when reaching EOF.
 * @return true if a match was found.
 */
bool editor_search_find_from(Editor *ed, const char *query,
                             size_t from_row, size_t from_col, bool wrap);

/**
 * @brief Replace the currently highlighted search match with replacement text.
 * @param ed Pointer to Editor.
 * @param replacement Replacement UTF-8 string (may be empty to delete match).
 * @param group Undo group ID (0 allocates a new group automatically).
 * @return true if a match was replaced.
 */
bool editor_replace_current_match(Editor *ed, const char *replacement, uint64_t group);

/**
 * @brief Replace all occurrences of query with replacement across the document as one undo group.
 * @param ed Pointer to Editor.
 * @param query Substring to match (case-insensitive).
 * @param replacement Replacement UTF-8 string.
 * @return Total number of occurrences replaced.
 */
size_t editor_replace_all(Editor *ed, const char *query, const char *replacement);

/** @brief Clamp and update vertical, horizontal, and preview scroll offsets for view_rows. */
void editor_update_scroll(Editor *ed, int view_rows);

/** @brief Re-render the cached Markdown preview document when marked dirty. */
void editor_refresh_rendered(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // MDE_EDITOR_H
