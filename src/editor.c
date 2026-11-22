#include "backup.h"
/**
 * @file editor.c
 * @brief Implementation of editor state management, navigation, and Markdown helpers.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "editor.h"
#include "utf8.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

/** @brief Initialize editor state, defaults, buffer, and optionally load filename. */
void editor_init(Editor *ed, const char *filename) {
    if (!ed) return;
    memset(ed, 0, sizeof(Editor));
    buffer_init(&ed->buffer);
    rendered_doc_init(&ed->rendered_doc);

    ed->view_mode = VIEW_SPLIT; // Default to dual-pane split view
    ed->insert_mode = true;     // Default to Insert mode (steady block cursor), press Ins for Overwrite
    ed->wrap_col = MDE_WRAP_COL_DEFAULT; // Default automatic line break at 78 columns
    ed->rendered_dirty = true;
    ed->rendered_view_start = 0;
    ed->rendered_view_end = 0;
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->preview_scroll_row = 0;
    ed->quit_requested = false;
    ed->backup_made = false;
    ed->search_active = false;
    ed->mark_active = false;
    ed->mark_row = 0;
    ed->mark_col = 0;
    ed->clipboard = NULL;
    ed->clipboard_len = 0;
    editor_touch_key(ed);

    if (filename && filename[0]) {
        if (!buffer_load(&ed->buffer, filename)) {
            // New file with that path
            ed->buffer.file_path = strdup(filename);
            editor_set_status(ed, "New file: %s", filename);
        } else {
            editor_set_status(ed, "Opened %s (%zu lines)", filename, buffer_line_count(&ed->buffer));
        }
    } else {
        editor_set_status(ed, "Welcome to mde - Press ^G for help, F2/M-P to toggle view");
    }
}

/** @brief Free all dynamic memory owned by ed (buffer, preview, clipboard, link history). */
void editor_free(Editor *ed) {
    if (!ed) return;
    buffer_free(&ed->buffer);
    rendered_doc_free(&ed->rendered_doc);
    free(ed->clipboard);
    ed->clipboard = NULL;
    ed->clipboard_len = 0;
    for (size_t i = 0; i < ed->link_history_count; ++i) {
        free(ed->link_history[i].file_path);
        ed->link_history[i].file_path = NULL;
    }
    ed->link_history_count = 0;
}

/** @brief Format and post a temporary notification message to the status bar. */
void editor_set_status(Editor *ed, const char *fmt, ...) {
    if (!ed || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ed->status_msg, sizeof(ed->status_msg), fmt, args);
    va_end(args);
    ed->status_time = time(NULL);
}

/** @brief Cycle active view mode: VIEW_SPLIT -> VIEW_INPLACE -> VIEW_PREVIEW. */
void editor_cycle_view_mode(Editor *ed) {
    if (!ed) return;
    ed->view_mode = (ViewMode)((ed->view_mode + 1) % 3);
    ed->rendered_dirty = true;
    switch (ed->view_mode) {
        case VIEW_SPLIT:
            editor_set_status(ed, "View: Dual-Pane Split (Editor | Rendered Preview)");
            break;
        case VIEW_INPLACE:
            editor_set_status(ed, "View: In-Place Rich Markdown Editor");
            break;
        case VIEW_PREVIEW:
            editor_set_status(ed, "View: Fullscreen Rendered Reader");
            break;
    }
}

/** @brief Rebuild the cached RenderedDoc preview whenever marked dirty. */
void editor_refresh_rendered(Editor *ed) {
    if (!ed || !ed->rendered_dirty) return;
    int p_width = ed->preview_width > 0 ? ed->preview_width : 80;

    size_t total_lines = buffer_line_count(&ed->buffer);
    if (total_lines > 1000) {
        // Document is large (> 1,000 lines): render view-bound range with lookahead margin
        size_t center = (ed->view_mode == VIEW_PREVIEW) ? ed->cursor_row : ed->scroll_row;
        size_t margin = 120;
        size_t start_row = center > margin ? (center - margin) : 0;
        size_t end_row = center + margin < total_lines ? (center + margin) : total_lines;

        render_document_range(&ed->buffer, p_width, start_row, end_row, &ed->rendered_doc);
        ed->rendered_view_start = start_row;
        ed->rendered_view_end = end_row;
    } else {
        render_document(&ed->buffer, p_width, &ed->rendered_doc);
        ed->rendered_view_start = 0;
        ed->rendered_view_end = total_lines;
    }
    ed->rendered_dirty = false;
}

/** @brief Move cursor one UTF-8 character left, wrapping to the previous line end. */
void editor_move_left(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_col > 0) {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_prev_char(l, ed->cursor_col);
    } else if (ed->cursor_row > 0) {
        ed->cursor_row--;
        ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
    }
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Move cursor one UTF-8 character right, wrapping to the next line start. */
void editor_move_right(Editor *ed) {
    if (!ed) return;
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    if (ed->cursor_col < line_len) {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_next_char(l, ed->cursor_col, line_len);
    } else if (ed->cursor_row + 1 < buffer_line_count(&ed->buffer)) {
        ed->cursor_row++;
        ed->cursor_col = 0;
    }
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Classify a byte as word-constituent (alphanumeric, '_', or UTF-8 multibyte). */
static bool is_word_byte(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 0x80;
}

/** @brief Move cursor backward by one word boundary, crossing to previous line end if at col 0. */
void editor_move_word_left(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_col == 0) {
        if (ed->cursor_row > 0) {
            ed->cursor_row--;
            ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
        }
    } else {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        size_t pos = ed->cursor_col;
        while (pos > 0) {
            size_t prev = utf8_prev_char(l, pos);
            if (!isspace((unsigned char)l[prev])) break;
            pos = prev;
        }
        if (pos > 0) {
            size_t prev = utf8_prev_char(l, pos);
            bool word_run = is_word_byte((unsigned char)l[prev]);
            while (pos > 0) {
                size_t p = utf8_prev_char(l, pos);
                unsigned char c = (unsigned char)l[p];
                if (isspace(c) || is_word_byte(c) != word_run) break;
                pos = p;
            }
        }
        ed->cursor_col = pos;
    }
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Move cursor forward by one word boundary, crossing to next line start if at EOL. */
void editor_move_word_right(Editor *ed) {
    if (!ed) return;
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    if (ed->cursor_col >= line_len) {
        if (ed->cursor_row + 1 < buffer_line_count(&ed->buffer)) {
            ed->cursor_row++;
            ed->cursor_col = 0;
        }
    } else {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        size_t pos = ed->cursor_col;
        if (!isspace((unsigned char)l[pos])) {
            bool word_run = is_word_byte((unsigned char)l[pos]);
            while (pos < line_len) {
                unsigned char c = (unsigned char)l[pos];
                if (isspace(c) || is_word_byte(c) != word_run) break;
                pos = utf8_next_char(l, pos, line_len);
            }
        }
        while (pos < line_len && isspace((unsigned char)l[pos])) {
            pos = utf8_next_char(l, pos, line_len);
        }
        ed->cursor_col = pos;
    }
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Move cursor up one row while preserving preferred visual column. */
void editor_move_up(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_row > 0) {
        ed->cursor_row--;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    }
}

/** @brief Move cursor down one row while preserving preferred visual column. */
void editor_move_down(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_row + 1 < buffer_line_count(&ed->buffer)) {
        ed->cursor_row++;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    }
}

/** @brief Move cursor to column 0 of the active line. */
void editor_move_home(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
}

/** @brief Move cursor to the end of the active line. */
void editor_move_end(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Move cursor up by one viewport page. */
void editor_move_page_up(Editor *ed) {
    if (!ed) return;
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    if (ed->cursor_row > step) ed->cursor_row -= step;
    else ed->cursor_row = 0;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
}

/** @brief Move cursor down by one viewport page. */
void editor_move_page_down(Editor *ed) {
    if (!ed) return;
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    size_t max_row = buffer_line_count(&ed->buffer) - 1;
    ed->cursor_row += step;
    if (ed->cursor_row > max_row) ed->cursor_row = max_row;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
}

/** @brief Jump cursor to the beginning of the document (row 0, col 0). */
void editor_move_doc_start(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
}

/** @brief Jump cursor to the end of the final line in the document. */
void editor_move_doc_end(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    size_t total = buffer_line_count(&ed->buffer);
    ed->cursor_row = total > 0 ? total - 1 : 0;
    ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

/** @brief Jump cursor to a 1-indexed line and optional 1-indexed visual column. */
void editor_goto_line(Editor *ed, size_t line_1based, size_t col_1based) {
    if (!ed) return;
    editor_touch_key(ed);

    size_t total = buffer_line_count(&ed->buffer);
    if (total == 0) return;

    size_t target_row = (line_1based > 0) ? (line_1based - 1) : 0;
    if (target_row >= total) target_row = total - 1;
    ed->cursor_row = target_row;

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    int target_vcol = (col_1based > 0) ? (int)(col_1based - 1) : 0;
    ed->cursor_col = utf8_col_to_byte(line, target_vcol);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);

    editor_set_status(ed, "Line %zu, Col %d", ed->cursor_row + 1, ed->target_visual_col + 1);
}

/** @brief Display current line, percentage, column, word, and char counts in status bar. */
void editor_report_position(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);

    size_t total_lines = buffer_line_count(&ed->buffer);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    int cur_vcol = utf8_byte_to_col(line, ed->cursor_col) + 1;
    int max_vcol = utf8_visual_width(line, line_len) + 1;
    int pct = total_lines > 0 ? (int)(((ed->cursor_row + 1) * 100) / total_lines) : 100;

    editor_set_status(ed, "Ln %zu/%zu (%d%%), Col %d/%d | %zu words, %zu chars",
                      ed->cursor_row + 1, total_lines, pct,
                      cur_vcol, max_vcol,
                      buffer_word_count(&ed->buffer),
                      buffer_char_count(&ed->buffer));
}

