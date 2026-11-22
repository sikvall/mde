#include "backup.h"
/**
 * @file editor.c
 * @brief Implementation of editor state management, navigation, and Markdown helpers.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "editor.h"
#include "config.h"
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
    ed->view_rows = 24;
    ed->quit_requested = false;
    ed->backup_made = false;
    ed->search_active = false;
    ed->mark_active = false;
    ed->mark_row = 0;
    ed->mark_col = 0;
    ed->clipboard = NULL;
    ed->clipboard_len = 0;
    ed->show_keybindings = true;
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

/** @brief Toggle visibility of bottom shortcut hint rows (F10 / F12 / Alt+K). */
void editor_toggle_keybindings(Editor *ed) {
    if (!ed) return;
    ed->show_keybindings = !ed->show_keybindings;
    if (ed->show_keybindings) {
        editor_set_status(ed, "Keybindings shown (press F10 to hide)");
    } else {
        editor_set_status(ed, "Keybindings hidden (press F10 to show)");
    }

    // Persist user preference to ~/.config/mde/config
    MdeConfig cfg;
    if (!config_load(&cfg)) {
        config_init_defaults(&cfg);
    }
    cfg.show_keybindings = ed->show_keybindings;
    config_save(&cfg);
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
    editor_touch_key(ed);
    size_t total = buffer_line_count(&ed->buffer);
    int vrows = ed->view_rows > 0 ? ed->view_rows : (ed->screen_rows > 3 ? (ed->screen_rows - 3) : 20);
    size_t normal_max = total > (size_t)vrows ? (total - (size_t)vrows) : 0;

    if (ed->view_mode == VIEW_SPLIT && ed->scroll_row > normal_max && ed->cursor_row + 1 >= total) {
        ed->scroll_row--;
    } else if (ed->cursor_row > 0) {
        ed->cursor_row--;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    }
}

