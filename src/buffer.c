/**
 * @file buffer.c
 * @brief Implementation of line-based text buffer with UTF-8 editing and undo history.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "buffer.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char EMPTY_STR[] = "";

/**
 * @brief Initialize an individual Line structure with small default capacity.
 */
static bool line_init(Line *line) {
    line->cap = 16;
    line->chars = (char*)malloc(line->cap);
    if (!line->chars) return false;
    line->chars[0] = '\0';
    line->len = 0;
    return true;
}

/**
 * @brief Free heap memory used by an individual line's string.
 */
static void line_free(Line *line) {
    if (line) {
        free(line->chars);
        line->chars = NULL;
        line->len = 0;
        line->cap = 0;
    }
}

/**
 * @brief Ensure line buffer has enough room for needed bytes plus null terminator.
 */
static bool line_reserve(Line *line, size_t needed) {
    if (needed + 1 <= line->cap) return true;
    size_t new_cap = line->cap == 0 ? 16 : line->cap * 2;
    while (new_cap < needed + 1) {
        new_cap *= 2;
    }
    char *new_chars = (char*)realloc(line->chars, new_cap);
    if (!new_chars) return false;
    line->chars = new_chars;
    line->cap = new_cap;
    return true;
}

void buffer_init(Buffer *buf) {
    if (!buf) return;
    buf->lines = (Line*)malloc(sizeof(Line));
    line_init(&buf->lines[0]);
    buf->count = 1;
    buf->cap = 1;
    buf->file_path = NULL;
    buf->is_dirty = false;
    undo_stack_init(&buf->undo_stack);
    undo_stack_init(&buf->redo_stack);
    buf->current_group_id = 1;
}

void buffer_free(Buffer *buf) {
    if (!buf) return;
    for (size_t i = 0; i < buf->count; ++i) {
        line_free(&buf->lines[i]);
    }
    free(buf->lines);
    buf->lines = NULL;
    buf->count = 0;
    buf->cap = 0;

    free(buf->file_path);
    buf->file_path = NULL;

    undo_stack_free(&buf->undo_stack);
    undo_stack_free(&buf->redo_stack);
}

const char *buffer_line(const Buffer *buf, size_t row) {
    if (!buf || row >= buf->count || !buf->lines[row].chars) {
        return EMPTY_STR;
    }
    return buf->lines[row].chars;
}

size_t buffer_line_len(const Buffer *buf, size_t row) {
    if (!buf || row >= buf->count) return 0;
    return buf->lines[row].len;
}

size_t buffer_line_count(const Buffer *buf) {
    return buf ? buf->count : 0;
}

/**
 * @brief Expand lines array capacity if needed.
 */
static bool buffer_ensure_cap(Buffer *buf, size_t needed) {
    if (needed <= buf->cap) return true;
    size_t new_cap = buf->cap == 0 ? 16 : buf->cap * 2;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    Line *new_lines = (Line*)realloc(buf->lines, new_cap * sizeof(Line));
    if (!new_lines) return false;
    buf->lines = new_lines;
    buf->cap = new_cap;
    return true;
}

bool buffer_load(Buffer *buf, const char *file_path) {
    if (!buf || !file_path) return false;

    FILE *f = fopen(file_path, "rb");
    if (!f) return false;

    // Clear existing buffer lines
    for (size_t i = 0; i < buf->count; ++i) {
        line_free(&buf->lines[i]);
    }
    buf->count = 0;

    char *line_buf = NULL;
    size_t line_cap = 0;
    ssize_t read_bytes;

    while ((read_bytes = getline(&line_buf, &line_cap, f)) != -1) {
        // Strip trailing \r and \n characters
        while (read_bytes > 0 && (line_buf[read_bytes - 1] == '\n' || line_buf[read_bytes - 1] == '\r')) {
            line_buf[--read_bytes] = '\0';
        }

        if (!buffer_ensure_cap(buf, buf->count + 1)) {
            fclose(f);
            free(line_buf);
            return false;
        }

        Line *l = &buf->lines[buf->count];
        l->cap = (size_t)read_bytes + 1;
        l->chars = (char*)malloc(l->cap);
        memcpy(l->chars, line_buf, (size_t)read_bytes);
        l->chars[read_bytes] = '\0';
        l->len = (size_t)read_bytes;
        buf->count++;
    }

    free(line_buf);
    fclose(f);

    // Ensure at least 1 empty line
    if (buf->count == 0) {
        buffer_ensure_cap(buf, 1);
        line_init(&buf->lines[0]);
        buf->count = 1;
    }

    free(buf->file_path);
    buf->file_path = strdup(file_path);
    buf->is_dirty = false;
    undo_stack_clear(&buf->undo_stack);
    undo_stack_clear(&buf->redo_stack);
    return true;
}