/** @brief Set the selection mark anchor at the current cursor position. */
void editor_set_mark(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->mark_active = true;
    ed->mark_row = ed->cursor_row;
    ed->mark_col = ed->cursor_col;
    editor_set_status(ed, "Mark Set");
}

/** @brief Clear the active selection mark. */
void editor_clear_mark(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->mark_active = false;
    editor_set_status(ed, "Mark Unset");
}

/** @brief Toggle the selection mark ON at the cursor, or OFF if already active. */
void editor_toggle_mark(Editor *ed) {
    if (!ed) return;
    if (ed->mark_active) {
        editor_clear_mark(ed);
    } else {
        editor_set_mark(ed);
    }
}

/** @brief Return true when a non-empty text selection spans mark..cursor. */
bool editor_has_selection(const Editor *ed) {
    if (!ed || !ed->mark_active) return false;
    return (ed->mark_row != ed->cursor_row || ed->mark_col != ed->cursor_col);
}

/** @brief Return ordered (start <= end) selection coordinates if a selection is active. */
bool editor_get_selection(const Editor *ed, size_t *start_row, size_t *start_col,
                          size_t *end_row, size_t *end_col) {
    if (!ed || !ed->mark_active) return false;
    if (ed->mark_row == ed->cursor_row && ed->mark_col == ed->cursor_col) return false;

    if (ed->mark_row < ed->cursor_row ||
        (ed->mark_row == ed->cursor_row && ed->mark_col < ed->cursor_col)) {
        if (start_row) *start_row = ed->mark_row;
        if (start_col) *start_col = ed->mark_col;
        if (end_row) *end_row = ed->cursor_row;
        if (end_col) *end_col = ed->cursor_col;
    } else {
        if (start_row) *start_row = ed->cursor_row;
        if (start_col) *start_col = ed->cursor_col;
        if (end_row) *end_row = ed->mark_row;
        if (end_col) *end_col = ed->mark_col;
    }
    return true;
}

/** @brief Extract the active selection into a newly allocated null-terminated string. */
char *editor_get_selected_text(const Editor *ed, size_t *out_len) {
    size_t s_row, s_col, e_row, e_col;
    if (!editor_get_selection(ed, &s_row, &s_col, &e_row, &e_col)) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t s_line_len = buffer_line_len(&ed->buffer, s_row);
    if (s_col > s_line_len) s_col = s_line_len;
    size_t e_line_len = buffer_line_len(&ed->buffer, e_row);
    if (e_col > e_line_len) e_col = e_line_len;

    size_t total_len = 0;
    if (s_row == e_row) {
        total_len = (e_col > s_col) ? (e_col - s_col) : 0;
    } else {
        total_len += (s_line_len > s_col ? s_line_len - s_col : 0) + 1; // +1 for '\n'
        for (size_t r = s_row + 1; r < e_row; ++r) {
            total_len += buffer_line_len(&ed->buffer, r) + 1; // +1 for '\n'
        }
        total_len += e_col;
    }

    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t offset = 0;
    if (s_row == e_row) {
        const char *l = buffer_line(&ed->buffer, s_row);
        if (e_col > s_col) {
            memcpy(buf + offset, l + s_col, e_col - s_col);
            offset += (e_col - s_col);
        }
    } else {
        const char *l0 = buffer_line(&ed->buffer, s_row);
        if (s_line_len > s_col) {
            memcpy(buf + offset, l0 + s_col, s_line_len - s_col);
            offset += (s_line_len - s_col);
        }
        buf[offset++] = '\n';

        for (size_t r = s_row + 1; r < e_row; ++r) {
            const char *lr = buffer_line(&ed->buffer, r);
            size_t lr_len = buffer_line_len(&ed->buffer, r);
            if (lr_len > 0) {
                memcpy(buf + offset, lr, lr_len);
                offset += lr_len;
            }
            buf[offset++] = '\n';
        }

        const char *le = buffer_line(&ed->buffer, e_row);
        if (e_col > 0) {
            memcpy(buf + offset, le, e_col);
            offset += e_col;
        }
    }

    buf[offset] = '\0';
    if (out_len) *out_len = offset;
    return buf;
}

/** @brief Delete the active selection from the buffer under a single undo group. */
bool editor_delete_selection(Editor *ed, uint64_t group) {
    size_t s_row, s_col, e_row, e_col;
    if (!editor_get_selection(ed, &s_row, &s_col, &e_row, &e_col)) {
        return false;
    }

    if (group == 0) {
        group = buffer_next_group(&ed->buffer);
    }

    size_t s_line_len = buffer_line_len(&ed->buffer, s_row);
    if (s_col > s_line_len) s_col = s_line_len;
    size_t e_line_len = buffer_line_len(&ed->buffer, e_row);
    if (e_col > e_line_len) e_col = e_line_len;

    if (s_row == e_row) {
        if (e_col > s_col) {
            buffer_delete_range(&ed->buffer, s_row, s_col, e_col - s_col, group);
        }
    } else {
        // Step 1: On e_row, delete range [0, e_col)
        if (e_col > 0) {
            buffer_delete_range(&ed->buffer, e_row, 0, e_col, group);
        }
        // Step 2: On intermediate lines from e_row - 1 down to s_row + 1:
        // delete entire line content, then join with line below
        for (size_t r = e_row - 1; r > s_row; --r) {
            size_t r_len = buffer_line_len(&ed->buffer, r);
            if (r_len > 0) {
                buffer_delete_range(&ed->buffer, r, 0, r_len, group);
            }
            buffer_join_lines(&ed->buffer, r, group);
        }
        // Step 3: On s_row, delete from s_col to end of line
        size_t cur_s_len = buffer_line_len(&ed->buffer, s_row);
        if (cur_s_len > s_col) {
            buffer_delete_range(&ed->buffer, s_row, s_col, cur_s_len - s_col, group);
        }
        // Step 4: Join s_row with next line (which is now what remains of e_row)
        buffer_join_lines(&ed->buffer, s_row, group);
    }

    ed->cursor_row = s_row;
    ed->cursor_col = s_col;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
    ed->mark_active = false;
    ed->rendered_dirty = true;
    return true;
}

/** @brief Store text in internal clipboard and sync to host terminal via ANSI OSC 52. */
static void set_clipboard(Editor *ed, const char *text, size_t len) {
    if (!ed) return;
    free(ed->clipboard);
    ed->clipboard = NULL;
    ed->clipboard_len = 0;

    if (text && len > 0) {
        ed->clipboard = (char *)malloc(len + 1);
        if (ed->clipboard) {
            memcpy(ed->clipboard, text, len);
            ed->clipboard[len] = '\0';
            ed->clipboard_len = len;
        }

        // Emit OSC 52 escape sequence so host terminal / OS clipboard receives the text
        static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t b64_len = 4 * ((len + 2) / 3);
        char *b64 = (char *)malloc(b64_len + 1);
        if (b64) {
            size_t j = 0;
            for (size_t i = 0; i < len; i += 3) {
                uint32_t octet_a = (unsigned char)text[i];
                uint32_t octet_b = (i + 1 < len) ? (unsigned char)text[i + 1] : 0;
                uint32_t octet_c = (i + 2 < len) ? (unsigned char)text[i + 2] : 0;
                uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

                b64[j++] = b64_table[(triple >> 18) & 0x3F];
                b64[j++] = b64_table[(triple >> 12) & 0x3F];
                b64[j++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
                b64[j++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';
            }
            b64[j] = '\0';
            fprintf(stdout, "\033]52;c;%s\007", b64);
            fflush(stdout);
            free(b64);
        }
    }
}

/** @brief Copy the active selection to the clipboard and clear the mark. */
bool editor_copy(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t len = 0;
    char *text = editor_get_selected_text(ed, &len);
    if (!text || len == 0) {
        free(text);
        editor_set_status(ed, "No selection to copy");
        return false;
    }

    set_clipboard(ed, text, len);
    free(text);
    ed->mark_active = false;
    editor_set_status(ed, "Copied %zu character%s", len, len == 1 ? "" : "s");
    return true;
}

/** @brief Copy the entire current line (with newline if not last line) to the clipboard. */
bool editor_copy_line(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t row = ed->cursor_row;
    size_t total = buffer_line_count(&ed->buffer);
    if (row >= total) return false;

    const char *line = buffer_line(&ed->buffer, row);
    size_t len = buffer_line_len(&ed->buffer, row);
    bool has_next = (row + 1 < total);

    size_t clip_len = len + (has_next ? 1 : 0);
    if (clip_len == 0) {
        editor_set_status(ed, "Empty line");
        return false;
    }

    char *clip = (char *)malloc(clip_len + 1);
    if (!clip) return false;
    if (len > 0) memcpy(clip, line, len);
    if (has_next) clip[len] = '\n';
    clip[clip_len] = '\0';
    set_clipboard(ed, clip, clip_len);
    free(clip);

    ed->mark_active = false;
    editor_set_status(ed, "Copied line");
    return true;
}

/** @brief Cut the active selection to the clipboard as an atomic undo group. */
bool editor_cut(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t len = 0;
    char *text = editor_get_selected_text(ed, &len);
    if (!text || len == 0) {
        free(text);
        editor_set_status(ed, "No selection to cut");
        return false;
    }

    set_clipboard(ed, text, len);
    free(text);

    uint64_t group = buffer_next_group(&ed->buffer);
    editor_delete_selection(ed, group);
    editor_set_status(ed, "Cut %zu character%s", len, len == 1 ? "" : "s");
    return true;
}

/** @brief Cut the entire current line (with newline if not last line) to the clipboard. */
bool editor_cut_line(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t row = ed->cursor_row;
    size_t total = buffer_line_count(&ed->buffer);
    if (row >= total) return false;

    const char *line = buffer_line(&ed->buffer, row);
    size_t len = buffer_line_len(&ed->buffer, row);
    bool has_next = (row + 1 < total);

    if (len == 0 && !has_next) {
        editor_set_status(ed, "Empty line");
        return false;
    }

    size_t clip_len = len + (has_next ? 1 : 0);
    char *clip = (char *)malloc(clip_len + 1);
    if (clip) {
        if (len > 0) memcpy(clip, line, len);
        if (has_next) clip[len] = '\n';
        clip[clip_len] = '\0';
        set_clipboard(ed, clip, clip_len);
        free(clip);
    }

    uint64_t group = buffer_next_group(&ed->buffer);
    if (len > 0) {
        buffer_delete_range(&ed->buffer, row, 0, len, group);
    }
    if (has_next) {
        buffer_join_lines(&ed->buffer, row, group);
    }

    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->mark_active = false;
    ed->rendered_dirty = true;
    editor_set_status(ed, "Cut line");
    return true;
}

/** @brief Insert arbitrary (possibly multi-line) UTF-8 text at the cursor as one undo group. */
bool editor_paste_text(Editor *ed, const char *text, size_t len) {
    if (!ed || !text || len == 0) return false;
    editor_touch_key(ed);

    uint64_t group = buffer_next_group(&ed->buffer);

    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        editor_delete_selection(ed, group);
    }

    size_t pos = 0;
    while (pos < len) {
        const char *nl = (const char *)memchr(text + pos, '\n', len - pos);
        if (nl) {
            size_t seg_len = (size_t)(nl - (text + pos));
            if (seg_len > 0) {
                buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col,
                                     text + pos, seg_len, group);
                ed->cursor_col += seg_len;
            }
            buffer_split_line(&ed->buffer, ed->cursor_row, ed->cursor_col, group);
            ed->cursor_row++;
            ed->cursor_col = 0;
            pos += seg_len + 1; // +1 to skip '\n'
        } else {
            size_t seg_len = len - pos;
            if (seg_len > 0) {
                buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col,
                                     text + pos, seg_len, group);
                ed->cursor_col += seg_len;
            }
            pos += seg_len;
        }
    }

    const char *cur_l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(cur_l, ed->cursor_col);
    ed->rendered_dirty = true;
    return true;
}

