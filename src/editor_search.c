/**
 * @file editor_search.c
 * @brief Substring search and replace implementation for the editor.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "editor.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/** @brief Portable case-insensitive substring search (C11 equivalent of strcasestr). */
static char *strcasestr_custom(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    for (; *haystack; ++haystack) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

/** @brief Search forward or backward (case-insensitive, wrap-around) and jump to the match. */
bool editor_search_find_next(Editor *ed, const char *query, bool forward) {
    if (!ed || !query || !*query) return false;

    strncpy(ed->search_query, query, sizeof(ed->search_query) - 1);
    ed->search_active = true;
    size_t qlen = strlen(query);
    size_t total_lines = buffer_line_count(&ed->buffer);

    if (forward) {
        size_t start_row = ed->cursor_row;
        size_t start_col = ed->cursor_col;

        for (size_t step = 0; step <= total_lines; ++step) {
            size_t r = (start_row + step) % total_lines;
            const char *line = buffer_line(&ed->buffer, r);
            size_t line_len = strlen(line);
            const char *search_from = line;
            if (step == 0) {
                if (start_col + 1 < line_len) {
                    search_from = line + start_col + 1;
                } else {
                    continue;
                }
            }
            char *found = strcasestr_custom(search_from, query);
            if (found && (step < total_lines || (size_t)(found - line) <= start_col)) {
                ed->cursor_row = r;
                ed->cursor_col = (size_t)(found - line);
                ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
                ed->search_match_row = r;
                ed->search_match_col = ed->cursor_col;
                ed->search_match_len = qlen;
                editor_set_status(ed, "Search: match at line %zu col %zu", r + 1, ed->cursor_col + 1);
                return true;
            }
        }
    } else {
        size_t start_row = ed->cursor_row;
        size_t start_col = ed->cursor_col;
        size_t r = start_row;
        for (size_t step = 0; step <= total_lines; ++step) {
            const char *line = buffer_line(&ed->buffer, r);
            size_t line_len = strlen(line);
            if (step == 0 && start_col == 0) {
                r = (r == 0) ? total_lines - 1 : r - 1;
                continue;
            }
            size_t limit = (step == 0) ? start_col : line_len;

            char *last_found = NULL;
            const char *curr = line;
            while (curr < line + limit) {
                char *f = strcasestr_custom(curr, query);
                if (f && (size_t)(f - line) < limit) {
                    last_found = f;
                    curr = f + 1;
                } else {
                    break;
                }
            }
            if (last_found) {
                ed->cursor_row = r;
                ed->cursor_col = (size_t)(last_found - line);
                ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
                ed->search_match_row = r;
                ed->search_match_col = ed->cursor_col;
                ed->search_match_len = qlen;
                editor_set_status(ed, "Search: match at line %zu col %zu", r + 1, ed->cursor_col + 1);
                return true;
            }
            r = (r == 0) ? total_lines - 1 : r - 1;
        }
    }

    editor_set_status(ed, "Search: '%s' not found", query);
    return false;
}

/** @brief Search forward (case-insensitive) starting strictly at (from_row, from_col). */
bool editor_search_find_from(Editor *ed, const char *query,
                             size_t from_row, size_t from_col, bool wrap) {
    if (!ed || !query || !*query) return false;

    strncpy(ed->search_query, query, sizeof(ed->search_query) - 1);
    ed->search_query[sizeof(ed->search_query) - 1] = '\0';
    size_t qlen = strlen(query);
    size_t total_lines = buffer_line_count(&ed->buffer);
    if (total_lines == 0) return false;
    if (from_row >= total_lines) {
        if (!wrap) return false;
        from_row = 0;
        from_col = 0;
    }

    size_t max_steps = wrap ? (total_lines + 1) : (total_lines - from_row);
    for (size_t step = 0; step < max_steps; ++step) {
        size_t r = (from_row + step) % total_lines;
        const char *line = buffer_line(&ed->buffer, r);
        size_t line_len = strlen(line);
        const char *search_from = line;
        if (step == 0) {
            if (from_col <= line_len) {
                search_from = line + from_col;
            } else {
                continue;
            }
        }
        char *found = strcasestr_custom(search_from, query);
        if (found && (!wrap || step < total_lines || (size_t)(found - line) < from_col)) {
            ed->search_active = true;
            ed->cursor_row = r;
            ed->cursor_col = (size_t)(found - line);
            ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
            ed->search_match_row = r;
            ed->search_match_col = ed->cursor_col;
            ed->search_match_len = qlen;
            return true;
        }
    }

    ed->search_active = false;
    return false;
}

/** @brief Replace the currently highlighted search match with replacement text. */
bool editor_replace_current_match(Editor *ed, const char *replacement, uint64_t group) {
    if (!ed || !replacement || !ed->search_active || ed->search_match_len == 0) return false;
    if (ed->search_match_row >= buffer_line_count(&ed->buffer)) return false;

    size_t row = ed->search_match_row;
    size_t col = ed->search_match_col;
    size_t mlen = ed->search_match_len;
    size_t line_len = buffer_line_len(&ed->buffer, row);
    if (col + mlen > line_len) return false;

    if (group == 0) {
        group = buffer_next_group(&ed->buffer);
    }

    size_t rlen = strlen(replacement);
    buffer_delete_range(&ed->buffer, row, col, mlen, group);
    if (rlen > 0) {
        buffer_insert_string(&ed->buffer, row, col, replacement, rlen, group);
    }

    ed->cursor_row = row;
    ed->cursor_col = col + rlen;
    ed->target_visual_col = utf8_byte_to_col(buffer_line(&ed->buffer, row), ed->cursor_col);
    ed->mark_active = false;
    ed->rendered_dirty = true;
    return true;
}

/** @brief Replace all case-insensitive occurrences of query with replacement as one undo group. */
size_t editor_replace_all(Editor *ed, const char *query, const char *replacement) {
    if (!ed || !query || !*query || !replacement) return 0;
    editor_touch_key(ed);

    uint64_t group = buffer_next_group(&ed->buffer);
    size_t count = 0;
    size_t row = 0;
    size_t col = 0;
    size_t rlen = strlen(replacement);

    while (editor_search_find_from(ed, query, row, col, false)) {
        size_t mrow = ed->search_match_row;
        size_t mcol = ed->search_match_col;
        if (!editor_replace_current_match(ed, replacement, group)) break;
        count++;
        row = mrow;
        col = mcol + rlen;
    }

    ed->search_active = false;
    if (count > 0) {
        editor_set_status(ed, "Replaced %zu occurrence%s", count, count == 1 ? "" : "s");
    } else {
        editor_set_status(ed, "Search: '%s' not found", query);
    }
    return count;
}

