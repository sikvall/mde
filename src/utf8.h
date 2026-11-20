/**
 * @file utf8.h
 * @brief UTF-8 multibyte sequence decoding, boundary validation, and visual widths.
 *
 * Provides terminal column calculations using `wcwidth`, handling ASCII,
 * multibyte Latin/European, CJK wide characters, and emojis cleanly without
 * splitting Unicode code points.
 */

#ifndef MDE_UTF8_H
#define MDE_UTF8_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine the byte length of a UTF-8 character from its leading byte.
 * @param c The leading unsigned byte.
 * @return Byte count (1 to 4).
 */
size_t utf8_char_len(unsigned char c);

/**
 * @brief Compute the total visual display width of a string in terminal cells.
 * @param str The UTF-8 string pointer.
 * @param byte_len Length of string in bytes.
 * @return Total screen cells required (taking wcwidth and tabs into account).
 */
int utf8_visual_width(const char *str, size_t byte_len);

/**
 * @brief Compute the visual display width of a substring range [byte_start, byte_end).
 * @param str The UTF-8 string pointer.
 * @param byte_start Start offset in bytes.
 * @param byte_end End offset in bytes.
 * @return Visual columns occupied by the specified byte range.
 */
int utf8_visual_width_range(const char *str, size_t byte_start, size_t byte_end);

/**
 * @brief Convert a byte offset in a UTF-8 string to a 0-indexed visual screen column.
 * @param str The UTF-8 string.
 * @param byte_offset Offset in bytes.
 * @return 0-indexed visual screen column.
 */
int utf8_byte_to_col(const char *str, size_t byte_offset);

/**
 * @brief Convert a 0-indexed visual screen column to the closest valid byte offset.
 * @param str The UTF-8 string.
 * @param target_col Target visual column.
 * @return Byte offset within str.
 */
size_t utf8_col_to_byte(const char *str, int target_col);

/**
 * @brief Move a byte offset backward to the preceding UTF-8 character boundary.
 * @param str The UTF-8 string.
 * @param byte_offset Current byte offset.
 * @return New byte offset at previous character boundary.
 */
size_t utf8_prev_char(const char *str, size_t byte_offset);

/**
 * @brief Move a byte offset forward to the next UTF-8 character boundary.
 * @param str The UTF-8 string.
 * @param byte_offset Current byte offset.
 * @param max_len Total string byte length.
 * @return New byte offset at next character boundary.
 */
size_t utf8_next_char(const char *str, size_t byte_offset, size_t max_len);

/**
 * @brief Count the number of Unicode codepoints in a UTF-8 string.
 * @param str The UTF-8 string.
 * @param byte_len Length in bytes.
 * @return Number of codepoints.
 */
size_t utf8_codepoint_count(const char *str, size_t byte_len);

/**
 * @brief Test whether a given byte offset aligns with a UTF-8 character boundary.
 * @param str The UTF-8 string.
 * @param byte_offset Offset to test.
 * @param max_len Total string byte length.
 * @return true if boundary is valid, false otherwise.
 */
bool utf8_is_boundary(const char *str, size_t byte_offset, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // MDE_UTF8_H