/** @brief Paste the internal clipboard contents at the current cursor position. */
bool editor_paste(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    if (!ed->clipboard || ed->clipboard_len == 0) {
        editor_set_status(ed, "Clipboard is empty");
        return false;
    }

    size_t len = ed->clipboard_len;
    editor_paste_text(ed, ed->clipboard, len);
    editor_set_status(ed, "Pasted %zu character%s", len, len == 1 ? "" : "s");
    return true;
}

/** @brief Kill text from cursor to end of line, or join with next line if already at EOL. */
void editor_kill_to_end_of_line(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);

    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);

    if (ed->cursor_col < line_len) {
        size_t kill_len = line_len - ed->cursor_col;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        set_clipboard(ed, line + ed->cursor_col, kill_len);

        uint64_t group = buffer_next_group(&ed->buffer);
        buffer_delete_range(&ed->buffer, ed->cursor_row, ed->cursor_col, kill_len, group);
        ed->rendered_dirty = true;
        ed->mark_active = false;
        editor_set_status(ed, "Killed %zu character%s", kill_len, kill_len == 1 ? "" : "s");
    } else if (ed->cursor_row + 1 < buffer_line_count(&ed->buffer)) {
        set_clipboard(ed, "\n", 1);
        uint64_t group = buffer_next_group(&ed->buffer);
        buffer_join_lines(&ed->buffer, ed->cursor_row, group);
        ed->rendered_dirty = true;
        ed->mark_active = false;
        editor_set_status(ed, "Killed newline");
    }
}

/** @brief Kill the word immediately preceding the cursor into the clipboard. */
void editor_kill_prev_word(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);

    if (ed->cursor_col == 0) {
        if (ed->cursor_row > 0) {
            ed->cursor_row--;
            ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
            uint64_t group = buffer_next_group(&ed->buffer);
            buffer_join_lines(&ed->buffer, ed->cursor_row, group);
            set_clipboard(ed, "\n", 1);
            const char *l = buffer_line(&ed->buffer, ed->cursor_row);
            ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
            ed->rendered_dirty = true;
            ed->mark_active = false;
            editor_set_status(ed, "Killed newline");
        }
        return;
    }

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t pos = ed->cursor_col;

    // 1. Skip whitespace backwards
    while (pos > 0 && isspace((unsigned char)line[pos - 1])) {
        pos--;
    }

    // 2. Skip word characters backwards
    if (pos > 0) {
        if (isalnum((unsigned char)line[pos - 1]) || line[pos - 1] == '_') {
            while (pos > 0 && (isalnum((unsigned char)line[pos - 1]) || line[pos - 1] == '_')) {
                pos--;
            }
        } else {
            while (pos > 0 && !isalnum((unsigned char)line[pos - 1]) && !isspace((unsigned char)line[pos - 1]) && line[pos - 1] != '_') {
                pos--;
            }
        }
    }

    if (pos < ed->cursor_col) {
        size_t kill_len = ed->cursor_col - pos;
        set_clipboard(ed, line + pos, kill_len);

        uint64_t group = buffer_next_group(&ed->buffer);
        buffer_delete_range(&ed->buffer, ed->cursor_row, pos, kill_len, group);
        ed->cursor_col = pos;
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
        ed->rendered_dirty = true;
        ed->mark_active = false;
        editor_set_status(ed, "Killed %zu character%s", kill_len, kill_len == 1 ? "" : "s");
    }
}

/**
 * @brief Automatically break the current line if its visual width exceeds ed->wrap_col.
 *
 * Prefers breaking at the last whitespace boundary at or before ed->wrap_col,
 * removing the break whitespace and preserving leading indentation or blockquote
 * prefixes on the wrapped continuation line. Falls back to a hard break at
 * ed->wrap_col on UTF-8 boundaries when no whitespace break point exists.
 */
static void editor_auto_wrap_line(Editor *ed, uint64_t group) {
    if (!ed || ed->wrap_col <= 0) return;

    for (int iter = 0; iter < 64; ++iter) {
        size_t row = ed->cursor_row;
        const char *line = buffer_line(&ed->buffer, row);
        size_t len = buffer_line_len(&ed->buffer, row);

        if (utf8_visual_width(line, len) <= ed->wrap_col) {
            break;
        }

        // Determine leading indentation or blockquote prefix
        size_t indent = 0;
        while (indent < len && (line[indent] == ' ' || line[indent] == '\t')) {
            indent++;
        }

        size_t prefix_end = indent;
        if (indent < len && line[indent] == '>') {
            size_t bq = indent;
            while (bq < len && (line[bq] == '>' || line[bq] == ' ')) {
                bq++;
            }
            prefix_end = bq;
        }

        char cont_prefix[128];
        size_t cont_prefix_len = 0;
        size_t min_break_byte = prefix_end;
        int prefix_vcol = utf8_visual_width_range(line, 0, prefix_end);
        if (prefix_end > 0 && prefix_end < sizeof(cont_prefix) &&
            prefix_vcol < ed->wrap_col - 2) {
            memcpy(cont_prefix, line, prefix_end);
            cont_prefix[prefix_end] = '\0';
            cont_prefix_len = prefix_end;
        } else {
            cont_prefix[0] = '\0';
            cont_prefix_len = 0;
            min_break_byte = 0;
        }

        // Scan UTF-8 characters to find the best break position at or before wrap_col
        size_t pos = 0;
        int vcol = 0;
        bool found_ws = false;
        size_t last_ws_byte = 0;
        size_t hard_break_byte = 0;

        while (pos < len) {
            size_t ch_len = utf8_char_len((unsigned char)line[pos]);
            if (pos + ch_len > len) ch_len = len - pos;
            int ch_w = utf8_visual_width_range(line, pos, pos + ch_len);

            if (pos >= min_break_byte && vcol <= ed->wrap_col &&
                (line[pos] == ' ' || line[pos] == '\t')) {
                found_ws = true;
                last_ws_byte = pos;
            }

            if (vcol + ch_w <= ed->wrap_col) {
                hard_break_byte = pos + ch_len;
            }

            if (vcol > ed->wrap_col) {
                break;
            }

            vcol += ch_w;
            pos += ch_len;
        }

        if (found_ws) {
            // Expand to contiguous whitespace run at the break point
            size_t ws_start = last_ws_byte;
            while (ws_start > min_break_byte &&
                   (line[ws_start - 1] == ' ' || line[ws_start - 1] == '\t')) {
                ws_start--;
            }
            size_t ws_end = last_ws_byte + 1;
            while (ws_end < len &&
                   (line[ws_end] == ' ' || line[ws_end] == '\t')) {
                ws_end++;
            }

            size_t old_cursor_col = ed->cursor_col;

            buffer_delete_range(&ed->buffer, row, ws_start, ws_end - ws_start, group);
            buffer_split_line(&ed->buffer, row, ws_start, group);
            if (cont_prefix_len > 0) {
                buffer_insert_string(&ed->buffer, row + 1, 0, cont_prefix, cont_prefix_len, group);
            }

            if (old_cursor_col >= ws_start) {
                ed->cursor_row = row + 1;
                if (old_cursor_col > ws_end) {
                    ed->cursor_col = cont_prefix_len + (old_cursor_col - ws_end);
                } else {
                    ed->cursor_col = cont_prefix_len;
                }
            }
        } else {
            // No whitespace break point: hard break at wrap_col boundary
            if (hard_break_byte == 0) {
                hard_break_byte = utf8_next_char(line, 0, len);
            }
            if (hard_break_byte >= len) {
                break;
            }

            size_t old_cursor_col = ed->cursor_col;

            buffer_split_line(&ed->buffer, row, hard_break_byte, group);
            if (cont_prefix_len > 0) {
                buffer_insert_string(&ed->buffer, row + 1, 0, cont_prefix, cont_prefix_len, group);
            }

            if (old_cursor_col >= hard_break_byte) {
                ed->cursor_row = row + 1;
                ed->cursor_col = cont_prefix_len + (old_cursor_col - hard_break_byte);
            }
        }

        // If cursor stayed on the original row, that row is now <= wrap_col
        if (ed->cursor_row == row) {
            break;
        }
    }
}