/** @brief Move cursor down one row while preserving preferred visual column. */
void editor_move_down(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    size_t total = buffer_line_count(&ed->buffer);
    if (total == 0) return;
    if (ed->cursor_row + 1 < total) {
        ed->cursor_row++;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    } else if (ed->view_mode == VIEW_SPLIT && ed->rendered_doc.count > 0) {
        int vrows = ed->view_rows > 0 ? ed->view_rows : (ed->screen_rows > 3 ? (ed->screen_rows - 3) : 20);
        if (ed->rendered_doc.count > (size_t)vrows &&
            ed->preview_scroll_row + (size_t)vrows < ed->rendered_doc.count) {
            ed->scroll_row++;
        }
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
    editor_touch_key(ed);
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    size_t total = buffer_line_count(&ed->buffer);
    int view_rows = ed->screen_rows > 3 ? (ed->screen_rows - 3) : 20;
    size_t normal_max = total > (size_t)view_rows ? (total - (size_t)view_rows) : 0;

    if (ed->view_mode == VIEW_SPLIT && ed->scroll_row > normal_max) {
        if (ed->scroll_row > normal_max + step) {
            ed->scroll_row -= step;
        } else {
            ed->scroll_row = normal_max;
        }
    } else {
        if (ed->cursor_row > step) ed->cursor_row -= step;
        else ed->cursor_row = 0;
    }
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
}

/** @brief Move cursor down by one viewport page. */
void editor_move_page_down(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    size_t total = buffer_line_count(&ed->buffer);
    size_t max_row = total > 0 ? total - 1 : 0;
    if (ed->cursor_row + step <= max_row) {
        ed->cursor_row += step;
    } else if (ed->cursor_row < max_row) {
        ed->cursor_row = max_row;
    } else if (ed->view_mode == VIEW_SPLIT && ed->rendered_doc.count > 0) {
        int view_rows = ed->screen_rows > 3 ? (ed->screen_rows - 3) : 20;
        if (ed->rendered_doc.count > (size_t)view_rows) {
            size_t max_p_scroll = ed->rendered_doc.count - (size_t)view_rows;
            if (ed->preview_scroll_row < max_p_scroll) {
                ed->scroll_row += step;
            }
        }
    }
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

/**
 * @brief Jump cursor between matching pairs of brackets () [] {} <> or Markdown delimiters.
 */
bool editor_jump_matching_pair(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    size_t total_lines = buffer_line_count(&ed->buffer);
    if (total_lines == 0 || ed->cursor_row >= total_lines) return false;

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    size_t col = ed->cursor_col;

    // If cursor is past line end or on non-delimiter, scan forward on current line for nearest delimiter
    if (col >= line_len || (!strchr("()[]{}`*~<>_", line[col]))) {
        size_t scan = col;
        while (scan < line_len && !strchr("()[]{}`*~<>_", line[scan])) {
            scan++;
        }
        if (scan < line_len) {
            col = scan;
        } else {
            editor_set_status(ed, "No matching bracket or delimiter found");
            return false;
        }
    }

    char c = line[col];

    // 1. Markdown bold '**'
    if (c == '*' && col + 1 < line_len && line[col + 1] == '*') {
        size_t fwd = col + 2;
        while (fwd + 1 < line_len && !(line[fwd] == '*' && line[fwd + 1] == '*')) {
            fwd++;
        }
        if (fwd + 1 < line_len) {
            ed->cursor_col = fwd;
            ed->target_visual_col = utf8_byte_to_col(line, fwd);
            editor_set_status(ed, "Jumped to matching '**'");
            return true;
        }
        if (col >= 2) {
            size_t bwd = col - 2;
            while (1) {
                if (line[bwd] == '*' && line[bwd + 1] == '*') {
                    ed->cursor_col = bwd;
                    ed->target_visual_col = utf8_byte_to_col(line, bwd);
                    editor_set_status(ed, "Jumped to matching '**'");
                    return true;
                }
                if (bwd == 0) break;
                bwd--;
            }
        }
    }

    // 2. Markdown strikethrough '~~'
    if (c == '~' && col + 1 < line_len && line[col + 1] == '~') {
        size_t fwd = col + 2;
        while (fwd + 1 < line_len && !(line[fwd] == '~' && line[fwd + 1] == '~')) {
            fwd++;
        }
        if (fwd + 1 < line_len) {
            ed->cursor_col = fwd;
            ed->target_visual_col = utf8_byte_to_col(line, fwd);
            editor_set_status(ed, "Jumped to matching '~~'");
            return true;
        }
        if (col >= 2) {
            size_t bwd = col - 2;
            while (1) {
                if (line[bwd] == '~' && line[bwd + 1] == '~') {
                    ed->cursor_col = bwd;
                    ed->target_visual_col = utf8_byte_to_col(line, bwd);
                    editor_set_status(ed, "Jumped to matching '~~'");
                    return true;
                }
                if (bwd == 0) break;
                bwd--;
            }
        }
    }

    // 3. Markdown code block fence ```
    if (c == '`' && col + 2 < line_len && line[col + 1] == '`' && line[col + 2] == '`') {
        for (size_t r = ed->cursor_row + 1; r < total_lines; ++r) {
            const char *fl = buffer_line(&ed->buffer, r);
            size_t fl_len = buffer_line_len(&ed->buffer, r);
            size_t k = 0;
            while (k < fl_len && isspace((unsigned char)fl[k])) k++;
            if (k + 2 < fl_len && fl[k] == '`' && fl[k + 1] == '`' && fl[k + 2] == '`') {
                ed->cursor_row = r;
                ed->cursor_col = k;
                ed->target_visual_col = utf8_byte_to_col(fl, k);
                editor_set_status(ed, "Jumped to closing code fence (line %zu)", r + 1);
                return true;
            }
        }
        if (ed->cursor_row > 0) {
            for (size_t r = ed->cursor_row - 1; ; --r) {
                const char *fl = buffer_line(&ed->buffer, r);
                size_t fl_len = buffer_line_len(&ed->buffer, r);
                size_t k = 0;
                while (k < fl_len && isspace((unsigned char)fl[k])) k++;
                if (k + 2 < fl_len && fl[k] == '`' && fl[k + 1] == '`' && fl[k + 2] == '`') {
                    ed->cursor_row = r;
                    ed->cursor_col = k;
                    ed->target_visual_col = utf8_byte_to_col(fl, k);
                    editor_set_status(ed, "Jumped to opening code fence (line %zu)", r + 1);
                    return true;
                }
                if (r == 0) break;
            }
        }
    }

    // 4. Markdown inline code '`'
    if (c == '`') {
        size_t fwd = col + 1;
        while (fwd < line_len && line[fwd] != '`') fwd++;
        if (fwd < line_len) {
            ed->cursor_col = fwd;
            ed->target_visual_col = utf8_byte_to_col(line, fwd);
            editor_set_status(ed, "Jumped to matching '`'");
            return true;
        }
        if (col > 0) {
            size_t bwd = col - 1;
            while (bwd > 0 && line[bwd] != '`') bwd--;
            if (line[bwd] == '`') {
                ed->cursor_col = bwd;
                ed->target_visual_col = utf8_byte_to_col(line, bwd);
                editor_set_status(ed, "Jumped to matching '`'");
                return true;
            }
        }
    }

    // 5. Standard brackets () [] {} <> with nesting across lines
    const char *open_brackets = "([{<";
    const char *close_brackets = ")]}>";
    const char *open_pos = strchr(open_brackets, c);
    const char *close_pos = strchr(close_brackets, c);

    if (open_pos) {
        char target_char = close_brackets[open_pos - open_brackets];
        char open_char = c;
        int depth = 1;

        for (size_t r = ed->cursor_row; r < total_lines; ++r) {
            const char *l = buffer_line(&ed->buffer, r);
            size_t len = buffer_line_len(&ed->buffer, r);
            size_t start_b = (r == ed->cursor_row) ? (col + 1) : 0;

            for (size_t b = start_b; b < len; ++b) {
                if (l[b] == open_char) {
                    depth++;
                } else if (l[b] == target_char) {
                    depth--;
                    if (depth == 0) {
                        ed->cursor_row = r;
                        ed->cursor_col = b;
                        ed->target_visual_col = utf8_byte_to_col(l, b);
                        editor_set_status(ed, "Jumped to matching '%c'", target_char);
                        return true;
                    }
                }
            }
        }
        editor_set_status(ed, "No matching '%c' found", target_char);
        return false;
    }

    if (close_pos) {
        char target_char = open_brackets[close_pos - close_brackets];
        char close_char = c;
        int depth = 1;

        for (size_t r = ed->cursor_row; ; --r) {
            const char *l = buffer_line(&ed->buffer, r);
            size_t len = buffer_line_len(&ed->buffer, r);
            if (len > 0) {
                size_t start_b = (r == ed->cursor_row) ? (col > 0 ? col - 1 : 0) : (len - 1);
                if (r < ed->cursor_row || col > 0) {
                    for (size_t b = start_b; ; --b) {
                        if (l[b] == close_char) {
                            depth++;
                        } else if (l[b] == target_char) {
                            depth--;
                            if (depth == 0) {
                                ed->cursor_row = r;
                                ed->cursor_col = b;
                                ed->target_visual_col = utf8_byte_to_col(l, b);
                                editor_set_status(ed, "Jumped to matching '%c'", target_char);
                                return true;
                            }
                        }
                        if (b == 0) break;
                    }
                }
            }
            if (r == 0) break;
        }
        editor_set_status(ed, "No matching '%c' found", target_char);
        return false;
    }

    // 6. Markdown single '*' or '_' (italic)
    if (c == '*' || c == '_') {
        size_t fwd = col + 1;
        while (fwd < line_len && line[fwd] != c) fwd++;
        if (fwd < line_len) {
            ed->cursor_col = fwd;
            ed->target_visual_col = utf8_byte_to_col(line, fwd);
            editor_set_status(ed, "Jumped to matching '%c'", c);
            return true;
        }
        if (col > 0) {
            size_t bwd = col - 1;
            while (bwd > 0 && line[bwd] != c) bwd--;
            if (line[bwd] == c) {
                ed->cursor_col = bwd;
                ed->target_visual_col = utf8_byte_to_col(line, bwd);
                editor_set_status(ed, "Jumped to matching '%c'", c);
                return true;
            }
        }
    }

    editor_set_status(ed, "No matching delimiter found");
    return false;
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

/** @brief Clamp and synchronize editor vertical/horizontal scroll and preview scroll offsets. */
void editor_update_scroll(Editor *ed, int view_rows) {
    if (!ed || view_rows <= 0) return;
    ed->view_rows = view_rows;

    size_t total_lines = buffer_line_count(&ed->buffer);
    size_t normal_max = total_lines > (size_t)view_rows ? (total_lines - (size_t)view_rows) : 0;
    size_t max_scroll = normal_max;

    if (ed->view_mode == VIEW_SPLIT && ed->rendered_doc.count > (size_t)view_rows) {
        size_t max_p_scroll = ed->rendered_doc.count - (size_t)view_rows;
        size_t rend_at_normal_max = rendered_doc_find_line(&ed->rendered_doc, normal_max);
        if (max_p_scroll > rend_at_normal_max) {
            max_scroll = normal_max + (max_p_scroll - rend_at_normal_max);
        }
    }

    // Clamp scroll_row when terminal grows taller so content fills the viewport
    if (total_lines <= (size_t)view_rows && ed->view_mode != VIEW_SPLIT) {
        ed->scroll_row = 0;
    } else if (ed->scroll_row > max_scroll) {
        ed->scroll_row = max_scroll;
    }

    if (ed->cursor_row < ed->scroll_row) {
        if (ed->view_mode != VIEW_SPLIT || ed->cursor_row + 1 < total_lines) {
            ed->scroll_row = ed->cursor_row;
        }
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
        if (ed->rendered_doc.count <= (size_t)view_rows) {
            ed->preview_scroll_row = 0;
        } else {
            size_t max_p_scroll = ed->rendered_doc.count - (size_t)view_rows;
            size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->scroll_row);

            if (ed->scroll_row > normal_max) {
                size_t overscroll = ed->scroll_row - normal_max;
                rend_idx += overscroll;
            }

            if (rend_idx > max_p_scroll) {
                rend_idx = max_p_scroll;
            }
            ed->preview_scroll_row = rend_idx;

            size_t cur_rend = rendered_doc_find_line(&ed->rendered_doc, ed->cursor_row);
            if (cur_rend < ed->preview_scroll_row) {
                ed->preview_scroll_row = cur_rend;
            }
            if (cur_rend >= ed->preview_scroll_row + (size_t)view_rows) {
                ed->preview_scroll_row = cur_rend - (size_t)view_rows + 1;
            }
            if (ed->preview_scroll_row > max_p_scroll) {
                ed->preview_scroll_row = max_p_scroll;
            }
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