bool buffer_save(Buffer *buf, const char *file_path) {
    if (!buf) return false;
    const char *path = file_path ? file_path : buf->file_path;
    if (!path) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    for (size_t i = 0; i < buf->count; ++i) {
        const char *txt = buffer_line(buf, i);
        size_t len = buffer_line_len(buf, i);
        if (len > 0) {
            fwrite(txt, 1, len, f);
        }
        if (i + 1 < buf->count || len > 0) {
            fputc('\n', f);
        }
    }

    fclose(f);

    if (file_path && (!buf->file_path || strcmp(buf->file_path, file_path) != 0)) {
        free(buf->file_path);
        buf->file_path = strdup(file_path);
    }
    buf->is_dirty = false;
    return true;
}

uint64_t buffer_next_group(Buffer *buf) {
    if (!buf) return 1;
    return ++buf->current_group_id;
}

bool buffer_insert_string(Buffer *buf, size_t row, size_t col,
                          const char *str, size_t len, uint64_t group) {
    if (!buf || !str || len == 0) return false;
    if (row >= buf->count) row = buf->count - 1;

    Line *l = &buf->lines[row];
    if (col > l->len) col = l->len;

    if (!line_reserve(l, l->len + len)) return false;

    memmove(l->chars + col + len, l->chars + col, l->len - col);
    memcpy(l->chars + col, str, len);
    l->len += len;
    l->chars[l->len] = '\0';

    undo_stack_push(&buf->undo_stack, ACTION_INSERT_TEXT, row, col, str, len, group);
    undo_stack_clear(&buf->redo_stack);
    buf->is_dirty = true;
    return true;
}

bool buffer_delete_range(Buffer *buf, size_t row, size_t col,
                         size_t byte_len, uint64_t group) {
    if (!buf || row >= buf->count || byte_len == 0) return false;

    Line *l = &buf->lines[row];
    if (col >= l->len) return false;

    if (col + byte_len > l->len) {
        byte_len = l->len - col;
    }

    undo_stack_push(&buf->undo_stack, ACTION_DELETE_TEXT, row, col, l->chars + col, byte_len, group);
    undo_stack_clear(&buf->redo_stack);

    memmove(l->chars + col, l->chars + col + byte_len, l->len - (col + byte_len));
    l->len -= byte_len;
    l->chars[l->len] = '\0';

    buf->is_dirty = true;
    return true;
}

bool buffer_split_line(Buffer *buf, size_t row, size_t col, uint64_t group) {
    if (!buf) return false;
    if (row >= buf->count) row = buf->count - 1;

    Line *cur = &buf->lines[row];
    if (col > cur->len) col = cur->len;

    size_t remainder_len = cur->len - col;
    char *remainder = NULL;
    if (remainder_len > 0) {
        remainder = (char*)malloc(remainder_len + 1);
        if (!remainder) return false;
        memcpy(remainder, cur->chars + col, remainder_len);
        remainder[remainder_len] = '\0';
    }

    cur->len = col;
    cur->chars[col] = '\0';

    if (!buffer_ensure_cap(buf, buf->count + 1)) {
        free(remainder);
        return false;
    }

    size_t lines_to_shift = buf->count - (row + 1);
    if (lines_to_shift > 0) {
        memmove(&buf->lines[row + 2], &buf->lines[row + 1], lines_to_shift * sizeof(Line));
    }
    buf->count++;

    Line *next_line = &buf->lines[row + 1];
    next_line->cap = remainder_len + 16;
    next_line->chars = (char*)malloc(next_line->cap);
    if (!next_line->chars) {
        free(remainder);
        return false;
    }

    if (remainder && remainder_len > 0) {
        memcpy(next_line->chars, remainder, remainder_len);
    }
    next_line->chars[remainder_len] = '\0';
    next_line->len = remainder_len;
    free(remainder);

    undo_stack_push(&buf->undo_stack, ACTION_SPLIT_LINE, row, col, NULL, 0, group);
    undo_stack_clear(&buf->redo_stack);
    buf->is_dirty = true;
    return true;
}