/** @brief Insert or overwrite a UTF-8 character sequence, applying auto line wrap. */
void editor_insert_char(Editor *ed, const char *utf8_seq, size_t len) {
    if (!ed || !utf8_seq || len == 0) return;
    editor_touch_key(ed);

    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        uint64_t group = buffer_next_group(&ed->buffer);
        editor_delete_selection(ed, group);
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, group);
        ed->cursor_col += len;
        editor_auto_wrap_line(ed, group);
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
        return;
    }

    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    uint64_t group = ed->buffer.current_group_id;

    if (ed->insert_mode || ed->cursor_col >= line_len) {
        // Insert mode (or at line end in overwrite mode): insert character
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, group);
        ed->cursor_col += len;
    } else {
        // Overwrite mode: replace existing character at cursor
        const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
        size_t old_char_len = utf8_char_len((unsigned char)cur_line[ed->cursor_col]);
        group = buffer_next_group(&ed->buffer);
        buffer_delete_range(&ed->buffer, ed->cursor_row, ed->cursor_col, old_char_len, group);
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, group);
        ed->cursor_col += len;
    }

    editor_auto_wrap_line(ed, group);

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
    ed->rendered_dirty = true;
}

/** @brief Delete the active selection or the UTF-8 character preceding the cursor. */
void editor_backspace(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        editor_delete_selection(ed, 0);
        return;
    }
    if (buffer_backspace(&ed->buffer, &ed->cursor_row, &ed->cursor_col, ed->buffer.current_group_id)) {
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
    }
}

/** @brief Delete the active selection or the UTF-8 character at the cursor. */
void editor_delete(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        editor_delete_selection(ed, 0);
        return;
    }
    if (buffer_delete(&ed->buffer, ed->cursor_row, ed->cursor_col, ed->buffer.current_group_id)) {
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
    }
}

/** @brief Insert a newline with smart list/task continuation and indentation matching. */
void editor_smart_enter(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    uint64_t group = buffer_next_group(&ed->buffer);

    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        editor_delete_selection(ed, group);
    }

    const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t cur_len = buffer_line_len(&ed->buffer, ed->cursor_row);

    size_t prefix_len = 0;
    char bullet[16] = {0};
    bool is_task = false;
    bool task_checked = false;

    bool is_list = md_detect_list(cur_line, cur_len, &prefix_len, bullet, sizeof(bullet),
                                  &is_task, &task_checked);

    if (is_list) {
        // Test if the current list item is completely empty after the bullet
        size_t after_prefix = prefix_len;
        while (after_prefix < cur_len && isspace((unsigned char)cur_line[after_prefix])) after_prefix++;

        if (after_prefix >= cur_len) {
            // Empty list item: clear the line and exit list mode
            buffer_replace_line(&ed->buffer, ed->cursor_row, "", 0, group);
            ed->cursor_col = 0;
            ed->target_visual_col = 0;
            ed->rendered_dirty = true;
            return;
        }

        // Split current line at cursor position
        buffer_split_line(&ed->buffer, ed->cursor_row, ed->cursor_col, group);
        ed->cursor_row++;
        ed->cursor_col = 0;

        // Construct continuation prefix matching indentation
        char next_prefix[64] = {0};

        size_t indent = 0;
        while (indent < cur_len && (cur_line[indent] == ' ' || cur_line[indent] == '\t')) indent++;
        size_t copy_indent = indent < sizeof(next_prefix) - 20 ? indent : 0;
        memcpy(next_prefix, cur_line, copy_indent);
        next_prefix[copy_indent] = '\0';

        if (is_task) {
            strncat(next_prefix, "- [ ] ", sizeof(next_prefix) - strlen(next_prefix) - 1);
        } else if (isdigit((unsigned char)bullet[0])) {
            int num = atoi(bullet);
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d. ", num + 1);
            strncat(next_prefix, num_buf, sizeof(next_prefix) - strlen(next_prefix) - 1);
        } else {
            strncat(next_prefix, bullet, sizeof(next_prefix) - strlen(next_prefix) - 1);
        }

        size_t nplen = strlen(next_prefix);
        buffer_insert_string(&ed->buffer, ed->cursor_row, 0, next_prefix, nplen, group);
        ed->cursor_col = nplen;
    } else {
        // Standard auto-indent matching leading whitespace
        size_t indent = 0;
        while (indent < cur_len && (cur_line[indent] == ' ' || cur_line[indent] == '\t')) indent++;

        buffer_split_line(&ed->buffer, ed->cursor_row, ed->cursor_col, group);
        ed->cursor_row++;
        ed->cursor_col = 0;

        if (indent > 0) {
            buffer_insert_string(&ed->buffer, ed->cursor_row, 0, cur_line, indent, group);
            ed->cursor_col = indent;
        }
    }

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
    ed->rendered_dirty = true;
}

/** @brief Indent the current line by two leading spaces. */
void editor_indent(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    uint64_t group = buffer_next_group(&ed->buffer);
    buffer_insert_string(&ed->buffer, ed->cursor_row, 0, "  ", 2, group);
    ed->cursor_col += 2;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
    ed->rendered_dirty = true;
}

/** @brief Remove up to two leading spaces from the current line. */
void editor_unindent(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t spaces = 0;
    while (spaces < 2 && line[spaces] == ' ') spaces++;
    if (spaces > 0) {
        uint64_t group = buffer_next_group(&ed->buffer);
        buffer_delete_range(&ed->buffer, ed->cursor_row, 0, spaces, group);
        if (ed->cursor_col >= spaces) ed->cursor_col -= spaces;
        else ed->cursor_col = 0;
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
        ed->rendered_dirty = true;
    }
}

/** @brief Toggle between Insert mode (steady block) and Overwrite mode (steady underline). */
void editor_toggle_insert_mode(Editor *ed) {
    if (!ed) return;
    ed->insert_mode = !ed->insert_mode;
    editor_touch_key(ed);
    if (ed->insert_mode) {
        editor_set_status(ed, "INSERT Mode (Block cursor)");
    } else {
        editor_set_status(ed, "OVERWRITE Mode (Underscore cursor)");
    }
}

/** @brief Validate and update the automatic line break column width (20-255 or 0=off). */
bool editor_set_wrap_col(Editor *ed, int cols) {
    if (!ed) return false;
    if (cols != MDE_WRAP_COL_OFF && (cols < MDE_WRAP_COL_MIN || cols > MDE_WRAP_COL_MAX)) {
        return false;
    }
    ed->wrap_col = cols;
    editor_touch_key(ed);
    if (cols == MDE_WRAP_COL_OFF) {
        editor_set_status(ed, "Auto line break: OFF");
    } else {
        editor_set_status(ed, "Auto line break: %d columns", cols);
    }
    return true;
}

/** @brief Parse a wrap width string ("20"-"255", "0", "off", "none") into out_cols. */
bool editor_parse_wrap_col(const char *str, int *out_cols) {
    if (!str || !out_cols) return false;

    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return false;

    // Extract trimmed token
    const char *end = str + strlen(str);
    while (end > str && isspace((unsigned char)*(end - 1))) end--;
    size_t tok_len = (size_t)(end - str);

    if ((tok_len == 3 && (tolower((unsigned char)str[0]) == 'o' &&
                          tolower((unsigned char)str[1]) == 'f' &&
                          tolower((unsigned char)str[2]) == 'f')) ||
        (tok_len == 4 && (tolower((unsigned char)str[0]) == 'n' &&
                          tolower((unsigned char)str[1]) == 'o' &&
                          tolower((unsigned char)str[2]) == 'n' &&
                          tolower((unsigned char)str[3]) == 'e'))) {
        *out_cols = MDE_WRAP_COL_OFF;
        return true;
    }

    // Must be purely digits (no sign or trailing junk)
    for (size_t i = 0; i < tok_len; ++i) {
        if (!isdigit((unsigned char)str[i])) return false;
    }

    char *parse_end = NULL;
    long val = strtol(str, &parse_end, 10);
    if (parse_end != end) return false;

    if (val == 0) {
        *out_cols = MDE_WRAP_COL_OFF;
        return true;
    }
    if (val >= MDE_WRAP_COL_MIN && val <= MDE_WRAP_COL_MAX) {
        *out_cols = (int)val;
        return true;
    }

    return false;
}

/** @brief Toggle a Markdown task checkbox (`- [ ]` <-> `- [x]`) or turn line into a task. */
void editor_toggle_checkbox(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t len = buffer_line_len(&ed->buffer, ed->cursor_row);

    bool in_code = false;
    char lang[32] = {0};
    MdParsedLine pl;
    md_parse_line(line, len, &in_code, lang, sizeof(lang), &pl);

    uint64_t group = buffer_next_group(&ed->buffer);

    if (pl.is_task) {
        // Toggle [ ] <-> [x]
        size_t b_start = pl.task_bracket_start;
        if (b_start + 2 < len) {
            char replace_char = pl.task_checked ? ' ' : 'x';
            char new_box[4];
            snprintf(new_box, sizeof(new_box), "[%c]", replace_char);
            buffer_delete_range(&ed->buffer, ed->cursor_row, b_start, 3, group);
            buffer_insert_string(&ed->buffer, ed->cursor_row, b_start, new_box, 3, group);
            editor_set_status(ed, "Toggled checkbox: %s", pl.task_checked ? "Unchecked" : "Checked");
        }
    } else if (pl.is_list_item) {
        // Convert list item into a task item
        size_t prefix_len = 0;
        char bullet[16] = {0};
        bool is_t = false, t_chk = false;
        if (md_detect_list(line, len, &prefix_len, bullet, sizeof(bullet), &is_t, &t_chk)) {
            buffer_insert_string(&ed->buffer, ed->cursor_row, prefix_len, "[ ] ", 4, group);
            if (ed->cursor_col >= prefix_len) ed->cursor_col += 4;
            editor_set_status(ed, "Converted to Task Checkbox: [ ]");
        }
    } else {
        // Add "- [ ] " at current indentation
        size_t indent = 0;
        while (indent < len && (line[indent] == ' ' || line[indent] == '\t')) indent++;
        buffer_insert_string(&ed->buffer, ed->cursor_row, indent, "- [ ] ", 6, group);
        ed->cursor_col += 6;
        editor_set_status(ed, "Created new Task: - [ ]");
    }

    md_parsed_line_free(&pl);
    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->rendered_dirty = true;
}

