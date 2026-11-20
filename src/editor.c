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

void editor_init(Editor *ed, const char *filename) {
    if (!ed) return;
    memset(ed, 0, sizeof(Editor));
    buffer_init(&ed->buffer);
    rendered_doc_init(&ed->rendered_doc);

    ed->view_mode = VIEW_SPLIT; // Default to dual-pane split view
    ed->insert_mode = false;    // Default to Overwrite (block cursor), press Ins for Insert
    ed->rendered_dirty = true;
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->preview_scroll_row = 0;
    ed->quit_requested = false;
    ed->search_active = false;
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
        editor_set_status(ed, "Welcome to mde - Press Ctrl+H for help, Ctrl+P to toggle view");
    }
}

void editor_free(Editor *ed) {
    if (!ed) return;
    buffer_free(&ed->buffer);
    rendered_doc_free(&ed->rendered_doc);
}

void editor_set_status(Editor *ed, const char *fmt, ...) {
    if (!ed || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ed->status_msg, sizeof(ed->status_msg), fmt, args);
    va_end(args);
    ed->status_time = time(NULL);
}

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

void editor_refresh_rendered(Editor *ed) {
    if (!ed || !ed->rendered_dirty) return;
    int p_width = ed->preview_width > 0 ? ed->preview_width : 80;
    render_document(&ed->buffer, p_width, &ed->rendered_doc);
    ed->rendered_dirty = false;
}

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

void editor_move_up(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_row > 0) {
        ed->cursor_row--;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    }
}

void editor_move_down(Editor *ed) {
    if (!ed) return;
    if (ed->cursor_row + 1 < buffer_line_count(&ed->buffer)) {
        ed->cursor_row++;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
    }
}

void editor_move_home(Editor *ed) {
    if (!ed) return;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
}

void editor_move_end(Editor *ed) {
    if (!ed) return;
    ed->cursor_col = buffer_line_len(&ed->buffer, ed->cursor_row);
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
}

void editor_move_page_up(Editor *ed) {
    if (!ed) return;
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    if (ed->cursor_row > step) ed->cursor_row -= step;
    else ed->cursor_row = 0;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
}

void editor_move_page_down(Editor *ed) {
    if (!ed) return;
    size_t step = ed->screen_rows > 3 ? (size_t)(ed->screen_rows - 3) : 10;
    size_t max_row = buffer_line_count(&ed->buffer) - 1;
    ed->cursor_row += step;
    if (ed->cursor_row > max_row) ed->cursor_row = max_row;
    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->cursor_col = utf8_col_to_byte(line, ed->target_visual_col);
}

void editor_insert_char(Editor *ed, const char *utf8_seq, size_t len) {
    if (!ed || !utf8_seq || len == 0) return;
    editor_touch_key(ed);

    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);

    if (ed->insert_mode || ed->cursor_col >= line_len) {
        // Insert mode (or at line end in overwrite mode): insert character
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, ed->buffer.current_group_id);
        ed->cursor_col += len;
    } else {
        // Overwrite mode: replace existing character at cursor
        const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
        size_t old_char_len = utf8_char_len((unsigned char)cur_line[ed->cursor_col]);
        uint64_t group = buffer_next_group(&ed->buffer);
        buffer_delete_range(&ed->buffer, ed->cursor_row, ed->cursor_col, old_char_len, group);
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, group);
        ed->cursor_col += len;
    }

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
    ed->rendered_dirty = true;
}

void editor_backspace(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    if (buffer_backspace(&ed->buffer, &ed->cursor_row, &ed->cursor_col, ed->buffer.current_group_id)) {
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
    }
}

void editor_delete(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    if (buffer_delete(&ed->buffer, ed->cursor_row, ed->cursor_col, ed->buffer.current_group_id)) {
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
    }
}

void editor_smart_enter(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    uint64_t group = buffer_next_group(&ed->buffer);

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

void editor_toggle_insert_mode(Editor *ed) {
    if (!ed) return;
    ed->insert_mode = !ed->insert_mode;
    editor_touch_key(ed);
    if (ed->insert_mode) {
        editor_set_status(ed, "INSERT Mode (Underscore cursor)");
    } else {
        editor_set_status(ed, "NORMAL Mode (Normal cursor)");
    }
}

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

void editor_wrap_format(Editor *ed, const char *marker) {
    if (!ed || !marker) return;
    editor_touch_key(ed);
    uint64_t group = buffer_next_group(&ed->buffer);
    size_t mlen = strlen(marker);

    // Insert marker twice (opening and closing), leaving cursor in the middle
    char both[32];
    snprintf(both, sizeof(both), "%s%s", marker, marker);
    buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, both, mlen * 2, group);
    ed->cursor_col += mlen;

    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->rendered_dirty = true;
}

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