bool buffer_join_lines(Buffer *buf, size_t row, uint64_t group) {
    if (!buf || row + 1 >= buf->count) return false;

    Line *cur = &buf->lines[row];
    Line *next = &buf->lines[row + 1];
    size_t split_col = cur->len;

    if (!line_reserve(cur, cur->len + next->len)) return false;

    memcpy(cur->chars + cur->len, next->chars, next->len);
    cur->len += next->len;
    cur->chars[cur->len] = '\0';

    line_free(next);
    size_t lines_to_shift = buf->count - (row + 2);
    if (lines_to_shift > 0) {
        memmove(&buf->lines[row + 1], &buf->lines[row + 2], lines_to_shift * sizeof(Line));
    }
    buf->count--;

    undo_stack_push(&buf->undo_stack, ACTION_JOIN_LINE, row, split_col, NULL, 0, group);
    undo_stack_clear(&buf->redo_stack);
    buf->is_dirty = true;
    return true;
}

bool buffer_replace_line(Buffer *buf, size_t row, const char *new_str,
                         size_t new_len, uint64_t group) {
    if (!buf || row >= buf->count || !new_str) return false;
    Line *l = &buf->lines[row];
    if (l->len == new_len && memcmp(l->chars, new_str, new_len) == 0) {
        return true;
    }
    buffer_delete_range(buf, row, 0, l->len, group);
    if (new_len > 0) {
        buffer_insert_string(buf, row, 0, new_str, new_len, group);
    }
    return true;
}

bool buffer_backspace(Buffer *buf, size_t *row, size_t *col, uint64_t group) {
    if (!buf || !row || !col || buf->count == 0) return false;
    if (*row >= buf->count) *row = buf->count - 1;

    Line *l = &buf->lines[*row];
    if (*col > 0) {
        size_t prev = utf8_prev_char(l->chars, *col);
        size_t byte_len = *col - prev;
        buffer_delete_range(buf, *row, prev, byte_len, group);
        *col = prev;
        return true;
    } else if (*row > 0) {
        size_t prev_len = buf->lines[*row - 1].len;
        buffer_join_lines(buf, *row - 1, group);
        *row -= 1;
        *col = prev_len;
        return true;
    }
    return false;
}

bool buffer_delete(Buffer *buf, size_t row, size_t col, uint64_t group) {
    if (!buf || row >= buf->count) return false;

    Line *l = &buf->lines[row];
    if (col < l->len) {
        size_t next = utf8_next_char(l->chars, col, l->len);
        buffer_delete_range(buf, row, col, next - col, group);
        return true;
    } else if (row + 1 < buf->count) {
        buffer_join_lines(buf, row, group);
        return true;
    }
    return false;
}

/**
 * @brief Low-level application of an undo/redo action to the buffer lines.
 */