/** @brief Wrap the active single-line selection with marker, or insert paired markers. */
void editor_wrap_format(Editor *ed, const char *marker) {
    if (!ed || !marker) return;
    editor_touch_key(ed);
    uint64_t group = buffer_next_group(&ed->buffer);
    size_t mlen = strlen(marker);

    size_t s_row, s_col, e_row, e_col;
    if (editor_get_selection(ed, &s_row, &s_col, &e_row, &e_col) && s_row == e_row) {
        // Wrap existing single-line selection
        buffer_insert_string(&ed->buffer, s_row, s_col, marker, mlen, group);
        buffer_insert_string(&ed->buffer, s_row, e_col + mlen, marker, mlen, group);
        ed->cursor_col = e_col + 2 * mlen;
        ed->mark_active = false;
    } else {
        // Insert marker twice (opening and closing), leaving cursor in the middle
        char both[32];
        snprintf(both, sizeof(both), "%s%s", marker, marker);
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, both, mlen * 2, group);
        ed->cursor_col += mlen;
    }

    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->rendered_dirty = true;
}

/**
 * @brief Inspect a line to determine if it can participate in paragraph reflow and extract its prefix.
 */
static bool is_reflowable_line(const char *line, size_t len, bool in_code_block,
                               char *out_prefix, size_t prefix_cap, size_t *out_content_start) {
    if (in_code_block || !line || len == 0) return false;

    size_t indent = 0;
    while (indent < len && (line[indent] == ' ' || line[indent] == '\t')) indent++;
    if (indent >= len) return false; // Whitespace-only line

    // Do not reflow headings, fences, hrules, tables, or list items
    if (line[indent] == '#' || line[indent] == '|') return false;
    if (indent + 3 <= len &&
        ((line[indent] == '`' && line[indent + 1] == '`' && line[indent + 2] == '`') ||
         (line[indent] == '~' && line[indent + 1] == '~' && line[indent + 2] == '~'))) {
        return false;
    }
    if (md_is_hrule(line, len)) return false;
    if (md_detect_list(line, len, NULL, NULL, 0, NULL, NULL)) return false;

    size_t prefix_end = indent;
    if (line[indent] == '>') {
        size_t bq = indent;
        while (bq < len && (line[bq] == '>' || line[bq] == ' ')) bq++;
        if (bq >= len) return false; // Empty blockquote line separates paragraphs
        prefix_end = bq;
    }

    if (out_prefix && prefix_cap > 0) {
        size_t copy_len = prefix_end < prefix_cap - 1 ? prefix_end : prefix_cap - 1;
        memcpy(out_prefix, line, copy_len);
        out_prefix[copy_len] = '\0';
    }
    if (out_content_start) *out_content_start = prefix_end;
    return true;
}

/** @brief Reflow the contiguous Markdown paragraph around cursor_row to ed->wrap_col. */
bool editor_reflow_paragraph(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t total = buffer_line_count(&ed->buffer);
    if (ed->cursor_row >= total) return false;

    // Determine fenced code-block state up to cursor_row
    bool in_code = false;
    char lang[32] = {0};
    for (size_t r = 0; r < ed->cursor_row; ++r) {
        MdParsedLine tmp;
        md_parse_line(buffer_line(&ed->buffer, r), buffer_line_len(&ed->buffer, r),
                      &in_code, lang, sizeof(lang), &tmp);
        md_parsed_line_free(&tmp);
    }

    char prefix[128] = {0};
    size_t cur_content = 0;
    if (!is_reflowable_line(buffer_line(&ed->buffer, ed->cursor_row),
                            buffer_line_len(&ed->buffer, ed->cursor_row),
                            in_code, prefix, sizeof(prefix), &cur_content)) {
        editor_set_status(ed, "Cannot reflow this block");
        return false;
    }

    // Expand upward while lines share the same prefix and are reflowable
    size_t start_row = ed->cursor_row;
    while (start_row > 0) {
        char pbuf[128] = {0};
        if (!is_reflowable_line(buffer_line(&ed->buffer, start_row - 1),
                                buffer_line_len(&ed->buffer, start_row - 1),
                                false, pbuf, sizeof(pbuf), NULL) ||
            strcmp(pbuf, prefix) != 0) {
            break;
        }
        start_row--;
    }

    // Expand downward while lines share the same prefix and are reflowable
    size_t end_row = ed->cursor_row;
    while (end_row + 1 < total) {
        char pbuf[128] = {0};
        if (!is_reflowable_line(buffer_line(&ed->buffer, end_row + 1),
                                buffer_line_len(&ed->buffer, end_row + 1),
                                false, pbuf, sizeof(pbuf), NULL) ||
            strcmp(pbuf, prefix) != 0) {
            break;
        }
        end_row++;
    }

    // Collect normalized space-separated words from [start_row..end_row]
    size_t raw_cap = 256;
    size_t raw_len = 0;
    char *joined = (char *)malloc(raw_cap);
    if (!joined) return false;
    joined[0] = '\0';

    size_t prefix_len = strlen(prefix);
    for (size_t r = start_row; r <= end_row; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t llen = buffer_line_len(&ed->buffer, r);
        size_t pos = prefix_len;
        while (pos < llen) {
            while (pos < llen && (l[pos] == ' ' || l[pos] == '\t')) pos++;
            if (pos >= llen) break;
            size_t wstart = pos;
            while (pos < llen && l[pos] != ' ' && l[pos] != '\t') pos++;
            size_t wlen = pos - wstart;
            size_t need = raw_len + (raw_len > 0 ? 1 : 0) + wlen + 1;
            if (need > raw_cap) {
                while (raw_cap < need) raw_cap *= 2;
                char *nj = (char *)realloc(joined, raw_cap);
                if (!nj) { free(joined); return false; }
                joined = nj;
            }
            if (raw_len > 0) joined[raw_len++] = ' ';
            memcpy(joined + raw_len, l + wstart, wlen);
            raw_len += wlen;
            joined[raw_len] = '\0';
        }
    }

    if (raw_len == 0) {
        free(joined);
        return false;
    }

    int limit = ed->wrap_col > 0 ? ed->wrap_col : MDE_WRAP_COL_DEFAULT;
    int prefix_vcol = utf8_visual_width(prefix, prefix_len);

    // Build wrapped output lines array
    size_t out_cap = 16, out_count = 0;
    char **out_lines = (char **)malloc(out_cap * sizeof(char *));
    if (!out_lines) { free(joined); return false; }

    size_t idx = 0;
    while (idx < raw_len) {
        size_t line_cap = (size_t)limit * 4 + prefix_len + 64;
        char *buf_line = (char *)malloc(line_cap);
        if (!buf_line) break;

        memcpy(buf_line, prefix, prefix_len);
        size_t blen = prefix_len;
        buf_line[blen] = '\0';
        int cur_w = prefix_vcol;
        bool first_word = true;

        while (idx < raw_len) {
            while (idx < raw_len && joined[idx] == ' ') idx++;
            if (idx >= raw_len) break;
            size_t wstart = idx;
            while (idx < raw_len && joined[idx] != ' ') idx++;
            size_t wlen = idx - wstart;
            int ww = utf8_visual_width(joined + wstart, wlen);
            int add_w = (first_word ? 0 : 1) + ww;

            if (!first_word && cur_w + add_w > limit) {
                idx = wstart; // Put word on next line
                break;
            }
            size_t need = blen + (first_word ? 0 : 1) + wlen + 1;
            if (need > line_cap) {
                line_cap = need * 2;
                char *nb = (char *)realloc(buf_line, line_cap);
                if (!nb) break;
                buf_line = nb;
            }
            if (!first_word) buf_line[blen++] = ' ';
            memcpy(buf_line + blen, joined + wstart, wlen);
            blen += wlen;
            buf_line[blen] = '\0';
            cur_w += add_w;
            first_word = false;
        }

        if (out_count >= out_cap) {
            out_cap *= 2;
            char **nl = (char **)realloc(out_lines, out_cap * sizeof(char *));
            if (!nl) { free(buf_line); break; }
            out_lines = nl;
        }
        out_lines[out_count++] = buf_line;
    }

    free(joined);
    if (out_count == 0) {
        free(out_lines);
        return false;
    }

    // Replace [start_row..end_row] atomically inside one undo group
    size_t old_count = end_row - start_row + 1;
    uint64_t group = buffer_next_group(&ed->buffer);

    for (size_t r = end_row; r > start_row; --r) {
        size_t rlen = buffer_line_len(&ed->buffer, r);
        if (rlen > 0) buffer_delete_range(&ed->buffer, r, 0, rlen, group);
        buffer_join_lines(&ed->buffer, r - 1, group);
    }

    buffer_replace_line(&ed->buffer, start_row, out_lines[0], strlen(out_lines[0]), group);
    for (size_t i = 1; i < out_count; ++i) {
        size_t prev_row = start_row + i - 1;
        buffer_split_line(&ed->buffer, prev_row, buffer_line_len(&ed->buffer, prev_row), group);
        buffer_insert_string(&ed->buffer, start_row + i, 0, out_lines[i], strlen(out_lines[i]), group);
    }

    for (size_t i = 0; i < out_count; ++i) free(out_lines[i]);
    free(out_lines);

    ed->cursor_row = start_row + out_count - 1;
    ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(buffer_line(&ed->buffer, ed->cursor_row), ed->cursor_col);
    ed->mark_active = false;
    ed->rendered_dirty = true;
    editor_set_status(ed, "Reflowed paragraph (%zu -> %zu line%s)",
                      old_count, out_count, out_count == 1 ? "" : "s");
    return true;
}