bool editor_save(Editor *ed, const char *opt_path) {
    if (!ed) return false;
    if (buffer_save(&ed->buffer, opt_path)) {
        editor_set_status(ed, "Saved %zu lines to %s", buffer_line_count(&ed->buffer), ed->buffer.file_path);
        return true;
    } else {
        editor_set_status(ed, "Error saving file!");
        return false;
    }
}

bool editor_open(Editor *ed, const char *path) {
    if (!ed || !path) return false;
    if (buffer_load(&ed->buffer, path)) {
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->target_visual_col = 0;
        ed->scroll_row = 0;
        ed->rendered_dirty = true;
        editor_touch_key(ed);
        editor_set_status(ed, "Opened %s", path);
        return true;
    } else {
        editor_set_status(ed, "Cannot open %s", path);
        return false;
    }
}

uint64_t editor_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

void editor_touch_key(Editor *ed) {
    if (!ed) return;
    ed->last_key_time = editor_get_time_ms();
}

bool editor_cursor_is_visible(const Editor *ed) {
    (void)ed;
    return true; // Steady, non-flashing cursor
}

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

bool editor_search_find_next(Editor *ed, const char *query, bool forward) {
    if (!ed || !query || !*query) return false;

    strncpy(ed->search_query, query, sizeof(ed->search_query) - 1);
    ed->search_active = true;
    size_t qlen = strlen(query);
    size_t total_lines = buffer_line_count(&ed->buffer);

    if (forward) {
        size_t start_row = ed->cursor_row;
        size_t start_col = ed->cursor_col;

        for (size_t step = 0; step < total_lines; ++step) {
            size_t r = (start_row + step) % total_lines;
            const char *line = buffer_line(&ed->buffer, r);
            const char *search_from = line;
            if (r == start_row && step == 0) {
                if (start_col + 1 < strlen(line)) {
                    search_from = line + start_col + 1;
                } else {
                    continue;
                }
            }
            char *found = strcasestr_custom(search_from, query);
            if (found) {
                ed->cursor_row = r;
                ed->cursor_col = found - line;
                ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
                ed->search_match_row = r;
                ed->search_match_col = ed->cursor_col;
                ed->search_match_len = qlen;
                editor_set_status(ed, "Search: match at line %zu col %zu", r + 1, ed->cursor_col + 1);
                return true;
            }
        }
    } else {
        size_t r = ed->cursor_row;
        for (size_t step = 0; step < total_lines; ++step) {
            const char *line = buffer_line(&ed->buffer, r);
            size_t line_len = strlen(line);
            size_t limit = (r == ed->cursor_row && step == 0 && ed->cursor_col > 0)
                           ? ed->cursor_col - 1 : line_len;

            char *last_found = NULL;
            const char *curr = line;
            while (curr < line + limit) {
                char *f = strcasestr_custom(curr, query);
                if (f && (size_t)(f - line) <= limit) {
                    last_found = f;
                    curr = f + 1;
                } else {
                    break;
                }
            }
            if (last_found) {
                ed->cursor_row = r;
                ed->cursor_col = last_found - line;
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

void editor_update_scroll(Editor *ed, int view_rows) {
    if (!ed || view_rows <= 0) return;

    if (ed->cursor_row < ed->scroll_row) {
        ed->scroll_row = ed->cursor_row;
    }
    if (ed->cursor_row >= ed->scroll_row + (size_t)view_rows) {
        ed->scroll_row = ed->cursor_row - (size_t)view_rows + 1;
    }

    const char *line = buffer_line(&ed->buffer, ed->cursor_row);
    int cursor_vcol = utf8_byte_to_col(line, ed->cursor_col);
    int editor_w = ed->editor_width > 8 ? ed->editor_width - 8 : 40;

    if (cursor_vcol < (int)ed->scroll_col) {
        ed->scroll_col = cursor_vcol > 0 ? (size_t)cursor_vcol : 0;
    }
    if (cursor_vcol >= (int)ed->scroll_col + editor_w) {
        ed->scroll_col = cursor_vcol - editor_w + 1;
    }

    // Synchronize preview scroll with editor cursor row
    if (ed->view_mode == VIEW_PREVIEW && ed->rendered_doc.count > 0) {
        size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->cursor_row);
        if (rend_idx < ed->preview_scroll_row) {
            ed->preview_scroll_row = rend_idx;
        }
        if (rend_idx >= ed->preview_scroll_row + (size_t)view_rows) {
            ed->preview_scroll_row = rend_idx - (size_t)view_rows + 1;
        }
    } else if (ed->view_mode == VIEW_SPLIT && ed->rendered_doc.count > 0) {
        size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->scroll_row);
        ed->preview_scroll_row = rend_idx;
    }
}