static void apply_raw_action(Buffer *buf, const UndoAction *act, bool is_undo,
                             size_t *out_row, size_t *out_col) {
    if (is_undo) {
        switch (act->type) {
            case ACTION_INSERT_TEXT: {
                if (act->row < buf->count) {
                    Line *l = &buf->lines[act->row];
                    size_t c = act->col <= l->len ? act->col : l->len;
                    size_t del_len = act->text_len;
                    if (c + del_len > l->len) del_len = l->len - c;
                    memmove(l->chars + c, l->chars + c + del_len, l->len - (c + del_len));
                    l->len -= del_len;
                    l->chars[l->len] = '\0';
                    *out_row = act->row;
                    *out_col = c;
                }
                break;
            }
            case ACTION_DELETE_TEXT: {
                if (act->row < buf->count) {
                    Line *l = &buf->lines[act->row];
                    size_t c = act->col <= l->len ? act->col : l->len;
                    line_reserve(l, l->len + act->text_len);
                    memmove(l->chars + c + act->text_len, l->chars + c, l->len - c);
                    memcpy(l->chars + c, act->text, act->text_len);
                    l->len += act->text_len;
                    l->chars[l->len] = '\0';
                    *out_row = act->row;
                    *out_col = c + act->text_len;
                }
                break;
            }
            case ACTION_SPLIT_LINE: {
                // Undo split = join
                if (act->row + 1 < buf->count) {
                    Line *cur = &buf->lines[act->row];
                    Line *next = &buf->lines[act->row + 1];
                    line_reserve(cur, cur->len + next->len);
                    memcpy(cur->chars + cur->len, next->chars, next->len);
                    cur->len += next->len;
                    cur->chars[cur->len] = '\0';
                    line_free(next);
                    size_t lines_to_shift = buf->count - (act->row + 2);
                    if (lines_to_shift > 0) {
                        memmove(&buf->lines[act->row + 1], &buf->lines[act->row + 2],
                                lines_to_shift * sizeof(Line));
                    }
                    buf->count--;
                    *out_row = act->row;
                    *out_col = act->col;
                }
                break;
            }
            case ACTION_JOIN_LINE: {
                // Undo join = split
                if (act->row < buf->count) {
                    Line *cur = &buf->lines[act->row];
                    size_t c = act->col <= cur->len ? act->col : cur->len;
                    size_t rem = cur->len - c;
                    char *rem_buf = NULL;
                    if (rem > 0) {
                        rem_buf = (char*)malloc(rem + 1);
                        if (rem_buf) {
                            memcpy(rem_buf, cur->chars + c, rem);
                            rem_buf[rem] = '\0';
                        }
                    }
                    cur->len = c;
                    cur->chars[c] = '\0';

                    buffer_ensure_cap(buf, buf->count + 1);
                    size_t lines_to_shift = buf->count - (act->row + 1);
                    if (lines_to_shift > 0) {
                        memmove(&buf->lines[act->row + 2], &buf->lines[act->row + 1],
                                lines_to_shift * sizeof(Line));
                    }
                    buf->count++;
                    Line *next_l = &buf->lines[act->row + 1];
                    next_l->cap = rem + 16;
                    next_l->chars = (char*)malloc(next_l->cap);
                    if (rem_buf && rem > 0) {
                        memcpy(next_l->chars, rem_buf, rem);
                    }
                    next_l->chars[rem] = '\0';
                    next_l->len = rem;
                    free(rem_buf);
                    *out_row = act->row;
                    *out_col = c;
                }
                break;
            }
        }
    } else {
        // Redo operations
        switch (act->type) {
            case ACTION_INSERT_TEXT: {
                if (act->row < buf->count) {
                    Line *l = &buf->lines[act->row];
                    size_t c = act->col <= l->len ? act->col : l->len;
                    line_reserve(l, l->len + act->text_len);
                    memmove(l->chars + c + act->text_len, l->chars + c, l->len - c);
                    memcpy(l->chars + c, act->text, act->text_len);
                    l->len += act->text_len;
                    l->chars[l->len] = '\0';
                    *out_row = act->row;
                    *out_col = c + act->text_len;
                }
                break;
            }
            case ACTION_DELETE_TEXT: {
                if (act->row < buf->count) {
                    Line *l = &buf->lines[act->row];
                    size_t c = act->col <= l->len ? act->col : l->len;
                    size_t del_len = act->text_len;
                    if (c + del_len > l->len) del_len = l->len - c;
                    memmove(l->chars + c, l->chars + c + del_len, l->len - (c + del_len));
                    l->len -= del_len;
                    l->chars[l->len] = '\0';
                    *out_row = act->row;
                    *out_col = c;
                }
                break;
            }
            case ACTION_SPLIT_LINE: {
                if (act->row < buf->count) {
                    Line *cur = &buf->lines[act->row];
                    size_t c = act->col <= cur->len ? act->col : cur->len;
                    size_t rem = cur->len - c;
                    char *rem_buf = NULL;
                    if (rem > 0) {
                        rem_buf = (char*)malloc(rem + 1);
                        if (rem_buf) {
                            memcpy(rem_buf, cur->chars + c, rem);
                            rem_buf[rem] = '\0';
                        }
                    }
                    cur->len = c;
                    cur->chars[c] = '\0';

                    buffer_ensure_cap(buf, buf->count + 1);
                    size_t lines_to_shift = buf->count - (act->row + 1);
                    if (lines_to_shift > 0) {
                        memmove(&buf->lines[act->row + 2], &buf->lines[act->row + 1],
                                lines_to_shift * sizeof(Line));
                    }
                    buf->count++;
                    Line *next_l = &buf->lines[act->row + 1];
                    next_l->cap = rem + 16;
                    next_l->chars = (char*)malloc(next_l->cap);
                    if (rem_buf && rem > 0) {
                        memcpy(next_l->chars, rem_buf, rem);
                    }
                    next_l->chars[rem] = '\0';
                    next_l->len = rem;
                    free(rem_buf);
                    *out_row = act->row + 1;
                    *out_col = 0;
                }
                break;
            }
            case ACTION_JOIN_LINE: {
                if (act->row + 1 < buf->count) {
                    Line *cur = &buf->lines[act->row];
                    Line *next = &buf->lines[act->row + 1];
                    line_reserve(cur, cur->len + next->len);
                    memcpy(cur->chars + cur->len, next->chars, next->len);
                    cur->len += next->len;
                    cur->chars[cur->len] = '\0';
                    line_free(next);
                    size_t lines_to_shift = buf->count - (act->row + 2);
                    if (lines_to_shift > 0) {
                        memmove(&buf->lines[act->row + 1], &buf->lines[act->row + 2],
                                lines_to_shift * sizeof(Line));
                    }
                    buf->count--;
                    *out_row = act->row;
                    *out_col = act->col;
                }
                break;
            }
        }
    }
}