/** @brief Undo the most recent atomic editing group and update cursor/scroll state. */
void editor_undo(Editor *ed) {
    if (!ed) return;
    if (buffer_undo(&ed->buffer, &ed->cursor_row, &ed->cursor_col)) {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
        ed->rendered_dirty = true;
        editor_set_status(ed, "Undo performed");
    } else {
        editor_set_status(ed, "Already at oldest change");
    }
}

/** @brief Redo the most recently undone atomic editing group and update cursor/scroll state. */
void editor_redo(Editor *ed) {
    if (!ed) return;
    if (buffer_redo(&ed->buffer, &ed->cursor_row, &ed->cursor_col)) {
        const char *l = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
        ed->rendered_dirty = true;
        editor_set_status(ed, "Redo performed");
    } else {
        editor_set_status(ed, "Already at newest change");
    }
}

/** @brief Return true if the current editor buffer has unsaved changes. */
bool editor_has_unsaved_changes(const Editor *ed) {
    return (ed && ed->buffer.is_dirty);
}

/** @brief Save the current buffer to opt_path (or existing file_path when NULL). */
bool editor_save(Editor *ed, const char *opt_path) {
    if (!ed) return false;
    const char *save_target = opt_path ? opt_path : ed->buffer.file_path;
    if (save_target && save_target[0] && !ed->backup_made) {
        backup_create_tilde(save_target);
        ed->backup_made = true;
    }
    if (buffer_save(&ed->buffer, opt_path)) {
        if (ed->buffer.file_path) {
            backup_remove_autosave(ed->buffer.file_path);
        }
        editor_set_status(ed, "Saved %zu lines to %s", buffer_line_count(&ed->buffer), ed->buffer.file_path);
        return true;
    } else {
        editor_set_status(ed, "Error saving file!");
        return false;
    }
}

/** @brief Load path from disk into the editor buffer and reset cursor, mark, and scroll state. */
bool editor_open(Editor *ed, const char *path) {
    if (!ed || !path) return false;
    if (buffer_load(&ed->buffer, path)) {
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->target_visual_col = 0;
        ed->scroll_row = 0;
        ed->scroll_col = 0;
        ed->mark_active = false;
        ed->search_active = false;
        ed->rendered_dirty = true;
        ed->backup_made = false;
        editor_touch_key(ed);
        editor_set_status(ed, "Opened %s", path);
        return true;
    } else {
        editor_set_status(ed, "Cannot open %s", path);
        return false;
    }
}

/* =========================================================================
 * Hypertext File Linking & Navigation
 * ========================================================================= */

/** @brief Return true if url begins with an external URI scheme (http, https, mailto, etc.). */
static bool is_external_url(const char *url) {
    if (!url || !url[0]) return false;
    if (strncmp(url, "http://", 7) == 0 ||
        strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "mailto:", 7) == 0 ||
        strncmp(url, "ftp://", 6) == 0 ||
        strncmp(url, "tel:", 4) == 0 ||
        strncmp(url, "data:", 5) == 0 ||
        strncmp(url, "//", 2) == 0) {
        return true;
    }
    return false;
}

/**
 * @brief Normalize an absolute path lexically (collapsing '.' and '..' components).
 */
static void normalize_abs_path(const char *in_path, char *out_path, size_t out_cap) {
    if (!in_path || !out_path || out_cap == 0) return;

    char abs_buf[PATH_MAX * 2];
    if (in_path[0] == '/') {
        snprintf(abs_buf, sizeof(abs_buf), "%s", in_path);
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(abs_buf, sizeof(abs_buf), "%s/%s", cwd, in_path);
        } else {
            snprintf(abs_buf, sizeof(abs_buf), "/%s", in_path);
        }
    }

    char *parts[256];
    size_t part_count = 0;
    char *copy = strdup(abs_buf);
    if (!copy) {
        snprintf(out_path, out_cap, "%s", abs_buf);
        return;
    }

    char *saveptr = NULL;
    char *tok = strtok_r(copy, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            // Skip
        } else if (strcmp(tok, "..") == 0) {
            if (part_count > 0) part_count--;
        } else if (part_count < 256) {
            parts[part_count++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    size_t pos = 0;
    if (part_count == 0) {
        snprintf(out_path, out_cap, "/");
    } else {
        out_path[0] = '\0';
        for (size_t i = 0; i < part_count; ++i) {
            int n = snprintf(out_path + pos, out_cap - pos, "/%s", parts[i]);
            if (n < 0 || pos + (size_t)n >= out_cap) break;
            pos += (size_t)n;
        }
    }
    free(copy);
}

/** @brief Extract the parent directory of file_path (or CWD when untitled). */
static void get_base_dir(const char *file_path, char *out_dir, size_t out_cap) {
    if (!out_dir || out_cap == 0) return;
    if (file_path && file_path[0]) {
        char norm[PATH_MAX];
        normalize_abs_path(file_path, norm, sizeof(norm));
        char *slash = strrchr(norm, '/');
        if (slash == norm) {
            snprintf(out_dir, out_cap, "/");
        } else if (slash) {
            *slash = '\0';
            snprintf(out_dir, out_cap, "%s", norm);
        } else {
            snprintf(out_dir, out_cap, "/");
        }
    } else {
        if (!getcwd(out_dir, out_cap)) {
            snprintf(out_dir, out_cap, ".");
        }
    }
}

/** @brief Compute a portable relative path from from_file's directory to to_file. */
bool editor_make_relative_path(const char *from_file, const char *to_file,
                               char *out_rel, size_t out_cap) {
    if (!to_file || !to_file[0] || !out_rel || out_cap == 0) return false;

    char from_dir[PATH_MAX];
    char to_abs[PATH_MAX];
    get_base_dir(from_file, from_dir, sizeof(from_dir));
    normalize_abs_path(to_file, to_abs, sizeof(to_abs));

    char *from_copy = strdup(from_dir);
    char *to_copy = strdup(to_abs);
    if (!from_copy || !to_copy) {
        free(from_copy);
        free(to_copy);
        snprintf(out_rel, out_cap, "%s", to_file);
        return true;
    }

    char *from_parts[256];
    char *to_parts[256];
    size_t nf = 0, nt = 0;
    char *sp1 = NULL, *sp2 = NULL;

    for (char *t = strtok_r(from_copy, "/", &sp1); t && nf < 256; t = strtok_r(NULL, "/", &sp1)) {
        if (t[0]) from_parts[nf++] = t;
    }
    for (char *t = strtok_r(to_copy, "/", &sp2); t && nt < 256; t = strtok_r(NULL, "/", &sp2)) {
        if (t[0]) to_parts[nt++] = t;
    }

    size_t common = 0;
    while (common < nf && common < nt && strcmp(from_parts[common], to_parts[common]) == 0) {
        common++;
    }

    size_t pos = 0;
    out_rel[0] = '\0';
    for (size_t i = common; i < nf; ++i) {
        int n = snprintf(out_rel + pos, out_cap - pos, "%s..", pos > 0 ? "/" : "");
        if (n < 0 || pos + (size_t)n >= out_cap) break;
        pos += (size_t)n;
    }
    for (size_t i = common; i < nt; ++i) {
        int n = snprintf(out_rel + pos, out_cap - pos, "%s%s", pos > 0 ? "/" : "", to_parts[i]);
        if (n < 0 || pos + (size_t)n >= out_cap) break;
        pos += (size_t)n;
    }

    if (out_rel[0] == '\0') {
        snprintf(out_rel, out_cap, ".");
    }

    free(from_copy);
    free(to_copy);
    return true;
}

/** @brief Convert heading text into a lowercase hyphenated anchor slug. */
static void make_heading_slug(const char *text, size_t len, char *out_slug, size_t cap) {
    if (!out_slug || cap == 0) return;
    size_t pos = 0;
    bool prev_dash = true;
    for (size_t i = 0; i < len && pos + 1 < cap; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c)) {
            out_slug[pos++] = (char)tolower(c);
            prev_dash = false;
        } else if ((c == ' ' || c == '-' || c == '_') && !prev_dash) {
            out_slug[pos++] = '-';
            prev_dash = true;
        }
    }
    while (pos > 0 && out_slug[pos - 1] == '-') pos--;
    out_slug[pos] = '\0';
}

