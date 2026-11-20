/**
 * @file utf8.c
 * @brief Implementation of UTF-8 decoding, width calculations, and boundary conversions.
 */

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "utf8.h"
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include <stdlib.h>

size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;       // 0xxxxxxx: 1-byte ASCII
    if ((c & 0xE0) == 0xC0) return 2;   // 110xxxxx: 2-byte sequence
    if ((c & 0xF0) == 0xE0) return 3;   // 1110xxxx: 3-byte sequence
    if ((c & 0xF8) == 0xF0) return 4;   // 11110xxx: 4-byte sequence
    return 1; // Fallback for invalid or continuation byte
}

int utf8_visual_width_range(const char *str, size_t byte_start, size_t byte_end) {
    if (!str || byte_start >= byte_end) return 0;

    int total_width = 0;
    size_t i = byte_start;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (i < byte_end) {
        wchar_t wc = 0;
        size_t len = mbrtowc(&wc, str + i, byte_end - i, &state);
        if (len == (size_t)-1 || len == (size_t)-2) {
            // Unparseable byte: treat as 1 column cell
            total_width += 1;
            i += 1;
            memset(&state, 0, sizeof(state));
        } else if (len == 0) {
            // Embedded NUL character
            i += 1;
        } else {
            if (wc == L'\t') {
                // Tab expansion to next 4-space tabstop
                total_width += 4 - (total_width % 4);
            } else {
                int w = wcwidth(wc);
                total_width += (w >= 0 ? w : 1);
            }
            i += len;
        }
    }
    return total_width;
}

int utf8_visual_width(const char *str, size_t byte_len) {
    return utf8_visual_width_range(str, 0, byte_len);
}

int utf8_byte_to_col(const char *str, size_t byte_offset) {
    return utf8_visual_width_range(str, 0, byte_offset);
}

size_t utf8_col_to_byte(const char *str, int target_col) {
    if (!str || target_col <= 0) return 0;

    size_t str_len = strlen(str);
    int current_col = 0;
    size_t i = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (i < str_len) {
        wchar_t wc = 0;
        size_t len = mbrtowc(&wc, str + i, str_len - i, &state);
        if (len == (size_t)-1 || len == (size_t)-2) {
            int char_w = 1;
            if (current_col + char_w > target_col) return i;
            current_col += char_w;
            i += 1;
            memset(&state, 0, sizeof(state));
        } else if (len == 0) {
            i += 1;
        } else {
            int char_w = 1;
            if (wc == L'\t') {
                char_w = 4 - (current_col % 4);
            } else {
                int w = wcwidth(wc);
                char_w = (w >= 0 ? w : 1);
            }

            if (current_col + char_w > target_col) {
                // Target column is inside a wide character; snap to character boundary
                return i;
            }
            current_col += char_w;
            i += len;
            if (current_col == target_col) return i;
        }
    }
    return str_len;
}

size_t utf8_prev_char(const char *str, size_t byte_offset) {
    if (!str || byte_offset == 0) return 0;
    size_t i = byte_offset - 1;
    // Walk back over UTF-8 continuation bytes (10xxxxxx)
    while (i > 0 && (((unsigned char)str[i]) & 0xC0) == 0x80) {
        i--;
    }
    return i;
}

size_t utf8_next_char(const char *str, size_t byte_offset, size_t max_len) {
    if (!str || byte_offset >= max_len) return max_len;
    size_t len = utf8_char_len((unsigned char)str[byte_offset]);
    size_t next = byte_offset + len;
    return (next > max_len) ? max_len : next;
}

size_t utf8_codepoint_count(const char *str, size_t byte_len) {
    if (!str) return 0;
    size_t count = 0;
    size_t i = 0;
    while (i < byte_len) {
        i += utf8_char_len((unsigned char)str[i]);
        count++;
    }
    return count;
}

bool utf8_is_boundary(const char *str, size_t byte_offset, size_t max_len) {
    if (!str || byte_offset > max_len) return false;
    if (byte_offset == 0 || byte_offset == max_len) return true;
    return (((unsigned char)str[byte_offset]) & 0xC0) != 0x80;
}