bool buffer_undo(Buffer *buf, size_t *out_row, size_t *out_col) {
    if (!buf || undo_stack_is_empty(&buf->undo_stack)) return false;

    UndoAction top;
    undo_stack_peek(&buf->undo_stack, &top);
    uint64_t target_group = top.group_id;

    while (!undo_stack_is_empty(&buf->undo_stack)) {
        undo_stack_peek(&buf->undo_stack, &top);
        if (target_group != 0 && top.group_id != target_group) {
            break;
        }
        UndoAction act;
        undo_stack_pop(&buf->undo_stack, &act);
        apply_raw_action(buf, &act, true, out_row, out_col);

        undo_stack_push(&buf->redo_stack, act.type, act.row, act.col,
                        act.text, act.text_len, act.group_id);
        free(act.text);
    }

    buf->is_dirty = true;
    return true;
}

bool buffer_redo(Buffer *buf, size_t *out_row, size_t *out_col) {
    if (!buf || undo_stack_is_empty(&buf->redo_stack)) return false;

    UndoAction top;
    undo_stack_peek(&buf->redo_stack, &top);
    uint64_t target_group = top.group_id;

    while (!undo_stack_is_empty(&buf->redo_stack)) {
        undo_stack_peek(&buf->redo_stack, &top);
        if (target_group != 0 && top.group_id != target_group) {
            break;
        }
        UndoAction act;
        undo_stack_pop(&buf->redo_stack, &act);
        apply_raw_action(buf, &act, false, out_row, out_col);

        undo_stack_push(&buf->undo_stack, act.type, act.row, act.col,
                        act.text, act.text_len, act.group_id);
        free(act.text);
    }

    buf->is_dirty = true;
    return true;
}

size_t buffer_char_count(const Buffer *buf) {
    if (!buf) return 0;
    size_t total = 0;
    for (size_t i = 0; i < buf->count; ++i) {
        total += utf8_codepoint_count(buf->lines[i].chars, buf->lines[i].len);
    }
    return total;
}

size_t buffer_word_count(const Buffer *buf) {
    if (!buf) return 0;
    size_t words = 0;
    for (size_t i = 0; i < buf->count; ++i) {
        const char *txt = buf->lines[i].chars;
        size_t len = buf->lines[i].len;
        bool in_word = false;
        for (size_t j = 0; j < len; ++j) {
            if (isspace((unsigned char)txt[j])) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }
    return words;
}