/** @brief Scan the active buffer for a heading matching anchor slug and jump to it. */
static bool jump_to_heading_anchor(Editor *ed, const char *anchor) {
    if (!ed || !anchor || !anchor[0]) return false;

    char target_slug[128];
    make_heading_slug(anchor, strlen(anchor), target_slug, sizeof(target_slug));
    if (!target_slug[0]) return false;

    bool in_code = false;
    char lang[32] = {0};
    size_t total = buffer_line_count(&ed->buffer);
    for (size_t r = 0; r < total; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine pl;
        md_parse_line(l, len, &in_code, lang, sizeof(lang), &pl);
        if (pl.header_level > 0) {
            char heading_txt[256];
            size_t hpos = 0;
            for (size_t s = 0; s < pl.span_count; ++s) {
                if (md_is_header_marker(pl.spans[s].type) ||
                    pl.spans[s].type == MD_BOLD_MARKER ||
                    pl.spans[s].type == MD_ITALIC_MARKER ||
                    pl.spans[s].type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                size_t slen = pl.spans[s].end - pl.spans[s].start;
                if (hpos + slen >= sizeof(heading_txt) - 1) slen = sizeof(heading_txt) - 1 - hpos;
                if (slen > 0) {
                    memcpy(heading_txt + hpos, l + pl.spans[s].start, slen);
                    hpos += slen;
                }
            }
            heading_txt[hpos] = '\0';
            char slug[128];
            make_heading_slug(heading_txt, hpos, slug, sizeof(slug));
            md_parsed_line_free(&pl);
            if (strcmp(slug, target_slug) == 0) {
                ed->cursor_row = r;
                ed->cursor_col = 0;
                ed->target_visual_col = 0;
                return true;
            }
        } else {
            md_parsed_line_free(&pl);
        }
    }
    return false;
}

/** @brief Extract the Markdown link URL and optional `#anchor` at the cursor or on the active line. */
bool editor_extract_link_at_cursor(const Editor *ed,
                                   char *out_url, size_t url_cap,
                                   char *out_anchor, size_t anchor_cap) {
    if (!ed || !out_url || url_cap == 0) return false;
    out_url[0] = '\0';
    if (out_anchor && anchor_cap > 0) out_anchor[0] = '\0';

    size_t row = ed->cursor_row;
    if (row >= buffer_line_count(&ed->buffer)) return false;

    // Determine code-block state up to cursor_row
    bool in_code = false;
    char lang[32] = {0};
    for (size_t r = 0; r < row; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine tmp;
        md_parse_line(l, len, &in_code, lang, sizeof(lang), &tmp);
        md_parsed_line_free(&tmp);
    }

    const char *line = buffer_line(&ed->buffer, row);
    size_t len = buffer_line_len(&ed->buffer, row);
    MdParsedLine pl;
    md_parse_line(line, len, &in_code, lang, sizeof(lang), &pl);

    if (pl.is_code_block) {
        md_parsed_line_free(&pl);
        return false;
    }

    const char *best_url = NULL;
    size_t best_url_len = 0;
    const char *first_url = NULL;
    size_t first_url_len = 0;

    for (size_t s = 0; s < pl.span_count; ++s) {
        // Skip images (![alt](url))
        if (pl.spans[s].type == MD_IMAGE_MARKER) {
            s++;
            while (s < pl.span_count &&
                   (pl.spans[s].type == MD_LINK_BRACKET ||
                    pl.spans[s].type == MD_LINK_TEXT ||
                    pl.spans[s].type == MD_LINK_URL_PAREN ||
                    pl.spans[s].type == MD_LINK_URL)) {
                bool end_p = (pl.spans[s].type == MD_LINK_URL_PAREN && line[pl.spans[s].start] == ')');
                if (end_p) break;
                s++;
            }
            continue;
        }

        if (pl.spans[s].type == MD_LINK_BRACKET && line[pl.spans[s].start] == '[') {
            size_t link_start = pl.spans[s].start;
            size_t link_end = pl.spans[s].end;
            const char *url_ptr = NULL;
            size_t url_len = 0;
            size_t k = s + 1;
            while (k < pl.span_count &&
                   (pl.spans[k].type == MD_LINK_BRACKET ||
                    pl.spans[k].type == MD_LINK_TEXT ||
                    pl.spans[k].type == MD_LINK_URL_PAREN ||
                    pl.spans[k].type == MD_LINK_URL)) {
                link_end = pl.spans[k].end;
                if (pl.spans[k].type == MD_LINK_URL) {
                    url_ptr = line + pl.spans[k].start;
                    url_len = pl.spans[k].end - pl.spans[k].start;
                }
                bool end_p = (pl.spans[k].type == MD_LINK_URL_PAREN && line[pl.spans[k].start] == ')');
                k++;
                if (end_p) break;
            }
            if (url_ptr && url_len > 0) {
                if (!first_url) {
                    first_url = url_ptr;
                    first_url_len = url_len;
                }
                if (ed->cursor_col >= link_start && ed->cursor_col <= link_end) {
                    best_url = url_ptr;
                    best_url_len = url_len;
                    break;
                }
            }
            s = k - 1;
        }
    }

    const char *chosen = best_url ? best_url : first_url;
    size_t chosen_len = best_url ? best_url_len : first_url_len;

    if (!chosen || chosen_len == 0) {
        md_parsed_line_free(&pl);
        return false;
    }

    // Trim surrounding whitespace
    while (chosen_len > 0 && isspace((unsigned char)chosen[0])) { chosen++; chosen_len--; }
    while (chosen_len > 0 && isspace((unsigned char)chosen[chosen_len - 1])) { chosen_len--; }

    // Split optional #anchor fragment
    const char *hash = (const char *)memchr(chosen, '#', chosen_len);
    if (hash) {
        size_t path_part_len = (size_t)(hash - chosen);
        size_t anc_len = chosen_len - path_part_len - 1;
        if (path_part_len >= url_cap) path_part_len = url_cap - 1;
        memcpy(out_url, chosen, path_part_len);
        out_url[path_part_len] = '\0';

        if (out_anchor && anchor_cap > 0) {
            if (anc_len >= anchor_cap) anc_len = anchor_cap - 1;
            memcpy(out_anchor, hash + 1, anc_len);
            out_anchor[anc_len] = '\0';
        }
    } else {
        if (chosen_len >= url_cap) chosen_len = url_cap - 1;
        memcpy(out_url, chosen, chosen_len);
        out_url[chosen_len] = '\0';
    }

    md_parsed_line_free(&pl);
    return true;
}

/** @brief Resolve a local relative or absolute link target against the active file's directory. */
bool editor_resolve_link_path(const Editor *ed, const char *link_target,
                              char *out_path, size_t out_cap) {
    if (!ed || !link_target || !out_path || out_cap == 0) return false;
    if (is_external_url(link_target)) return false;

    if (!link_target[0]) {
        if (ed->buffer.file_path && ed->buffer.file_path[0]) {
            normalize_abs_path(ed->buffer.file_path, out_path, out_cap);
        } else {
            out_path[0] = '\0';
        }
        return true;
    }

    char combined[PATH_MAX * 2];
    if (link_target[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", link_target);
    } else {
        char base_dir[PATH_MAX];
        get_base_dir(ed->buffer.file_path, base_dir, sizeof(base_dir));
        if (strcmp(base_dir, "/") == 0) {
            snprintf(combined, sizeof(combined), "/%s", link_target);
        } else {
            snprintf(combined, sizeof(combined), "%s/%s", base_dir, link_target);
        }
    }

    normalize_abs_path(combined, out_path, out_cap);
    return true;
}

/** @brief Derive a default link label from target_path's first H1 heading or filename stem. */
static void derive_link_label(const char *target_path, char *out_label, size_t cap) {
    if (!out_label || cap == 0) return;
    out_label[0] = '\0';

    // Try reading the first H1 heading if the target file exists
    FILE *f = fopen(target_path, "rb");
    if (f) {
        char line[512];
        bool in_code = false;
        char lang[32] = {0};
        for (int i = 0; i < 64 && fgets(line, sizeof(line), f); ++i) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            MdParsedLine pl;
            md_parse_line(line, l, &in_code, lang, sizeof(lang), &pl);
            if (pl.header_level == 1 && pl.span_count > 1) {
                size_t pos = 0;
                for (size_t s = 0; s < pl.span_count; ++s) {
                    if (md_is_header_marker(pl.spans[s].type)) continue;
                    size_t slen = pl.spans[s].end - pl.spans[s].start;
                    if (pos + slen >= cap - 1) slen = cap - 1 - pos;
                    if (slen > 0) {
                        memcpy(out_label + pos, line + pl.spans[s].start, slen);
                        pos += slen;
                    }
                }
                out_label[pos] = '\0';
                md_parsed_line_free(&pl);
                if (out_label[0]) {
                    fclose(f);
                    return;
                }
            }
            md_parsed_line_free(&pl);
        }
        fclose(f);
    }

    // Fallback to basename without extension
    const char *slash = strrchr(target_path, '/');
    const char *base = slash ? (slash + 1) : target_path;
    snprintf(out_label, cap, "%s", base);
    char *dot = strrchr(out_label, '.');
    if (dot && dot != out_label) *dot = '\0';
    if (!out_label[0]) snprintf(out_label, cap, "link");
}

/** @brief Insert a relative Markdown hyperlink `[Label](relative/path)` at the cursor. */
bool editor_insert_file_link(Editor *ed, const char *target_path) {
    if (!ed || !target_path || !target_path[0]) return false;
    editor_touch_key(ed);

    char rel_path[PATH_MAX];
    if (!editor_make_relative_path(ed->buffer.file_path, target_path, rel_path, sizeof(rel_path))) {
        return false;
    }

    char label[256] = {0};
    uint64_t group = buffer_next_group(&ed->buffer);

    size_t sel_len = 0;
    char *sel_text = editor_get_selected_text(ed, &sel_len);
    if (sel_text && sel_len > 0) {
        // Use single-line portion of selected text as label
        size_t copy_len = sel_len;
        const char *nl = (const char *)memchr(sel_text, '\n', sel_len);
        if (nl) copy_len = (size_t)(nl - sel_text);
        if (copy_len >= sizeof(label)) copy_len = sizeof(label) - 1;
        memcpy(label, sel_text, copy_len);
        label[copy_len] = '\0';
        free(sel_text);
        editor_delete_selection(ed, group);
    } else {
        free(sel_text);
        derive_link_label(target_path, label, sizeof(label));
    }

    char md_link[PATH_MAX + 300];
    int n = snprintf(md_link, sizeof(md_link), "[%s](%s)", label, rel_path);
    if (n <= 0) return false;
    size_t link_len = (size_t)n < sizeof(md_link) ? (size_t)n : sizeof(md_link) - 1;

    buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, md_link, link_len, group);
    ed->cursor_col += link_len;
    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->rendered_dirty = true;
    editor_set_status(ed, "Linked: [%s](%s)", label, rel_path);
    return true;
}

