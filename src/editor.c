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
    ed->insert_mode = true;     // Default to Insert mode (steady block cursor), press Ins for Overwrite
    ed->rendered_dirty = true;
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->preview_scroll_row = 0;
    ed->quit_requested = false;
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
        editor_set_status(ed, "Welcome to mde - Press Ctrl+H for help, Ctrl+P to toggle view");
    }
}

void editor_free(Editor *ed) {
    if (!ed) return;
    buffer_free(&ed->buffer);
    rendered_doc_free(&ed->rendered_doc);
    free(ed->clipboard);
    ed->clipboard = NULL;
    ed->clipboard_len = 0;
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
    editor_touch_key(ed);
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
}

void editor_move_end(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
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

void editor_set_mark(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->mark_active = true;
    ed->mark_row = ed->cursor_row;
    ed->mark_col = ed->cursor_col;
    editor_set_status(ed, "Mark set");
}

void editor_clear_mark(Editor *ed) {
    if (!ed) return;
    editor_touch_key(ed);
    ed->mark_active = false;
    editor_set_status(ed, "Mark cleared");
}

bool editor_has_selection(const Editor *ed) {
    if (!ed || !ed->mark_active) return false;
    return (ed->mark_row != ed->cursor_row || ed->mark_col != ed->cursor_col);
}

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

void editor_insert_char(Editor *ed, const char *utf8_seq, size_t len) {
    if (!ed || !utf8_seq || len == 0) return;
    editor_touch_key(ed);

    if (editor_get_selection(ed, NULL, NULL, NULL, NULL)) {
        uint64_t group = buffer_next_group(&ed->buffer);
        editor_delete_selection(ed, group);
        buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, utf8_seq, len, group);
        ed->cursor_col += len;
        const char *line = buffer_line(&ed->buffer, ed->cursor_row);
        ed->target_visual_col = utf8_byte_to_col(line, ed->cursor_col);
        ed->rendered_dirty = true;
        return;
    }

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
        editor_set_status(ed, "INSERT Mode (Block cursor)");
    } else {
        editor_set_status(ed, "OVERWRITE Mode (Underscore cursor)");
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
