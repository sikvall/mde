/**
 * @file buffer.h
 * @brief Line-based dynamic text buffer with UTF-8 awareness and undo/redo support.
 *
 * Implements an efficient array of heap-allocated dynamic lines, supporting
 * arbitrary text insertion, deletion, line splitting, joining, backspacing,
 * file loading, and saving.
 */

#ifndef MDE_BUFFER_H
#define MDE_BUFFER_H

#include "undo.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single line of text stored with its byte length and allocated capacity.
 */
typedef struct {
    char *chars;         /**< Null-terminated character string */
    size_t len;          /**< Length in bytes (excluding null terminator) */
    size_t cap;          /**< Allocated buffer capacity in bytes */
} Line;

/**
 * @brief The complete document text buffer.
 */
typedef struct {
    Line *lines;                 /**< Dynamic array of lines */
    size_t count;                /**< Current line count (at least 1) */
    size_t cap;                  /**< Capacity of lines array */
    char *file_path;             /**< Absolute or relative path to file */
    bool is_dirty;               /**< Unsaved changes flag */
    UndoStack undo_stack;        /**< Undo history */
    UndoStack redo_stack;        /**< Redo history */
    uint64_t current_group_id;   /**< Current batch keystroke group ID */
    size_t saved_undo_depth;     /**< Undo stack depth at last load/save */
    bool saved_depth_valid;      /**< False if undo history branched past saved state */
    bool has_crlf_endings;       /**< True if source file uses CR+LF line endings */
} Buffer;

/**
 * @brief Initialize a buffer with one empty line.
 * @param buf Pointer to Buffer structure.
 */
void buffer_init(Buffer *buf);

/**
 * @brief Free all allocated lines, strings, and undo/redo histories.
 * @param buf Pointer to Buffer structure.
 */
void buffer_free(Buffer *buf);

/**
 * @brief Load file contents from disk into the buffer.
 *
 * Strips carriage returns (\r\n -> \n), cleans undo history, and clears dirty flag.
 *
 * @param buf Pointer to Buffer structure.
 * @param file_path Path to file on disk.
 * @return true on success, false on I/O failure.
 */
bool buffer_load(Buffer *buf, const char *file_path);

/**
 * @brief Save buffer contents to disk.
 *
 * @param buf Pointer to Buffer structure.
 * @param file_path Destination file path (or NULL to use existing buf->file_path).
 * @return true on success, false on write error.
 */
bool buffer_save(Buffer *buf, const char *file_path);

/**
 * @brief Access line content by 0-indexed row number.
 * @param buf Pointer to Buffer.
 * @param row 0-indexed row number.
 * @return Const pointer to null-terminated line string (empty string if out of range).
 */
const char *buffer_line(const Buffer *buf, size_t row);

/**
 * @brief Retrieve the byte length of a line.
 * @param buf Pointer to Buffer.
 * @param row 0-indexed row number.
 * @return Byte count of line.
 */
size_t buffer_line_len(const Buffer *buf, size_t row);

/**
 * @brief Get total number of lines in the buffer.
 * @param buf Pointer to Buffer.
 * @return Line count (always >= 1).
 */
size_t buffer_line_count(const Buffer *buf);

/**
 * @brief Calculate total Unicode character count across all lines.
 * @param buf Pointer to Buffer.
 * @return Total codepoint count.
 */
size_t buffer_char_count(const Buffer *buf);

/**
 * @brief Calculate total word count across all lines.
 * @param buf Pointer to Buffer.
 * @return Total word count.
 */
size_t buffer_word_count(const Buffer *buf);

/**
 * @brief Insert a UTF-8 string at a given (row, col) coordinate.
 *
 * @param buf Pointer to Buffer.
 * @param row Line index.
 * @param col Byte offset within line.
 * @param str String to insert.
 * @param len Byte length of string.
 * @param group Batch undo group ID.
 * @return true on success.
 */
bool buffer_insert_string(Buffer *buf, size_t row, size_t col,
                          const char *str, size_t len, uint64_t group);

/**
 * @brief Delete a range of bytes on a line starting at col.
 *
 * @param buf Pointer to Buffer.
 * @param row Line index.
 * @param col Byte offset within line.
 * @param byte_len Number of bytes to remove.
 * @param group Batch undo group ID.
 * @return true on success.
 */
bool buffer_delete_range(Buffer *buf, size_t row, size_t col,
                         size_t byte_len, uint64_t group);

/**
 * @brief Split a line into two lines at col (simulates Enter key).
 *
 * @param buf Pointer to Buffer.
 * @param row Line index.
 * @param col Split position.
 * @param group Batch undo group ID.
 * @return true on success.
 */
bool buffer_split_line(Buffer *buf, size_t row, size_t col, uint64_t group);

/**
 * @brief Merge line row with line row + 1.
 *
 * @param buf Pointer to Buffer.
 * @param row Index of the first line.
 * @param group Batch undo group ID.
 * @return true on success.
 */
bool buffer_join_lines(Buffer *buf, size_t row, uint64_t group);

/**
 * @brief Replace an entire line's content with a new string.
 *
 * @param buf Pointer to Buffer.
 * @param row Line index.
 * @param new_str New string content.
 * @param new_len Length of new string in bytes.
 * @param group Batch undo group ID.
 * @return true on success.
 */
bool buffer_replace_line(Buffer *buf, size_t row, const char *new_str,
                         size_t new_len, uint64_t group);

/**
 * @brief Delete the character preceding cursor position (simulates Backspace).
 *
 * Handles multi-byte UTF-8 character deletion and line joining if col == 0.
 *
 * @param buf Pointer to Buffer.
 * @param row In/out row coordinate.
 * @param col In/out byte offset coordinate.
 * @param group Batch undo group ID.
 * @return true if a character or line boundary was deleted.
 */
bool buffer_backspace(Buffer *buf, size_t *row, size_t *col, uint64_t group);

/**
 * @brief Delete the character at cursor position (simulates Delete key).
 *
 * @param buf Pointer to Buffer.
 * @param row Row coordinate.
 * @param col Byte offset coordinate.
 * @param group Batch undo group ID.
 * @return true if a character or line boundary was deleted.
 */
bool buffer_delete(Buffer *buf, size_t row, size_t col, uint64_t group);

/**
 * @brief Revert the most recent change group from the undo stack.
 *
 * @param buf Pointer to Buffer.
 * @param out_row Output cursor row position after undo.
 * @param out_col Output cursor column byte offset after undo.
 * @return true if an action was undone, false if undo stack was empty.
 */
bool buffer_undo(Buffer *buf, size_t *out_row, size_t *out_col);

/**
 * @brief Re-apply the most recently undone change group from the redo stack.
 *
 * @param buf Pointer to Buffer.
 * @param out_row Output cursor row position after redo.
 * @param out_col Output cursor column byte offset after redo.
 * @return true if an action was redone, false if redo stack was empty.
 */
bool buffer_redo(Buffer *buf, size_t *out_row, size_t *out_col);

/**
 * @brief Allocate a new unique batch group ID for grouping related mutations.
 * @param buf Pointer to Buffer.
 * @return New 64-bit group ID.
 */
uint64_t buffer_next_group(Buffer *buf);

#ifdef __cplusplus
}
#endif

#endif // MDE_BUFFER_H