/** @brief Push the current file path and cursor position onto the link navigation history stack. */
static void push_link_history(Editor *ed) {
    if (!ed || !ed->buffer.file_path || !ed->buffer.file_path[0]) return;

    if (ed->link_history_count >= MDE_LINK_HISTORY_MAX) {
        free(ed->link_history[0].file_path);
        memmove(&ed->link_history[0], &ed->link_history[1],
                (MDE_LINK_HISTORY_MAX - 1) * sizeof(LinkHistoryEntry));
        ed->link_history_count = MDE_LINK_HISTORY_MAX - 1;
    }

    char norm[PATH_MAX];
    normalize_abs_path(ed->buffer.file_path, norm, sizeof(norm));
    ed->link_history[ed->link_history_count].file_path = strdup(norm);
    ed->link_history[ed->link_history_count].cursor_row = ed->cursor_row;
    ed->link_history[ed->link_history_count].cursor_col = ed->cursor_col;
    ed->link_history_count++;
}

/** @brief Return true if following the link at cursor will load a different local file. */
bool editor_link_will_switch_file(const Editor *ed, char *out_resolved, size_t cap) {
    if (out_resolved && cap > 0) out_resolved[0] = '\0';
    if (!ed) return false;

    char url[PATH_MAX] = {0};
    char anchor[128] = {0};
    if (!editor_extract_link_at_cursor(ed, url, sizeof(url), anchor, sizeof(anchor))) {
        return false;
    }

    // Pure #anchor or external URL will not load another file
    if (!url[0] || is_external_url(url)) {
        return false;
    }

    char resolved[PATH_MAX];
    if (!editor_resolve_link_path(ed, url, resolved, sizeof(resolved))) {
        return false;
    }

    // Same-file link (with or without #anchor) does not switch files
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char cur_norm[PATH_MAX];
        normalize_abs_path(ed->buffer.file_path, cur_norm, sizeof(cur_norm));
        if (strcmp(cur_norm, resolved) == 0) {
            return false;
        }
    }

    if (out_resolved && cap > 0) {
        snprintf(out_resolved, cap, "%s", resolved);
    }
    return true;
}

/** @brief Follow a local Markdown file link or `#heading` anchor under the cursor. */
bool editor_follow_link(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    char url[PATH_MAX] = {0};
    char anchor[128] = {0};
    if (!editor_extract_link_at_cursor(ed, url, sizeof(url), anchor, sizeof(anchor))) {
        editor_set_status(ed, "No link found on current line");
        return false;
    }

    if (is_external_url(url)) {
        editor_set_status(ed, "External URL: %s", url);
        return false;
    }

    // Same-document #anchor jump
    if (!url[0] && anchor[0]) {
        if (jump_to_heading_anchor(ed, anchor)) {
            editor_set_status(ed, "Jumped to #%s", anchor);
            return true;
        }
        editor_set_status(ed, "Heading '#%s' not found", anchor);
        return false;
    }

    char resolved[PATH_MAX];
    if (!editor_resolve_link_path(ed, url, resolved, sizeof(resolved))) {
        editor_set_status(ed, "Cannot resolve link: %s", url);
        return false;
    }

    // Check if link points to the same file
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char cur_norm[PATH_MAX];
        normalize_abs_path(ed->buffer.file_path, cur_norm, sizeof(cur_norm));
        if (strcmp(cur_norm, resolved) == 0) {
            if (anchor[0] && jump_to_heading_anchor(ed, anchor)) {
                editor_set_status(ed, "Jumped to #%s", anchor);
            } else {
                editor_set_status(ed, "Already viewing %s", url);
            }
            return true;
        }
    }

    push_link_history(ed);

    struct stat st;
    if (stat(resolved, &st) == 0 && S_ISREG(st.st_mode)) {
        if (!buffer_load(&ed->buffer, resolved)) {
            editor_set_status(ed, "Cannot open linked file: %s", url);
            return false;
        }
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->target_visual_col = 0;
        ed->scroll_row = 0;
        ed->scroll_col = 0;
        ed->mark_active = false;
        ed->rendered_dirty = true;
        if (anchor[0]) {
            jump_to_heading_anchor(ed, anchor);
        }
        editor_set_status(ed, "Opened link: %s (F8/M-- to go back)", url);
        return true;
    }

    // Target does not exist yet: open a new buffer at that path so user can author it
    buffer_free(&ed->buffer);
    buffer_init(&ed->buffer);
    ed->buffer.file_path = strdup(resolved);
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->mark_active = false;
    ed->rendered_dirty = true;
    editor_set_status(ed, "New linked file: %s (F8/M-- to go back)", url);
    return true;
}

/** @brief Pop link history, reload the previous file, and restore saved cursor coordinates. */
bool editor_navigate_back(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    if (ed->link_history_count == 0) {
        editor_set_status(ed, "No previous file in link history");
        return false;
    }

    LinkHistoryEntry entry = ed->link_history[--ed->link_history_count];
    ed->link_history[ed->link_history_count].file_path = NULL;

    bool loaded = buffer_load(&ed->buffer, entry.file_path);
    if (!loaded) {
        buffer_free(&ed->buffer);
        buffer_init(&ed->buffer);
        ed->buffer.file_path = strdup(entry.file_path);
    }

    size_t max_row = buffer_line_count(&ed->buffer);
    ed->cursor_row = (entry.cursor_row < max_row) ? entry.cursor_row : (max_row > 0 ? max_row - 1 : 0);
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    ed->cursor_col = (entry.cursor_col <= line_len) ? entry.cursor_col : line_len;
    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->scroll_col = 0;
    ed->mark_active = false;
    ed->rendered_dirty = true;

    editor_set_status(ed, "Back to %s", entry.file_path);
    free(entry.file_path);
    return true;
}

/** @brief Return the current monotonic clock time in milliseconds. */
uint64_t editor_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

/** @brief Update ed->last_key_time to the current monotonic timestamp. */
void editor_touch_key(Editor *ed) {
    if (!ed) return;
    ed->last_key_time = editor_get_time_ms();
    if (ed->buffer.file_path && ed->buffer.is_dirty) {
        backup_create_autosave(ed->buffer.file_path, &ed->buffer);
    }
}

/** @brief Return true when the cursor should be shown (always true for steady cursor). */
bool editor_cursor_is_visible(const Editor *ed) {
    (void)ed;
    return true; // Steady, non-flashing cursor
}

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

/** @brief Clamp and synchronize editor vertical/horizontal scroll and preview scroll offsets. */
void editor_update_scroll(Editor *ed, int view_rows) {
    if (!ed || view_rows <= 0) return;

    size_t total_lines = buffer_line_count(&ed->buffer);

    // Clamp scroll_row when terminal grows taller so content fills the viewport
    if (total_lines <= (size_t)view_rows) {
        ed->scroll_row = 0;
    } else if (ed->scroll_row + (size_t)view_rows > total_lines) {
        ed->scroll_row = total_lines - (size_t)view_rows;
    }

    if (ed->cursor_row < ed->scroll_row) {
        ed->scroll_row = ed->cursor_row;
    }
    if (ed->cursor_row >= ed->scroll_row + (size_t)view_rows) {
        ed->scroll_row = ed->cursor_row - (size_t)view_rows + 1;
    }

    // Compute actual visible content width in editor pane (accounting for gutter)
    int digits = 1;
    size_t tmp_lines = total_lines;
    while (tmp_lines >= 10) {
        digits++;
        tmp_lines /= 10;
    }
    if (digits < 3) digits = 3;
    int gutter_w = digits + 4;
    int editor_w = ed->editor_width - gutter_w;
    if (editor_w < 1) editor_w = 1;

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    int cursor_vcol = utf8_byte_to_col(line, ed->cursor_col);

    // If terminal widened enough for cursor to fit without horizontal scroll, reset scroll_col
    if (cursor_vcol < editor_w && ed->scroll_col > 0) {
        ed->scroll_col = 0;
    }
    if (cursor_vcol < (int)ed->scroll_col) {
        ed->scroll_col = cursor_vcol > 0 ? (size_t)cursor_vcol : 0;
    }
    if (cursor_vcol >= (int)ed->scroll_col + editor_w) {
        ed->scroll_col = (size_t)(cursor_vcol - editor_w + 1);
    }

    // Synchronize preview scroll with editor cursor row / viewport
    if (ed->view_mode == VIEW_PREVIEW && ed->rendered_doc.count > 0) {
        if (ed->rendered_doc.count <= (size_t)view_rows) {
            ed->preview_scroll_row = 0;
        } else if (ed->preview_scroll_row + (size_t)view_rows > ed->rendered_doc.count) {
            ed->preview_scroll_row = ed->rendered_doc.count - (size_t)view_rows;
        }
        size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->cursor_row);
        if (rend_idx < ed->preview_scroll_row) {
            ed->preview_scroll_row = rend_idx;
        }
        if (rend_idx >= ed->preview_scroll_row + (size_t)view_rows) {
            ed->preview_scroll_row = rend_idx - (size_t)view_rows + 1;
        }
    } else if (ed->view_mode == VIEW_SPLIT && ed->rendered_doc.count > 0) {
        size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->scroll_row);
        if (ed->rendered_doc.count <= (size_t)view_rows) {
            ed->preview_scroll_row = 0;
        } else if (rend_idx + (size_t)view_rows > ed->rendered_doc.count) {
            ed->preview_scroll_row = ed->rendered_doc.count - (size_t)view_rows;
        } else {
            ed->preview_scroll_row = rend_idx;
        }
    }

    if (total_lines > 1000) {
        size_t check_row = (ed->view_mode == VIEW_PREVIEW) ? ed->cursor_row : ed->scroll_row;
        if (check_row < ed->rendered_view_start + 25 ||
            check_row + 25 >= ed->rendered_view_end) {
            ed->rendered_dirty = true;
        }
    }
}
